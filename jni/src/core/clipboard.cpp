#include "clipboard.h"

#include "java_bridge.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace aimgui {
namespace clipboard {
namespace {

std::string g_text;
bool        g_used_system = false;

const char* GetFn(ImGuiContext*) { return Get(); }
void        SetFn(ImGuiContext*, const char* text) { Set(text); }

}

const char* Path() { return "Java Clipboard"; }
bool UsedSystem() { return g_used_system; }
const char* SystemError() { return java_bridge::LastError(); }

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

    g_used_system = java_bridge::SetClipboard(g_text.c_str());
    Report("copy", g_used_system, g_text.size(), g_used_system ? nullptr : java_bridge::LastError());
}

const char* Get() {

    std::string sys;
    if (java_bridge::GetClipboard(&sys)) {
        g_used_system = true;
        g_text = std::move(sys);
        Report("paste", true, g_text.size());
        return g_text.c_str();
    }
    g_used_system = false;
    g_text.clear();
    Report("paste", false, 0, java_bridge::LastError());
    return g_text.c_str();
}

void Install() {

    std::fprintf(stderr, "[clip] Java clipboard bridge ready\n");
    std::fflush(stderr);

    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = &GetFn;
    pio.Platform_SetClipboardTextFn = &SetFn;
    pio.Platform_ClipboardUserData  = nullptr;
}

}
}
