#include "clipboard_system.h"

#include <android/log.h>
#include <cstdint>
#include <cstring>
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

std::vector<int8_t>* g_sink = nullptr;
bool ByteArrayAllocator(void* /*data*/, int32_t length, int8_t** outBuffer) {
    if (length < 0) { *outBuffer = nullptr; return true; }
    g_sink->assign((size_t)length, 0);
    *outBuffer = g_sink->data();
    return true;
}

bool ReadString8(void* p, std::string* out) {
    const int32_t pos = g.getDataPos(p);
    int32_t n = 0;
    if (g.readInt32(p, &n) != 0) return false;
    if (n < 0) { out->clear(); g.setDataPos(p, pos + 4); return true; }
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

// [int32 length][int32 magic][length bytes], or a lone -1 for null. The length
// counts what follows the magic, so skipping is 8 + length.
bool SkipBundle(void* p) {
    int32_t len = 0;
    if (g.readInt32(p, &len) != 0) return false;
    if (len < 0) return true;
    return g.setDataPos(p, g.getDataPos(p) + 4 + len) == 0;
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

void* g_clazz = nullptr;

void* OpenService() {
    if (!LoadNdk()) return nullptr;
    if (!g_clazz) {
        g_clazz = g.classDefine(kDescriptor, (void*)&OnCreateStub,
                                (void*)&OnDestroyStub, (void*)&OnTransactStub);
        if (!g_clazz) return nullptr;
    }
    void* b = g.getService("clipboard");
    if (!b) return nullptr;
    // Not a formality: this asks the far end for its own descriptor and
    // compares, so success is proof the object really is IClipboard before a
    // transaction is sent whose meaning depends entirely on that.
    if (!g.associateClass(b, g_clazz)) {
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

bool DoRead(std::string* out) {
    void* svc = OpenService();
    if (!svc) return false;
    void* in = nullptr;
    if (g.prepare(svc, &in) != 0) { g.decStrong(svc); return false; }
    WriteCallerTail(in);

    void* rep = nullptr;
    const int32_t st = g.transact(svc, kGetPrimaryClip, &in, &rep, 0);
    g.decStrong(svc);
    if (st != 0 || !rep) { LOGI("[clip] transact failed: %d", st); return false; }

    bool ok = false;
    int32_t v = 0;
    do {
        if (g.readInt32(rep, &v) != 0 || v != 0) {          // exception header
            LOGI("[clip] service threw: %d", v);
            break;
        }
        if (g.readInt32(rep, &v) != 0 || v == 0) break;     // null ClipData
        // ClipDescription
        std::string label;
        if (!ReadCharSequence(rep, &label)) break;
        int32_t mimeCount = 0;
        if (g.readInt32(rep, &mimeCount) != 0) break;
        for (int32_t i = 0; i < mimeCount && i < 64; ++i) {
            std::string mime;
            g_sink = nullptr;
            // Mime types are Java Strings (utf-16 on the wire), unlike the
            // String8s above, so the NDK's own reader is right for them.
            struct A { static bool Alloc(void* d, int32_t len, char** buf) {
                auto* s = (std::string*)d;
                if (len < 0) { *buf = nullptr; return true; }
                s->assign((size_t)len, '\0');
                *buf = s->data();
                return true;
            } };
            if (g.readString(rep, &mime, (void*)&A::Alloc) != 0) { mimeCount = -1; break; }
        }
        if (mimeCount < 0) break;
        if (!SkipBundle(rep)) break;                        // extras
        int64_t ts = 0;
        if (g.readInt64(rep, &ts) != 0) break;              // timestamp
        if (g.readInt32(rep, &v) != 0) break;               // isStyledText
        if (g.readInt32(rep, &v) != 0) break;               // classification
        if (!SkipBundle(rep)) break;                        // confidences
        if (g.readInt32(rep, &v) != 0) break;               // icon present
        if (v != 0) { LOGI("[clip] clip carries an icon; not parsed"); break; }
        int32_t items = 0;
        if (g.readInt32(rep, &items) != 0 || items <= 0) break;
        ok = ReadCharSequence(rep, out);                    // item[0].mText
    } while (false);

    if (g.deleteParcel) g.deleteParcel(rep);
    return ok;
}

bool DoWrite(const char* text) {
    void* svc = OpenService();
    if (!svc) return false;
    void* in = nullptr;
    if (g.prepare(svc, &in) != 0) { g.decStrong(svc); return false; }

    g.writeInt32(in, 1);                 // ClipData is present
    WriteCharSequence(in, "AImGui");     // ClipDescription.mLabel
    g.writeInt32(in, 1);                 // one mime type
    const char* kMime = "text/plain";
    g.writeString(in, kMime, (int32_t)std::strlen(kMime));
    g.writeInt32(in, -1);                // extras: null PersistableBundle
    g.writeInt64(in, 0);                 // timestamp; the service sets its own
    g.writeInt32(in, 0);                 // isStyledText
    g.writeInt32(in, 0);                 // classification status
    g.writeInt32(in, -1);                // confidences: null Bundle
    g.writeInt32(in, 0);                 // no icon
    g.writeInt32(in, 1);                 // one item
    WriteCharSequence(in, text);         // item[0].mText
    WriteString8(in, nullptr);           // htmlText
    for (int i = 0; i < 5; ++i) g.writeInt32(in, 0);  // intent, sender, uri,
                                                      // activityInfo, textLinks
    WriteCallerTail(in);

    void* rep = nullptr;
    const int32_t st = g.transact(svc, kSetPrimaryClip, &in, &rep, 0);
    g.decStrong(svc);
    bool ok = false;
    if (st == 0 && rep) {
        int32_t exc = -1;
        ok = (g.readInt32(rep, &exc) == 0 && exc == 0);
        if (!ok) LOGI("[clip] set threw: %d", exc);
    } else {
        LOGI("[clip] set transact failed: %d", st);
    }
    if (g.deleteParcel) g.deleteParcel(rep);
    return ok;
}

// ─── The child ───────────────────────────────────────────────────────────
// Everything above runs here and nowhere else. Two reasons, and both matter:
// the uid has to be shell's for the access check to pass, and a parcel layout
// that turns out not to match the source it was written from should cost a
// process nobody will miss rather than the UI.
constexpr uid_t kShellUid = 2000;
constexpr gid_t kShellGid = 2000;

std::string g_error;

bool RunInShell(bool write, const char* text, std::string* out) {
    if (!LoadNdk()) { g_error = "libbinder_ndk unavailable"; return false; }

    int fds[2];
    if (pipe(fds) != 0) { g_error = "pipe failed"; return false; }

    const pid_t pid = fork();
    if (pid < 0) { close(fds[0]); close(fds[1]); g_error = "fork failed"; return false; }

    if (pid == 0) {
        close(fds[0]);
        // Group before user: once the uid is dropped the gid can no longer be
        // changed, and a mismatched pair is worse than either alone.
        setgroups(0, nullptr);
        setgid(kShellGid);
        if (setuid(kShellUid) != 0) _exit(2);
        std::string got;
        const bool ok = write ? DoWrite(text) : DoRead(&got);
        if (ok && !write && !got.empty())
            (void)!::write(fds[1], got.data(), got.size());
        close(fds[1]);
        _exit(ok ? 0 : 1);
    }

    close(fds[1]);
    std::string got;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) got.append(buf, (size_t)n);
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    const bool exited_ok = WIFEXITED(status) && WEXITSTATUS(status) == 0;
    if (!exited_ok) {
        if (WIFSIGNALED(status)) g_error = "clipboard helper crashed";
        else if (WIFEXITED(status) && WEXITSTATUS(status) == 2) g_error = "cannot drop to shell";
        else g_error = "clipboard refused";
        return false;
    }
    g_error.clear();
    if (out) *out = std::move(got);
    return true;
}

} // namespace

bool Available() { return LoadNdk(); }
const char* LastError() { return g_error.c_str(); }

bool ReadText(std::string* out)     { return RunInShell(false, nullptr, out); }
bool WriteText(const char* text)    { return RunInShell(true, text ? text : "", nullptr); }

} // namespace aimgui::sysclip
