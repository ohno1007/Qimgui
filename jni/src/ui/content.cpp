#include "ui/ui_internal.h"
#include "ui/icons.h"

#include "imgui.h"

#include <cstdio>

namespace aimgui {

void DrawContent(UiState* state, Page page) {

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(34, 20));

    ImGui::BeginChild("##content", ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    DrawPage(state, page);

    DrawResizeGrip(state);

    ContentGesture("##content", state);
    ImGui::EndChild();

    ImGui::PopStyleVar();
}

void DrawCardContent(const UiState* state) {
    ImGuiIO& io = ImGui::GetIO();

    constexpr float kCardPadX = 30.0f;
    constexpr float kCardPadY = 24.0f;
    ImGui::Indent(kCardPadX);
    ImGui::Dummy(ImVec2(0, kCardPadY));
    ImGui::PushFont(nullptr, 34.0f);
    if (state->card_icon) { ImGui::TextUnformatted(state->card_icon); ImGui::SameLine(0, 14); }
    ImGui::TextUnformatted(state->card_title ? state->card_title : "AImGui");
    ImGui::PopFont();
    ImGui::Spacing();

    if (state->card_body) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(state->card_body);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(kCardPadX);
        return;
    }

    ImGui::Text(u8"%.0f FPS   ·   %.2f ms", io.Framerate, 1000.0f / io.Framerate);
    ImGui::TextDisabled("%s", state->renderer_name ? state->renderer_name : "?");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state->screen_mirror_running) {
        ImGui::TextDisabled(u8"液体玻璃   %dx%d", state->screen_mirror_w,
                            state->screen_mirror_h);
    } else {
        ImGui::TextDisabled(u8"液体玻璃   未开启");
    }
    ImGui::TextDisabled(u8"防录屏   %s", state->permeate_record ? u8"已开启" : u8"已关闭");

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextDisabled(u8"再点一次展开窗口   ·   上滑收起");
    ImGui::Unindent(kCardPadX);
}

void DrawIslandContent(const UiState* state) {
    char buf[96];
    if (state->island_text) {
        std::snprintf(buf, sizeof(buf), "%s%s%s",
                      state->island_icon ? state->island_icon : "",
                      state->island_icon ? "  " : "",
                      state->island_text);
    } else {
        std::snprintf(buf, sizeof(buf), "%s%s%.0f FPS",
                      state->island_icon ? state->island_icon : "",
                      state->island_icon ? "  " : "",
                      ImGui::GetIO().Framerate);
    }

    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - ts.x) * 0.5f, (ws.y - ts.y) * 0.5f));
    ImGui::TextUnformatted(buf);
}

void DrawDotContent(const UiState* state, float alpha) {
    if (g_ui.dot_radius < 6.0f || alpha <= 0.01f) return;
    const char* txt = state->dot_text ? state->dot_text : state->island_icon;
    if (!txt || !*txt) return;
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(g_ui.dot_center.x - ts.x * 0.5f,
               g_ui.dot_center.y - ts.y * 0.5f),
        ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), txt);
}

}
