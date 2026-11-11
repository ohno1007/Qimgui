#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>

namespace md = aimgui::md3;
using Draw = std::function<void()>;
static void Frame(const Draw& draw) {
    ImGui::NewFrame(); md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("MD3 layout tests",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    draw(); ImGui::End(); md::PopTheme(); ImGui::Render();
    md3test::ValidateDrawData(); md3test::UploadTextures();
}
static void Click(const Draw& draw,ImVec2 point) {
    auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);Frame(draw);
    io.AddMouseButtonEvent(0,true);Frame(draw);io.AddMouseButtonEvent(0,false);Frame(draw);
}
static void Key(const Draw& draw,ImGuiKey key) {
    auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);Frame(draw);io.AddKeyEvent(key,false);Frame(draw);
}
static void TabKeyboardSelection() {
    md3test::ConfigureContext();md::SetTheme(false);
    const char* labels[]={"First","Disabled","Third","Last"};
    bool enabled[]={true,false,true,true};bool disabled=false;
    int selected=0,changes=0;ImVec2 start;ImGuiID scope=0;
    Draw draw=[&]{
        start=ImGui::GetCursorScreenPos();ImGui::BeginDisabled(disabled);
        scope=ImGui::GetID("nested-tab-scope");ImGui::PushFocusScope(scope);
        if(md::Tabs("tabs",&selected,labels,4,400,true,enabled))++changes;
        ImGui::PopFocusScope();ImGui::EndDisabled();
    };
    Frame(draw);Frame(draw);Click(draw,ImVec2(start.x+50,start.y+24));assert(selected==0);
    Key(draw,ImGuiKey_RightArrow);assert(selected==2&&changes==1);
    assert(GImGui->NavFocusScopeId==scope);
    Key(draw,ImGuiKey_RightArrow);assert(selected==3&&changes==2);
    Key(draw,ImGuiKey_RightArrow);assert(selected==0&&changes==3);
    Key(draw,ImGuiKey_LeftArrow);assert(selected==3&&changes==4);
    Key(draw,ImGuiKey_Home);assert(selected==0&&changes==5);
    Key(draw,ImGuiKey_End);assert(selected==3&&changes==6);
    disabled=true;Key(draw,ImGuiKey_Home);assert(selected==3&&changes==6);
    disabled=false;enabled[3]=false;Click(draw,ImVec2(start.x+50,start.y+24));
    Key(draw,ImGuiKey_End);assert(selected==2);
    enabled[2]=false;Click(draw,ImVec2(start.x+50,start.y+24));
    const int oldChanges=changes;Key(draw,ImGuiKey_RightArrow);assert(selected==0&&changes==oldChanges);
    md3test::DestroyContext();
}
static void SnackbarCountdown() {
    md3test::ConfigureContext();md::SetTheme(false);
    bool open=true;float seconds=0;ImVec2 start;bool activated=false;
    Draw draw=[&]{
        start=ImGui::GetCursorScreenPos();
        activated|=md::Snackbar("toast","Saved",&open,&seconds,"Undo",320);
    };
    Frame(draw);Frame(draw);
    auto& io=ImGui::GetIO();io.AddMousePosEvent(start.x+20,start.y+20);Frame(draw);
    seconds=.25f;for(int i=0;i<30;++i)Frame(draw);
    assert(open&&std::abs(seconds-.25f)<.001f&&!activated);
    io.AddMousePosEvent(800,800);Frame(draw);assert(seconds<.25f);
    // Actual keyboard focus pauses the action's countdown.
    Key(draw,ImGuiKey_Tab);seconds=.25f;
    for(int i=0;i<30;++i)Frame(draw);assert(open&&std::abs(seconds-.25f)<.001f);
    // Clicking outside removes keyboard focus; timeout resumes and closes it.
    Click(draw,ImVec2(800,800));for(int i=0;i<30;++i)Frame(draw);
    assert(!open&&seconds==0&&!activated);
    // Zero is persistent; pressing the action closes and reports activation.
    open=true;seconds=0;Frame(draw);Frame(draw);
    Click(draw,ImVec2(start.x+240,start.y+24));assert(activated&&!open);
    md3test::DestroyContext();
}
static void NarrowSnackbarBounds() {
    md3test::ConfigureContext();
    for(float scale:{.75f,1.f,1.5f}) {
        md::SetTheme(false,scale);
        for(float width:{32.f,48.f,80.f,120.f}) {
            bool open=true;float seconds=0;
            Draw draw=[&]{
                const ImVec2 start=ImGui::GetCursorScreenPos();
                auto* list=ImGui::GetWindowDrawList();const int first=list->VtxBuffer.Size;
                md::Snackbar("narrow","Long message",&open,&seconds,"Long action label",width);
                for(int i=first;i<list->VtxBuffer.Size;++i) {
                    assert(list->VtxBuffer[i].pos.x>=start.x-1.5f);
                    assert(list->VtxBuffer[i].pos.x<=start.x+width*scale+1.5f);
                }
            };
            Frame(draw);Frame(draw);
        }
    }
    md3test::DestroyContext();
}
void RunMd3LayoutTests() {
    TabKeyboardSelection();std::puts("PASS: tabs skip disabled destinations, wrap and handle Home/End once");
    SnackbarCountdown();std::puts("PASS: snackbar hover/focus pause, timeout and action activation");
    NarrowSnackbarBounds();std::puts("PASS: narrow snackbar drawing and actions remain inside the surface");
}

#ifdef MD3_LAYOUT_STANDALONE
int main() { RunMd3LayoutTests(); }
#endif
