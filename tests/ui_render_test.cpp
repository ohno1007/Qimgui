#include "imgui.h"
#include "core/text_outline.h"
#include <cassert>
#include <cstdio>

static void Frame(bool text, bool callback = false) {
    ImGui::NewFrame();
    auto* list = ImGui::GetBackgroundDrawList();
    list->AddRectFilled(ImVec2(30,30),ImVec2(160,100),IM_COL32_WHITE,12);
    list->AddLine(ImVec2(20,110),ImVec2(180,130),IM_COL32_WHITE,2);
    if (text) list->AddText(ImVec2(40,50),IM_COL32_WHITE,"Glass 1.93");
    if (callback) list->AddCallback([](const ImDrawList*, const ImDrawCmd*) {},nullptr);
    ImGui::Render();
    auto* draw = ImGui::GetDrawData();
    const int before = draw->TotalIdxCount;
    auto* original = draw->CmdLists[0];
    aimgui::OutlineText(draw,1.0f,100);
    if (callback) assert(draw->CmdLists[0] == original);
    else if (text) assert(draw->TotalIdxCount > before);
    else assert(draw->TotalIdxCount == before); // Atlas-backed corners/lines aren't text.
    for (auto* commands : draw->CmdLists) {
        for (const auto& cmd : commands->CmdBuffer) {
            if (cmd.UserCallback) continue;
            for (unsigned i=0;i<cmd.ElemCount;++i)
                assert(cmd.VtxOffset + commands->IdxBuffer[cmd.IdxOffset+i] < static_cast<unsigned>(commands->VtxBuffer.Size));
        }
    }
    // Simulate renderer upload AFTER the outline pass, as on Android.
    for (auto* texture : ImGui::GetPlatformIO().Textures) {
        if (texture->Status == ImTextureStatus_WantCreate || texture->Status == ImTextureStatus_WantUpdates) {
            texture->SetTexID(1); texture->SetStatus(ImTextureStatus_OK);
        } else if (texture->Status == ImTextureStatus_WantDestroy) {
            texture->SetTexID(ImTextureID_Invalid); texture->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}

int main() {
    ImGui::CreateContext();
    auto& io=ImGui::GetIO();
    io.IniFilename=nullptr;
    io.DisplaySize=ImVec2(640,480); io.DeltaTime=1.0f/60;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    Frame(false); Frame(true); Frame(true,true); Frame(false); Frame(true);
    aimgui::ShutdownTextOutline();
    for (auto* texture : ImGui::GetPlatformIO().Textures) {
        texture->SetTexID(ImTextureID_Invalid); texture->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::DestroyContext();
    std::puts("PASS: ImGui 1.93 atlas shapes, glyph outlines, pending texture uploads, callbacks and vertex indices");
}
