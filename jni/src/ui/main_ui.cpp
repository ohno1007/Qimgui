#include "ui/main_ui.h"
#include "ui/ui.h"
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_showcase.h"
#include "imgui_internal.h"
#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif
#include <algorithm>
#include <array>
#include <cctype>
#include <cmath>
#include <cstdio>
#include <cstring>

namespace aimgui {
const PageItem kPages[] = {
    {Page::Dashboard, "@home", u8"概览"}, {Page::Widgets, "@widgets", u8"组件"},
    {Page::Window, "@settings", u8"设置"}, {Page::Performance, "@speed", u8"性能"},
    {Page::About, "@info", u8"关于"},
};
const int kPagesCount = IM_ARRAYSIZE(kPages);
namespace {
namespace md = aimgui::md3;
struct PageState {
    ImGuiContext* owner = nullptr;
    PageState* next = nullptr;
    int widget_mode = 0, clicks = 0, choice = 0;
    bool enabled = true, checked = true, notifications = true, filter = false;
    float value = 40.f, lower = 20.f, upper = 70.f;
    char name[128] = "", saved_name[128] = "";
    bool name_error = false, confirmation = false, confirm_save = false, snackbar = false;
    float snackbar_seconds = 0;
    const char* snackbar_text = u8"操作已完成";
    std::array<float, 120> history{};
    int history_count = 0, history_cursor = 0;
};
PageState* page_states = nullptr;
void ReleaseState(ImGuiContext* context, ImGuiContextHook*) {
    PageState** current = &page_states;
    while (*current) {
        if ((*current)->owner == context) {
            PageState* removed = *current; *current = removed->next; IM_DELETE(removed); return;
        }
        current = &(*current)->next;
    }
}
PageState& State() {
    for (PageState* value = page_states; value; value = value->next)
        if (value->owner == ImGui::GetCurrentContext()) return *value;
    auto* value = IM_NEW(PageState)();
    value->owner = ImGui::GetCurrentContext(); value->next = page_states; page_states = value;
    ImGuiContextHook hook; hook.Type = ImGuiContextHookType_Shutdown; hook.Callback = ReleaseState;
    ImGui::AddContextHook(value->owner, &hook);
    return *value;
}
float Scale() { return md::GetTheme().scale; }
float Width() { return std::max(1.f, ImGui::GetContentRegionAvail().x / Scale()); }
void Note(const char* message) {
    ImGui::PushStyleColor(ImGuiCol_Text, md::GetTheme().onSurfaceVariant);
    ImGui::TextWrapped("%s", message); ImGui::PopStyleColor();
}
void Heading(const char* title, const char* supporting = nullptr) {
    ImGui::PushFont(ImGui::GetFont(), 24.f * Scale());
    ImGui::TextWrapped("%s", title); ImGui::PopFont();
    if (supporting) Note(supporting);
    ImGui::Dummy(ImVec2(0, 4.f * Scale()));
}
void Section(const char* title) {
    ImGui::PushFont(ImGui::GetFont(), 18.f * Scale());
    ImGui::TextWrapped("%s", title); ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, 4.f * Scale()));
}
void Detail(const char* label, const char* value) {
    Note(label); ImGui::TextWrapped("%s", value); ImGui::Spacing();
}
void Notify(PageState& page, const char* text) {
    page.snackbar = true; page.snackbar_seconds = 5.f; page.snackbar_text = text;
}
const char* GlassStatus(const UiState& ui) {
    if (!ui.screen_mirror) return u8"已关闭";
    if (!ui.glass_available) return u8"当前设备不可用";
    if (ui.screen_mirror_running && ui.screen_texture_id) return u8"已开启";
    return ui.glass_failed ? u8"暂时不可用" : u8"正在准备";
}
void Dashboard(UiState& ui) {
    Heading(u8"欢迎使用 Qimgui", u8"导航、控件与设置均采用 Material Design 3。");
    if (md::BeginCard("overview-style", ImVec2(0, 0))) {
        Section(u8"当前外观");
        Detail(u8"主题", ui.md3_dark ? u8"深色" : u8"浅色");
        Detail(u8"液体玻璃", GlassStatus(ui));
        Note(u8"使用顶部开关切换表面效果，组件和操作方式保持一致。");
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("overview-status", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Section(u8"运行状态");
        Detail(u8"渲染后端", ui.renderer_name ? ui.renderer_name : u8"等待初始化");
        char value[64]; std::snprintf(value, sizeof(value), "%.0f FPS", ImGui::GetIO().Framerate);
        Detail(u8"当前帧率", value);
        std::snprintf(value, sizeof(value), "%d × %d", ui.display_w, ui.display_h);
        Detail(u8"显示尺寸", value);
    }
    md::EndCard(); ImGui::Spacing();
    if (md::Button(u8"体验组件##overview-components", md::ButtonStyle::Filled, ImVec2(Width(), 40)))
        ui.nav_page = static_cast<int>(Page::Widgets);
    if (md::Button(u8"调整偏好##overview-settings", md::ButtonStyle::Tonal, ImVec2(Width(), 40)))
        ui.nav_page = static_cast<int>(Page::Window);
    Note(u8"拖动顶部标题移动窗口；音量键收起或展开。");
}
bool Blank(const char* text) {
    for (; *text; ++text) if (!std::isspace(static_cast<unsigned char>(*text))) return false;
    return true;
}
void InteractiveComponents(PageState& page) {
    if (md::BeginCard("actions-card", ImVec2(0, 0))) {
        Section(u8"按钮与状态");
        md::Switch(u8"允许点击##component-enabled", &page.enabled);
        ImGui::BeginDisabled(!page.enabled);
        if (md::Button(u8"点击 +1##component-count", md::ButtonStyle::Filled)) ++page.clicks;
        ImGui::EndDisabled(); ImGui::SameLine();
        if (md::Button(u8"重置##component-reset", md::ButtonStyle::Text)) page.clicks = 0;
        ImGui::Text(u8"已点击 %d 次", page.clicks);
        if (md::Button(u8"打开确认框##component-dialog", md::ButtonStyle::Outlined)) {
            page.confirm_save = false; page.confirmation = true;
        }
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("selection-card", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Section(u8"选择与调节");
        md::Checkbox(u8"启用此选项##component-checkbox", &page.checked);
        md::Switch(u8"接收通知##component-notifications", &page.notifications);
        md::RadioButton(u8"标准模式##component-radio-standard", &page.choice, 0);
        md::RadioButton(u8"专注模式##component-radio-focus", &page.choice, 1);
        md::Chip(u8"收藏##component-filter", md::ChipStyle::Filter, &page.filter);
        md::Slider(u8"强度##component-value", &page.value, 0, 100, Width(), 10);
        md::RangeSlider(u8"范围##component-range", &page.lower, &page.upper, 0, 100, Width(), 10);
        md::LinearProgress("component-progress", page.value / 100.f, Width());
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("input-card", ImVec2(0, 0))) {
        Section(u8"输入与反馈");
        if (md::TextField(u8"名称##component-name", page.name, sizeof(page.name), md::TextFieldStyle::Outlined,
                          Width(), page.name_error ? u8"请填写名称" : u8"文字键入需外接键盘", page.name_error))
            page.name_error = false;
        if (md::Button(u8"填入示例##component-fill", md::ButtonStyle::Tonal)) {
            std::snprintf(page.name, sizeof(page.name), "%s", "Material 3"); page.name_error = false;
        }
        ImGui::SameLine();
        if (md::Button(u8"保存##component-save")) {
            page.name_error = Blank(page.name);
            if (!page.name_error) { page.confirm_save = true; page.confirmation = true; }
        }
        if (page.saved_name[0]) ImGui::TextWrapped(u8"已保存：%s", page.saved_name);
        Note(u8"此处是交互样例，数据只保留在本次运行中。");
    }
    md::EndCard();
}
void Components(UiState& ui, PageState& page) {
    if (page.widget_mode == 0)
        Heading(u8"组件", u8"试用按钮、选择、输入和反馈，或浏览完整组件。");
    const char* modes[] = {u8"基础交互", u8"完整组件"};
    md::Tabs("component-modes", &page.widget_mode, modes, 2);
    if (page.widget_mode == 0) { InteractiveComponents(page); return; }
    const char* categories[] = {u8"按钮与标签", u8"输入与选择", u8"导航", u8"容器与反馈", u8"日期与时间", u8"媒体与菜单", u8"形状", u8"表现力组件"};
    const bool changed = md::Select(u8"组件分类##component-category", &ui.component_page, categories, md::ShowcasePageCount, Width());
    if (ui.component_page < 6) Note(u8"宽内容可拖动底部滚动条查看。");
    if (ui.component_page < 6) ImGui::SetNextWindowContentSize(ImVec2(760.f * Scale(), 0));
    const float height = std::max(160.f * Scale(), ImGui::GetContentRegionAvail().y);
    if (ImGui::BeginChild("component-gallery", ImVec2(0, height), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar)) {
        if (changed) { ImGui::SetScrollX(0); ImGui::SetScrollY(0); }
        md::Showcase(ui.component_page);
    }
    ImGui::EndChild();
}
void Settings(UiState& ui, PageState& page) {
    Heading(u8"设置", u8"选择适合自己的主题、显示大小和输入方式。");
    if (md::BeginCard("appearance-card", ImVec2(0, 0))) {
        Section(u8"外观");
        md::Switch(u8"深色主题##setting-dark", &ui.md3_dark);
        const char* sizes[] = {u8"自动", u8"小", u8"标准", u8"大"};
        const float values[] = {0.f, 1.f, 1.5f, 2.f};
        int selected = 0;
        if (ui.ui_scale > 0.f) {
            selected = 1;
            for (int i = 2; i < 4; ++i)
                if (std::fabs(ui.ui_scale - values[i]) < std::fabs(ui.ui_scale - values[selected])) selected = i;
        }
        if (md::Select(u8"显示大小##setting-scale", &selected, sizes, 4, Width())) ui.ui_scale = values[selected];
        Note(u8"自动跟随屏幕尺寸，旋转后会重新适配。");
        if (md::Button(u8"重置窗口位置##setting-center", md::ButtonStyle::Tonal, ImVec2(Width(), 40)))
            ui.request_center_window = true;
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("interaction-card", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Section(u8"交互");
        md::Switch(u8"触感反馈##setting-haptics", &ui.haptics_enabled);
        ImGui::BeginDisabled(!ui.block_touch_ok);
        md::Switch(u8"拦截窗口内触摸##setting-touch", &ui.block_touch);
        ImGui::EndDisabled();
        Note(ui.block_touch_ok ? u8"开启后，窗口内的操作不会传到后面的应用。" : u8"当前设备暂不支持触摸拦截。");
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("privacy-card", ImVec2(0, 0))) {
        Section(u8"录屏");
        bool hidden = ui.permeate_record;
        if (md::Switch(u8"防录屏##setting-recording", &hidden)) ui.request_permeate_toggle = true;
        Note(u8"控制窗口是否在系统录屏和投屏中显示。");
    }
    md::EndCard();
#ifdef AIMGUI_LIVE2D
    ImGui::Spacing();
    if (md::BeginCard("model-card", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Section(u8"Live2D");
        md::Slider(u8"模型大小##setting-model-scale", &ui.ball_scale, .4f, 3.f, Width(), 0, "%.2f");
        if (md::Button(u8"说句话##setting-model-speak", md::ButtonStyle::Tonal)) live2d::Speak();
    }
    md::EndCard();
#endif
    ImGui::Spacing();
    if (md::Button(u8"显示提示##setting-snackbar", md::ButtonStyle::Text)) Notify(page, u8"设置会自动保存");
}
void Performance(UiState& ui, PageState& page) {
    Heading(u8"性能", u8"观察当前帧率和最近的绘制节奏。");
    const float fps = ImGui::GetIO().Framerate;
    page.history[page.history_cursor] = fps;
    page.history_cursor = (page.history_cursor + 1) % static_cast<int>(page.history.size());
    page.history_count = std::min(page.history_count + 1, static_cast<int>(page.history.size()));
    if (md::BeginCard("performance-card", ImVec2(0, 0))) {
        Section(u8"当前帧率");
        ImGui::PushFont(ImGui::GetFont(), 36.f * Scale()); ImGui::Text("%.0f FPS", fps); ImGui::PopFont();
        ImGui::Text(u8"每帧 %.2f ms", fps > 0.f ? 1000.f / fps : 0.f);
        Note(u8"由显示设备的垂直同步控制提交节奏。"); ImGui::Spacing();
        const ImVec2 origin = ImGui::GetCursorScreenPos();
        const ImVec2 size(ImGui::GetContentRegionAvail().x, 96.f * Scale());
        ImGui::Dummy(size);
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(origin, ImVec2(origin.x + size.x, origin.y + size.y),
                           ImGui::GetColorU32(md::GetTheme().surfaceContainerLow), 12.f * Scale());
        float maximum = 60.f;
        for (float value : page.history) maximum = std::max(maximum, value * 1.1f);
        const int start = page.history_count == static_cast<int>(page.history.size()) ? page.history_cursor : 0;
        for (int i = 1; i < page.history_count; ++i) {
            const auto point = [&](int index) {
                const float value = page.history[(start + index) % page.history.size()];
                const float t = std::clamp(value / maximum, 0.f, 1.f);
                return ImVec2(origin.x + index * size.x / (page.history.size() - 1),
                              origin.y + (1.f - t) * (size.y - 8.f * Scale()) + 4.f * Scale());
            };
            draw->AddLine(point(i - 1), point(i), ImGui::GetColorU32(md::GetTheme().primary), 2.f * Scale());
        }
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("performance-details", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Detail(u8"渲染后端", ui.renderer_name ? ui.renderer_name : "—");
        Detail(u8"液体玻璃", GlassStatus(ui));
        if (ui.screen_mirror_running) {
            char size[64]; std::snprintf(size, sizeof(size), "%d × %d", ui.screen_mirror_w, ui.screen_mirror_h);
            Detail(u8"背景画面尺寸", size);
        }
        Note(u8"实际帧率还会受到设备负载、温度和显示刷新率影响。");
    }
    md::EndCard();
}
void About() {
    Heading("Qimgui", u8"原生 Android 悬浮界面与 Material Design 3 组件库。");
    if (md::BeginCard("about-design", ImVec2(0, 0))) {
        Section(u8"统一设计");
        Note(u8"应用栏、导航、按钮、表单和对话框使用同一套 MD3 颜色、尺寸与交互状态。");
        ImGui::Spacing(); Note(u8"液体玻璃是可选的表面效果；关闭后使用实色 Material 表面。");
    }
    md::EndCard(); ImGui::Spacing();
    if (md::BeginCard("about-project", ImVec2(0, 0), md::CardStyle::Outlined)) {
        Section(u8"开源组件");
        Detail("Qimgui", "Apache-2.0");
        char version[80]; std::snprintf(version, sizeof(version), "%s · MIT", ImGui::GetVersion());
        Detail("Dear ImGui", version); Detail("Roboto", "OFL-1.1");
        Note(u8"MD3Demo 提供独立的组件与交互测试窗口。");
    }
    md::EndCard();
}
void Feedback(PageState& page) {
    if (md::BeginDialog("component-confirmation", &page.confirmation,
                        page.confirm_save ? u8"保存名称？" : u8"确认操作", false,
                        std::min(360.f, std::max(240.f, Width())))) {
        if (page.confirm_save) ImGui::TextWrapped(u8"将名称保存为“%s”？", page.name);
        else Note(u8"确认后显示一条操作完成提示。你也可以取消返回。");
        if (md::Button(u8"取消##component-cancel", md::ButtonStyle::Text)) page.confirmation = false;
        ImGui::SameLine();
        if (md::Button(u8"确认##component-confirm")) {
            page.confirmation = false;
            if (page.confirm_save) std::snprintf(page.saved_name, sizeof(page.saved_name), "%s", page.name);
            Notify(page, page.confirm_save ? u8"名称已保存" : u8"操作已完成");
        }
        md::EndDialog();
    }
    // Keep feedback visible even when the originating card is partway through
    // a long settings or component page.
    if (page.snackbar) {
        const ImVec2 cursor = ImGui::GetCursorScreenPos();
        const auto* window = ImGui::GetCurrentWindow();
        const float pad = 8.f * Scale();
        const float width = std::max(1.f, window->InnerRect.GetWidth() - 2.f * pad);
        ImGui::SetCursorScreenPos(ImVec2(window->InnerRect.Min.x + pad,
            std::max(window->InnerRect.Min.y, window->InnerRect.Max.y - 56.f * Scale() - pad)));
        md::Snackbar("page-feedback", page.snackbar_text, &page.snackbar,
                     &page.snackbar_seconds, nullptr, width / Scale(), true);
        ImGui::SetCursorScreenPos(cursor);
    }
}
} // namespace
void DrawPage(UiState* ui, Page page) {
    if (!ui) return;
    auto& state = State();
    ImGui::PushID("app-pages");
    switch (page) {
        case Page::Dashboard: Dashboard(*ui); break;
        case Page::Widgets: Components(*ui, state); break;
        case Page::Window: Settings(*ui, state); break;
        case Page::Performance: Performance(*ui, state); break;
        case Page::About: About(); break;
    }
    Feedback(state);
    ImGui::PopID();
}
} // namespace aimgui
