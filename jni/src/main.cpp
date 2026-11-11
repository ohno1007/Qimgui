#include "core/font.h"
#include "core/keyboard_input.h"
#include "core/config.h"
#include "core/crash_diagnostics.h"
#include "core/haptics.h"
#include "core/screen_mirror.h"
#include "core/sensor_tilt.h"

#include <android/hardware_buffer.h>
#include "core/window_session.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "ui/ui.h"
#include "ui/md3.h"
#include "imgui_internal.h"
#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <algorithm>
#include <csignal>
#include <cstdlib>
#include <cstdio>
#include <sys/system_properties.h>
#include <unistd.h>
#include <chrono>

#ifdef AIMGUI_LIVE2D

static void Live2DScenePreDraw() { aimgui::live2d::Draw(); }
#endif

#if defined(AIMGUI_DIAGNOSTICS)
#define DIAG_STAGE(name) aimgui::diagnostics::SetStage(name)
#else
#define DIAG_STAGE(name) ((void)0)
#endif

// stderr survives early startup failures even when logcat is unavailable.
#define BOOT(step) do { \
    DIAG_STAGE(step); \
    std::fprintf(stderr, "[boot] %s\n", step); \
    __android_log_print(ANDROID_LOG_INFO, "AImGui", "[boot] " step); \
} while (0)

namespace {
void DispatchUiHaptics() {
    const ImGuiContext& context = *ImGui::GetCurrentContext();
    const auto& released = context.DeactivatedItemData;
    // ButtonBehavior reports accepted activations here for MD3 controls and
    // navigation. Wait for release so dragging/scrolling does not buzz.
    const bool pointer_press = context.IO.MouseReleased[ImGuiMouseButton_Left] &&
        released.ID != 0 && released.ID == context.HoveredId && released.IsAlive &&
        released.ElapseFrame >= context.FrameCount && context.ActiveIdHasBeenPressedBefore &&
        !ImGui::IsMouseDragPastThreshold(ImGuiMouseButton_Left) && !context.DragDropActive;
    const bool navigation_press = context.NavActivatePressedId != 0 &&
        (context.ActiveId == context.NavActivatePressedId || released.ID == context.NavActivatePressedId) &&
        context.ActiveIdHasBeenPressedBefore;
    if (pointer_press || navigation_press) aimgui::haptic::Tap();
}
} // namespace

int main(int argc, char** argv) {

    using namespace android;
    using clock = std::chrono::steady_clock;

    std::setvbuf(stderr, nullptr, _IONBF, 0);
#if defined(AIMGUI_DIAGNOSTICS)
    aimgui::diagnostics::InstallCrashHandler(Touch::EmergencyRelease);
#endif
    char release[PROP_VALUE_MAX] = {}, sdk[PROP_VALUE_MAX] = {};
    __system_property_get("ro.build.version.release", release);
    __system_property_get("ro.build.version.sdk", sdk);
    std::fprintf(stderr, "[boot] Android %s, SDK %s, pid %ld\n", release, sdk, static_cast<long>(getpid()));
    BOOT("display info");
    auto info = ANativeWindowCreator::GetDisplayInfo();
    if (info.width <= 0 || info.height <= 0) {
        std::fprintf(stderr, "[boot] display query failed; no valid display dimensions.\n");
        return 1;
    }
    std::fprintf(stderr, "[boot] display %d x %d\n", info.width, info.height);
    const int W = info.width > info.height ? info.width : info.height;
    const int H = info.width > info.height ? info.height : info.width;

    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange | ImGuiConfigFlags_NavEnableKeyboard;

    io.ConfigWindowsMoveFromTitleBarOnly = true;

    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    BOOT("MD3 fonts");
    const auto fonts = aimgui::LoadMd3Fonts();
    io.FontDefault = fonts.body;
    aimgui::md3::SetFonts(fonts.body, fonts.label);

    aimgui::UiState st;
    st.display_w = info.width; st.display_h = info.height;

    BOOT("config");
    aimgui::config::Load(&st);
#if defined(AIMGUI_DIAGNOSTICS)
    std::fprintf(stderr, "[boot] screen mirror requested: %d\n", st.screen_mirror);
#endif

    BOOT("haptics");
    aimgui::Haptics haptics;
    haptics.Init();
    aimgui::haptic::Install(&haptics, &st.haptics_enabled);

    BOOT("glass availability");
    st.glass_available = aimgui::ScreenMirror::Available();
    std::fprintf(stderr, "[boot] glass capture available: %d\n", st.glass_available);

    BOOT("surface + renderer");
    aimgui::WindowSession ws;
    if (!ws.Build(W, st.permeate_record)) {
        std::fprintf(stderr, "[boot] surface or renderer initialization failed.\n");
        ImGui::DestroyContext(); return 1;
    }
    st.renderer_name = ws.renderer()->Name();
    std::fprintf(stderr, "[boot] renderer %s\n", st.renderer_name);
#if defined(AIMGUI_DIAGNOSTICS)
    aimgui::diagnostics::RefreshLoadedModules();
#endif
    BOOT("touch");
    const bool touch_ready = Touch::Init({(float)W, (float)H}, false);
    st.block_touch_ok = touch_ready;

#if !defined(AIMGUI_DIAGNOSTICS)
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
#endif
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

    aimgui::SensorTilt tilt;
    bool tilt_attempted = false;
    BOOT("entering main loop");

    int frames_presented = 0;
    constexpr int kMirrorHoldoff = 12;
    auto last = clock::now();
    auto next_mirror_attempt = last;

    auto        cfg_dirty_since = clock::now();
    bool        cfg_dirty       = false;
    auto last_display_poll = last;
    uint32_t orient = info.orientation;
    bool running = true;
    bool hidden_now = false, hidden_known = false;
    bool visibility_error_reported = false;
    bool visibility_target = false, visibility_target_known = false;
    auto next_visibility_attempt = last;
    int exit_code = 0;
    const auto sync_capture_visibility = [&](bool hidden, clock::time_point now) {
        if (!visibility_target_known || visibility_target != hidden) {
            visibility_target = hidden;
            visibility_target_known = true;
            visibility_error_reported = false;
            next_visibility_attempt = now;
        }
        if ((hidden_known && hidden_now == hidden) || now < next_visibility_attempt) return;
        DIAG_STAGE("window-capture-flag");
        if (ANativeWindowCreator::SetSkipScreenshot(ws.window(), hidden)) {
            hidden_now = hidden;
            hidden_known = true;
            visibility_error_reported = false;
        } else {
            if (!visibility_error_reported)
                std::fprintf(stderr, "[recording] could not apply capture visibility (hidden=%d); retrying\n", hidden);
            visibility_error_reported = true;
            next_visibility_attempt = now + std::chrono::seconds(3);
        }
    };
    const auto clear_mirror_state = [&] {
        st.screen_texture_id = 0;
        st.screen_mirror_running = false;
        st.screen_mirror_frames = 0;
        st.screen_mirror_w = st.screen_mirror_h = 0;
    };
    const auto stop_tilt = [&] {
        if (tilt_attempted) tilt.Shutdown();
        tilt_attempted = false;
        st.tilt_x = st.tilt_y = 0;
        st.glass_light_x = -0.6f;
        st.glass_light_y = -0.8f;
    };
    while (running) {
        auto now = clock::now();
        io.DeltaTime = std::max(1e-6f, std::chrono::duration<float>(now - last).count());
        last = now;
        Touch::Heartbeat();

        if (now - last_display_poll >= std::chrono::milliseconds(200)) {
            last_display_poll = now;
            DIAG_STAGE("display-poll");
            const auto updated_info = ANativeWindowCreator::GetDisplayInfo();
            if (updated_info.width > 0 && updated_info.height > 0)
                info = updated_info;
            st.display_w = info.width; st.display_h = info.height;
            if (info.orientation != orient) { orient = info.orientation; Touch::setOrientation((int)orient); }
        }

        if (aimgui::kbd_input::ConsumeVolumePresses() > 0) {
            st.stage = (st.stage == aimgui::UiState::StageIsland)
                           ? aimgui::UiState::StageWindow
                           : aimgui::UiState::StageIsland;
        }

        const bool want_glass = st.screen_mirror && st.glass_available;
        DIAG_STAGE("screen-mirror");
        if (mirror.NeedsRestart(info.width, info.height)) {
            DIAG_STAGE("mirror-stop-resize");
            ws.renderer()->ReleaseHardwareBuffers();
            mirror.Stop();
            clear_mirror_state();
        }
        if (!want_glass) {
            next_mirror_attempt = now;
            st.glass_failed = false;
        }
        if (want_glass && !mirror.running() && frames_presented >= kMirrorHoldoff && now >= next_mirror_attempt) {
            DIAG_STAGE("mirror-start");
            st.glass_failed = !mirror.Start(info.width / 2, info.height / 2, info.width, info.height);
            if (st.glass_failed) next_mirror_attempt = now + std::chrono::seconds(5);
        } else if (!want_glass && mirror.running()) {
            DIAG_STAGE("mirror-stop-disabled");
            ws.renderer()->ReleaseHardwareBuffers();
            mirror.Stop();
            clear_mirror_state();
        }

        // Glass capture must exclude its own output to avoid feedback, but
        // stopping it must never clear the user's explicit privacy setting.
        sync_capture_visibility(st.permeate_record || (want_glass && mirror.running()), now);
        DIAG_STAGE("mirror-update");
        mirror.Update();
        if (mirror.running()) {

            DIAG_STAGE("mirror-acquire-image");
            if (AHardwareBuffer* ahb = mirror.AcquireLatest()) {
                DIAG_STAGE("mirror-gpu-import");
                const unsigned long long id = ws.renderer()->ImportHardwareBuffer(
                    ahb, mirror.width(), mirror.height());
                if (id) st.screen_texture_id = id;
            }
            st.screen_mirror_frames = mirror.frames();
            st.screen_mirror_w      = mirror.width();
            st.screen_mirror_h      = mirror.height();
        } else {
            clear_mirror_state();
        }
        st.screen_mirror_running = mirror.running();

        DIAG_STAGE("sensor-events");
        if (want_glass && !tilt_attempted) {
            tilt_attempted = true;
            tilt.Init();
        } else if (!want_glass) {
            stop_tilt();
        }
        if (want_glass) tilt.Update(io.DeltaTime);
        if (want_glass && tilt.Available()) {
            st.glass_light_x = -0.6f + tilt.x() * 0.9f;
            st.glass_light_y = -0.8f + tilt.y() * 0.9f;

            st.tilt_x = tilt.x();
            st.tilt_y = tilt.y();
        }

        DIAG_STAGE("display-mirror-transaction");
        if (!st.permeate_record) ANativeWindowCreator::ProcessMirrorDisplay();
        DIAG_STAGE("keyboard-events");
        aimgui::kbd_input::Flush();

        DIAG_STAGE("renderer-new-frame");
        ws.renderer()->NewFrame();
        // Popup layout uses the visible screen, not the square rotation buffer.
        io.DisplaySize = ImVec2(static_cast<float>(info.width), static_cast<float>(info.height));
#ifdef AIMGUI_LIVE2D

        aimgui::live2d::Resize(info.width, info.height);
        aimgui::live2d::SetLookScreen(io.MousePos.x, io.MousePos.y, io.MouseDown[0]);
        aimgui::live2d::Update(io.DeltaTime);
#endif
        st.scene_snapshot_id = ws.renderer()->GetSceneSnapshotID();
        DIAG_STAGE("imgui-new-frame");
        ImGui::NewFrame();
        DIAG_STAGE("draw-ui");
        aimgui::DrawUi(&st, &running);
        DispatchUiHaptics();
        const ImVec4 input_bounds = st.modal_open
            ? ImVec4(0, 0, static_cast<float>(info.width), static_cast<float>(info.height))
            : st.interaction_rect;
        Touch::SetBlockRegion(input_bounds.x, input_bounds.y, input_bounds.z, input_bounds.w,
                              running && st.block_touch && st.block_touch_ok &&
                              input_bounds.z > 0 && input_bounds.w > 0);
        st.block_touch_on = Touch::Blocking();

        // The MD3 switch takes effect on this frame; stale captures are never
        // submitted after the user turns the material off.
        if (!st.screen_mirror || !st.glass_available) {
            if (mirror.running()) {
                ws.renderer()->ReleaseHardwareBuffers();
                mirror.Stop();
            }
            clear_mirror_state();
            st.glass_failed = false;
            st.glass_count = 0;
            stop_tilt();
            sync_capture_visibility(st.permeate_record, now);
        }
#ifdef AIMGUI_LIVE2D

        aimgui::live2d::SetBall(st.ball_pos.x, st.ball_pos.y);
        aimgui::live2d::SetBallScale(st.ball_scale);
        aimgui::live2d::SetView(st.expand);
#endif
        // Components keep MD3 colours and geometry in both material modes.
        st.widget_count = 0;
        ws.renderer()->SetWidgetGlass(nullptr, 0);
        const bool glass_visible = st.screen_mirror && st.screen_texture_id != 0;
        ws.renderer()->SetGlassRects(glass_visible ? st.glass_rects : nullptr,
                                     glass_visible ? st.glass_count : 0, st.display_w, st.display_h);
        ws.renderer()->SetBloomIntensity(0);
        ws.renderer()->SetSnapshotFrozen(false);
        DIAG_STAGE("renderer-end-frame");
        ws.renderer()->EndFrame();
        if (frames_presented <= kMirrorHoldoff) {
            if (++frames_presented == 1) BOOT("first frame presented");
        }

        DIAG_STAGE("config-save");
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
            ws.renderer()->ReleaseHardwareBuffers();
            mirror.Stop();
            clear_mirror_state();
            ws.Destroy();
            if (!ws.Build(W, st.permeate_record)) { exit_code = 1; running = false; break; }
            hidden_known = false;
            visibility_error_reported = false;
            next_visibility_attempt = clock::now();
            frames_presented = 0;
            next_mirror_attempt = clock::now();
            st.renderer_name = ws.renderer()->Name();
#if defined(AIMGUI_DIAGNOSTICS)
            aimgui::diagnostics::RefreshLoadedModules();
#endif
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
    Touch::SetBlockRegion(0, 0, 0, 0, false);
    Touch::EmergencyRelease();
    if (ws.renderer()) ws.renderer()->ReleaseHardwareBuffers();
    mirror.Stop();
    haptics.Shutdown();
    tilt.Shutdown();
    aimgui::kbd_input::Shutdown();
    ws.Destroy();

    // TouchHelper retains unjoined readers that can still access ImGuiIO.
    // Keep their context/storage alive until process exit after explicit device
    // and renderer cleanup, as in the standalone MD3Demo entry point.
    std::_Exit(exit_code);
}
