#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_showcase.h"
#include "ui/md3_extra.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <functional>
#include <string>
namespace md = aimgui::md3;
using Draw = std::function<void()>;

static void Frame(const Draw& draw) {
    ImGui::NewFrame(); md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0,0)); ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Native MD3",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings|ImGuiWindowFlags_NoMove);
    draw(); ImGui::End(); md::PopTheme(); ImGui::Render();
    md3test::ValidateDrawData(); md3test::UploadTextures();
}
static ImVec2 Center() {
    ImVec2 a=ImGui::GetItemRectMin(),b=ImGui::GetItemRectMax(); return ImVec2((a.x+b.x)*.5f,(a.y+b.y)*.5f);
}
static void Click(const Draw& draw,ImVec2 point) {
    auto& io=ImGui::GetIO(); io.AddMousePosEvent(point.x,point.y); Frame(draw);
    io.AddMouseButtonEvent(0,true); Frame(draw); io.AddMouseButtonEvent(0,false); Frame(draw);
}
static void Key(const Draw& draw,ImGuiKey key) {
    auto& io=ImGui::GetIO(); io.AddKeyEvent(key,true); Frame(draw); io.AddKeyEvent(key,false); Frame(draw);
}
static void Dates() {
    assert(md::IsLeapYear(2000)); assert(!md::IsLeapYear(1900)); assert(md::IsLeapYear(2024));
    assert(md::DaysInMonth(2024,2)==29); assert(md::DaysInMonth(2025,2)==28);
    assert(md::DaysInMonth(2026,0)==0); assert(md::DaysInMonth(2026,13)==0);
    assert(!md::IsValidDate({0,1,1})); assert(!md::IsValidDate({10000,1,1}));
    assert(!md::IsValidDate({2025,2,29})); assert(md::IsValidDate({2024,2,29}));
    assert(md::CompareDates({2024,12,31},{2025,1,1})<0);
    assert(md::CompareDates({2026,11,11},{2026,11,11})==0);
    assert(md::IsValidDate(md::ClampDate({-10,99,99})));
    for(int year: {1,100,400,1900,2000,2024,9999}) for(int month=1;month<=12;++month) {
        int days=md::DaysInMonth(year,month); assert(days>=28&&days<=31);
        assert(md::IsValidDate({year,month,days})); assert(!md::IsValidDate({year,month,days+1}));
    }
}
static void Controls() {
    md3test::ConfigureContext(); md::SetTheme(false);
    int clicks=0; bool disabled=false,check=false,toggle=false; ImVec2 point;
    Draw button=[&]{ImGui::BeginDisabled(disabled);if(md::Button("Save"))++clicks;point=Center();ImGui::EndDisabled();};
    Frame(button);Frame(button);Click(button,point);assert(clicks==1);
    disabled=true;Frame(button);Click(button,point);assert(clicks==1);disabled=false;
    Draw checkbox=[&]{md::Checkbox("Selected",&check);point=Center();};
    Frame(checkbox);Click(checkbox,point);assert(check);Click(checkbox,point);assert(!check);
    Draw sw=[&]{md::Switch("Enabled",&toggle);point=Center();};
    Frame(sw);Click(sw,point);assert(toggle);
    ImGuiID sliderId=0;ImGuiWindow* window=nullptr;float value=35;
    Draw slider=[&]{ImGui::BeginDisabled(disabled);md::Slider("Volume",&value,0,100);point=Center();sliderId=ImGui::GetItemID();window=ImGui::GetCurrentWindow();ImGui::EndDisabled();};
    Frame(slider);Frame(slider);Click(slider,point);assert(value>40&&value<60);
    disabled=true;value=35;Frame(slider);
    // Recreate the focused-then-disabled case; no key path may bypass disabled.
    ImGui::SetFocusID(sliderId,window);Key(slider,ImGuiKey_Home);assert(value==35);
    ImGui::SetFocusID(sliderId,window);Key(slider,ImGuiKey_End);assert(value==35);
    Click(slider,ImVec2(point.x+80,point.y));assert(value==35);
    disabled=false;
    float lower=25,upper=75;
    Draw range=[&]{md::RangeSlider("Range",&lower,&upper,0,100,240,10);};
    for(int i=0;i<4;++i)Frame(range);assert(lower<=upper&&lower>=0&&upper<=100);
    char text[128]="";
    Draw input=[&]{md::TextField("Message",text,sizeof(text));point=Center();};
    Frame(input);Frame(input);Click(input,point);ImGui::GetIO().AddInputCharactersUTF8("Hello");Frame(input);assert(std::strstr(text,"Hello"));
    md3test::DestroyContext();
}
static void ExtraControls() {
    md3test::ConfigureContext();md::SetTheme(false);
    const md::CarouselItem cards[]={{"A","First"},{"B","Second"},{"C","Third"},{"D","Fourth"}};
    int selected=0;bool disabled=false;ImVec2 start;
    Draw carousel=[&]{start=ImGui::GetCursorScreenPos();ImGui::BeginDisabled(disabled);md::Carousel("cards",&selected,cards,4,600,160,180);ImGui::EndDisabled();};
    Frame(carousel);Frame(carousel);Click(carousel,ImVec2(start.x+60,start.y+60));assert(selected==0);
    Key(carousel,ImGuiKey_RightArrow);assert(selected==1);
    Key(carousel,ImGuiKey_RightArrow);assert(selected==2);
    Key(carousel,ImGuiKey_LeftArrow);assert(selected==1);
    disabled=true;Key(carousel,ImGuiKey_RightArrow);assert(selected==1);
    disabled=false;
    const char* actions[]={"A","B"};int action=-1;ImVec2 bar;
    Draw appbar=[&]{bar=ImGui::GetCursorScreenPos();int hit=md::BottomAppBar("bar",actions,2,"+",400);if(hit>=0)action=hit;};
    Frame(appbar);Frame(appbar);Click(appbar,ImVec2(bar.x+28,bar.y+40));assert(action==0);
    Click(appbar,ImVec2(bar.x+356,bar.y+40));assert(action==2);
    md3test::DestroyContext();
}
static void NarrowLayout() {
    md3test::ConfigureContext();md::SetTheme(false);
    const char* actions[]={"A","B"};ImVec2 start;int action=-1;float width=120;
    Draw bar=[&]{
        start=ImGui::GetCursorScreenPos();auto* list=ImGui::GetWindowDrawList();int first=list->VtxBuffer.Size;
        int hit=md::TopAppBar("narrow","A very long heading",md::AppBarStyle::Small,"<",actions,2,width);if(hit!=-1)action=hit;
        for(int i=first;i<list->VtxBuffer.Size;++i){assert(list->VtxBuffer[i].pos.x>=start.x-4);assert(list->VtxBuffer[i].pos.x<=start.x+width+4);}
    };
    Frame(bar);Frame(bar);Click(bar,ImVec2(start.x+28,start.y+32));assert(action==-2);
    Click(bar,ImVec2(start.x+88,start.y+32));assert(action==0);
    width=40;Frame(bar);Frame(bar);
    float maximum=0,offset=0;bool scroll=false;
    Draw narrow=[&]{
        ImGui::SetNextWindowContentSize(ImVec2(760,0));
        ImGui::BeginChild("narrow-gallery",ImVec2(320,600),ImGuiChildFlags_None,ImGuiWindowFlags_HorizontalScrollbar);
        if(scroll)ImGui::SetScrollX(300);
        maximum=ImGui::GetScrollMaxX();offset=ImGui::GetScrollX();md::Showcase(0);ImGui::EndChild();
    };
    Frame(narrow);Frame(narrow);assert(maximum>300);scroll=true;Frame(narrow);Frame(narrow);assert(offset>250);
    md3test::DestroyContext();
}
static void Popups() {
    md3test::ConfigureContext(); md::SetTheme(false);
    bool open=true,openMenu=false,openSub=false;int depth=0;
    Draw draw=[&]{
        if(md::BeginDialog("dialog",&open,"Dialog")) {
            if(openMenu){md::OpenMenu("menu");openMenu=false;}
            if(md::BeginMenuPopup("menu")) {
                if(openSub){ImGui::OpenPopup("Nested");openSub=false;}
                if(md::BeginSubMenu("Nested")){md::MenuItem("Child");md::EndSubMenu();}
                md::MenuItem("Other");md::EndMenuPopup();
            }
            md::EndDialog();
        }
        depth=GImGui->OpenPopupStack.Size;
    };
    Frame(draw);Frame(draw);assert(open&&depth==1);
    openMenu=true;Frame(draw);Frame(draw);assert(open&&depth==2);
    openSub=true;Frame(draw);Frame(draw);assert(open&&depth==3);
    Key(draw,ImGuiKey_Escape);assert(open&&depth==2);
    Key(draw,ImGuiKey_Escape);assert(open&&depth==1);
    Key(draw,ImGuiKey_Escape);assert(!open&&depth==0);
    md3test::DestroyContext();
}
static void Gallery() {
    for(int context=0;context<2;++context) {
        md3test::ConfigureContext();
        for(bool dark:{false,true})for(float scale:{.75f,1.f,1.5f}) {
            md::SetTheme(dark,scale);
            for(int page=0;page<md::ShowcasePageCount;++page) for(int frame=0;frame<3;++frame)Frame([&]{md::Showcase(page);});
        }
        md3test::DestroyContext();
    }
}
static void Capture(const char* directory,const char* font,const char* medium) {
    std::filesystem::create_directories(directory);
    for(bool dark:{false,true})for(int page=0;page<md::ShowcasePageCount;++page) {
        md3test::ConfigureContext(font); md::SetTheme(dark);
        ImFont* body=ImGui::GetIO().Fonts->Fonts[0];
        ImFont* label=medium?ImGui::GetIO().Fonts->AddFontFromFileTTF(medium,14):body;
        md::SetFonts(body,label);
        for(int frame=0;frame<8;++frame)Frame([&]{md::Showcase(page);});
        std::string name=std::string(directory)+"/native-"+(dark?"dark-":"light-")+std::to_string(page)+".draw";
        md3test::DumpDrawData(name);md3test::DestroyContext();
    }
    for(bool dark:{false,true})for(int variant=0;variant<5;++variant) {
        md3test::ConfigureContext(font); md::SetTheme(dark);
        ImFont* body=ImGui::GetIO().Fonts->Fonts[0];
        md::SetFonts(body,medium?ImGui::GetIO().Fonts->AddFontFromFileTTF(medium,14):body);
        bool open=true,first=true; int selected=1;
        const char* options[]={"Apple","Banana","Cherry"}; ImVec2 selectPoint;
        Draw draw=[&]{
            ImGui::TextUnformatted("Native Material Design 3 / popup states");
            if(variant==0&&md::BeginDialog("capture-dialog",&open,"Use location service?")) {
                ImGui::TextWrapped("Let apps determine location. Anonymous location data may be sent when apps are not running.");
                md::Button("Cancel",md::ButtonStyle::Text); ImGui::SameLine(); md::Button("Accept",md::ButtonStyle::Text); md::EndDialog();
            }
            if(variant==1||variant==2) {
                if(md::BeginSheet("capture-sheet",&open,variant==1?md::SheetSide::Bottom:md::SheetSide::Right,320)) {
                    ImGui::TextUnformatted("Additional settings"); md::ListItem("Notifications","Messages and alerts",nullptr,"N",false,true,2);
                    md::ListItem("Privacy","Permissions and activity",nullptr,"P",false,true,2); md::EndSheet();
                }
            }
            if(variant==3) {
                md::Button("Open menu");
                if(first){md::OpenMenu("capture-menu");first=false;}
                if(md::BeginMenuPopup("capture-menu")) {
                    md::MenuItem("Cut","Ctrl+X"); md::MenuItem("Copy","Ctrl+C"); md::MenuDivider(); md::MenuItem("Paste","Ctrl+V",false,false); md::EndMenuPopup();
                }
            }
            if(variant==4){md::Select("Fruit",&selected,options,3);selectPoint=Center();}
        };
        for(int frame=0;frame<8;++frame)Frame(draw);
        if(variant==4){Click(draw,selectPoint);Frame(draw);Frame(draw);}
        const char* names[]={"dialog","bottom-sheet","side-sheet","menu","select"};
        std::string name=std::string(directory)+"/native-"+(dark?"dark-":"light-")+names[variant]+".draw";
        md3test::DumpDrawData(name);md3test::DestroyContext();
    }
}
int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>=3&&std::strcmp(argv[1],"--capture")==0) {
        Capture(argv[2],argc>3?argv[3]:nullptr,argc>4?argv[4]:nullptr);std::puts("PASS: native draw captures exported");return 0;
    }
    Dates();std::puts("PASS: Gregorian date limits and leap-year boundaries");
    Controls();std::puts("PASS: mouse controls, disabled Home/End, range ordering and text input");
    ExtraControls();std::puts("PASS: carousel single-step keyboard navigation and app bar actions");
    NarrowLayout();std::puts("PASS: narrow app bar bounds and horizontally scrollable gallery");
    Popups();std::puts("PASS: nested Escape closes exactly one popup level");
    Gallery();std::puts("PASS: all gallery pages, themes, scales and context teardown");
}
