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

// ImGui hands out the pointer returned from the get handler and reads it before
// calling anything else, so a single buffer owned here is enough and saves
// handing ownership back and forth.
std::string g_text;
bool        g_used_system = false;

const char* GetFn(ImGuiContext*) { return Get(); }
void        SetFn(ImGuiContext*, const char* text) { Set(text); }

} // namespace

const char* Path() { return kPath; }
bool UsedSystem() { return g_used_system; }
const char* SystemError() { return sysclip::LastError(); }

// Also to the terminal this was launched from, not only to logcat and the UI.
//
// With the glass on, the window sets skipScreenshot — so the one surface that
// says what happened is the one surface that cannot be captured, and asking for
// a screenshot of it is asking for the impossible. stderr is not redirected and
// lands in the shell the binary was exec'd from, which is somewhere the answer
// can actually be read.
void Report(const char* op, bool system_path, size_t n, const char* why = nullptr) {
    if (system_path) {
        std::fprintf(stderr, "[clip] %s: system clipboard, %zu bytes\n", op, n);
    } else {
        // The reason is passed in, not fetched, so a copy — which never touches
        // the Binder path — cannot pick up the last paste's error and print it
        // as if it explained why the copy went to a file. The copy had no other
        // destination; only a paste has a reason worth showing.
        std::fprintf(stderr, "[clip] %s: file, %zu bytes%s%s\n", op, n,
                     (why && *why) ? " — " : "", (why && *why) ? why : "");
    }
    std::fflush(stderr);
}

void Set(const char* text) {
    if (!text) text = "";
    g_text = text;
    // File only. Writing to the Android clipboard was built and abandoned; see
    // the note at the foot of clipboard_system.h for what was ruled out and
    // what is left to try. This still gives text a way out of the process —
    // `cat` the path — and it is what ImGui's own copy handler lands on.
    g_used_system = false;
    Report("copy", false, g_text.size());   // no reason: copy only ever writes the file
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
    // The real clipboard first; the file is what is left when the transaction
    // is refused or the service is not there at all.
    std::string sys;
    if (sysclip::ReadText(&sys)) {
        g_used_system = true;
        g_text = std::move(sys);
        Report("paste", true, g_text.size());
        return g_text.c_str();
    }
    g_used_system = false;

    // Read every time rather than trusting the copy in memory: the whole point
    // of the file is that something outside this process can put text there
    // while it runs, and a cache would never see it.
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

    // A file written with `echo` picks up a trailing newline that nobody meant
    // to paste.
    while (!in.empty() && (in.back() == '\n' || in.back() == '\r')) in.pop_back();
    g_text = std::move(in);
    Report("paste", false, g_text.size(), sysclip::LastError());
    return g_text.c_str();
}

void Install() {
    // Unconditional, at startup, on the same channel copy and paste report on.
    // It settles two things that otherwise have to be guessed at from a device
    // the developer cannot reach: that the binary running is the new one, and
    // that stderr reaches the terminal at all. Without it, "nothing printed"
    // has three explanations and no way to tell them apart.
    std::fprintf(stderr, "[clip] ready, file fallback at %s\n", kPath);
    std::fflush(stderr);

    ImGuiPlatformIO& pio = ImGui::GetPlatformIO();
    pio.Platform_GetClipboardTextFn = &GetFn;
    pio.Platform_SetClipboardTextFn = &SetFn;
    pio.Platform_ClipboardUserData  = nullptr;
}

} // namespace clipboard
} // namespace aimgui
