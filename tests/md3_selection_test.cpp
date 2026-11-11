#include "ui/md3.h"
#include "ui/md3_selection.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

namespace {
namespace md = aimgui::md3;
using Draw = std::function<void()>;
void Frame(const Draw& draw) {
    ImGui::NewFrame(); md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Selection tests",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    draw(); ImGui::End(); md::PopTheme(); ImGui::Render();
    md3test::ValidateDrawData(); md3test::UploadTextures();
}
void Click(const Draw& draw, ImVec2 p) {
    auto& io=ImGui::GetIO(); io.AddMousePosEvent(p.x,p.y); Frame(draw);
    io.AddMouseButtonEvent(0,true); Frame(draw); io.AddMouseButtonEvent(0,false); Frame(draw);
}
void Key(const Draw& draw, ImGuiKey key) {
    auto& io=ImGui::GetIO(); io.AddKeyEvent(key,true); Frame(draw);
    io.AddKeyEvent(key,false); Frame(draw);
}
ImVec2 DialPoint(ImVec2 center,float ticks,float total,float radius) {
    const float angle=(ticks/total*2-.5f)*3.14159265f;
    return ImVec2(center.x+std::cos(angle)*radius,center.y+std::sin(angle)*radius);
}
void ClockDial(float scale, bool use24Hour) {
    md3test::ConfigureContext(); md::SetTheme(false,scale);
    md::Time time{use24Hour?10:13,0}; bool enabled=true,outerDisabled=false;
    ImVec2 center; ImGuiID dial=0; int changes=0;
    Draw draw=[&]{
        ImGui::BeginDisabled(outerDisabled);
        if(md::TimePicker("time",&time,use24Hour,enabled)) ++changes;
        const ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax();
        center=ImVec2((a.x+b.x)/2,(a.y+b.y)/2); dial=ImGui::GetItemID();
        ImGui::EndDisabled();
    };
    Frame(draw); Frame(draw);
    auto& io=ImGui::GetIO();
    ImVec2 point=DialPoint(center,3,12,(use24Hour?72:108)*scale);
    io.AddMousePosEvent(point.x,point.y); Frame(draw);
    io.AddMouseButtonEvent(0,true); Frame(draw); assert(time.hour==15);
    point=DialPoint(center,0,12,(use24Hour?72:108)*scale);
    io.AddMousePosEvent(point.x,point.y); Frame(draw);
    assert(time.hour==(use24Hour?0:12));
    io.AddMouseButtonEvent(0,false); Frame(draw);
    // Selecting an hour advances to the minute dial, including unlabelled ticks.
    Click(draw,DialPoint(center,7,60,108*scale)); assert(time.minute==7);
    Click(draw,DialPoint(center,59,60,108*scale)); assert(time.minute==59);
    Key(draw,ImGuiKey_RightArrow); assert(time.minute==0);
    Key(draw,ImGuiKey_LeftArrow); assert(time.minute==59);
    assert(GImGui->NavId==dial);
    point=DialPoint(center,0,60,108*scale);
    io.AddMousePosEvent(point.x,point.y); Frame(draw);
    io.AddMouseButtonEvent(0,true); Frame(draw); assert(time.minute==0);
    point=DialPoint(center,37,60,108*scale);
    io.AddMousePosEvent(point.x,point.y); Frame(draw); assert(time.minute==37);
    io.AddMouseButtonEvent(0,false); Frame(draw);
    const int previousChanges=changes;
    enabled=false; Frame(draw); Key(draw,ImGuiKey_RightArrow);
    Click(draw,DialPoint(center,10,60,108*scale)); assert(time.minute==37);
    enabled=true; outerDisabled=true; Frame(draw); Key(draw,ImGuiKey_LeftArrow);
    Click(draw,DialPoint(center,20,60,108*scale)); assert(time.minute==37);
    assert(changes==previousChanges);
    md3test::DestroyContext();
}
void DateKeyboard() {
    md3test::ConfigureContext(); md::SetTheme(false);
    md::Date date{2024,2,1}; md::DateRange range{{2024,2,1},{2024,2,3}};
    bool enabled=true; int changes=0; ImGuiID target=0; ImGuiWindow* window=nullptr;
    const char* cell="29##day29";
    Draw draw=[&]{
        if(md::DatePicker("date",&date,enabled)) ++changes;
        window=ImGui::GetCurrentWindow();
        ImGui::PushID("date");ImGui::PushID("calendar");target=ImGui::GetID(cell);ImGui::PopID();ImGui::PopID();
    };
    Frame(draw);Frame(draw);ImGui::SetFocusID(target,window);ImGui::SetNavCursorVisible(true);Key(draw,ImGuiKey_Enter);
    assert(date.year==2024&&date.month==2&&date.day==29&&changes==1);
    enabled=false;cell="14##day14";Frame(draw);ImGui::SetFocusID(target,window);ImGui::SetNavCursorVisible(true);Key(draw,ImGuiKey_Enter);
    assert(date.day==29&&changes==1);
    Draw rangeDraw=[&]{
        md::DateRangePicker("range",&range);
        window=ImGui::GetCurrentWindow();
        ImGui::PushID("range");ImGui::PushID("calendar");target=ImGui::GetID(cell);ImGui::PopID();ImGui::PopID();
    };
    Frame(rangeDraw);Frame(rangeDraw);ImGui::SetFocusID(target,window);ImGui::SetNavCursorVisible(true);Key(rangeDraw,ImGuiKey_Enter);
    assert(range.start.day==14&&range.end.day==14);
    cell="5##day5";Frame(rangeDraw);ImGui::SetFocusID(target,window);ImGui::SetNavCursorVisible(true);Key(rangeDraw,ImGuiKey_Enter);
    assert(range.start.day==5&&range.end.day==14);
    md3test::DestroyContext();
}
}
void RunMd3SelectionTests() {
    for(float scale:{.75f,1.f,1.5f}) for(bool use24Hour:{false,true}) ClockDial(scale,use24Hour);
    std::puts("PASS: time dial dragging, minute precision, 12/24-hour rings, keyboard wrapping and disabled states");
    DateKeyboard();
    std::puts("PASS: keyboard leap-day selection, disabled dates and reversed range selection");
}
#ifdef MD3_SELECTION_STANDALONE
int main() { RunMd3SelectionTests(); }
#endif
