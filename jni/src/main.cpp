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

#ifdef AIMGUI_LIVE2D
// Drawn into the scene framebuffer (before ImGui) via the renderer hook.
static void Live2DScenePreDraw() { aimgui::live2d::Draw(); }
#endif

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
    // Optional Live2D layer, built on Cubism's Vulkan renderer sharing this
    // renderer's device/queue. Non-fatal if the backend or model is absent.
    if (const aimgui::Live2DVkContext* l2dctx = ws.renderer()->GetLive2DVkContext()) {
        if (aimgui::live2d::VkInit(l2dctx)) {
            aimgui::live2d::Resize(info.width, info.height);  // size masks before load
            // Prefer the model baked into the binary; fall back to /data/local/tmp.
            if (!aimgui::live2d::LoadEmbedded())
                aimgui::live2d::AutoLoad("/data/local/tmp/live2d");
        }
        ws.renderer()->SetScenePreDraw(&Live2DScenePreDraw);
    }
    // Boot as the floating ball (the character), not the open window.
    st.collapsed = true;
    st.expand = 0.0f;
#endif

    aimgui::FramePacer pacer;
    auto last = clock::now();
    auto last_display_poll = last;
    uint32_t orient = info.orientation;
    bool running = true;
    while (running) {
        auto now = clock::now();
        io.DeltaTime = std::max(1e-6f, std::chrono::duration<float>(now - last).count());
        last = now;
        pacer.SetTargetFps(st.target_fps);
        // GetDisplayInfo() is a binder round-trip to SurfaceFlinger. Polling
        // it per frame costs one IPC every frame (120/s on a 120 Hz panel)
        // to watch for a rotation that happens maybe once a minute. Poll at
        // 5 Hz instead — 200 ms of latency is invisible next to the ~300 ms
        // system rotation animation.
        if (now - last_display_poll >= std::chrono::milliseconds(200)) {
            last_display_poll = now;
            info = ANativeWindowCreator::GetDisplayInfo();
            st.display_w = info.width; st.display_h = info.height;
            if (info.orientation != orient) { orient = info.orientation; Touch::setOrientation((int)orient); }
        }
        if (aimgui::kbd_input::ConsumeVolumePresses() > 0) st.collapsed = !st.collapsed;
        if (!st.permeate_record) ANativeWindowCreator::ProcessMirrorDisplay();
        aimgui::kbd_input::Flush();

        ws.renderer()->NewFrame();
#ifdef AIMGUI_LIVE2D
        // Advance the model here; the actual draw happens in the scene-predraw
        // hook so ImGui composites on top of it. The eyes follow the touch.
        aimgui::live2d::Resize(info.width, info.height);
        aimgui::live2d::SetLookScreen(io.MousePos.x, io.MousePos.y, io.MouseDown[0]);
        aimgui::live2d::Update(io.DeltaTime);
#endif
        st.scene_snapshot_id = ws.renderer()->GetSceneSnapshotID();
        ImGui::NewFrame();
        aimgui::DrawUi(&st, &running);
#ifdef AIMGUI_LIVE2D
        // The character is the collapsed floating ball (drawn at ball_pos) and
        // shrinks away as the window expands.
        aimgui::live2d::SetBall(st.ball_pos.x, st.ball_pos.y);
        aimgui::live2d::SetBallScale(st.ball_scale);
        aimgui::live2d::SetView(st.expand);
#endif
        ws.renderer()->SetBloomIntensity(st.bloom_intensity);
        ws.renderer()->SetSnapshotFrozen(st.exit_anim_active);
        ws.renderer()->EndFrame();
        pacer.Wait();

        if (st.request_permeate_toggle) {
            st.request_permeate_toggle = false;
            st.permeate_record = !st.permeate_record;
#ifdef AIMGUI_LIVE2D
            // The renderer (and its Vulkan device) is torn down and rebuilt, so
            // the Cubism renderer bound to the old device must be released and
            // re-initialised against the new one.
            aimgui::live2d::Shutdown();
#endif
            ws.Destroy();
            if (!ws.Build(W, st.permeate_record)) { running = false; break; }
            st.renderer_name = ws.renderer()->Name();
#ifdef AIMGUI_LIVE2D
            if (const aimgui::Live2DVkContext* l2dctx = ws.renderer()->GetLive2DVkContext()) {
                if (aimgui::live2d::VkInit(l2dctx)) {
                    aimgui::live2d::Resize(info.width, info.height);
                    if (!aimgui::live2d::LoadEmbedded())
                        aimgui::live2d::AutoLoad("/data/local/tmp/live2d");
                }
                ws.renderer()->SetScenePreDraw(&Live2DScenePreDraw);
            }
#endif
        }
    }
#ifdef AIMGUI_LIVE2D
    aimgui::live2d::Shutdown();
#endif
    aimgui::kbd_input::Shutdown();
    ws.Destroy();
    ImGui::DestroyContext();
}
