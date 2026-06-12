// AImGui: a minimal Dear ImGui Android ARM64 ELF.
#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/keyboard_input.h"
#include "core/window_session.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "ui/ui.h"

#include <atomic>
#include <chrono>
#include <thread>

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

    // ProcessMirrorDisplay() forks `dumpsys display`, parses it, and
    // for every previously-unseen layerStack calls MirrorSurface() on
    // the SurfaceFlinger composer. Calling that from the render loop
    // races the renderer's own SF transactions and crashes the moment
    // the system screen recorder adds its VirtualDisplay layerStack.
    // Move it onto a dedicated thread that wakes once a second. The
    // helper internally throttles + caches per-layerStack state, so a
    // 1 s poll is plenty for "user just hit record".
    std::atomic<bool> mirror_running{true};
    std::atomic<bool> mirror_active{!st.permeate_record};
    std::thread mirror_thread([&]{
        while (mirror_running.load(std::memory_order_acquire)) {
            if (mirror_active.load(std::memory_order_acquire))
                ANativeWindowCreator::ProcessMirrorDisplay();
            std::this_thread::sleep_for(std::chrono::seconds(1));
        }
    });

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
        aimgui::kbd_input::Flush();

        ws.renderer()->NewFrame();
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
            // Pause the mirror poller while the surface is gone so it
            // can't MirrorSurface() against a destroyed SurfaceControl.
            mirror_active.store(false, std::memory_order_release);
            ws.Destroy();
            if (!ws.Build(W, st.permeate_record)) { running = false; break; }
            st.renderer_name = ws.renderer()->Name();
            mirror_active.store(!st.permeate_record, std::memory_order_release);
        }
    }
    mirror_running.store(false, std::memory_order_release);
    mirror_thread.join();
    aimgui::kbd_input::Shutdown();
    // Tear down renderer/surface BEFORE ImGui — backend Shutdown unhooks
    // from the active context.
    ws.Destroy();
    ImGui::DestroyContext();
}
