#include "app.hpp"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
#include <unordered_map>
#include <vector>
namespace app=clock_example;
namespace md=qimgui::md3;
namespace {
struct Item { ImRect bounds,clip; ImGuiID id; ImGuiWindow* window; };
std::unordered_map<std::string,std::vector<Item>> items;
std::vector<Item> destinations;
const char* labels[]={"theme-toggle","window-minimize","window-close","window-drag",
 "Start###timer-toggle","Pause###timer-toggle","Again###timer-toggle","Reset##timer-reset",
 "Duration##timer-duration","Start###stopwatch-toggle","Pause###stopwatch-toggle",
 "Lap##stopwatch-lap","Reset##stopwatch-reset","Edit##alarm-edit","##alarm-enabled",
 "Add alarm##alarm-add","Hour##alarm-hour","Minute##alarm-minute","##input","08","45",
 "Enabled##alarm-draft-enabled","Save##alarm-save","Cancel##alarm-cancel","24-hour time##clock-format"};
constexpr std::time_t kWall=1762848300;
const char* bodyFont="tests/md3_web/fonts/Roboto-400.ttf";
const char* labelFont="tests/md3_web/fonts/Roboto-500.ttf";
void Configure(ImVec2 size) {
 md3test::ConfigureContext(); auto& io=ImGui::GetIO(); io.DisplaySize=size;
 io.FontDefault=md3test::LoadComparisonFont(bodyFont);
 md::SetFonts(io.FontDefault,md3test::LoadComparisonFont(labelFont));
 GImGui->TestEngineHookItems=true;
}
bool Frame(app::App& state,bool& visible) {
 items.clear();destinations.clear();ImGui::NewFrame();
 const bool running=app::Draw(state,visible,ImGui::GetIO().DisplaySize);
 ImGui::Render();md3test::ValidateDrawData();md3test::UploadTextures();
 for(const auto* list:ImGui::GetDrawData()->CmdLists)for(const auto& v:list->VtxBuffer)
  assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y));
 return running;
}
void Settle(app::App& state,bool& visible){Frame(state,visible);Frame(state,visible);Frame(state,visible);}
Item Find(const char* label,std::size_t index=0) {
 const auto found=items.find(label);
 if(found==items.end()||found->second.size()<=index)std::fprintf(stderr,"Missing widget: %s [%zu]\n",label,index);
 assert(found!=items.end()&&found->second.size()>index);return found->second[index];
}
bool ClickPoint(app::App& state,bool& visible,ImVec2 point) {
 auto& io=ImGui::GetIO();io.AddMousePosEvent(point.x,point.y);Frame(state,visible);
 io.AddMouseButtonEvent(0,true);Frame(state,visible);
 io.AddMouseButtonEvent(0,false);const bool running=Frame(state,visible);
 Frame(state,visible);return running;
}
bool Click(app::App& state,bool& visible,const char* label,std::size_t index=0) {
 Item item=Find(label,index);
 if(!item.clip.Contains(item.bounds.GetCenter())){
  ImGui::SetScrollFromPosY(item.window,item.bounds.GetCenter().y-item.window->Pos.y,.5f);
  Settle(state,visible);item=Find(label,index);
 }
 ImRect clipped=item.bounds;clipped.ClipWith(item.clip);
 if(clipped.GetWidth()<=0||clipped.GetHeight()<=0)std::fprintf(stderr,"Widget clipped: %s\n",label);
 assert(clipped.GetWidth()>0&&clipped.GetHeight()>0);
 return ClickPoint(state,visible,clipped.GetCenter());
}
void Navigate(app::App& state,bool& visible,app::Page page) {
 assert(destinations.size()==4);const auto target=destinations[static_cast<int>(page)];
 assert(target.clip.Contains(target.bounds.GetCenter()));
 ClickPoint(state,visible,target.bounds.GetCenter());assert(state.state.page==page);
}
void Key(app::App& state,bool& visible,ImGuiKey key) {
 auto& io=ImGui::GetIO();io.AddKeyEvent(key,true);Frame(state,visible);
 io.AddKeyEvent(key,false);Frame(state,visible);Frame(state,visible);
}
void Bounds(const app::App& state) {
 const auto display=ImGui::GetIO().DisplaySize;const auto r=state.inputBounds;
 assert(r.x>=-.1f&&r.y>=-.1f&&r.z>0&&r.w>0);
 assert(r.x+r.z<=display.x+.1f&&r.y+r.w<=display.y+.1f);
 ImGuiWindow* rail=nullptr;ImGuiWindow* content=nullptr;
 for(auto* window:GImGui->Windows)if(window->Active){
  if(std::strstr(window->Name,"clock-rail"))rail=window;
  if(std::strstr(window->Name,"clock-page"))content=window;
 }
 assert(rail&&content);assert(rail->Pos.x+rail->Size.x<=content->Pos.x+.1f);
 assert(content->Pos.x+content->Size.x<=r.x+r.z+.1f&&content->Pos.y+content->Size.y<=r.y+r.w+.1f);
 assert(content->ClipRect.GetWidth()>0&&content->ClipRect.GetHeight()>0);
 for(const char* label:{"theme-toggle","window-minimize","window-close"}){
  const auto item=Find(label);
  assert(item.bounds.Min.x>=r.x-.1f&&item.bounds.Max.x<=r.x+r.z+.1f);
  assert(item.bounds.Min.y>=r.y-.1f&&item.bounds.Max.y<=r.y+r.w+.1f);
 }
}
void Model() {
 app::State s;app::Update(s,100,kWall);app::SetTimerDuration(s,3);app::ToggleTimer(s);
 app::Update(s,101.25,kWall+8000);assert(std::abs(s.timer.remainingSeconds-1.75)<1e-8);
 app::Update(s,90,kWall);app::Update(s,std::numeric_limits<double>::quiet_NaN(),kWall);
 assert(std::abs(s.timer.remainingSeconds-1.75)<1e-8);
 app::ToggleTimer(s);app::Update(s,200,kWall);assert(std::abs(s.timer.remainingSeconds-1.75)<1e-8);
 app::ToggleTimer(s);app::Update(s,202,kWall);assert(s.timer.finished&&!s.timer.running&&s.timer.remainingSeconds==0);
 app::ToggleTimer(s);assert(s.timer.running&&!s.timer.finished&&s.timer.remainingSeconds==3);
 app::ResetTimer(s);assert(!s.timer.running&&!s.timer.finished&&s.timer.remainingSeconds==3);
 app::SetTimerDuration(s,-5);assert(s.timer.durationSeconds==1);
 app::SetTimerDuration(s,1e9);assert(s.timer.durationSeconds==86400);
 app::SetTimerDuration(s,std::numeric_limits<double>::infinity());assert(s.timer.durationSeconds==86400);
 app::ToggleStopwatch(s);app::Update(s,203.125,kWall);
 assert(std::abs(s.stopwatch.elapsedSeconds-1.125)<1e-8&&app::AddLap(s));assert(!app::AddLap(s));
 app::ToggleStopwatch(s);app::Update(s,900,kWall);assert(std::abs(s.stopwatch.elapsedSeconds-1.125)<1e-8&&!app::AddLap(s));
 app::ToggleStopwatch(s);for(int i=0;i<150;++i){app::Update(s,901+i,kWall);app::AddLap(s);}
 assert(s.stopwatch.laps.size()==100);app::ResetStopwatch(s);
 assert(!s.stopwatch.running&&s.stopwatch.elapsedSeconds==0&&s.stopwatch.laps.empty());
 assert(app::FormatDuration(3599.99,true)=="59:59.99"&&app::FormatDuration(3600)=="1:00:00"&&app::FormatDuration(-1)=="00:00");
 std::puts("PASS: elapsed-time model, pause/resume, completion, invalid clock samples and lap bounds");
}
void PagesAndGeometry() {
 for(const auto size:{ImVec2(360,640),ImVec2(480,320),ImVec2(720,480),ImVec2(1200,900),ImVec2(1216,2640)}){
  Configure(size);app::App state;bool visible=true;app::Update(state.state,100,kWall);
  for(bool dark:{false,true}){state.state.dark=dark;for(int page=0;page<4;++page){
   state.state.page=static_cast<app::Page>(page);Settle(state,visible);Bounds(state);
   assert(destinations.size()==4&&ImGui::GetDrawData()->TotalVtxCount>0);
  }}
  ImGui::GetIO().DisplaySize=ImVec2(size.y,size.x);Settle(state,visible);Bounds(state);md3test::DestroyContext();
 }
 std::puts("PASS: four pages in both themes, compact left/right layout and display rotation");
}
void Interaction() {
 Configure(ImVec2(1200,900));app::App state;bool visible=true;
 app::Update(state.state,100,kWall);Settle(state,visible);
 assert(state.state.page==app::Page::Timer);
 Click(state,visible,"Start###timer-toggle");assert(state.state.timer.running);
 app::Update(state.state,110,kWall);Settle(state,visible);assert(state.state.timer.remainingSeconds==1490);
 Navigate(state,visible,app::Page::Clock);app::Update(state.state,120,kWall);
 const bool format=state.state.use24Hour;Click(state,visible,"24-hour time##clock-format");
 assert(state.state.use24Hour!=format&&state.state.timer.remainingSeconds==1480);
 Navigate(state,visible,app::Page::Timer);Click(state,visible,"Pause###timer-toggle");
 assert(!state.state.timer.running);app::Update(state.state,200,kWall);assert(state.state.timer.remainingSeconds==1480);
 Click(state,visible,"Reset##timer-reset");assert(state.state.timer.remainingSeconds==1500);
 Click(state,visible,"Start###timer-toggle");visible=false;app::Update(state.state,207,kWall);Frame(state,visible);
 assert(state.state.timer.remainingSeconds==1493&&state.inputBounds.z==0);
 visible=true;Settle(state,visible);Navigate(state,visible,app::Page::Stopwatch);
 Click(state,visible,"Start###stopwatch-toggle");assert(state.state.stopwatch.running);
 app::Update(state.state,219.5,kWall);Settle(state,visible);
 Click(state,visible,"Lap##stopwatch-lap");assert(state.state.stopwatch.laps.size()==1&&state.state.stopwatch.laps[0]==12.5);
 Click(state,visible,"Reset##stopwatch-reset");assert(state.state.stopwatch.running&&state.state.stopwatch.elapsedSeconds==12.5);
 Click(state,visible,"Pause###stopwatch-toggle");assert(!state.state.stopwatch.running);
 app::Update(state.state,300,kWall);assert(state.state.stopwatch.elapsedSeconds==12.5);
 Click(state,visible,"Lap##stopwatch-lap");assert(state.state.stopwatch.laps.size()==1);
 Click(state,visible,"Reset##stopwatch-reset");assert(state.state.stopwatch.laps.empty()&&state.state.stopwatch.elapsedSeconds==0);
 Navigate(state,visible,app::Page::Alarm);
 Click(state,visible,"##alarm-enabled",0);assert(!state.state.alarms[0].enabled);
 Click(state,visible,"Edit##alarm-edit",0);assert(state.state.alarmEditorOpen&&state.state.editingAlarm==0);
 Settle(state,visible);Click(state,visible,"Hour##alarm-hour");Settle(state,visible);
 Click(state,visible,"08");assert(state.state.draftHour==8);
 Click(state,visible,"##input");
 ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,true);Key(state,visible,ImGuiKey_A);
 ImGui::GetIO().AddKeyEvent(ImGuiMod_Ctrl,false);Frame(state,visible);
 ImGui::GetIO().AddInputCharactersUTF8("Focus time");Frame(state,visible);
 assert(std::strcmp(state.state.draftLabel,"Focus time")==0);
 Click(state,visible,"Save##alarm-save");
 assert(!state.state.alarmEditorOpen&&state.state.alarms[0].hour==8&&state.state.alarms[0].label=="Focus time");
 Click(state,visible,"Add alarm##alarm-add");Settle(state,visible);
 Click(state,visible,"Cancel##alarm-cancel");assert(state.state.alarms.size()==3);
 Click(state,visible,"Add alarm##alarm-add");Settle(state,visible);
 Click(state,visible,"Save##alarm-save");assert(state.state.alarms.size()==4);
 Click(state,visible,"theme-toggle");assert(state.state.dark);
 Click(state,visible,"window-minimize");assert(!visible&&state.inputBounds.z==0);
 visible=true;Settle(state,visible);assert(!Click(state,visible,"window-close"));
 md3test::DestroyContext();
 std::puts("PASS: rail navigation, timer across pages/hidden frames, stopwatch actions, alarm edit/add/cancel and window controls");
}
void WindowGeometryActions() {
 Configure(ImVec2(1200,900));app::App state;bool visible=true;Settle(state,visible);
 auto& io=ImGui::GetIO();
 const auto original=state.position;
 const auto handle=Find("window-drag").bounds.GetCenter();
 io.AddMousePosEvent(handle.x,handle.y);Frame(state,visible);
 io.AddMouseButtonEvent(0,true);Frame(state,visible);
 io.AddMousePosEvent(handle.x+32,handle.y+20);Frame(state,visible);
 io.AddMouseButtonEvent(0,false);Settle(state,visible);
 assert(std::abs(state.position.x-original.x-32)<1&&std::abs(state.position.y-original.y-20)<1);
 Bounds(state);
 const auto before=state.size;
 const ImVec2 grip(state.inputBounds.x+before.x-3,state.inputBounds.y+before.y-3);
 io.AddMousePosEvent(grip.x,grip.y);Frame(state,visible);
 io.AddMouseButtonEvent(0,true);Frame(state,visible);
 io.AddMousePosEvent(grip.x+40,grip.y+24);Frame(state,visible);
 io.AddMouseButtonEvent(0,false);Settle(state,visible);
 assert(state.size.x>before.x+20&&state.size.y>before.y+10);Bounds(state);
 md3test::DestroyContext();
 std::puts("PASS: drag handle moves the window and native resize grip changes its size");
}
void CaptureWindow(const std::filesystem::path& path, ImVec4 bounds) {
 // Render the native window viewport directly, without the unused screen area.
 auto* data=ImGui::GetDrawData();
 const auto position=data->DisplayPos,size=data->DisplaySize;
 data->DisplayPos=ImVec2(bounds.x-4,bounds.y-4);
 data->DisplaySize=ImVec2(bounds.z+8,bounds.w+8);
 md3test::DumpDrawData(path.string());
 data->DisplayPos=position;data->DisplaySize=size;
}
void Capture(const std::filesystem::path& directory) {
 std::filesystem::create_directories(directory);const char* names[]={"alarm","clock","timer","stopwatch"};
 for(bool dark:{false,true})for(int page=0;page<4;++page){
  Configure(ImVec2(1200,900));app::App state;bool visible=true;
  state.state.dark=dark;state.state.page=static_cast<app::Page>(page);app::Update(state.state,100,kWall);
  if(page==3){state.state.stopwatch.elapsedSeconds=83.42;state.state.stopwatch.laps={24.63,53.12,83.42};}
  for(int i=0;i<16;++i)Frame(state,visible);
  const std::string filename=std::string("md3-clock-")+names[page]+(dark?"-dark.draw":"-light.draw");
  md3test::DumpDrawData((directory/filename).string());
  CaptureWindow(directory/("window-"+filename),state.inputBounds);
  md3test::DestroyContext();
 }
 Configure(ImVec2(1216,2640));app::App phone;bool visible=true;app::Update(phone.state,100,kWall);
 for(int i=0;i<16;++i)Frame(phone,visible);
 md3test::DumpDrawData((directory/"md3-clock-phone.draw").string());md3test::DestroyContext();
 std::printf("Captured SDK clock app in %s\n",directory.string().c_str());
}
}
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context,ImGuiID id,const ImRect& bounds,const ImGuiLastItemData*) {
 auto* window=context->CurrentWindow;if(!window||window->IDStack.empty())return;
 const Item item{bounds,window->ClipRect,id,window};
 if(std::strstr(window->Name,"clock-rail")&&window->GetID("destination")==id)destinations.push_back(item);
 for(const char* label:labels){
  if(window->GetID(label)==id)items[label].push_back(item);
  if(window->IDStack.Size>=2&&window->GetID("##field")==id&&
   ImHashStr(label,0,window->IDStack[window->IDStack.Size-2])==window->IDStack.back())items[label].push_back(item);
 }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID,const char*,ImGuiItemStatusFlags){}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...){}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID){return nullptr;}
int main(int argc,char** argv){
 if(argc>1){assert(argc==3||argc==5);assert(std::strcmp(argv[1],"--capture")==0);if(argc==5){bodyFont=argv[3];labelFont=argv[4];}}
 Model();PagesAndGeometry();Interaction();WindowGeometryActions();if(argc>1)Capture(argv[2]);
}
