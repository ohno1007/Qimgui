#include "qimgui/android_overlay.hpp"

#include "core/crash_diagnostics.h"
#include "core/font.h"
#include "core/keyboard_input.h"
#include "runtime/simple_renderer.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <sys/system_properties.h>
#include <unistd.h>

namespace qimgui::android {
namespace {
void Stage(const char* name) { aimgui::diagnostics::SetStage(name); }
void Boot(const char* name) {
    Stage(name);
    std::fprintf(stderr, "[overlay] %s\n", name);
}
ImVec4 InputBounds(const Frame& frame) {
    const auto r = frame.inputBounds;
    if (!std::isfinite(r.x) || !std::isfinite(r.y) ||
        !std::isfinite(r.z) || !std::isfinite(r.w) || r.z <= 0 || r.w <= 0)
        return ImVec4(0, 0, 0, 0);
    const float x = std::clamp(r.x, 0.f, frame.displaySize.x);
    const float y = std::clamp(r.y, 0.f, frame.displaySize.y);
    const float right = std::clamp(r.x + r.z, x, frame.displaySize.x);
    const float bottom = std::clamp(r.y + r.w, y, frame.displaySize.y);
    return ImVec4(x, y, right - x, bottom - y);
}
} // namespace

int RunOverlay(const OverlayOptions& options, FrameCallback callback, void* userData) {
    using namespace aimgui;
    using ::android::ANativeWindowCreator;
    using Clock = std::chrono::steady_clock;
    if (!callback || ImGui::GetCurrentContext()) {
        std::fprintf(stderr, "[overlay] requires a callback and no existing ImGui context\n");
        return 1;
    }
    const char* title = options.title && options.title[0] ? options.title : "Qimgui MD3";
    std::setvbuf(stderr, nullptr, _IONBF, 0);
    diagnostics::InstallCrashHandler(Touch::EmergencyRelease);
    char release[PROP_VALUE_MAX]{}, sdk[PROP_VALUE_MAX]{};
    __system_property_get("ro.build.version.release", release);
    __system_property_get("ro.build.version.sdk", sdk);
    std::fprintf(stderr, "[overlay] %s; Android %s, SDK %s, pid %ld\n",
                 title, release, sdk, static_cast<long>(getpid()));

    Boot("display info");
    auto info = ANativeWindowCreator::GetDisplayInfo();
    if (info.width <= 0 || info.height <= 0) {
        std::fprintf(stderr, "[overlay] display query failed\n");
        return 1;
    }
    const int side = std::max(info.width, info.height);
    const int shortSide = std::min(info.width, info.height);
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard | ImGuiConfigFlags_NoMouseCursorChange;
    io.ConfigWindowsMoveFromTitleBarOnly = true;

    Boot("MD3 fonts");
    const auto fonts = LoadMd3Fonts();
    io.FontDefault = fonts.body;
    Frame frame;
    frame.bodyFont = fonts.body;
    frame.labelFont = fonts.label;
    Boot("surface + OpenGL ES 3");
    ANativeWindow* window = ANativeWindowCreator::Create(title, side, side, false);
    if (!window) {
        std::fprintf(stderr, "[overlay] surface creation failed\n");
        ImGui::DestroyContext();
        return 1;
    }
    detail::SimpleRenderer renderer;
    if (!renderer.Init(window, side, side)) {
        ANativeWindowCreator::Destroy(window);
        ImGui::DestroyContext();
        return 1;
    }
    diagnostics::RefreshLoadedModules();
    Boot("touch");
    const bool touchReady = Touch::Init({static_cast<float>(side), static_cast<float>(shortSide)}, false);
    if (!touchReady) std::fprintf(stderr, "[overlay] touch unavailable; keyboard navigation remains available\n");
    Touch::setOrientation(static_cast<int>(info.orientation));
    Boot("keyboard");
    kbd_input::Init();
    Boot("recording mirror enabled");

    bool running = true, firstFrame = true;
    int result = 0;
    auto last = Clock::now();
    auto lastDisplayPoll = last;
    Boot("entering application loop");
    while (running) {
        const auto now = Clock::now();
        io.DeltaTime = std::max(1e-6f, std::chrono::duration<float>(now - last).count());
        last = now;
        Touch::Heartbeat();
        if (now - lastDisplayPoll >= std::chrono::milliseconds(200)) {
            Stage("overlay-display-poll");
            lastDisplayPoll = now;
            const auto updated = ANativeWindowCreator::GetDisplayInfo();
            if (updated.width > 0 && updated.height > 0) {
                if (updated.orientation != info.orientation)
                    Touch::setOrientation(static_cast<int>(updated.orientation));
                info = updated;
            }
        }
        // Recorder displays may use their own layer stack on Android 13+.
        // Continue polling while hidden to handle disconnects and rotation.
        // The adapter throttles internally; Destroy(window) releases mirrors.
        Stage("overlay-recording-mirror");
        ANativeWindowCreator::ProcessMirrorDisplay();
        Stage("overlay-keyboard");
        kbd_input::Flush();
        if (kbd_input::ConsumeVolumePresses() % 2 != 0) frame.visible = !frame.visible;
        frame.displaySize = ImVec2(static_cast<float>(info.width), static_cast<float>(info.height));
        frame.inputBounds = ImVec4(0, 0, 0, 0);
        Stage("overlay-new-frame");
        renderer.NewFrame(frame.displaySize);
        ImGui::NewFrame();
        Stage("overlay-application");
        running = callback(frame, userData);

        if (frame.visible && running) {
            const bool popup = ImGui::IsPopupOpen(nullptr, ImGuiPopupFlags_AnyPopup);
            const ImVec4 bounds = popup ? ImVec4(0, 0, frame.displaySize.x, frame.displaySize.y) : InputBounds(frame);
            Touch::SetBlockRegion(bounds.x, bounds.y, bounds.z, bounds.w, touchReady);
        } else Touch::SetBlockRegion(0, 0, 0, 0, false);
        Stage("overlay-present");
        if (!renderer.EndFrame()) { result = 1; break; }
        if (firstFrame) { Boot("first frame presented"); firstFrame = false; }
    }

    Boot("shutdown");
    Touch::SetBlockRegion(0, 0, 0, 0, false);
    Touch::EmergencyRelease();
    kbd_input::Shutdown();
    renderer.Shutdown();
    ANativeWindowCreator::Destroy(window);
    std::fprintf(stderr, "[overlay] exited (%d)\n", result);
    // Active touch readers still hold Device/IO
    // references. The process owns this host and the OS reclaims those workers.
    std::_Exit(result);
}
} // namespace qimgui::android
