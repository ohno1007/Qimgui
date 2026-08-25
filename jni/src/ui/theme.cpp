#include "ui/ui_internal.h"

#include "imgui.h"

namespace aimgui {

void ApplyGlassPalette() {
    auto& s = ImGui::GetStyle();
    const ImVec4 clear(0, 0, 0, 0);
    auto white = [](float alpha) { return ImVec4(1, 1, 1, alpha); };

    s.Colors[ImGuiCol_FrameBg]        = clear;
    s.Colors[ImGuiCol_FrameBgHovered] = clear;
    s.Colors[ImGuiCol_FrameBgActive]  = clear;
    s.Colors[ImGuiCol_Button]         = clear;
    s.Colors[ImGuiCol_ButtonHovered]  = clear;
    s.Colors[ImGuiCol_ButtonActive]   = clear;

    s.Colors[ImGuiCol_Header]           = white(0.14f);
    s.Colors[ImGuiCol_HeaderHovered]    = white(0.20f);
    s.Colors[ImGuiCol_HeaderActive]     = white(0.26f);

    s.Colors[ImGuiCol_CheckMark]        = white(0.92f);
    s.Colors[ImGuiCol_SliderGrab]       = white(0.92f);
    s.Colors[ImGuiCol_SliderGrabActive] = white(1.00f);
    s.Colors[ImGuiCol_Border]           = white(0.14f);
    s.Colors[ImGuiCol_BorderShadow]     = clear;
    s.Colors[ImGuiCol_Separator]        = white(0.10f);
    s.Colors[ImGuiCol_SeparatorHovered] = white(0.20f);
    s.Colors[ImGuiCol_SeparatorActive]  = white(0.30f);
    s.Colors[ImGuiCol_PlotHistogram]    = white(0.55f);
    s.Colors[ImGuiCol_PlotLines]        = white(0.70f);
    s.Colors[ImGuiCol_ResizeGrip]        = white(0.10f);
    s.Colors[ImGuiCol_ResizeGripHovered] = white(0.22f);
    s.Colors[ImGuiCol_ResizeGripActive]  = white(0.34f);

    s.Colors[ImGuiCol_TextDisabled] = white(0.45f);

    s.Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.96f);
}

void ApplyStyleOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& s = ImGui::GetStyle();
    s.WindowRounding          = 12.0f;
    s.ChildRounding           = 10.0f;

    s.FrameRounding           = 999.0f;
    s.GrabRounding            = 999.0f;
    s.PopupRounding           = 6.0f;
    s.ScrollbarRounding       = 10.0f;
    s.WindowBorderSize        = 1.0f;
    s.FrameBorderSize         = 0.0f;
    s.WindowPadding           = ImVec2(0, 0);
    s.ItemSpacing             = ImVec2(14, 10);
    s.ItemInnerSpacing        = ImVec2(8, 6);
    s.FramePadding            = ImVec2(14, 10);
    s.ScrollbarSize           = 26.0f;
    s.GrabMinSize             = 16.0f;
    s.SeparatorTextBorderSize = 3.0f;
    s.SeparatorTextPadding    = ImVec2(28, 8);

    s.Colors[ImGuiCol_TitleBg]          = s.Colors[ImGuiCol_TitleBgActive];
    s.Colors[ImGuiCol_TitleBgCollapsed] = s.Colors[ImGuiCol_TitleBgActive];

    ApplyGlassPalette();
}
}
