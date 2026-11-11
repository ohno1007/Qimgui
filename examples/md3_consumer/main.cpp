#include <qimgui/md3.hpp>
#ifdef MD3_CONSUMER_SHOWCASE
#include <qimgui/md3_showcase.hpp>
#endif
#include <cstdio>

namespace md = qimgui::md3;

// Application-owned state; no Android, global singleton or renderer is needed.
struct ExampleState {
    bool enabled = true;
    float progress = .4f;
    int clicks = 0;
    bool bold = true, italic = false, underline = false, shareMenu = false;
    int period = 0;
};

void DrawExample(ExampleState& state) {
    md::ScopedTheme theme;
    ImGui::SetNextWindowPos(ImVec2(16,16), ImGuiCond_Once);
    ImGui::SetNextWindowSize(ImVec2(640,600), ImGuiCond_Once);
    if (ImGui::Begin("Reusable MD3 components")) {
        md::Switch("Enabled", &state.enabled);
        ImGui::BeginDisabled(!state.enabled);
        if (md::Button("Count")) ++state.clicks;
        ImGui::EndDisabled();
        ImGui::Text("Clicks: %d", state.clicks);
        md::Slider("Progress", &state.progress, 0, 1, 280, 0, "%.2f");
        md::WavyLinearProgress("progress", state.progress, 280);
        md::WavyCircularProgress("pending", -1.f);
        const md::ToolbarItem tools[] = {
            {"bold", "B", nullptr, &state.bold},
            {"italic", "I", nullptr, &state.italic},
            {"underline", "U", nullptr, &state.underline},
        };
        md::Toolbar("formatting", tools, 3);
        const md::ButtonGroupItem periods[] = {{"Day"}, {"Week"}, {"Month"}};
        md::ButtonGroupOptions group; group.width = 280.f;
        md::ButtonGroup("period", &state.period, periods, 3, group);
        const auto share = md::SplitButton("share", "Share", &state.shareMenu);
        if (share == md::SplitButtonAction::Primary) ++state.clicks;
        if (share == md::SplitButtonAction::ToggleMenu && state.shareMenu)
            md::OpenMenu("share-options");
        if (md::BeginMenuPopup("share-options")) {
            if (!state.shareMenu) ImGui::CloseCurrentPopup();
            if (md::MenuItem("Copy link")) { ++state.clicks; state.shareMenu = false; }
            if (md::MenuItem("Save a copy")) { ++state.clicks; state.shareMenu = false; }
            md::EndMenuPopup();
        } else state.shareMenu = false;
        md::Shape("flower", md::MaterialShape::Flower, ImVec2(96,96));
#ifdef MD3_CONSUMER_SHOWCASE
        md::Showcase(0);
#endif
    }
    ImGui::End();
}

// Headless integration smoke test. A real app calls DrawExample between its
// backend's NewFrame() and Render(), and uploads textures to its own GPU.
int main() {
    IMGUI_CHECKVERSION();
    ImGui::CreateContext();
    auto& io = ImGui::GetIO();
    io.IniFilename = nullptr; io.LogFilename = nullptr;
    io.DisplaySize = ImVec2(800,800); io.DeltaTime = 1.f/60.f;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    md::SetTheme(false, 1.f);
    ExampleState state;
    int vertices = 0;
    bool valid = true;
    for (int frame=0; frame<3; ++frame) {
        ImGui::NewFrame(); DrawExample(state); ImGui::Render();
        const auto* data = ImGui::GetDrawData();
        valid = valid && data && data->Valid;
        if (data) vertices = data->TotalVtxCount;
        for (auto* texture : ImGui::GetPlatformIO().Textures) {
            if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
                texture->SetTexID(static_cast<ImTextureID>(texture->UniqueID + 1));
                texture->SetStatus(ImTextureStatus_OK);
            } else if (texture->Status == ImTextureStatus_WantDestroy) {
                texture->SetTexID(ImTextureID_Invalid); texture->SetStatus(ImTextureStatus_Destroyed);
            }
        }
    }
    for (auto* texture : ImGui::GetPlatformIO().Textures) {
        texture->SetTexID(ImTextureID_Invalid); texture->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::DestroyContext();
    if (!valid || vertices <= 0) return 1;
    std::printf("Qimgui MD3 %s: consumer rendered %d vertices without platform/backend dependencies\n", md::LibraryVersion, vertices);
    return 0;
}
