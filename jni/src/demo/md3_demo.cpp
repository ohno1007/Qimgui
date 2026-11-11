#include "ui/md3_demo.h"
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_showcase.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aimgui::md3demo {
namespace {
namespace md = aimgui::md3;

float AutoScale(ImVec2 display) {
    return std::clamp(std::min(display.x, display.y) / 480.f, 1.f, 2.5f);
}

float Scale(const State& state, ImVec2 display) {
    const float requested = state.scale > 0 ? state.scale : AutoScale(display);
    // Keep the fixed header actions reachable even on a small rotated window.
    return std::clamp(requested, .75f, std::max(.75f, std::min(2.5f, (display.x - 24.f) / 320.f)));
}

void ClampGeometry(State& state, ImVec2 display, float scale) {
    const float margin = std::min(12.f * scale, std::min(display.x, display.y) * .025f);
    const ImVec2 maximum(std::max(1.f, display.x - 2 * margin), std::max(1.f, display.y - 2 * margin));
    const ImVec2 minimum(std::min(maximum.x, 320.f * scale), std::min(maximum.y, 380.f * scale));
    if (!state.positioned) {
        state.size = ImVec2(maximum.x, std::min(maximum.y, display.y * .92f));
        state.position = ImVec2((display.x - state.size.x) * .5f, (display.y - state.size.y) * .5f);
        state.positioned = true;
    }
    state.size.x = std::clamp(state.size.x, minimum.x, maximum.x);
    state.size.y = std::clamp(state.size.y, minimum.y, maximum.y);
    state.position.x = std::clamp(state.position.x, margin, std::max(margin, display.x - margin - state.size.x));
    state.position.y = std::clamp(state.position.y, margin, std::max(margin, display.y - margin - state.size.y));
}

void Title(const char* text, float size = 20.f) {
    ImGui::PushFont(ImGui::GetFont(), size * md::GetTheme().scale);
    ImGui::TextUnformatted(text);
    ImGui::PopFont();
}

void Note(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, md::GetTheme().onSurfaceVariant);
    ImGui::TextWrapped("%s", text);
    ImGui::PopStyleColor();
}

void SampleActions(State& state) {
    Title(u8"按钮与禁用状态");
    Note(u8"连续点击按钮，检查计数；关闭开关后，按钮应停止响应。");
    ImGui::Spacing();
    md::Switch(u8"允许计数##sample-enabled", &state.sample_enabled);
    ImGui::BeginDisabled(!state.sample_enabled);
    if (md::Button(u8"点击 +1##sample-count", md::ButtonStyle::Filled)) ++state.sample_clicks;
    ImGui::EndDisabled();
    ImGui::Spacing();
    Title(u8"实时结果", 16);
    ImGui::Text(u8"点击次数：%d", state.sample_clicks);
    ImGui::TextUnformatted(state.sample_enabled ? u8"按钮状态：可交互" : u8"按钮状态：已禁用");
    ImGui::Spacing();
    md::Divider();
    Note(u8"预期：按下一次增加 1；禁用时不变。支持触摸，以及 Tab / Enter 键。");
}

void SampleSettings(State& state, float width) {
    Title(u8"设置与连续调节");
    Note(u8"勾选、切换通知，再拖动滑块，观察下面的值是否同步。");
    ImGui::Spacing();
    md::Checkbox(u8"勾选选项##sample-checked", &state.sample_checked);
    md::Switch(u8"通知##sample-notifications", &state.sample_notifications);
    md::Slider(u8"强度##sample-value", &state.sample_value, 0, 100, width, 10);
    md::LinearProgress("sample-progress", state.sample_value / 100.f, width);
    ImGui::Spacing();
    Title(u8"实时结果", 16);
    ImGui::Text(u8"选项：%s    通知：%s", state.sample_checked ? u8"已勾选" : u8"未勾选",
                state.sample_notifications ? u8"开" : u8"关");
    ImGui::Text(u8"强度：%.0f / 100", state.sample_value);
    ImGui::Spacing();
    md::Divider();
    Note(u8"预期：滑块每格变化 10，进度条同步；方向键逐格调整，Home / End 到达两端。");
}

bool NameIsBlank(const char* value) {
    for (; *value; ++value) if (!std::isspace(static_cast<unsigned char>(*value))) return false;
    return true;
}

void SampleForm(State& state, float width) {
    Title(u8"校验、确认与反馈");
    Note(u8"先留空保存以检查错误状态，再填写名称保存。也可填入示例文字测试。");
    ImGui::Spacing();
    if (md::TextField(u8"名称##sample-name", state.sample_name, sizeof(state.sample_name),
                      md::TextFieldStyle::Outlined, width,
                      state.sample_error ? u8"名称不能为空" : u8"文字键入需外接键盘",
                      state.sample_error)) {
        state.sample_error = false;
    }
    if (md::Button(u8"填入示例##sample-fill", md::ButtonStyle::Tonal)) {
        std::snprintf(state.sample_name, sizeof(state.sample_name), "%s", "Material 3");
        state.sample_error = false;
    }
    ImGui::SameLine();
    if (md::Button(u8"保存##sample-save")) {
        state.sample_error = NameIsBlank(state.sample_name);
        if (!state.sample_error) state.sample_dialog = true;
    }
    ImGui::Spacing();
    Title(u8"保存结果", 16);
    if (state.sample_saved) ImGui::TextWrapped(u8"已保存：%s", state.saved_name);
    else Note(u8"尚未保存。取消确认不会改变保存结果。");
    md::Snackbar("sample-snackbar", u8"样例已保存", &state.snackbar_open,
                 &state.snackbar_seconds, nullptr, width, true);
    if (md::BeginDialog("sample-confirm", &state.sample_dialog, u8"确认保存", false,
                        std::min(360.f, width))) {
        ImGui::TextWrapped(u8"将名称保存为“%s”？", state.sample_name);
        Note(u8"这里只更新当前样例，关闭程序后会恢复初始状态。");
        if (md::Button(u8"取消##save-cancel", md::ButtonStyle::Text)) state.sample_dialog = false;
        ImGui::SameLine();
        if (md::Button(u8"确认保存##save-confirm")) {
            std::snprintf(state.saved_name, sizeof(state.saved_name), "%s", state.sample_name);
            state.sample_saved = true;
            state.sample_dialog = false;
            state.snackbar_open = true;
            state.snackbar_seconds = 4.f;
        }
        md::EndDialog();
    }
}

void Samples(State& state, float scale) {
    const float width = std::max(1.f, ImGui::GetContentRegionAvail().x / scale);
    switch (std::clamp(state.sample_page, 0, 2)) {
        case 0: SampleActions(state); break;
        case 1: SampleSettings(state, width); break;
        case 2: SampleForm(state, width); break;
    }
}

void Header(State& state, bool* running, float scale, ImVec2 display, bool compact) {
    const float gap = ImGui::GetStyle().ItemSpacing.x;
    const float actions = 96.f * scale + 2 * gap;
    const ImVec2 title_pos = ImGui::GetCursorScreenPos();
    const float title_width = std::max(1.f, ImGui::GetContentRegionAvail().x - actions);
    ImGui::InvisibleButton("window-drag", ImVec2(title_width, 48.f * scale));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.position.x += ImGui::GetIO().MouseDelta.x;
        state.position.y += ImGui::GetIO().MouseDelta.y;
        ClampGeometry(state, display, scale);
        ImGui::SetWindowPos(state.position);
    }
    const ImVec4 clip(title_pos.x, title_pos.y, title_pos.x + title_width, title_pos.y + 48.f * scale);
    ImGui::GetWindowDrawList()->AddText(ImGui::GetFont(), 20.f * scale,
        ImVec2(title_pos.x, title_pos.y + 14.f * scale), ImGui::GetColorU32(md::GetTheme().onSurface),
        u8"MD3 组件测试", nullptr, 0, &clip);
    ImGui::SameLine();
    if (md::IconButton("window-minimize", "-", md::IconButtonStyle::Standard)) state.visible = false;
    md::Tooltip(u8"收起窗口；音量键恢复");
    ImGui::SameLine();
    if (md::IconButton("window-close", "x", md::IconButtonStyle::Standard) && running) *running = false;
    md::Tooltip(u8"退出组件测试");

    md::Switch(u8"深色##theme", &state.dark);
    ImGui::SameLine();
    if (md::IconButton("zoom-out", "-", md::IconButtonStyle::Outlined)) state.scale = std::max(.75f, scale - .25f);
    ImGui::SameLine();
    if (md::IconButton("zoom-in", "+", md::IconButtonStyle::Outlined)) state.scale = std::min(2.5f, scale + .25f);
    ImGui::SameLine();
    if (md::Button(u8"自动##zoom-auto", md::ButtonStyle::Text, ImVec2(64,40))) state.scale = 0;
    if (!compact) Note(u8"拖动标题移动 · 右下角调整大小 · 音量键收起 / 恢复");
}

void ResizeGrip(State& state, ImVec2 display, float scale) {
    const ImVec2 window_pos = ImGui::GetWindowPos(), window_size = ImGui::GetWindowSize();
    const float side = 28.f * scale;
    const ImVec2 pos(window_pos.x + window_size.x - side, window_pos.y + window_size.y - side);
    ImGui::SetCursorScreenPos(pos);
    ImGui::InvisibleButton("window-resize", ImVec2(side, side));
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        state.size.x += ImGui::GetIO().MouseDelta.x;
        state.size.y += ImGui::GetIO().MouseDelta.y;
        ClampGeometry(state, display, scale);
    }
    auto* draw = ImGui::GetWindowDrawList();
    const ImU32 color = ImGui::GetColorU32(md::GetTheme().outline);
    for (int i = 0; i < 2; ++i) {
        const float inset = (8.f + i * 5.f) * scale;
        draw->AddLine(ImVec2(pos.x + side - inset, pos.y + side - 6.f * scale),
                      ImVec2(pos.x + side - 6.f * scale, pos.y + side - inset), color, scale);
    }
}
} // namespace

void ResetSamples(State& state) {
    state.sample_clicks = 0;
    state.sample_enabled = true;
    state.sample_checked = false;
    state.sample_notifications = true;
    state.sample_value = 40;
    state.sample_name[0] = state.saved_name[0] = '\0';
    state.sample_error = state.sample_saved = state.sample_dialog = false;
    state.snackbar_open = false;
    state.snackbar_seconds = 0;
}

void Draw(State& state, bool* running, ImVec2 display) {
    state.window_rect = ImVec4();
    if (!state.visible || display.x <= 0 || display.y <= 0) return;
    const float scale = Scale(state, display);
    ClampGeometry(state, display, scale);
    const bool compact = state.size.y / scale < 470.f;
    md::SetTheme(state.dark, scale);
    md::PushTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(16.f * scale, (compact ? 8.f : 12.f) * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(8.f * scale, (compact ? 4.f : 8.f) * scale));
    ImGui::PushStyleVar(ImGuiStyleVar_ScrollbarSize, 14.f * scale);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, scale);
    ImGui::SetNextWindowPos(state.position);
    ImGui::SetNextWindowSize(state.size);
    ImGui::SetNextWindowBgAlpha(1.f);
    const auto flags = ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                       ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoScrollWithMouse;
    if (ImGui::Begin("MD3Demo", nullptr, flags)) {
        Header(state, running, scale, display, compact);
        const char* modes[] = {u8"组件一览", u8"交互样例"};
        md::SegmentedButtons("demo-mode", &state.mode, modes, 2, 0);
        if (state.mode == 0) {
            const char* pages[] = {u8"按钮与标签", u8"输入与选择", u8"导航", u8"容器与反馈", u8"日期与时间", u8"媒体与菜单", u8"形状", u8"表现力组件"};
            md::Select(u8"组件分类##demo-page", &state.page, pages, md::ShowcasePageCount,
                       ImGui::GetContentRegionAvail().x / scale, true);
            if (!compact && state.page < 6) Note(u8"横向内容可拖动底部滚动条查看；上方按钮可调整显示大小。");
        } else {
            const char* pages[] = {u8"按钮", u8"设置", u8"表单"};
            const float tabs_width = compact ? ImGui::GetContentRegionAvail().x / scale - 96.f : 0;
            md::Tabs("sample-pages", &state.sample_page, pages, 3, tabs_width, false);
            if (compact) ImGui::SameLine();
            if (md::Button(u8"重置样例##samples-reset", md::ButtonStyle::Text, ImVec2(88,40))) ResetSamples(state);
        }
        const int key = state.mode == 0 ? state.page : 100 + state.sample_page;
        const bool page_changed = state.content_key != key;
        state.content_key = key;
        const float footer = (compact ? 12.f : 20.f) * scale;
        const ImVec2 content_size(0, std::max(1.f, ImGui::GetContentRegionAvail().y - footer));
        if (state.mode == 0 && state.page < 6) ImGui::SetNextWindowContentSize(ImVec2(760.f * scale, 0));
        ImGui::PushStyleColor(ImGuiCol_ChildBg, md::GetTheme().surfaceContainerLow);
        if (ImGui::BeginChild("demo-content", content_size, ImGuiChildFlags_AlwaysUseWindowPadding,
                              ImGuiWindowFlags_HorizontalScrollbar)) {
            if (page_changed) { ImGui::SetScrollX(0); ImGui::SetScrollY(0); }
            if (state.mode == 0) md::Showcase(state.page);
            else Samples(state, scale);
        }
        ImGui::EndChild();
        ImGui::PopStyleColor();
        ResizeGrip(state, display, scale);
        const ImVec2 pos = ImGui::GetWindowPos(), size = ImGui::GetWindowSize();
        if (state.visible) state.window_rect = ImVec4(pos.x, pos.y, size.x, size.y);
    }
    ImGui::End();
    ImGui::PopStyleVar(4);
    md::PopTheme();
}

} // namespace aimgui::md3demo
