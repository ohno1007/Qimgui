#include "app.hpp"
#include "pages.hpp"
#include <algorithm>

namespace clock_example {
namespace {
namespace md = qimgui::md3;

float Scale(ImVec2 display) {
    return std::clamp(std::min(display.x, display.y) / 900.f, .75f, 1.4f);
}
void Place(App& app, ImVec2 display, float scale) {
    const float margin = std::min(12.f * scale, std::min(display.x, display.y) * .025f);
    const ImVec2 maximum(std::max(1.f, display.x - margin * 2.f),
                         std::max(1.f, display.y - margin * 2.f));
    const ImVec2 minimum(std::min(maximum.x, 520.f * scale),
                         std::min(maximum.y, 400.f * scale));
    if (!app.positioned) {
        app.size = ImVec2(std::min(maximum.x, 840.f * scale),
                          std::min(maximum.y, 560.f * scale));
        app.position = ImVec2((display.x - app.size.x) * .5f,
                              (display.y - app.size.y) * .5f);
        app.positioned = true;
    }
    app.size.x = std::clamp(app.size.x, minimum.x, maximum.x);
    app.size.y = std::clamp(app.size.y, minimum.y, maximum.y);
    app.position.x = std::clamp(app.position.x, margin, std::max(margin, display.x - margin - app.size.x));
    app.position.y = std::clamp(app.position.y, margin, std::max(margin, display.y - margin - app.size.y));
}
void Label(const char* text, float size, ImVec4 color) {
    ImGui::PushFont(ImGui::GetFont(), size * md::GetTheme().scale);
    ImGui::PushStyleColor(ImGuiCol_Text, color);
    ImGui::TextUnformatted(text);
    ImGui::PopStyleColor();
    ImGui::PopFont();
}
void DragHandle(App& app, ImVec2 position, ImVec2 size) {
    ImGui::SetCursorScreenPos(position);
    ImGui::InvisibleButton("window-drag", size);
    if (ImGui::IsItemActive() && ImGui::IsMouseDragging(ImGuiMouseButton_Left)) {
        const auto delta = ImGui::GetIO().MouseDelta;
        app.position.x += delta.x;
        app.position.y += delta.y;
    }
}
void Rail(App& app, ImVec2 origin, float width, float height, ImVec4 background) {
    const float s = md::GetTheme().scale;
    ImGui::SetCursorScreenPos(origin);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    if (ImGui::BeginChild("clock-rail", ImVec2(width, height), ImGuiChildFlags_None,
                          ImGuiWindowFlags_NoBackground | ImGuiWindowFlags_NoScrollbar)) {
        ImGui::SetCursorPos(ImVec2(0, 16.f * s));
        const md::NavigationItem destinations[] = {
            {"Alarm", md::icons::Alarm}, {"Clock", md::icons::Clock},
            {"Timer", md::icons::Timer}, {"Stopwatch", md::icons::Stopwatch},
        };
        int selected = static_cast<int>(app.state.page);
        auto& theme = md::GetTheme();
        const ImVec4 surface = theme.surface;
        theme.surface = background;
        if (md::Navigation("clock-navigation", &selected, destinations, 4,
                           md::NavigationStyle::Rail, width / s))
            app.state.page = static_cast<Page>(selected);
        theme.surface = surface;
    }
    ImGui::EndChild();
    ImGui::PopStyleVar();
}
}

bool Draw(App& app, bool& visible, ImVec2 displaySize) {
    app.inputBounds = ImVec4(0, 0, 0, 0);
    if (!visible || displaySize.x <= 0 || displaySize.y <= 0) return true;
    const float s = Scale(displaySize);
    Place(app, displaySize, s);
    md::SetTheme(app.state.dark, s);
    md::SetMotionSettings({app.state.reducedMotion, 1.f});
    md::ScopedTheme scope;
    const auto& theme = md::GetTheme();
    const ImVec4 railColor = app.state.dark ? ImVec4(.19f,.17f,.29f,1.f)
                                           : ImVec4(.81f,.78f,.94f,1.f);
    const float railWidth = (app.size.x / s < 600.f ? 88.f : 112.f) * s;
    const float padding = (app.size.x / s < 600.f ? 16.f : 28.f) * s;
    ImGui::SetNextWindowPos(app.position, ImGuiCond_Always);
    ImGui::SetNextWindowSize(app.size, ImGuiCond_Always);
    ImGui::SetNextWindowSizeConstraints(
        ImVec2(std::min(displaySize.x-16.f,520.f*s),std::min(displaySize.y-16.f,400.f*s)),
        ImVec2(std::max(1.f,displaySize.x-16.f),std::max(1.f,displaySize.y-16.f)));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0,0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, 24.f*s);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 1.f*s);
    ImGui::PushStyleColor(ImGuiCol_WindowBg, theme.surface);
    bool running = true;
    if (ImGui::Begin("Tempo##clock-example", nullptr, ImGuiWindowFlags_NoTitleBar |
                     ImGuiWindowFlags_NoCollapse | ImGuiWindowFlags_NoScrollbar |
                     ImGuiWindowFlags_NoScrollWithMouse | ImGuiWindowFlags_NoMove)) {
        const auto pos = ImGui::GetWindowPos();
        const auto size = ImGui::GetWindowSize();
        app.size = size;
        auto* draw = ImGui::GetWindowDrawList();
        draw->AddRectFilled(pos, ImVec2(pos.x+railWidth,pos.y+size.y),
                            ImGui::ColorConvertFloat4ToU32(railColor),24.f*s,
                            ImDrawFlags_RoundLeft);
        // One application surface: rail on the left, page directly on the right.
        ImGui::SetCursorScreenPos(ImVec2(pos.x+20.f*s,pos.y+23.f*s));
        Label("Tempo",20.f,theme.onSurface);
        const float right = pos.x+size.x;
        const float contentX = pos.x+railWidth+padding;
        DragHandle(app,ImVec2(pos.x,pos.y),ImVec2(std::max(1.f,size.x-152.f*s),64.f*s));
        ImGui::SetCursorScreenPos(ImVec2(right-152.f*s,pos.y+12.f*s));
        if (md::IconButton("theme-toggle",app.state.dark?md::icons::Sun:md::icons::Moon))
            app.state.dark = !app.state.dark;
        md::Tooltip(app.state.dark?"Light theme":"Dark theme");
        ImGui::SameLine(0,0);
        if (md::IconButton("window-minimize",md::icons::Minimize)) visible=false;
        md::Tooltip("Hide · volume key to restore");
        ImGui::SameLine(0,0);
        if (md::IconButton("window-close",md::icons::Close)) running=false;
        md::Tooltip("Close");
        Rail(app,ImVec2(pos.x,pos.y+64.f*s),railWidth,std::max(1.f,size.y-64.f*s),railColor);
        ImGui::SetCursorScreenPos(ImVec2(contentX,pos.y+66.f*s));
        Label(PageTitle(app.state.page),30.f,theme.onSurface);
        ImGui::SetCursorScreenPos(ImVec2(contentX,pos.y+106.f*s));
        Label(PageSubtitle(app.state.page),14.f,theme.onSurfaceVariant);
        ImGui::SetCursorScreenPos(ImVec2(contentX,pos.y+142.f*s));
        ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(0,0));
        const ImVec2 available(std::max(1.f,size.x-railWidth-padding*2.f),
                               std::max(1.f,size.y-160.f*s));
        if (ImGui::BeginChild("clock-page",available,ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoBackground)) {
            if (app.previousPage != static_cast<int>(app.state.page)) {
                ImGui::SetScrollY(0);
                app.previousPage=static_cast<int>(app.state.page);
            }
            DrawPage(app.state);
        }
        ImGui::EndChild();
        ImGui::PopStyleVar();
        if (visible && running) app.inputBounds=ImVec4(pos.x,pos.y,size.x,size.y);
    }
    ImGui::End();
    ImGui::PopStyleColor();
    ImGui::PopStyleVar(3);
    return running;
}
} // namespace clock_example
