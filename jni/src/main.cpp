// AImGui: a minimal Dear ImGui Android ARM64 ELF.
#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/keyboard_input.h"
#include "core/window_session.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "ui/ui.h"
#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <chrono>

int main() {
    using namespace android;
    using clock = std::chrono::steady_clock;

    auto info = ANativeWindowCreator::GetDisplayInfo();
    const int W = info.width > info.height ? info.width : info.height;
    const int H = info.width > info.height ? info.height : info.width;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // ImGui 1.92's stb font loader raises a *recoverable* IM_ASSERT_USER_ERROR
    // when a system font fails to parse. By default that aborts the process
    // (SIGABRT) on first text render. Disable the assert so a bad/unsupported
    // system font degrades gracefully (logged, skipped) instead of crashing.
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    aimgui::LoadDefaultAndSystemCJKFont(25.0f);

    aimgui::UiState st;
    st.display_w = info.width; st.display_h = info.height;

    aimgui::WindowSession ws;
    if (!ws.Build(W, st.permeate_record)) { ImGui::DestroyContext(); return 1; }
    st.renderer_name = ws.renderer()->Name();
    Touch::Init({(float)W, (float)H}, false);
    Touch::setOrientation((int)info.orientation);
    aimgui::kbd_input::Init();

#ifdef AIMGUI_LIVE2D
    // Optional Live2D layer. Model assets are pushed to the device under
    // /data/local/tmp/live2d/<Model>/ (see docs/LIVE2D.md). Non-fatal if absent.
    if (aimgui::live2d::Init()) {
        aimgui::live2d::Resize(info.width, info.height);  // size masks before load
        aimgui::live2d::AutoLoad("/data/local/tmp/live2d");
    }
#endif

    aimgui::FramePacer pacer;
    auto last = clock::now();
    uint32_t orient = info.orientation;
    bool running = true;
    while (running) {
        auto now = clock::now();
        io.DeltaTime = std::max(1e-6f, std::chrono::duration<float>(now - last).count());
        last = now;
        pacer.SetTargetFps(st.target_fps);
        info = ANativeWindowCreator::GetDisplayInfo();
        st.display_w = info.width; st.display_h = info.height;
        if (info.orientation != orient) { orient = info.orientation; Touch::setOrientation((int)orient); }
        if (aimgui::kbd_input::ConsumeVolumePresses() > 0) st.collapsed = !st.collapsed;
        if (!st.permeate_record) ANativeWindowCreator::ProcessMirrorDisplay();
        aimgui::kbd_input::Flush();

        ws.renderer()->NewFrame();
#ifdef AIMGUI_LIVE2D
        // Draw the model into the freshly-begun GL framebuffer; ImGui (rendered
        // in EndFrame) then composites its UI on top.
        aimgui::live2d::Resize(info.width, info.height);
        aimgui::live2d::Update(io.DeltaTime);
        aimgui::live2d::Draw();
#endif
        st.scene_snapshot_id = ws.renderer()->GetSceneSnapshotID();
        ImGui::NewFrame();
        aimgui::DrawUi(&st, &running);
        ws.renderer()->SetBloomIntensity(st.bloom_intensity);
        ws.renderer()->SetSnapshotFrozen(st.exit_anim_active);
        ws.renderer()->EndFrame();
        pacer.Wait();

        if (st.request_permeate_toggle) {
            st.request_permeate_toggle = false;
            st.permeate_record = !st.permeate_record;
            ws.Destroy();
            if (!ws.Build(W, st.permeate_record)) { running = false; break; }
            st.renderer_name = ws.renderer()->Name();
        }
    }
#ifdef AIMGUI_LIVE2D
    aimgui::live2d::Shutdown();
#endif
    aimgui::kbd_input::Shutdown();
    ws.Destroy();
    ImGui::DestroyContext();
}
