// AImGui: a minimal Dear ImGui Android ARM64 ELF.
#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/keyboard_input.h"
#include "core/clipboard.h"
#include "core/clipboard_system.h"
#include "core/config.h"
#include "core/haptics.h"
#include "core/screen_mirror.h"
#include "core/sensor_tilt.h"

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

#include <chrono>

#ifdef AIMGUI_LIVE2D
// Drawn into the scene framebuffer (before ImGui) via the renderer hook.
static void Live2DScenePreDraw() { aimgui::live2d::Draw(); }
#endif

// Startup markers. Everything below runs before the first frame is on screen,
// so a hang in any of it looks identical from the outside: a full-screen
// surface that never draws and never lets a touch through, which on a phone is
// indistinguishable from the device itself locking up. These make logcat say
// which step it stopped at instead of leaving it to guesswork.
#define BOOT(step) __android_log_print(ANDROID_LOG_INFO, "AImGui", "[boot] " step)

int main(int argc, char** argv) {
    // Before anything else. When this process was spawned to run a clipboard
    // transaction as shell it must not build a UI, take a surface, or touch
    // SurfaceFlinger — it exists for one binder call and then exits.
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
    // ImGui moves a window when dragged from anywhere in it by default, which
    // collides head-on with dragging the content to scroll. Restrict its own
    // moves to the title bar; ContentGesture in ui.cpp decides for itself when
    // a drag in the content should move the window instead of scrolling, and
    // drives the position directly on those frames.
    io.ConfigWindowsMoveFromTitleBarOnly = true;
    // ImGui 1.92's stb font loader raises a *recoverable* IM_ASSERT_USER_ERROR
    // when a system font fails to parse. By default that aborts the process
    // (SIGABRT) on first text render. Disable the assert so a bad/unsupported
    // system font degrades gracefully (logged, skipped) instead of crashing.
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    BOOT("fonts");
    aimgui::LoadDefaultAndSystemCJKFont(25.0f);
    aimgui::clipboard::Install();

    aimgui::UiState st;
    st.display_w = info.width; st.display_h = info.height;
    // Before the window is built: permeate_record decides how the surface is
    // created, so it has to be known by then rather than applied afterwards
    // through the rebuild path.
    BOOT("config");
    aimgui::config::Load(&st);

    BOOT("haptics");
    aimgui::Haptics haptics;
    haptics.Init();
    aimgui::haptic::Install(&haptics, &st.haptics_enabled);
    // Defaults for the three rest states. All optional — null on any of these
    // falls back to what the UI showed before there was a way to set them.
    st.island_icon = ICON_FA_BOLT;
    st.dot_text    = ICON_FA_ROBOT;
    st.card_icon   = ICON_FA_CUBE;

    BOOT("surface + renderer");
    aimgui::WindowSession ws;
    if (!ws.Build(W, st.permeate_record)) { ImGui::DestroyContext(); return 1; }
    st.renderer_name = ws.renderer()->Name();
    BOOT("touch");
    Touch::Init({(float)W, (float)H}, false);
    Touch::setOrientation((int)info.orientation);
    BOOT("keyboard");
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
    st.stage = aimgui::UiState::StageIsland;
    st.expand = 0.0f;
#endif

    aimgui::ScreenMirror mirror;
    // Optional: no sensor is reachable from a package-less process on some
    // builds, in which case the glass keeps its fixed key light.
    BOOT("sensors");
    aimgui::SensorTilt tilt;
    tilt.Init();
    BOOT("entering main loop");
    aimgui::FramePacer pacer;
    // The mirror is a persisted setting now, so it can be on before the first
    // frame has ever been presented — which it never was when it could only be
    // switched on from a running UI. Starting it there means building a virtual
    // display and powering it on while our own surface has not yet been through
    // a composition cycle, so it waits for the window to be up and drawing.
    int frames_presented = 0;
    constexpr int kMirrorHoldoff = 12;
    auto last = clock::now();
    // Settings are written a beat after they stop changing, not on every frame
    // a slider is being dragged — the file would otherwise be rewritten 120
    // times a second for the length of the drag.
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
        // Volume key jumps between the two ends rather than stepping, so it
        // stays a one-press show/hide however far the window is opened.
        if (aimgui::kbd_input::ConsumeVolumePresses() > 0) {
            st.stage = (st.stage == aimgui::UiState::StageIsland)
                           ? aimgui::UiState::StageWindow
                           : aimgui::UiState::StageIsland;
        }

        // Live screen mirror. Half the display's resolution is plenty for a
        // blurred/refracted backdrop and halves the compositor's scaling work.
        // A rotation invalidates the mirror: its reader is sized for one
        // orientation and the virtual display's projection is fixed to the
        // dimensions it was created with. Neither resizes in place, so rebuild.
        if (mirror.NeedsRestart(info.width, info.height)) {
            mirror.Stop();
        }
        if (st.screen_mirror && !mirror.running() && frames_presented >= kMirrorHoldoff) {
            mirror.Start(info.width / 2, info.height / 2, info.width, info.height);
        } else if (!st.screen_mirror && mirror.running()) {
            mirror.Stop();
        }
        // Keeping our own output out of the frames we sample is what stops the
        // mirror feeding the window back into itself until the glass saturates
        // — but the same flag is what a screenshot obeys, so it is applied only
        // while both the mirror is running and the user has left it on. Pushed
        // only on a change: it is a binder round-trip to SurfaceFlinger.
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
            // Import straight to a texture — no copy, the image aliases the
            // memory SurfaceFlinger composited into. Keep the previous handle
            // on a frame where nothing new arrived so the backdrop holds
            // rather than blinking.
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
        // Steer the glass's key light by how the panel is leaning. A highlight
        // that never moves reads as painted on; one that sweeps as the device
        // tilts is most of what sells the rim as a reflection.
        tilt.Update(io.DeltaTime);
        if (tilt.Available()) {
            st.glass_light_x = -0.6f + tilt.x() * 0.9f;
            st.glass_light_y = -0.8f + tilt.y() * 0.9f;
            // Same reading drives the island itself, which slides downhill as
            // the panel leans.
            st.tilt_x = tilt.x();
            st.tilt_y = tilt.y();
        }

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
    // The exit animation runs for over a second after the button is pressed, so
    // there is always time to get this out before the process goes.
    aimgui::config::Save(&st);
    haptics.Shutdown();
    tilt.Shutdown();
    aimgui::kbd_input::Shutdown();
    ws.Destroy();
    ImGui::DestroyContext();
}
