#include "clipboard_system.h"

#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <dlfcn.h>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AImGui", __VA_ARGS__)

namespace aimgui::sysclip {
namespace {

// ─── libbinder_ndk, by hand ──────────────────────────────────────────────
// The stable C ABI rather than libbinder's C++ one: no mangled names, no
// guessing a vtable index, and — the part most hand-rolled attempts get wrong
// — AIBinder_prepareTransaction writes the four-word interface token in
// exactly the form Java's Parcel expects. dlopen'd rather than linked because
// this binary targets API 24, where the library does not exist.
struct Ndk {
    void* (*getService)(const char*)                                   = nullptr;
    void* (*classDefine)(const char*, void*, void*, void*)             = nullptr;
    bool  (*associateClass)(void*, void*)                              = nullptr;
    void  (*decStrong)(void*)                                          = nullptr;
    int32_t (*prepare)(void*, void**)                                  = nullptr;
    int32_t (*transact)(void*, uint32_t, void**, void**, uint32_t)     = nullptr;
    int32_t (*writeInt32)(void*, int32_t)                              = nullptr;
    int32_t (*writeInt64)(void*, int64_t)                              = nullptr;
    int32_t (*writeString)(void*, const char*, int32_t)                = nullptr;
    int32_t (*writeByteArray)(void*, const int8_t*, int32_t)           = nullptr;
    int32_t (*readInt32)(const void*, int32_t*)                        = nullptr;
    int32_t (*readInt64)(const void*, int64_t*)                        = nullptr;
    int32_t (*readString)(const void*, void*, void*)                   = nullptr;
    int32_t (*readByteArray)(const void*, void*, void*)                = nullptr;
    int32_t (*getDataPos)(const void*)                                 = nullptr;
    int32_t (*setDataPos)(const void*, int32_t)                        = nullptr;
    void    (*deleteParcel)(void*)                                     = nullptr;
    bool ok = false;
};
Ndk g;

int  OnTransactStub(void*, uint32_t, const void*, void*) { return -38; }
void* OnCreateStub(void* a) { return a; }
void OnDestroyStub(void*) {}

bool LoadNdk() {
    if (g.ok) return true;
    void* h = dlopen("libbinder_ndk.so", RTLD_NOW | RTLD_LOCAL);
    if (!h) return false;
    auto S = [&](const char* n) { return dlsym(h, n); };
    g.getService     = (decltype(g.getService))     S("AServiceManager_getService");
    g.classDefine    = (decltype(g.classDefine))    S("AIBinder_Class_define");
    g.associateClass = (decltype(g.associateClass)) S("AIBinder_associateClass");
    g.decStrong      = (decltype(g.decStrong))      S("AIBinder_decStrong");
    g.prepare        = (decltype(g.prepare))        S("AIBinder_prepareTransaction");
    g.transact       = (decltype(g.transact))       S("AIBinder_transact");
    g.writeInt32     = (decltype(g.writeInt32))     S("AParcel_writeInt32");
    g.writeInt64     = (decltype(g.writeInt64))     S("AParcel_writeInt64");
    g.writeString    = (decltype(g.writeString))    S("AParcel_writeString");
    g.writeByteArray = (decltype(g.writeByteArray)) S("AParcel_writeByteArray");
    g.readInt32      = (decltype(g.readInt32))      S("AParcel_readInt32");
    g.readInt64      = (decltype(g.readInt64))      S("AParcel_readInt64");
    g.readString     = (decltype(g.readString))     S("AParcel_readString");
    g.readByteArray  = (decltype(g.readByteArray))  S("AParcel_readByteArray");
    g.getDataPos     = (decltype(g.getDataPos))     S("AParcel_getDataPosition");
    g.setDataPos     = (decltype(g.setDataPos))     S("AParcel_setDataPosition");
    g.deleteParcel   = (decltype(g.deleteParcel))   S("AParcel_delete");
    g.ok = g.getService && g.classDefine && g.associateClass && g.prepare &&
           g.transact && g.writeInt32 && g.writeInt64 && g.writeString &&
           g.writeByteArray && g.readInt32 && g.readInt64 && g.readString &&
           g.readByteArray && g.getDataPos && g.setDataPos;
    return g.ok;
}

// ─── Parcel pieces Java writes that AParcel has no direct call for ───────
//
// A Java String8 is [int32 charCount][charCount+1 bytes utf-8, NUL included,
// padded to 4]. A byte array is [int32 byteCount][byteCount bytes, padded to
// 4]. They differ only in that trailing NUL, which is exactly enough to make
// AParcel_readByteArray read the right bytes and then leave the cursor in the
// wrong place — so the cursor is set explicitly afterwards either way.

int32_t Align4(int32_t n) { return (n + 3) & ~3; }

// Nothing on a clipboard is this big, and a length that says otherwise means
// the cursor is somewhere the layout did not predict. Refusing to allocate on
// it is what keeps a wrong guess about the parcel from becoming an allocation
// the size of whatever four bytes happened to be sitting there.
constexpr int32_t kMaxField = 1 << 20;

std::vector<int8_t>* g_sink = nullptr;
bool ByteArrayAllocator(void* /*data*/, int32_t length, int8_t** outBuffer) {
    if (length < 0 || length > kMaxField) { *outBuffer = nullptr; return length < 0; }
    g_sink->assign((size_t)length, 0);
    *outBuffer = g_sink->data();
    return true;
}

bool ReadString8(void* p, std::string* out) {
    const int32_t pos = g.getDataPos(p);
    int32_t n = 0;
    if (g.readInt32(p, &n) != 0) return false;
    if (n < 0) { out->clear(); g.setDataPos(p, pos + 4); return true; }
    if (n > kMaxField) return false;
    g.setDataPos(p, pos);
    std::vector<int8_t> bytes;
    g_sink = &bytes;
    const int32_t st = g.readByteArray(p, nullptr, (void*)&ByteArrayAllocator);
    g_sink = nullptr;
    if (st != 0) return false;
    out->assign((const char*)bytes.data(), bytes.size());
    // Past the length word and the NUL-terminated, 4-padded payload.
    g.setDataPos(p, pos + 4 + Align4(n + 1));
    return true;
}

void WriteString8(void* p, const char* s) {
    const int32_t n = s ? (int32_t)std::strlen(s) : -1;
    if (n < 0) { g.writeInt32(p, -1); return; }
    const int32_t pos = g.getDataPos(p);
    // Written as a byte array including the NUL, then the length word is
    // corrected: the array call would otherwise claim n+1 characters.
    g.writeByteArray(p, (const int8_t*)s, n + 1);
    const int32_t end = g.getDataPos(p);
    g.setDataPos(p, pos);
    g.writeInt32(p, n);
    g.setDataPos(p, end);
}

// A Bundle or PersistableBundle on the wire, skipped without unpacking it.
//
// Three shapes, not two, and the third is what this got wrong. From
// BaseBundle.writeToParcelInner:
//
//     -1            null — writePersistableBundle's own early return
//      0            "Special case for empty bundles": writeInt(0) and return.
//                   No magic word follows. Nothing follows.
//     len > 0       [int32 len][int32 magic][len bytes], len counting only
//                   what comes after the magic
//
// Treating 0 like len > 0 skipped four bytes that were never written, and the
// clip's confidence bundle is empty for any ordinary text — so the cursor came
// out four bytes long and every field after it read garbage. It surfaced as a
// failure at the icon flag, four fields later, which is the nature of walking a
// parcel: the report names where it stopped, not where it went wrong.
bool SkipBundle(void* p) {
    int32_t len = 0;
    if (g.readInt32(p, &len) != 0) return false;
    if (len <= 0) return true;
    if (len > kMaxField) return false;
    return g.setDataPos(p, g.getDataPos(p) + 4 + len) == 0;
}

// A Java String (utf-16 on the wire), through the NDK's own reader.
bool ReadJavaString(void* p, std::string* out) {
    struct A {
        static bool Alloc(void* d, int32_t len, char** buf) {
            auto* s = (std::string*)d;
            if (len < 0) { *buf = nullptr; return true; }
            if (len > kMaxField) { *buf = nullptr; return false; }
            s->assign((size_t)len, '\0');
            *buf = s->data();
            return true;
        }
    };
    out->clear();
    if (g.readString(p, out, (void*)&A::Alloc) != 0) return false;
    // The allocator is handed a length that counts the terminator.
    while (!out->empty() && out->back() == '\0') out->pop_back();
    return true;
}

// Java's exception header is the code and then the message — Parcel.writeException
// does writeInt(code) followed immediately by writeString(e.getMessage()). Reading
// only the code and discarding the parcel throws away the one thing that says what
// actually went wrong on the far side, which is the difference between "-2" and
// the sentence describing which field of which class failed to unparcel.
std::string ReadExceptionMessage(void* p) {
    std::string msg;
    if (!ReadJavaString(p, &msg)) return "";
    return msg;
}

// TextUtils.writeToParcel: an int for whether the text carries spans, then the
// text itself. Only the text is wanted, and it comes first either way, so the
// span records that would follow a spanned string are never walked.
bool ReadCharSequence(void* p, std::string* out) {
    int32_t kind = 0;
    if (g.readInt32(p, &kind) != 0) return false;
    return ReadString8(p, out);
}

void WriteCharSequence(void* p, const char* s) {
    g.writeInt32(p, 1);   // not spanned
    WriteString8(p, s);
}

// ─── The transaction ─────────────────────────────────────────────────────
constexpr uint32_t kSetPrimaryClip = 1;   // IClipboard.aidl order, identical
constexpr uint32_t kGetPrimaryClip = 4;   // on Android 14, 15 and 16
constexpr const char* kDescriptor  = "android.content.IClipboard";
constexpr const char* kPackage     = "com.android.shell";

// Declared here rather than beside the entry points so the transaction code
// can fill it in. Whatever stopped it is shown in the UI, not only logged:
// asking someone to reproduce a failure under logcat is a poor trade when the
// screen is right there and the answer is one short string.
std::string g_error;

void* g_clazz = nullptr;

void* OpenService() {
    if (!LoadNdk()) return nullptr;
    if (!g_clazz) {
        g_clazz = g.classDefine(kDescriptor, (void*)&OnCreateStub,
                                (void*)&OnDestroyStub, (void*)&OnTransactStub);
        if (!g_clazz) return nullptr;
    }
    void* b = g.getService("clipboard");
    if (!b) { g_error = "no clipboard service"; LOGI("[clip] no 'clipboard' service"); return nullptr; }
    // Not a formality: this asks the far end for its own descriptor and
    // compares, so success is proof the object really is IClipboard before a
    // transaction is sent whose meaning depends entirely on that. It is also
    // the first thing that would fail if the service were renamed or wrapped,
    // so it is worth hearing about separately from a failed transaction.
    if (!g.associateClass(b, g_clazz)) {
        g_error = "not IClipboard";
        LOGI("[clip] 'clipboard' is not %s", kDescriptor);
        g.decStrong(b);
        return nullptr;
    }
    return b;
}

// Trailing arguments shared by both calls: the caller's identity, the user and
// the "device" a virtual-device-aware framework wants. 0/0 is this user on the
// default device.
void WriteCallerTail(void* in) {
    g.writeString(in, kPackage, (int32_t)std::strlen(kPackage));
    g.writeInt32(in, -1);   // attributionTag: null
    g.writeInt32(in, 0);    // userId
    g.writeInt32(in, 0);    // deviceId (DEVICE_ID_DEFAULT)
}

struct Layout {
    const char* name[20];
    int32_t     at[20];
    int         n = 0;
    int32_t     base = 0;
    void Mark(void* p, const char* what) {
        if (n < 20) { name[n] = what; at[n] = g.getDataPos(p) - base; ++n; }
    }
    void Dump(const char* tag) const {
        std::string s;
        char buf[64];
        for (int i = 0; i < n; ++i) {
            std::snprintf(buf, sizeof(buf), "%s=%d ", name[i], at[i]);
            s += buf;
        }
        std::fprintf(stderr, "[clip] %s layout: %s\n", tag, s.c_str());
        std::fflush(stderr);
        LOGI("[clip] %s layout: %s", tag, s.c_str());
    }
};

bool DoRead(std::string* out) {
    void* svc = OpenService();
    if (!svc) return false;
    void* in = nullptr;
    if (g.prepare(svc, &in) != 0) { g.decStrong(svc); return false; }
    WriteCallerTail(in);

    void* rep = nullptr;
    const int32_t st = g.transact(svc, kGetPrimaryClip, &in, &rep, 0);
    if (st != 0 || !rep) {
        g_error = "transact " + std::to_string(st);
        LOGI("[clip] read: transact failed, status %d", st);
        g.decStrong(svc);
        return false;
    }

    // Every step is named. A parcel walked against the wrong layout fails at
    // some particular field and then silently at every field after it, so
    // "it did not work" is useless — which one stopped is the whole diagnosis.
    const char* step = "exception header";
    Layout L;
    bool ok = false;
    int32_t v = 0;
    do {
        if (g.readInt32(rep, &v) != 0) break;
        if (v != 0) {
            // The service refusing is the interesting case and reads nothing
            // like a layout mismatch, so it is worded as itself — with the
            // message the service already sent along behind the code.
            const std::string msg = ReadExceptionMessage(rep);
            g_error = "service refused (" + std::to_string(v) + ")" +
                      (msg.empty() ? "" : ": " + msg);
            LOGI("[clip] read: service threw %d: %s", v, msg.c_str());
            step = nullptr;
            break;
        }

        step = "clip presence";
        if (g.readInt32(rep, &v) != 0) break;
        if (v == 0) {
            // Not a failure. The clipboard is simply empty, and reporting that
            // as a broken transaction would send the UI to the file fallback
            // and show the wrong text.
            LOGI("[clip] read: clipboard is empty");
            out->clear();
            ok = true;
            break;
        }

        // The same marks the write path reports, taken here on a ClipData the
        // device itself produced. The write side matches AOSP exactly and is
        // still 28 bytes too long, so AOSP is not what this device speaks —
        // and a successful read is the only place its real layout is visible.
        L.base = g.getDataPos(rep);

        step = "description label";
        std::string label;
        if (!ReadCharSequence(rep, &label)) break;
        L.Mark(rep, "label");

        step = "mime types";
        int32_t mimeCount = 0;
        if (g.readInt32(rep, &mimeCount) != 0) break;
        if (mimeCount > 64) break;
        L.Mark(rep, "mimeN");
        for (int32_t i = 0; i < mimeCount; ++i) {
            // Mime types are Java Strings (utf-16 on the wire), unlike the
            // String8s above, so the NDK's own reader is right for them.
            std::string mime;
            if (!ReadJavaString(rep, &mime)) { mimeCount = -1; break; }
        }
        if (mimeCount < 0) break;
        L.Mark(rep, "mime");
        LOGI("[clip] read: label=\"%s\" mimeN=%d", label.c_str(), mimeCount);
        std::fprintf(stderr, "[clip] read: label=\"%s\" (%zu bytes) mimeN=%d\n",
                     label.c_str(), label.size(), mimeCount);

        step = "extras bundle";
        if (!SkipBundle(rep)) break;
        L.Mark(rep, "extras");
        step = "timestamp";
        int64_t ts = 0;
        if (g.readInt64(rep, &ts) != 0) break;
        L.Mark(rep, "stamp");
        step = "styled flag";
        if (g.readInt32(rep, &v) != 0) break;
        step = "classification";
        if (g.readInt32(rep, &v) != 0) break;
        L.Mark(rep, "styled+class");
        step = "confidences bundle";
        if (!SkipBundle(rep)) break;
        L.Mark(rep, "confid");
        step = "icon presence";
        if (g.readInt32(rep, &v) != 0) break;
        // A presence flag can only be 0 or 1. Anything else is not a clip with
        // a strange icon, it is the cursor in the wrong place — and saying so
        // is worth more than the one true case it rules out, because a value
        // out of range is the earliest honest sign that the walk has drifted.
        if (v != 0 && v != 1) { step = "icon presence (bad flag, layout drift)"; break; }
        if (v == 1) { step = "icon present, not parsed"; break; }
        step = "item count";
        int32_t items = 0;
        if (g.readInt32(rep, &items) != 0) break;
        if (items <= 0 || items > 64) { step = "item count (out of range, layout drift)"; break; }
        L.Mark(rep, "icon+count");
        step = "item text";
        ok = ReadCharSequence(rep, out);
        L.Mark(rep, "itemText");
        L.Dump("read");
    } while (false);

    if (!ok && step) {
        g_error = std::string("stopped at ") + step;
        LOGI("[clip] read: stopped at '%s'", step);
    } else if (!ok) {
        LOGI("[clip] read failed");
    }
    else     LOGI("[clip] read: ok, %d bytes", (int)out->size());

    if (g.deleteParcel) g.deleteParcel(rep);
    // Only now: the reply parcel was created against this binder and may still
    // reference it, so dropping the last reference before the parcel is gone
    // is a use-after-free waiting for the wrong moment to happen.
    g.decStrong(svc);
    return ok;
}

// Byte offsets of the fields as they were actually written, relative to the end
// of the interface token.
//
// "Parcel data not fully consumed, unread size: 28" says the service read a
// whole valid ClipData and found 28 bytes to spare — but not where they came
// from. Counting the layout by hand against AOSP twice produced a total that
// should have been consumed exactly, which means one of the assumptions about
// what libbinder_ndk actually emits is wrong, and no amount of further counting
// will say which. So the parcel reports its own shape.

bool DoWrite(const char* text) {
    void* svc = OpenService();
    if (!svc) return false;
    void* in = nullptr;
    if (g.prepare(svc, &in) != 0) { g.decStrong(svc); return false; }

    Layout L;
    L.base = g.getDataPos(in);           // just past the interface token

    g.writeInt32(in, 1);                 // ClipData is present
    L.Mark(in, "present");
    WriteCharSequence(in, "AImGui");     // ClipDescription.mLabel
    L.Mark(in, "label");
    g.writeInt32(in, 1);                 // one mime type
    L.Mark(in, "mimeN");
    const char* kMime = "text/plain";
    g.writeString(in, kMime, (int32_t)std::strlen(kMime));
    L.Mark(in, "mime");
    // extras may be null — ClipDescription's constructor just stores whatever
    // readPersistableBundle returns.
    g.writeInt32(in, -1);
    L.Mark(in, "extras");
    g.writeInt64(in, 0);                 // timestamp; the service sets its own
    L.Mark(in, "stamp");
    g.writeInt32(in, 0);                 // isStyledText
    g.writeInt32(in, 0);                 // classification status
    L.Mark(in, "styled+class");
    // Confidences may NOT. The constructor ends with
    //
    //     readBundleToConfidences(in.readBundle());
    //
    // and that method opens with bundle.keySet(), unguarded — so a null here is
    // a NullPointerException inside the service, which comes back as exception
    // code -4 and nothing else. The real writer never sends null either:
    // confidencesToBundle() always returns a Bundle, usually an empty one. An
    // empty bundle is a lone zero on the wire, which is the same fact about
    // empty bundles that broke the read path, arriving from the other side.
    g.writeInt32(in, 0);
    L.Mark(in, "confid");
    g.writeInt32(in, 0);                 // no icon
    g.writeInt32(in, 1);                 // one item
    L.Mark(in, "icon+count");
    WriteCharSequence(in, text);         // item[0].mText
    L.Mark(in, "itemText");
    WriteString8(in, nullptr);           // htmlText
    L.Mark(in, "html");
    for (int i = 0; i < 5; ++i) g.writeInt32(in, 0);  // intent, sender, uri,
                                                      // activityInfo, textLinks
    L.Mark(in, "typed5");
    WriteCallerTail(in);
    L.Mark(in, "tail");
    L.Dump("write");

    void* rep = nullptr;
    const int32_t st = g.transact(svc, kSetPrimaryClip, &in, &rep, 0);
    bool ok = false;
    if (st == 0 && rep) {
        int32_t exc = -1;
        ok = (g.readInt32(rep, &exc) == 0 && exc == 0);
        if (!ok) {
            const std::string msg = ReadExceptionMessage(rep);
            g_error = "write refused (" + std::to_string(exc) + ")" +
                      (msg.empty() ? "" : ": " + msg);
            LOGI("[clip] write: service threw %d: %s", exc, msg.c_str());
        } else {
            LOGI("[clip] write: ok");
        }
    } else {
        g_error = "transact " + std::to_string(st);
        LOGI("[clip] write: transact failed, status %d", st);
    }
    if (g.deleteParcel) g.deleteParcel(rep);
    // After the parcel, for the same reason as the read path.
    g.decStrong(svc);
    return ok;
}

// ─── Which process sends it ──────────────────────────────────────────────
//
// Not this one. AppOpsService lets root name any package, which is what I first
// took to mean root could pass as shell. It cannot: ClipboardService resolves
// the caller separately, from Binder.getCallingUid(), and getPrimaryClip then
// calls addActiveOwnerLocked, which requires that uid to own the package it was
// handed. Root naming com.android.shell fails that — and the failure is not a
// refusal. It is caught, and the catch block does this:
//
//     Slog.i(TAG, "Could not grant permission to primary clip. Clearing clipboard.");
//     setPrimaryClipInternalLocked(null, intendingUid, intendingDeviceId, pkg);
//     return null;
//
// It wipes the user's clipboard and returns null, which is indistinguishable
// from an empty one. Every read after the first was then honestly reporting
// nothing there, because the first had destroyed it.
//
// So the transaction goes from a process that really is shell. fork alone
// cannot do it — libbinder's atfork handler poisons ProcessState in the child
// and the UI process has already initialised it through libgui — but exec
// replaces the address space, and the binder in the new image is clean.
constexpr uid_t kShellUid = 2000;
constexpr gid_t kShellGid = 2000;
constexpr const char* kFlagGet = "--aimgui-clip-get";
constexpr const char* kFlagSet = "--aimgui-clip-set";

// Distinct from failure. An empty clipboard is a legitimate answer and must not
// send the caller to the file fallback.
constexpr int kExitEmpty = 3;

bool Spawn(bool write, const char* text, std::string* out) {
    int to_child[2]   = {-1, -1};
    int from_child[2] = {-1, -1};
    if (pipe(to_child) != 0) { g_error = "pipe failed"; return false; }
    if (pipe(from_child) != 0) {
        close(to_child[0]); close(to_child[1]);
        g_error = "pipe failed";
        return false;
    }

    const pid_t pid = fork();
    if (pid < 0) {
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        g_error = "fork failed";
        return false;
    }

    if (pid == 0) {
        dup2(to_child[0], STDIN_FILENO);
        dup2(from_child[1], STDOUT_FILENO);
        close(to_child[0]); close(to_child[1]);
        close(from_child[0]); close(from_child[1]);
        // Still root here on purpose: /data is not readable by shell, so a
        // child that dropped privileges first could not exec itself.
        execl("/proc/self/exe", "AImGui", write ? kFlagSet : kFlagGet, (char*)nullptr);
        _exit(127);
    }

    close(to_child[0]);
    close(from_child[1]);
    if (write && text) {
        const size_t n = std::strlen(text);
        // Through a pipe rather than argv: the clipboard is the user's text,
        // and argv is readable by anything that can list processes.
        if (n) (void)!::write(to_child[1], text, n);
    }
    close(to_child[1]);

    std::string got;
    char buf[4096];
    ssize_t n;
    while ((n = read(from_child[0], buf, sizeof(buf))) > 0) got.append(buf, (size_t)n);
    close(from_child[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) { g_error = "clipboard helper crashed"; return false; }
    const int code = WEXITSTATUS(status);
    if (code == kExitEmpty) { if (out) out->clear(); g_error.clear(); return true; }
    if (code == 127)        { g_error = "helper could not exec"; return false; }
    // The helper prints its own reason to stderr on the way out — an exit code
    // is a byte and the diagnosis is a sentence — so this only has to say that
    // it failed and point at where the detail went.
    if (code != 0)          { g_error = "helper exit " + std::to_string(code) +
                                        ", see [clip] helper line"; return false; }
    g_error.clear();
    if (out) *out = std::move(got);
    return true;
}

} // namespace

bool Available() { return LoadNdk(); }
const char* LastError() { return g_error.c_str(); }

bool ReadText(std::string* out) { return Spawn(false, nullptr, out); }
bool WriteText(const char* text) { return Spawn(true, text ? text : "", nullptr); }

// Only stdin and stdout are redirected into the pipes, so the child's stderr is
// still the terminal the app was launched from. That is the one channel by
// which what it learned can escape: an exit code carries a byte, and the whole
// diagnosis — which service, which transaction, which parcel field — is a
// sentence. Without this the parent could only report "refused" and every
// distinct failure looked the same from outside.
void HelperSay(const char* what) {
    std::fprintf(stderr, "[clip] helper: %s\n", what);
    std::fflush(stderr);
}

int RunHelperMain(int argc, char** argv) {
    if (argc < 2) return -1;
    const bool get = std::strcmp(argv[1], kFlagGet) == 0;
    const bool set = std::strcmp(argv[1], kFlagSet) == 0;
    if (!get && !set) return -1;

    // Group before user: once the uid is dropped the gid can no longer be
    // changed. From here on this process genuinely is shell, which is the
    // entire point of it existing.
    setgroups(0, nullptr);
    if (setgid(kShellGid) != 0) { HelperSay("setgid failed"); return 1; }
    if (setuid(kShellUid) != 0) { HelperSay("setuid failed"); return 1; }
    if (getuid() != kShellUid) { HelperSay("uid did not drop"); return 1; }

    if (!LoadNdk()) { HelperSay("libbinder_ndk unavailable"); return 1; }

    if (get) {
        std::string text;
        if (!DoRead(&text)) {
            HelperSay(g_error.empty() ? "read failed" : g_error.c_str());
            return 1;
        }
        if (text.empty()) return kExitEmpty;
        (void)!::write(STDOUT_FILENO, text.data(), text.size());
        return 0;
    }

    std::string text;
    char buf[4096];
    ssize_t n;
    while ((n = read(STDIN_FILENO, buf, sizeof(buf))) > 0) text.append(buf, (size_t)n);
    if (DoWrite(text.c_str())) return 0;
    HelperSay(g_error.empty() ? "write failed" : g_error.c_str());
    return 1;
}

} // namespace aimgui::sysclip
