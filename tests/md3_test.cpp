#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_showcase.h"
#include "ui/md3_extra.h"
#include "ui/md3/md3_internal.h"
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
static void ChipAndSliderGeometry() {
    md3test::ConfigureContext();
    for(float scale:{.75f,1.f,1.5f}) {
        md::SetTheme(false,scale);
        bool selected=false,removed=false,disabled=false;ImVec2 size,normal,closePoint;
        Draw chip=[&]{ImGui::BeginDisabled(disabled);md::Chip("Input",md::ChipStyle::Input,&selected,&removed);size=ImGui::GetItemRectSize();closePoint=ImVec2(ImGui::GetItemRectMax().x-17*scale,Center().y);ImGui::EndDisabled();};
        Frame(chip);Frame(chip);normal=size;Click(chip,closePoint);assert(removed&&!selected);
        removed=false;disabled=true;Frame(chip);assert(std::abs(size.x-normal.x)<.01f&&std::abs(size.y-normal.y)<.01f);
        Click(chip,closePoint);assert(!removed&&!selected);
        bool filterSelected=true;
        Frame([&]{float expected=md::detail::Measure("Filter",14,true).x+50*scale;md::Chip("Filter",md::ChipStyle::Filter,&filterSelected);assert(std::abs(ImGui::GetItemRectSize().x-expected)<.01f);});
        float value=50;ImVec2 left,right;
        Draw slider=[&]{ImVec2 start=ImGui::GetCursorScreenPos();md::Slider("Endpoint",&value,0,100,240);left=ImVec2(start.x+20*scale,start.y+44*scale);right=ImVec2(start.x+220*scale,left.y);};
        Frame(slider);Frame(slider);Click(slider,left);assert(std::abs(value)<.001f);Click(slider,right);assert(std::abs(value-100)<.001f);
        float lower=25,upper=75;
        Draw range=[&]{ImVec2 start=ImGui::GetCursorScreenPos();md::RangeSlider("Endpoints",&lower,&upper,0,100,240);left=ImVec2(start.x+20*scale,start.y+44*scale);right=ImVec2(start.x+220*scale,left.y);};
        Frame(range);Frame(range);Click(range,left);assert(std::abs(lower)<.001f);Click(range,right);assert(std::abs(upper-100)<.001f);
    }
    md3test::DestroyContext();
}
static void SelectFocusAppearance() {
    md3test::ConfigureContext();md::SetTheme(false);int selected=0;const char* options[]={"Apple","Banana","Cherry"};bool primaryUnderline=false;
    Draw draw=[&]{
        ImVec2 start=ImGui::GetCursorScreenPos();auto* list=ImGui::GetWindowDrawList();int first=list->VtxBuffer.Size;
        md::Select("Fruit",&selected,options,3,280,false);
        primaryUnderline=false;const ImU32 primary=ImGui::GetColorU32(md::GetTheme().primary);
        for(int i=first;i<list->VtxBuffer.Size;++i){const auto& v=list->VtxBuffer[i];if(v.pos.y>=start.y+53&&v.pos.y<=start.y+57&&v.col==primary)primaryUnderline=true;}
    };
    for(int i=0;i<4;++i)Frame(draw);assert(!primaryUnderline); // Initial NavId is not visible keyboard focus.
    Key(draw,ImGuiKey_Tab);Frame(draw);assert(primaryUnderline);
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
static void Ripples() {
    md3test::ConfigureContext();md::SetTheme(false);
    bool disabled=false;int vertices=0;ImVec2 origin;ImRect bounds;
    Draw draw=[&]{
        ImGui::BeginDisabled(disabled);
        const auto item=md::detail::Item("ripple-region",ImVec2(112,40));bounds=item.rect;origin=ImVec2(bounds.Min.x+4,bounds.GetCenter().y);
        auto* list=ImGui::GetWindowDrawList();const int first=list->VtxBuffer.Size;
        md::detail::StateLayer(item,md::GetTheme().primary,20);vertices=list->VtxBuffer.Size-first;
        for(int i=first;i<list->VtxBuffer.Size;++i){
            const ImVec2 p=list->VtxBuffer[i].pos;const ImVec2 c=bounds.GetCenter();
            const float qx=std::abs(p.x-c.x)-(bounds.GetWidth()*.5f-20);
            const float qy=std::abs(p.y-c.y)-(bounds.GetHeight()*.5f-20);
            const float outside=std::hypot(std::max(qx,0.f),std::max(qy,0.f))+std::min(std::max(qx,qy),0.f)-20;
            assert(outside<=1.5f); // Allow the normal antialias fringe.
        }
        ImGui::EndDisabled();
    };
    Frame(draw);Frame(draw);auto& io=ImGui::GetIO();io.AddMousePosEvent(origin.x,origin.y);
    for(int i=0;i<20;++i)Frame(draw);const int hoverVertices=vertices;assert(hoverVertices>0);
    io.AddMouseButtonEvent(0,true);for(int i=0;i<6;++i)Frame(draw);assert(vertices>hoverVertices);
    disabled=true;Frame(draw);assert(vertices==0);
    io.AddMouseButtonEvent(0,false);io.AddMousePosEvent(500,500);
    for(int i=0;i<60;++i)Frame(draw);disabled=false;Frame(draw);assert(vertices==0);
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
        ImFont* label=medium?md3test::LoadComparisonFont(medium):body;
        md::SetFonts(body,label);
        for(int frame=0;frame<8;++frame)Frame([&]{md::Showcase(page);});
        std::string name=std::string(directory)+"/native-"+(dark?"dark-":"light-")+std::to_string(page)+".draw";
        md3test::DumpDrawData(name);md3test::DestroyContext();
    }
    for(bool dark:{false,true})for(int variant=0;variant<7;++variant) {
        md3test::ConfigureContext(font); md::SetTheme(dark);
        ImFont* body=ImGui::GetIO().Fonts->Fonts[0];
        md::SetFonts(body,medium?md3test::LoadComparisonFont(medium):body);
        bool open=true,first=true,nested=true; int selected=variant==6?0:1;
        const char* options[]={"Apple","Banana","Cherry"}; ImVec2 selectPoint;
        Draw draw=[&]{
            ImGui::TextUnformatted("Native Material Design 3 / popup states");
            if(variant==0&&md::BeginDialog("capture-dialog",&open,"Save changes?",false,360,false)) {
                ImGui::TextWrapped("Your changes will be saved to this project.");
                md::Button("Cancel",md::ButtonStyle::Text); ImGui::SameLine(); md::Button("Save",md::ButtonStyle::Filled); md::EndDialog();
            }
            if(variant==1||variant==2) {
                if(md::BeginSheet("capture-sheet",&open,variant==1?md::SheetSide::Bottom:md::SheetSide::Right,320)) {
                    ImGui::TextUnformatted("Additional settings"); md::ListItem("Notifications","Messages and alerts",nullptr,"N",false,true,2);
                    md::ListItem("Privacy","Permissions and activity",nullptr,"P",false,true,2); md::EndSheet();
                }
            }
            if(variant==3||variant==5) {
                md::Button("Open menu");
                ImVec2 anchor(ImGui::GetItemRectMin().x,ImGui::GetItemRectMax().y+4);
                if(first){md::OpenMenu("capture-menu");first=false;}
                ImGui::SetNextWindowPos(anchor,ImGuiCond_Appearing);
                if(md::BeginMenuPopup("capture-menu")) {
                    if(variant==5){if(nested){ImGui::OpenPopup("More actions");nested=false;}if(md::BeginSubMenu("More actions")){md::MenuItem("Duplicate");md::MenuItem("Export");md::EndSubMenu();}
                        md::MenuItem("Cut","Ctrl+X");md::MenuItem("Copy","Ctrl+C");md::MenuDivider();md::MenuItem("Paste","Ctrl+V",false,false);
                    }else{
                        md::MenuItem("New item","Ctrl+N");md::MenuItem("Show details",nullptr,true);md::MenuDivider();
                        if(md::BeginSubMenu("More actions")){md::MenuItem("Duplicate","Ctrl+D");md::EndSubMenu();}md::MenuItem("Delete");
                    }
                    md::EndMenuPopup();
                }
            }
            if(variant==4){md::Select("Fruit",&selected,options,3);selectPoint=Center();}
            if(variant==6){md::Select("Fruit",&selected,options,3,280,false,true,"Choose a fruit");}
        };
        for(int frame=0;frame<8;++frame)Frame(draw);
        if(variant==4){Click(draw,selectPoint);Frame(draw);Frame(draw);}
        const char* names[]={"dialog","bottom-sheet","side-sheet","menu","select","submenu","select-filled"};
        std::string name=std::string(directory)+"/native-"+(dark?"dark-":"light-")+names[variant]+".draw";
        md3test::DumpDrawData(name);md3test::DestroyContext();
    }
    for(bool dark:{false,true}) {
        md3test::ConfigureContext(font);md::SetTheme(dark);ImGui::GetIO().DisplaySize=ImVec2(480,240);
        ImFont* body=ImGui::GetIO().Fonts->Fonts[0];md::SetFonts(body,medium?md3test::LoadComparisonFont(medium):body);
        ImVec2 press;
        Draw draw=[&]{
            ImGui::TextUnformatted("Press feedback / expanding ripple");
            md::Button("Pressed",md::ButtonStyle::Outlined,ImVec2(240,48));
            press=ImVec2(ImGui::GetItemRectMin().x+8,ImGui::GetItemRectMin().y+24);
            ImGui::BeginDisabled();md::Button("Disabled",md::ButtonStyle::Outlined,ImVec2(240,48));ImGui::EndDisabled();
            ImGui::TextUnformatted("The ripple is clipped to the rounded button.");
        };
        for(int i=0;i<8;++i)Frame(draw);ImGui::GetIO().AddMousePosEvent(press.x,press.y);Frame(draw);
        ImGui::GetIO().AddMouseButtonEvent(0,true);for(int i=0;i<6;++i)Frame(draw);
        md3test::DumpDrawData(std::string(directory)+"/native-"+(dark?"dark-":"light-")+"pressed.draw");
        md3test::DestroyContext();
    }
}
int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>=3&&std::strcmp(argv[1],"--capture")==0) {
        Capture(argv[2],argc>3?argv[3]:nullptr,argc>4?argv[4]:nullptr);std::puts("PASS: native draw captures exported");return 0;
    }
    Dates();std::puts("PASS: Gregorian date limits and leap-year boundaries");
    Controls();std::puts("PASS: mouse controls, disabled Home/End, range ordering and text input");
    ChipAndSliderGeometry();std::puts("PASS: chip close targets and disabled dimensions; slider endpoints at three densities");
    SelectFocusAppearance();std::puts("PASS: filled select uses visible keyboard focus, not automatic initial NavId");
    ExtraControls();std::puts("PASS: carousel single-step keyboard navigation and app bar actions");
    Ripples();std::puts("PASS: expanding ripples stay inside rounded bounds and disabled states emit none");
    NarrowLayout();std::puts("PASS: narrow app bar bounds and horizontally scrollable gallery");
    Popups();std::puts("PASS: nested Escape closes exactly one popup level");
    Gallery();std::puts("PASS: all gallery pages, themes, scales and context teardown");
}
