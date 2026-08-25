#include "clipboard_system.h"

#include <android/log.h>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dlfcn.h>
#include <string>
#include <sys/wait.h>
#include <unistd.h>
#include <vector>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AImGui", __VA_ARGS__)

namespace aimgui::sysclip {
namespace {

struct Ndk {
    void*   (*getService)(const char*)                             = nullptr;
    void*   (*classDefine)(const char*, void*, void*, void*)       = nullptr;
    bool    (*associateClass)(void*, void*)                        = nullptr;
    void    (*decStrong)(void*)                                    = nullptr;
    int32_t (*prepare)(void*, void**)                              = nullptr;
    int32_t (*transact)(void*, uint32_t, void**, void**, uint32_t) = nullptr;
    int32_t (*writeInt32)(void*, int32_t)                          = nullptr;
    int32_t (*writeString)(void*, const char*, int32_t)            = nullptr;
    int32_t (*readInt32)(const void*, int32_t*)                    = nullptr;
    int32_t (*readInt64)(const void*, int64_t*)                    = nullptr;
    int32_t (*readString)(const void*, void*, void*)               = nullptr;
    int32_t (*readByteArray)(const void*, void*, void*)            = nullptr;
    int32_t (*getDataPos)(const void*)                             = nullptr;
    int32_t (*setDataPos)(const void*, int32_t)                    = nullptr;
    void    (*deleteParcel)(void*)                                 = nullptr;
    bool ok = false;
};
Ndk g;

std::string g_error;

int   OnTransactStub(void*, uint32_t, const void*, void*) { return -38; }
void* OnCreateStub(void* a) { return a; }
void  OnDestroyStub(void*) {}

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
    g.writeString    = (decltype(g.writeString))    S("AParcel_writeString");
    g.readInt32      = (decltype(g.readInt32))      S("AParcel_readInt32");
    g.readInt64      = (decltype(g.readInt64))      S("AParcel_readInt64");
    g.readString     = (decltype(g.readString))     S("AParcel_readString");
    g.readByteArray  = (decltype(g.readByteArray))  S("AParcel_readByteArray");
    g.getDataPos     = (decltype(g.getDataPos))     S("AParcel_getDataPosition");
    g.setDataPos     = (decltype(g.setDataPos))     S("AParcel_setDataPosition");
    g.deleteParcel   = (decltype(g.deleteParcel))   S("AParcel_delete");
    g.ok = g.getService && g.classDefine && g.associateClass && g.prepare &&
           g.transact && g.writeInt32 && g.writeString && g.readInt32 &&
           g.readInt64 && g.readString && g.readByteArray && g.getDataPos &&
           g.setDataPos;
    return g.ok;
}

int32_t Align4(int32_t n) { return (n + 3) & ~3; }

constexpr int32_t kMaxField = 1 << 20;

std::vector<int8_t>* g_sink = nullptr;
bool ByteArrayAllocator(void* , int32_t length, int8_t** outBuffer) {
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
    g.setDataPos(p, pos + 4 + Align4(n + 1));
    return true;
}

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

    while (!out->empty() && out->back() == '\0') out->pop_back();
    return true;
}

std::string ReadExceptionMessage(void* p) {
    std::string msg;
    if (!ReadJavaString(p, &msg)) return "";
    return msg;
}

bool SkipBundle(void* p) {
    int32_t len = 0;
    if (g.readInt32(p, &len) != 0) return false;
    if (len <= 0) return true;
    if (len > kMaxField) return false;
    return g.setDataPos(p, g.getDataPos(p) + 4 + len) == 0;
}

bool ReadCharSequence(void* p, std::string* out) {
    int32_t kind = 0;
    if (g.readInt32(p, &kind) != 0) return false;
    return ReadString8(p, out);
}

constexpr uint32_t kGetPrimaryClip = 4;

constexpr const char* kDescriptor = "android.content.IClipboard";
constexpr const char* kPackage    = "com.android.shell";

void* g_clazz = nullptr;

void* OpenService() {
    if (!LoadNdk()) return nullptr;
    if (!g_clazz) {
        g_clazz = g.classDefine(kDescriptor, (void*)&OnCreateStub,
                                (void*)&OnDestroyStub, (void*)&OnTransactStub);
        if (!g_clazz) return nullptr;
    }
    void* b = g.getService("clipboard");
    if (!b) { g_error = "no clipboard service"; return nullptr; }

    if (!g.associateClass(b, g_clazz)) {
        g_error = "not IClipboard";
        g.decStrong(b);
        return nullptr;
    }
    return b;
}

bool DoRead(std::string* out) {
    void* svc = OpenService();
    if (!svc) return false;
    void* in = nullptr;
    if (g.prepare(svc, &in) != 0) { g.decStrong(svc); return false; }

    g.writeString(in, kPackage, (int32_t)std::strlen(kPackage));
    g.writeInt32(in, -1);
    g.writeInt32(in, 0);
    g.writeInt32(in, 0);

    void* rep = nullptr;
    const int32_t st = g.transact(svc, kGetPrimaryClip, &in, &rep, 0);
    if (st != 0 || !rep) {
        g_error = "transact " + std::to_string(st);
        g.decStrong(svc);
        return false;
    }

    const char* step = "exception header";
    bool ok = false;
    int32_t v = 0;
    do {
        if (g.readInt32(rep, &v) != 0) break;
        if (v != 0) {
            const std::string msg = ReadExceptionMessage(rep);
            g_error = "service refused (" + std::to_string(v) + ")" +
                      (msg.empty() ? "" : ": " + msg);
            step = nullptr;
            break;
        }

        step = "clip presence";
        if (g.readInt32(rep, &v) != 0) break;
        if (v == 0) {

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
            if (!ReadJavaString(rep, &mime)) { mimeCount = -1; break; }
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

        if (v != 0 && v != 1) { step = "icon presence (bad flag, layout drift)"; break; }
        if (v == 1) { step = "icon present, not parsed"; break; }
        step = "item count";
        int32_t items = 0;
        if (g.readInt32(rep, &items) != 0) break;
        if (items <= 0 || items > 64) { step = "item count (out of range, layout drift)"; break; }

        step = "item text";
        ok = ReadCharSequence(rep, out);
    } while (false);

    if (!ok && step) {
        g_error = std::string("stopped at ") + step;
        LOGI("[clip] read stopped at '%s'", step);
    }

    if (rep && g.deleteParcel) g.deleteParcel(rep);

    g.decStrong(svc);
    return ok;
}

constexpr uid_t kShellUid = 2000;
constexpr gid_t kShellGid = 2000;
constexpr const char* kFlagGet = "--aimgui-clip-get";

constexpr int kExitEmpty = 3;

void HelperSay(const char* what) {
    std::fprintf(stderr, "[clip] helper: %s\n", what);
    std::fflush(stderr);
}

bool Spawn(std::string* out) {
    int fds[2] = {-1, -1};
    if (pipe(fds) != 0) { g_error = "pipe failed"; return false; }

    const pid_t pid = fork();
    if (pid < 0) {
        close(fds[0]); close(fds[1]);
        g_error = "fork failed";
        return false;
    }

    if (pid == 0) {
        dup2(fds[1], STDOUT_FILENO);
        close(fds[0]); close(fds[1]);

        execl("/proc/self/exe", "AImGui", kFlagGet, (char*)nullptr);
        _exit(127);
    }

    close(fds[1]);
    std::string got;
    char buf[4096];
    ssize_t n;
    while ((n = read(fds[0], buf, sizeof(buf))) > 0) got.append(buf, (size_t)n);
    close(fds[0]);

    int status = 0;
    waitpid(pid, &status, 0);
    if (!WIFEXITED(status)) { g_error = "clipboard helper crashed"; return false; }
    const int code = WEXITSTATUS(status);
    if (code == kExitEmpty) { if (out) out->clear(); g_error.clear(); return true; }
    if (code == 127)        { g_error = "helper could not exec"; return false; }
    if (code != 0)          { g_error = "helper exit " + std::to_string(code) +
                                        ", see [clip] helper line"; return false; }
    g_error.clear();
    if (out) *out = std::move(got);
    return true;
}

}

const char* LastError() { return g_error.c_str(); }
bool ReadText(std::string* out) { return Spawn(out); }

int RunHelperMain(int argc, char** argv) {
    if (argc < 2 || std::strcmp(argv[1], kFlagGet) != 0) return -1;

    setgroups(0, nullptr);
    if (setgid(kShellGid) != 0) { HelperSay("setgid failed"); return 1; }
    if (setuid(kShellUid) != 0) { HelperSay("setuid failed"); return 1; }
    if (getuid() != kShellUid)  { HelperSay("uid did not drop"); return 1; }
    if (!LoadNdk())             { HelperSay("libbinder_ndk unavailable"); return 1; }

    std::string text;
    if (!DoRead(&text)) {
        HelperSay(g_error.empty() ? "read failed" : g_error.c_str());
        return 1;
    }
    if (text.empty()) return kExitEmpty;
    (void)!::write(STDOUT_FILENO, text.data(), text.size());
    return 0;
}

}
