#include "clipboard_system.h"

#include <android/log.h>
#include <cstdint>
#include <cstring>
#include <string>
#include <dlfcn.h>
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
    bool ok = false;
    int32_t v = 0;
    do {
        if (g.readInt32(rep, &v) != 0) break;
        if (v != 0) {
            // The service refusing is the interesting case and reads nothing
            // like a layout mismatch, so it is worded as itself.
            g_error = "service refused (" + std::to_string(v) + ")";
            LOGI("[clip] read: service threw %d", v);
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

        step = "description label";
        std::string label;
        if (!ReadCharSequence(rep, &label)) break;

        step = "mime types";
        int32_t mimeCount = 0;
        if (g.readInt32(rep, &mimeCount) != 0) break;
        if (mimeCount > 64) break;
        for (int32_t i = 0; i < mimeCount; ++i) {
            std::string mime;
            // Mime types are Java Strings (utf-16 on the wire), unlike the
            // String8s above, so the NDK's own reader is right for them.
            struct A { static bool Alloc(void* d, int32_t len, char** buf) {
                auto* s = (std::string*)d;
                if (len < 0) { *buf = nullptr; return true; }
                if (len > kMaxField) { *buf = nullptr; return false; }
                s->assign((size_t)len, '\0');
                *buf = s->data();
                return true;
            } };
            if (g.readString(rep, &mime, (void*)&A::Alloc) != 0) { mimeCount = -1; break; }
        }
        if (mimeCount < 0) break;

        step = "extras bundle";
        if (!SkipBundle(rep)) break;
        step = "timestamp";
        int64_t ts = 0;
        if (g.readInt64(rep, &ts) != 0) break;
        step = "styled flag";
        if (g.readInt32(rep, &v) != 0) break;
        step = "classification";
        if (g.readInt32(rep, &v) != 0) break;
        step = "confidences bundle";
        if (!SkipBundle(rep)) break;
        step = "icon presence";
        if (g.readInt32(rep, &v) != 0) break;
        if (v != 0) { LOGI("[clip] read: clip carries an icon; not parsed"); break; }
        step = "item count";
        int32_t items = 0;
        if (g.readInt32(rep, &items) != 0 || items <= 0) break;
        step = "item text";
        ok = ReadCharSequence(rep, out);
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
    bool ok = false;
    if (st == 0 && rep) {
        int32_t exc = -1;
        ok = (g.readInt32(rep, &exc) == 0 && exc == 0);
        if (!ok) { g_error = "write refused (" + std::to_string(exc) + ")";
                   LOGI("[clip] write: service threw %d", exc); }
        else     LOGI("[clip] write: ok");
    } else {
        g_error = "transact " + std::to_string(st);
        LOGI("[clip] write: transact failed, status %d", st);
    }
    if (g.deleteParcel) g.deleteParcel(rep);
    // After the parcel, for the same reason as the read path.
    g.decStrong(svc);
    return ok;
}

// ─── No child process ────────────────────────────────────────────────────
// This ran in a forked child at first, to drop to shell's uid for the access
// check and to contain a parcel walk that might not match. Both reasons turned
// out to be wrong, the first fatally: libbinder installs an atfork handler that
// poisons ProcessState in the child, and this process has already initialised
// it through libgui, so the first transaction after the fork aborted with
// "libbinder ProcessState can not be used after fork".
//
// It was also unnecessary. AppOpsService.verifyAndGetBypass opens with
//
//     if (uid == Process.ROOT_UID) {
//         // For backwards compatibility, don't check package name for root UID.
//
// so root may name any package, and the permission that unlocks background
// reads is checked against the *package* — com.android.shell genuinely holds
// READ_CLIPBOARD_IN_BACKGROUND. Root claiming to be shell passes both.
//
// The containment the child gave up is replaced by the caps above and by
// checking every AParcel status: AParcel is bounds-checked and returns an
// error rather than running off the end, so a layout that does not match now
// fails the read instead of the process.

} // namespace

bool Available() { return LoadNdk(); }
const char* LastError() { return g_error.c_str(); }

bool ReadText(std::string* out) {
    if (!LoadNdk()) { g_error = "libbinder_ndk unavailable"; return false; }
    g_error.clear();
    if (!DoRead(out)) {
        if (g_error.empty()) g_error = "read failed";
        return false;
    }
    g_error.clear();
    return true;
}

bool WriteText(const char* text) {
    if (!LoadNdk()) { g_error = "libbinder_ndk unavailable"; return false; }
    g_error.clear();
    if (!DoWrite(text ? text : "")) {
        if (g_error.empty()) g_error = "write failed";
        return false;
    }
    g_error.clear();
    return true;
}

} // namespace aimgui::sysclip
