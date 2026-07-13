// AImGui APK entry point.
//
// This is the non-root port of jni/src/main.cpp. The original creates a
// SurfaceFlinger overlay via ANativeWindowCreator and reads /dev/input/*
// directly — both require root. Here we run inside a normal, unprivileged
// android.app.NativeActivity: the window comes from the Activity's own
// Surface and input arrives as AInputEvents. Everything below the window /
// input boundary — the GL/VK renderers, bloom, CJK font loader and the whole
// ImGui UI (ui.cpp / main_ui.cpp) — is reused verbatim from jni/.
#include <android_native_app_glue.h>

#include <android/input.h>
#include <android/keycodes.h>
#include <android/log.h>
#include <android/native_activity.h>
#include <android/native_window.h>

#include <algorithm>
#include <chrono>
#include <memory>

#include "imgui.h"

#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/renderer.h"
#include "ui/ui.h"

#define LOG_TAG "AImGui"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

namespace {

struct Engine {
    struct android_app*                app          = nullptr;
    std::unique_ptr<aimgui::IRenderer> renderer;
    aimgui::UiState                    st;
    bool                               imgui_ready  = false;
    int                                width        = 0;
    int                                height       = 0;
    std::chrono::steady_clock::time_point last;
};

// One-time ImGui context + font setup. Mirrors the head of the original
// main() so behaviour (no ini/log files, recoverable font asserts, dark
// theme, 25px CJK-capable font) is identical.
void InitImGuiOnce(Engine* e) {
    if (e->imgui_ready) return;
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    ImGuiIO& io = ImGui::GetIO();
    io.IniFilename = nullptr;
    io.LogFilename = nullptr;
    io.ConfigFlags |= ImGuiConfigFlags_NoMouseCursorChange;
    // ImGui 1.92's stb font loader raises a *recoverable* IM_ASSERT_USER_ERROR
    // when a system font fails to parse; without this it would SIGABRT on the
    // first text render. Degrade gracefully instead.
    io.ConfigErrorRecoveryEnableAssert = false;
    ImGui::StyleColorsDark();
    aimgui::LoadDefaultAndSystemCJKFont(25.0f);
    e->imgui_ready = true;
}

void CreateRenderer(Engine* e) {
    if (!e->app->window) return;
    e->width  = ANativeWindow_getWidth(e->app->window);
    e->height = ANativeWindow_getHeight(e->app->window);
    if (e->width <= 0 || e->height <= 0) return;
    e->renderer = aimgui::MakeRenderer(e->app->window, e->width, e->height,
                                       aimgui::Backend::Auto);
    if (e->renderer) {
        e->st.renderer_name = e->renderer->Name();
        e->st.display_w = e->width;
        e->st.display_h = e->height;
        LOGI("renderer up: %s (%dx%d)", e->st.renderer_name, e->width, e->height);
    } else {
        LOGW("no renderer backend could initialise");
    }
}

void DestroyRenderer(Engine* e) {
    if (e->renderer) {
        e->renderer->Shutdown();
        e->renderer.reset();
    }
}

// Lifecycle callbacks from native_app_glue (run on the app thread).
void HandleCmd(struct android_app* app, int32_t cmd) {
    Engine* e = static_cast<Engine*>(app->userData);
    switch (cmd) {
        case APP_CMD_INIT_WINDOW:
            InitImGuiOnce(e);
            CreateRenderer(e);
            break;
        case APP_CMD_TERM_WINDOW:
            DestroyRenderer(e);
            break;
        default:
            // Rotations / resizes are picked up per-frame by comparing the
            // live ANativeWindow size against the renderer's, so nothing to
            // do for WINDOW_RESIZED / CONFIG_CHANGED here.
            break;
    }
}

// Touch → ImGui pointer, volume keys → Dynamic Island collapse toggle.
// The original derived the same io state from raw /dev/input events; here it
// comes from the framework, so no root and no coordinate scaling is needed —
// AMotionEvent coordinates are already in this window's pixels.
int32_t HandleInput(struct android_app* app, AInputEvent* ev) {
    Engine* e = static_cast<Engine*>(app->userData);
    if (!e->imgui_ready || ImGui::GetCurrentContext() == nullptr) return 0;
    ImGuiIO& io = ImGui::GetIO();

    const int32_t type = AInputEvent_getType(ev);
    if (type == AINPUT_EVENT_TYPE_MOTION) {
        const int32_t action = AMotionEvent_getAction(ev) & AMOTION_EVENT_ACTION_MASK;
        const float x = AMotionEvent_getX(ev, 0);
        const float y = AMotionEvent_getY(ev, 0);
        switch (action) {
            case AMOTION_EVENT_ACTION_DOWN:
            case AMOTION_EVENT_ACTION_POINTER_DOWN:
                io.AddMousePosEvent(x, y);
                io.AddMouseButtonEvent(0, true);
                break;
            case AMOTION_EVENT_ACTION_MOVE:
                io.AddMousePosEvent(x, y);
                break;
            case AMOTION_EVENT_ACTION_UP:
            case AMOTION_EVENT_ACTION_POINTER_UP:
            case AMOTION_EVENT_ACTION_CANCEL:
                io.AddMouseButtonEvent(0, false);
                break;
            default:
                break;
        }
        return 1;
    }

    if (type == AINPUT_EVENT_TYPE_KEY) {
        const int32_t code   = AKeyEvent_getKeyCode(ev);
        const int32_t kaction = AKeyEvent_getAction(ev);
        // Volume keys collapse / expand the Dynamic Island, matching the
        // original's volume-key shortcut. Consume so the system UI slider
        // doesn't also pop up.
        if (code == AKEYCODE_VOLUME_UP || code == AKEYCODE_VOLUME_DOWN) {
            if (kaction == AKEY_EVENT_ACTION_DOWN)
                e->st.collapsed = !e->st.collapsed;
            return 1;
        }
        // Let the framework handle Back (finishes the Activity) and anything
        // else we don't map.
        return 0;
    }
    return 0;
}

}  // namespace

void android_main(struct android_app* app) {
    Engine engine;
    engine.app       = app;
    app->userData    = &engine;
    app->onAppCmd    = HandleCmd;
    app->onInputEvent = HandleInput;

    aimgui::FramePacer pacer;
    engine.last = std::chrono::steady_clock::now();

    while (true) {
        // Drain pending lifecycle / input events. The timeout is re-evaluated
        // on every poll: block (-1) only while we have no window to draw into,
        // otherwise poll non-blocking (0) so a window created mid-drain starts
        // rendering immediately instead of stalling until the next event.
        int events;
        struct android_poll_source* source;
        while (ALooper_pollOnce((engine.renderer && app->window) ? 0 : -1,
                                nullptr, &events,
                                reinterpret_cast<void**>(&source)) >= 0) {
            if (source) source->process(app, source);
            if (app->destroyRequested) {
                DestroyRenderer(&engine);
                if (ImGui::GetCurrentContext()) ImGui::DestroyContext();
                return;
            }
        }

        if (!engine.renderer || !app->window) continue;

        // Handle rotation / surface resize: rebuild the backend at the new
        // size (the ImGui context and fonts survive).
        const int w = ANativeWindow_getWidth(app->window);
        const int h = ANativeWindow_getHeight(app->window);
        if ((w > 0 && h > 0) && (w != engine.width || h != engine.height)) {
            DestroyRenderer(&engine);
            CreateRenderer(&engine);
            if (!engine.renderer) continue;
        }

        const auto now = std::chrono::steady_clock::now();
        ImGuiIO& io = ImGui::GetIO();
        io.DeltaTime = std::max(
            1e-6f, std::chrono::duration<float>(now - engine.last).count());
        engine.last = now;
        pacer.SetTargetFps(engine.st.target_fps);

        engine.st.display_w = engine.width;
        engine.st.display_h = engine.height;

        engine.renderer->NewFrame();
        engine.st.scene_snapshot_id = engine.renderer->GetSceneSnapshotID();
        ImGui::NewFrame();
        bool keep_running = true;
        aimgui::DrawUi(&engine.st, &keep_running);
        engine.renderer->SetBloomIntensity(engine.st.bloom_intensity);
        engine.renderer->SetSnapshotFrozen(engine.st.exit_anim_active);
        engine.renderer->EndFrame();
        pacer.Wait();

        // The anti-screen-recording toggle is a SurfaceFlinger/root feature
        // with no equivalent for an ordinary app window; swallow the request
        // so the UI stays consistent without doing anything privileged.
        if (engine.st.request_permeate_toggle) {
            engine.st.request_permeate_toggle = false;
            engine.st.permeate_record = false;
        }

        // The 退出 button plays its shatter animation, then clears
        // keep_running — finish the Activity so the app exits cleanly.
        if (!keep_running) {
            ANativeActivity_finish(app->activity);
        }
    }
}
