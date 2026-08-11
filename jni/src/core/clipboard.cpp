#include "clipboard.h"

#include "imgui.h"

#include <cstdio>
#include <string>

namespace aimgui {
namespace clipboard {
namespace {

constexpr const char* kPath = "/data/local/tmp/aimgui.clip";
constexpr size_t      kMax  = 64 * 1024;

// ImGui hands out the pointer returned from the get handler and reads it before
// calling anything else, so a single buffer owned here is enough and saves
// handing ownership back and forth.
std::string g_text;

const char* GetFn(ImGuiContext*) { return Get(); }
void        SetFn(ImGuiContext*, const char* text) { Set(text); }

} // namespace

const char* Path() { return kPath; }

void Set(const char* text) {
    if (!text) text = "";
    g_text = text;
    // Whole-file replace through a temp, same as the config: a reader that
    // catches us mid-write would otherwise get a truncated string rather than
    // the old one.
    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", kPath);
    if (FILE* f = std::fopen(tmp, "wb")) {
        std::fwrite(g_text.data(), 1, g_text.size(), f);
        std::fclose(f);
        std::rename(tmp, kPath);
    }
}

const char* Get() {
    // Read every time rather than trusting the copy in memory: the whole point
    // of the file is that something outside this process can put text there
    // while it runs, and a cache would never see it.
    FILE* f = std::fopen(kPath, "rb");
    if (!f) return g_text.c_str();

    std::string in;
    char buf[4096];
    size_t n;
    while ((n = std::fread(buf, 1, sizeof(buf), f)) > 0) {
        in.append(buf, n);
        if (in.size() > kMax) { in.resize(kMax); break; }
    }
    std::fclose(f);

    // A file written with `echo` picks up a trailing newline that nobody meant
    // to paste.
    while (!in.empty() && (in.back() == '\n' || in.back() == '\r')) in.pop_back();
    g_text = std::move(in);
    return g_text.c_str();
}

void Install() {
    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = &GetFn;
    pio.Platform_SetClipboardTextFn = &SetFn;
    pio.Platform_ClipboardUserData  = nullptr;
}

} // namespace clipboard
} // namespace aimgui
