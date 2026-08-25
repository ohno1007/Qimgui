#include "clipboard.h"

#include "clipboard_system.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace aimgui {
namespace clipboard {
namespace {

constexpr const char* kPath = "/data/local/tmp/aimgui.clip";
constexpr size_t      kMax  = 64 * 1024;

std::string g_text;
bool        g_used_system = false;

const char* GetFn(ImGuiContext*) { return Get(); }
void        SetFn(ImGuiContext*, const char* text) { Set(text); }

}

const char* Path() { return kPath; }
bool UsedSystem() { return g_used_system; }
const char* SystemError() { return sysclip::LastError(); }

void Report(const char* op, bool system_path, size_t n, const char* why = nullptr) {
    if (system_path) {
        std::fprintf(stderr, "[clip] %s: system clipboard, %zu bytes\n", op, n);
    } else {

        std::fprintf(stderr, "[clip] %s: file, %zu bytes%s%s\n", op, n,
                     (why && *why) ? " — " : "", (why && *why) ? why : "");
    }
    std::fflush(stderr);
}

void Set(const char* text) {
    if (!text) text = "";
    g_text = text;

    g_used_system = false;
    Report("copy", false, g_text.size());

    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", kPath);
    if (FILE* f = std::fopen(tmp, "wb")) {
        std::fwrite(g_text.data(), 1, g_text.size(), f);
        std::fclose(f);
        std::rename(tmp, kPath);
    }
}

const char* Get() {

    std::string sys;
    if (sysclip::ReadText(&sys)) {
        g_used_system = true;
        g_text = std::move(sys);
        Report("paste", true, g_text.size());
        return g_text.c_str();
    }
    g_used_system = false;

    FILE* f = std::fopen(kPath, "rb");
    if (!f) { Report("paste", false, 0, sysclip::LastError()); return g_text.c_str(); }

    std::string in;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        in.append(buf, n);
        if (in.size() > kMax) { in.resize(kMax); break; }
    }
    std::fclose(f);

    while (!in.empty() && (in.back() == '\n' || in.back() == '\r')) in.pop_back();
    g_text = std::move(in);
    Report("paste", false, g_text.size(), sysclip::LastError());
    return g_text.c_str();
}

void Install() {

    std::fprintf(stderr, "[clip] ready, file fallback at %s\n", kPath);
    std::fflush(stderr);

    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = &GetFn;
    pio.Platform_SetClipboardTextFn = &SetFn;
    pio.Platform_ClipboardUserData  = nullptr;
}

}
}
