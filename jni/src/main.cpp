#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/keyboard_input.h"
#include "core/clipboard.h"
#include "core/clipboard_system.h"
#include "core/config.h"
#include "core/haptics.h"
#include "core/screen_mirror.h"
#include "core/sensor_tilt.h"
#include "core/text_outline.h"

#include <android/hardware_buffer.h>
#include "core/window_session.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "ui/ui.h"
#include "ui/icons.h"
#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <csignal>
#include <chrono>

#ifdef AIMGUI_LIVE2D

static void Live2DScenePreDraw() { aimgui::live2d::Draw(); }
#endif

#define BOOT(step) __android_log_print(ANDROID_LOG_INFO, "AImGui", "[boot] " step)

int main(int argc, char** argv) {

    if (const int rc = aimgui::sysclip::RunHelperMain(argc, argv); rc != -1) return rc;

    using namespace android;
    using clock = std::chrono::steady_clock;

    BOOT("display info");
    auto info = ANativeWindowCreator::GetDisplayInfo();
    const int W = info.width > info.height ? info.width : info.height;
    const int H = info.width > info.height ? info.height : info.width;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    BOOT("fonts");
    aimgui::LoadDefaultAndSystemCJKFont(25.0f);
    aimgui::clipboard::Install();

    aimgui::UiState st;
    st.display_w = info.width; st.display_h = info.height;

    BOOT("config");
    aimgui::config::Load(&st);

    BOOT("haptics");
    aimgui::Haptics haptics;
    haptics.Init();
    aimgui::haptic::Install(&haptics, &st.haptics_enabled);

    st.island_icon = ICON_FA_BOLT;
    st.dot_text    = ICON_FA_ROBOT;
    st.card_icon   = ICON_FA_CUBE;

    BOOT("surface + renderer");
    aimgui::WindowSession ws;
    if (!ws.Build(W, st.permeate_record)) { ImGui::DestroyContext(); return 1; }
    st.renderer_name = ws.renderer()->Name();
    BOOT("touch");
    Touch::Init({(float)W, (float)H}, false);
    st.block_touch_ok = true;
    // An exclusive grab outlives the process that took it, so every way out of
    // here has to give it back — including the ones nobody plans for. The
    // watchdog inside Touch covers a hang; this covers a crash.
    {
        struct sigaction sa{};
        sa.sa_handler = [](int sig) {
            Touch::EmergencyRelease();
            signal(sig, SIG_DFL);
            raise(sig);
        };
        sigemptyset(&sa.sa_mask);
        for (int sig : { SIGSEGV, SIGBUS, SIGILL, SIGFPE, SIGABRT, SIGTERM, SIGINT })
            sigaction(sig, &sa, nullptr);
    }
    Touch::setOrientation((int)info.orientation);
    BOOT("keyboard");
    aimgui::kbd_input::Init();

#ifdef AIMGUI_LIVE2D

    if (const aimgui::Live2DVkContext* l2dctx = ws.renderer()->GetLive2DVkContext()) {
        if (aimgui::live2d::VkInit(l2dctx)) {
            aimgui::live2d::Resize(info.width, info.height);

            if (!aimgui::live2d::LoadEmbedded())
                aimgui::live2d::AutoLoad("/data/local/tmp/live2d");
        }
        ws.renderer()->SetScenePreDraw(&Live2DScenePreDraw);
    }

    st.stage = aimgui::UiState::StageIsland;
    st.expand = 0.0f;
#endif

    aimgui::ScreenMirror mirror;

    BOOT("sensors");
    aimgui::SensorTilt tilt;
    tilt.Init();
    BOOT("entering main loop");
    aimgui::FramePacer pacer;

    int frames_presented = 0;
    constexpr int kMirrorHoldoff = 12;
    auto last = clock::now();

    auto        cfg_dirty_since = clock::now();
    bool        cfg_dirty       = false;
    auto last_display_poll = last;
    uint32_t orient = info.orientation;
    bool running = true;
    while (running) {
        auto now = clock::now();
        io.DeltaTime = std::max(1e-6f, std::chrono::duration<float>(now - last).count());
        last = now;
        pacer.SetTargetFps(st.target_fps);

        if (now - last_display_poll >= std::chrono::milliseconds(200)) {
            last_display_poll = now;
            info = ANativeWindowCreator::GetDisplayInfo();
            st.display_w = info.width; st.display_h = info.height;
            if (info.orientation != orient) { orient = info.orientation; Touch::setOrientation((int)orient); }
        }

        if (aimgui::kbd_input::ConsumeVolumePresses() > 0) {
            st.stage = (st.stage == aimgui::UiState::StageIsland)
                           ? aimgui::UiState::StageWindow
                           : aimgui::UiState::StageIsland;
        }

        if (mirror.NeedsRestart(info.width, info.height)) {
            mirror.Stop();
        }
        if (st.screen_mirror && !mirror.running() && frames_presented >= kMirrorHoldoff) {
            mirror.Start(info.width / 2, info.height / 2, info.width, info.height);
        } else if (!st.screen_mirror && mirror.running()) {
            mirror.Stop();
        }

        {
            const bool want_hidden = mirror.running() && st.mirror_hides_window;
            static bool hidden_now = false;
            static bool hidden_known = false;
            if (!hidden_known || want_hidden != hidden_now) {
                ANativeWindowCreator::SetSkipScreenshot(ws.window(), want_hidden);
                hidden_now   = want_hidden;
                hidden_known = true;
            }
        }
        mirror.Update();
        if (mirror.running()) {

            if (AHardwareBuffer* ahb = mirror.AcquireLatest()) {
                const unsigned long long id = ws.renderer()->ImportHardwareBuffer(
                    ahb, mirror.width(), mirror.height());
                if (id) st.screen_texture_id = id;
            }
            st.screen_mirror_frames = mirror.frames();
            st.screen_mirror_w      = mirror.width();
            st.screen_mirror_h      = mirror.height();
        } else {
            st.screen_texture_id = 0;
        }
        st.screen_mirror_running = mirror.running();

        tilt.Update(io.DeltaTime);
        if (tilt.Available()) {
            st.glass_light_x = -0.6f + tilt.x() * 0.9f;
            st.glass_light_y = -0.8f + tilt.y() * 0.9f;

            st.tilt_x = tilt.x();
            st.tilt_y = tilt.y();
        }

        if (!st.permeate_record) ANativeWindowCreator::ProcessMirrorDisplay();
        aimgui::kbd_input::Flush();

        ws.renderer()->NewFrame();
#ifdef AIMGUI_LIVE2D

        aimgui::live2d::Resize(info.width, info.height);
        aimgui::live2d::SetLookScreen(io.MousePos.x, io.MousePos.y, io.MouseDown[0]);
        aimgui::live2d::Update(io.DeltaTime);
#endif
        st.scene_snapshot_id = ws.renderer()->GetSceneSnapshotID();
        ImGui::NewFrame();
        aimgui::DrawUi(&st, &running);
#ifdef AIMGUI_LIVE2D

        aimgui::live2d::SetBall(st.ball_pos.x, st.ball_pos.y);
        aimgui::live2d::SetBallScale(st.ball_scale);
        aimgui::live2d::SetView(st.expand);
#endif
        st.widget_glass_ok = ws.renderer()->SupportsWidgetGlass();
        ws.renderer()->SetWidgetGlass(st.widget_glass ? st.widget_rects : nullptr,
                                      st.widget_glass ? st.widget_count : 0);
        ws.renderer()->SetGlassRects(st.glass_rects, st.glass_count,
                                     st.display_w, st.display_h);
        ws.renderer()->SetBloomIntensity(st.bloom_intensity);
        ws.renderer()->SetSnapshotFrozen(st.exit_anim_active);
        ws.renderer()->EndFrame();
        if (frames_presented <= kMirrorHoldoff) {
            if (++frames_presented == 1) BOOT("first frame presented");
        }
        pacer.Wait();

        if (aimgui::config::Dirty(&st)) {
            if (!cfg_dirty) { cfg_dirty = true; cfg_dirty_since = now; }
        }
        if (cfg_dirty && now - cfg_dirty_since >= std::chrono::milliseconds(1200)) {
            aimgui::config::Save(&st);
            cfg_dirty = false;
        }

        if (st.request_permeate_toggle) {
            st.request_permeate_toggle = false;
            st.permeate_record = !st.permeate_record;
#ifdef AIMGUI_LIVE2D

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

    aimgui::config::Save(&st);
    haptics.Shutdown();
    tilt.Shutdown();
    aimgui::kbd_input::Shutdown();
    ws.Destroy();

    aimgui::ShutdownTextOutline();
    ImGui::DestroyContext();
}
