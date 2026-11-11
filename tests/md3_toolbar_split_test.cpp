#include "ui/md3_toolbar.h"
#include "ui/md3_split_button.h"
#include "ui/md3.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>

namespace {
namespace md = aimgui::md3;
using Draw = std::function<void()>;
void Frame(const Draw& draw) {
    ImGui::NewFrame(); md::PushTheme();
    ImGui::SetNextWindowPos({0,0}); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("toolbar-split-tests", nullptr,
                  ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    draw(); ImGui::End(); md::PopTheme(); ImGui::Render();
    md3test::ValidateDrawData(); md3test::UploadTextures();
    for (auto* list : ImGui::GetDrawData()->CmdLists)
        for (const auto& vertex : list->VtxBuffer) assert(std::isfinite(vertex.pos.x) && std::isfinite(vertex.pos.y));
}
void Click(const Draw& draw, ImVec2 point) {
    auto& io = ImGui::GetIO(); io.AddMousePosEvent(point.x, point.y); Frame(draw);
    io.AddMouseButtonEvent(0, true); Frame(draw);
    io.AddMouseButtonEvent(0, false); Frame(draw);
}
void Key(const Draw& draw, ImGuiKey key) {
    auto& io = ImGui::GetIO(); io.AddKeyEvent(key, true); Frame(draw);
    io.AddKeyEvent(key, false); Frame(draw);
}
bool Near(float a, float b) { return std::abs(a-b) < .01f; }

void ToolbarInteractions() {
    md3test::ConfigureContext(); md::SetTheme(false);
    bool first = false, third = false, disabled = false;
    md::ToolbarItem items[] = {{"bold", "@format_bold", nullptr, &first},
                              {"disabled", "@format_italic", nullptr, nullptr, false},
                              {"underline", "@format_underlined", nullptr, &third}};
    int result = -1, activated[3] = {};
    ImVec2 origin, size; ImGuiID ids[3] = {};
    md::ToolbarOrientation orientation = md::ToolbarOrientation::Horizontal;
    const Draw draw = [&] {
        origin = ImGui::GetCursorScreenPos();
        ImGui::PushID("tools");
        for (int i=0;i<3;++i) { ImGui::PushID(i); ids[i] = ImGui::GetID(items[i].id); ImGui::PopID(); }
        ImGui::PopID();
        ImGui::BeginDisabled(disabled);
        result = md::Toolbar("tools", items, 3, orientation);
        if (result >= 0) ++activated[result];
        size = ImGui::GetItemRectSize();
        ImGui::EndDisabled();
    };
    Frame(draw); Frame(draw);
    assert(Near(size.x,168) && Near(size.y,64));
    Click(draw,{origin.x+32,origin.y+32}); assert(first && !third && result == 0);
    Click(draw,{origin.x+84,origin.y+32}); assert(result == -1 && activated[1] == 0);
    Click(draw,{origin.x+136,origin.y+32}); assert(first && third && result == 2);
    Key(draw,ImGuiKey_LeftArrow); assert(GImGui->NavId == ids[0]);
    Key(draw,ImGuiKey_Space); assert(!first && third);
    Key(draw,ImGuiKey_End); assert(GImGui->NavId == ids[2]);
    Key(draw,ImGuiKey_RightArrow); assert(GImGui->NavId == ids[0]);
    Key(draw,ImGuiKey_Home); assert(GImGui->NavId == ids[0]);
    disabled = true; Frame(draw);
    const int previousFirst = activated[0], previousThird = activated[2];
    Click(draw,{origin.x+32,origin.y+32}); Key(draw,ImGuiKey_Space); Key(draw,ImGuiKey_End);
    assert(!first && third && activated[0] == previousFirst && activated[2] == previousThird);
    disabled = false; orientation = md::ToolbarOrientation::Vertical;
    Frame(draw); Frame(draw); assert(Near(size.x,64) && Near(size.y,168));
    Click(draw,{origin.x+32,origin.y+136}); assert(!third);
    Key(draw,ImGuiKey_UpArrow); assert(GImGui->NavId == ids[0]);
    Key(draw,ImGuiKey_DownArrow); assert(GImGui->NavId == ids[2]);
    md3test::DestroyContext();
    std::puts("PASS: toolbar independent toggles, action indices, disabled controls and horizontal/vertical keyboard focus");
}

void SplitInteractions() {
    md3test::ConfigureContext(); md::SetTheme(false);
    bool expanded = false, disabled = false, enabled = true;
    int primary = 0, menu = 0;
    ImVec2 origin, size; ImGuiID primaryId = 0, menuId = 0;
    md::SplitButtonAction result = md::SplitButtonAction::None;
    const Draw draw = [&] {
        origin = ImGui::GetCursorScreenPos();
        ImGui::PushID("share"); primaryId = ImGui::GetID("primary"); menuId = ImGui::GetID("menu"); ImGui::PopID();
        ImGui::BeginDisabled(disabled);
        result = md::SplitButton("share", "Share", &expanded, md::ButtonStyle::Tonal, "@person_add", 220, enabled);
        if (result == md::SplitButtonAction::Primary) ++primary;
        if (result == md::SplitButtonAction::ToggleMenu) ++menu;
        size = ImGui::GetItemRectSize();
        ImGui::EndDisabled();
    };
    Frame(draw); Frame(draw); assert(Near(size.x,220) && Near(size.y,40));
    Click(draw,{origin.x+70,origin.y+20}); assert(primary == 1 && menu == 0 && !expanded);
    Click(draw,{origin.x+196,origin.y+20}); assert(primary == 1 && menu == 1 && expanded);
    Click(draw,{origin.x+171,origin.y+20}); assert(primary == 1 && menu == 1 && expanded);
    Click(draw,{origin.x+70,origin.y+20}); assert(primary == 2 && menu == 1 && expanded);
    assert(GImGui->NavId == primaryId);
    Key(draw,ImGuiKey_RightArrow); assert(GImGui->NavId == menuId);
    Key(draw,ImGuiKey_Space); assert(primary == 2 && menu == 2 && !expanded);
    Key(draw,ImGuiKey_LeftArrow); assert(GImGui->NavId == primaryId);
    Key(draw,ImGuiKey_Enter); assert(primary == 3 && menu == 2);
    disabled = true; Frame(draw);
    Click(draw,{origin.x+196,origin.y+20}); Key(draw,ImGuiKey_Space);
    assert(primary == 3 && menu == 2 && !expanded);
    disabled = false; enabled = false; Frame(draw);
    Click(draw,{origin.x+70,origin.y+20}); assert(primary == 3 && menu == 2);
    md3test::DestroyContext();
    std::puts("PASS: split primary and menu actions stay independent, gap is inert, keyboard and disabled states agree");
}

void ConstrainedAndInvalidInputs() {
    md3test::ConfigureContext();
    bool selected = true, expanded = true;
    const md::ToolbarItem items[] = {
        {"long", "@format_bold", "A deliberately long action label", &selected},
        {"share", "@share"}, {"color", "@format_color_fill"}
    };
    for (bool dark : {false,true}) for (float scale : {.75f,1.f,2.f}) {
        md::SetTheme(dark,scale);
        for (float extent : {8.f,32.f,80.f,200.f}) {
            const Draw draw = [&] {
                ImGui::BeginChild("narrow", ImVec2(240*scale,400*scale));
                md::Toolbar("constrained",items,3,md::ToolbarOrientation::Horizontal,md::ToolbarStyle::Vibrant,extent);
                assert(ImGui::GetItemRectSize().x <= extent*scale+.01f);
                assert(Near(ImGui::GetItemRectSize().y,64*scale));
                for (auto style : {md::ButtonStyle::Filled,md::ButtonStyle::Tonal,md::ButtonStyle::Outlined,
                                   md::ButtonStyle::Text,md::ButtonStyle::Elevated}) {
                    ImGui::PushID(static_cast<int>(style));
                    md::SplitButton("narrow-split","A label too long for this row",&expanded,style,"@person_add",extent);
                    assert(ImGui::GetItemRectSize().x <= extent*scale+.01f);
                    assert(Near(ImGui::GetItemRectSize().y,40*scale));
                    ImGui::PopID();
                }
                ImGui::EndChild();
            };
            Frame(draw); Frame(draw);
        }
    }
    Frame([&] {
        const float nan = std::numeric_limits<float>::quiet_NaN();
        const ImVec2 before = ImGui::GetCursorScreenPos();
        assert(md::Toolbar(nullptr,items,3) == -1);
        assert(md::Toolbar("bad",nullptr,3) == -1);
        assert(md::Toolbar("bad",items,0) == -1);
        assert(md::Toolbar("bad",items,65) == -1);
        assert(md::Toolbar("bad",items,3,md::ToolbarOrientation::Horizontal,md::ToolbarStyle::Standard,nan) == -1);
        assert(md::SplitButton(nullptr,"Share",&expanded) == md::SplitButtonAction::None);
        assert(md::SplitButton("bad",nullptr,&expanded) == md::SplitButtonAction::None);
        assert(md::SplitButton("bad","Share",nullptr) == md::SplitButtonAction::None);
        assert(md::SplitButton("bad","Share",&expanded,md::ButtonStyle::Filled,nullptr,nan) == md::SplitButtonAction::None);
        const ImVec2 after = ImGui::GetCursorScreenPos();
        assert(before.x == after.x && before.y == after.y);
    });
    md3test::DestroyContext();
    std::puts("PASS: toolbar/split narrow slots, long labels, themes/scales and invalid inputs keep finite draw data and bounded layout");
}
}

void RunMd3ToolbarSplitTests() {
    ToolbarInteractions(); SplitInteractions(); ConstrainedAndInvalidInputs();
}
