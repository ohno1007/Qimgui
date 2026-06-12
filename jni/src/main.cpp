// AImGui: a minimal Dear ImGui Android ARM64 ELF.
#include "core/font.h"
#include "core/frame_pacer.h"
#include "core/keyboard_input.h"
#include "core/window_session.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "ui/ui.h"

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
        // NOTE: ProcessMirrorDisplay() forks `dumpsys display` every
        // frame and walks new-layerStack → MirrorSurface() on the way
        // in. When the system screen recorder spins up its virtual
        // display, the first hit on that branch segfaults the process.
        // The mirror path is a ROM-compat workaround; on stock Android
        // the surface's recording visibility is already governed by the
        // skipScreenshot flag we pass at Build() time, so the mirror is
        // redundant. Skip it.
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
            ws.Destroy();
            if (!ws.Build(W, st.permeate_record)) { running = false; break; }
            st.renderer_name = ws.renderer()->Name();
        }
    }
    aimgui::kbd_input::Shutdown();
    // Tear down renderer/surface BEFORE ImGui — backend Shutdown unhooks
    // from the active context.
    ws.Destroy();
    ImGui::DestroyContext();
}
