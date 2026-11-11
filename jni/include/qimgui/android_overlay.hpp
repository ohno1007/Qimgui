#pragma once

#include "imgui.h"

namespace qimgui::android {

struct Frame {
    ImVec2 displaySize;
    ImFont* bodyFont = nullptr;
    ImFont* labelFont = nullptr;
    // Volume keys toggle this value. A callback may also hide the window.
    bool visible = true;
    // x/y/width/height in display pixels. Set each frame to the application's
    // visible window bounds; zero means touches pass through to other apps.
    ImVec4 inputBounds = ImVec4(0, 0, 0, 0);
};

struct OverlayOptions {
    const char* title = "Qimgui MD3";
};

// Runs after ImGui::NewFrame and before ImGui::Render. Also called while hidden,
// so application clocks and state may advance. Return false to close the host.
// Fonts and the ImGui context belong to the runtime. The callback must balance
// its ImGui stacks and must not create/destroy the context or throw exceptions.
using FrameCallback = bool (*)(Frame& frame, void* userData);

// Android/root process host, intended to be called once from main(). Requires
// the optional Qimgui::AndroidOverlay SDK target. This owns the Surface, GLES3
// renderer, fonts, keyboard/touch input and frame loop; no MD3 UI is included.
// Android 13+ recording-display mirrors are maintained by the platform adapter
// (polled every 3 seconds). Known ROM interfaces are required; failures log.
// Initialization failures return a nonzero status. After input workers start,
// shutdown releases input and exits the process because the shared platform
// touch backend retains worker references to ImGuiIO. Save application state
// in the callback before returning false; do not rely on main() destructors.
int RunOverlay(const OverlayOptions& options, FrameCallback callback,
               void* userData = nullptr);

} // namespace qimgui::android
