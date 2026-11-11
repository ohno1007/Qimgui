#include "pages.hpp"
#include <qimgui/md3.hpp>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <string>

namespace clock_example {
namespace {
namespace md = qimgui::md3;
float Scale() { return md::GetTheme().scale; }
float Width() { return ImGui::GetContentRegionAvail().x / Scale(); }
void Gap(float size = 8.f) { ImGui::SetCursorPosY(ImGui::GetCursorPosY() + size * Scale()); }
void Muted(const char* text) {
    ImGui::PushStyleColor(ImGuiCol_Text, md::GetTheme().onSurfaceVariant);
    ImGui::TextWrapped("%s", text); ImGui::PopStyleColor();
}
void CenterText(const char* text, float fontSize, ImVec4 color) {
    const float width = ImGui::GetContentRegionAvail().x;
    ImGui::PushFont(ImGui::GetFont(), fontSize * Scale());
    const ImVec2 measured = ImGui::CalcTextSize(text); ImGui::PopFont();
    const float fit = measured.x > width && measured.x > 0 ? width / measured.x : 1.f;
    ImGui::PushFont(ImGui::GetFont(), fontSize * Scale() * fit);
    const float textWidth = ImGui::CalcTextSize(text).x;
    const float x = ImGui::GetCursorPosX();
    ImGui::SetCursorPosX(x + std::max(0.f, (width - textWidth) * .5f));
    ImGui::PushStyleColor(ImGuiCol_Text, color); ImGui::TextUnformatted(text);
    ImGui::PopStyleColor(); ImGui::PopFont(); ImGui::SetCursorPosX(x);
}
void Hero(const char* caption, const std::string& time, const char* supporting) {
    CenterText(caption, 12, md::GetTheme().onSurfaceVariant); Gap(2);
    CenterText(time.c_str(), 72, md::GetTheme().onSurface);
    CenterText(supporting, 14, md::GetTheme().onSurfaceVariant);
}
void TimerPage(State& state) {
    auto& timer = state.timer;
    Hero("FOCUS SESSION", FormatDuration(std::ceil(timer.remainingSeconds)),
         timer.finished ? "Session complete" : timer.running ? "Make room for one thing" : "Ready when you are");
    Gap(10);
    md::WavyProgressStyle wave; wave.animate = timer.running;
    md::WavyLinearProgress("timer-progress", static_cast<float>(1.0-timer.remainingSeconds/timer.durationSeconds), Width(), wave);
    Gap(14);
    static const md::ButtonGroupItem presets[] = {{"5 min"}, {"15 min"}, {"25 min"}, {"45 min"}};
    static const int minutes[] = {5, 15, 25, 45};
    ImGui::BeginDisabled(timer.running);
    int preset = timer.preset;
    if (md::ButtonGroup("timer-presets", &preset, presets, 4)) SetTimerDuration(state, minutes[preset]*60.0);
    Gap(6);
    float duration = timer.minutes;
    if (md::Slider("Duration##timer-duration", &duration, 1.f, 90.f, Width(), 89, "%.0f min"))
        SetTimerDuration(state, std::round(duration)*60.0);
    ImGui::EndDisabled(); Gap(8);
    const float width = Width();
    const float primary = std::min(160.f, (width-8.f)*.58f);
    const float secondary = std::max(48.f, std::min(112.f, width-primary-8.f));
    const char* action = timer.running ? "Pause###timer-toggle" : timer.finished ? "Again###timer-toggle" : "Start###timer-toggle";
    if (md::Button(action, md::ButtonStyle::Filled, ImVec2(primary,48),
                   width >= 280 ? (timer.running ? md::icons::Pause : md::icons::Play) : nullptr)) ToggleTimer(state);
    ImGui::SameLine(0,8*Scale());
    if (md::Button("Reset##timer-reset", md::ButtonStyle::Tonal, ImVec2(secondary,48),
                   width >= 280 ? md::icons::Reset : nullptr)) ResetTimer(state);
    Gap(8); Muted(timer.running ? "You can switch pages while the timer runs." : "Choose a preset or set your own duration.");
}
std::tm LocalTime(std::time_t stamp) {
    std::tm value{}; if (const std::tm* result=std::localtime(&stamp)) value=*result; return value;
}
std::tm UtcTime(std::time_t stamp) {
    std::tm value{}; if (const std::tm* result=std::gmtime(&stamp)) value=*result; return value;
}
std::string ClockText(const std::tm& time,bool twentyFour,bool seconds) {
    char buffer[32]{};
    const char* format=twentyFour?(seconds?"%H:%M:%S":"%H:%M"):(seconds?"%I:%M:%S":"%I:%M %p");
    std::strftime(buffer,sizeof(buffer),format,&time); return buffer;
}
void ClockPage(State& state) {
    const auto local=LocalTime(state.wallTime); char date[80]{};
    std::strftime(date,sizeof(date),"%A, %d %B",&local);
    Hero(state.use24Hour?"LOCAL TIME":local.tm_hour<12?"LOCAL TIME / AM":"LOCAL TIME / PM",
         ClockText(local,state.use24Hour,true),date);
    Gap(12); md::Switch("24-hour time##clock-format",&state.use24Hour,false); Gap(4); md::Divider();
    // Fixed offsets are explicit; this example does not resolve DST rules.
    const std::array<int,2> offsets={8,9};
    const char* names[]={"Beijing","Tokyo"};
    const char* zones[]={"UTC +08:00","UTC +09:00"};
    for(int i=0;i<2;++i) {
        const auto city=UtcTime(state.wallTime+offsets[i]*3600);
        const auto time=ClockText(city,state.use24Hour,false);
        md::ListItem(names[i],zones[i],time.c_str(),nullptr,false,true,2);
    }
}
void StopwatchPage(State& state) {
    auto& stopwatch=state.stopwatch;
    Hero("ELAPSED TIME",FormatDuration(stopwatch.elapsedSeconds,true),
         stopwatch.running?"Every moment counts":stopwatch.elapsedSeconds>0?"Paused":"Start a new session");
    Gap(16);
    const float width=Width(),primary=std::min(144.f,(width-16.f)*.44f);
    const float other=std::min(100.f,(width-primary-16.f)*.5f);
    if(md::Button(stopwatch.running?"Pause###stopwatch-toggle":"Start###stopwatch-toggle",md::ButtonStyle::Filled,
                  ImVec2(primary,48),width>350?(stopwatch.running?md::icons::Pause:md::icons::Play):nullptr)) ToggleStopwatch(state);
    ImGui::SameLine(0,8*Scale()); ImGui::BeginDisabled(!stopwatch.running||stopwatch.laps.size()>=100);
    if(md::Button("Lap##stopwatch-lap",md::ButtonStyle::Tonal,ImVec2(other,48))) AddLap(state);
    ImGui::EndDisabled(); ImGui::SameLine(0,8*Scale());
    ImGui::BeginDisabled(stopwatch.running||stopwatch.elapsedSeconds<=0);
    if(md::Button("Reset##stopwatch-reset",md::ButtonStyle::Outlined,ImVec2(other,48))) ResetStopwatch(state);
    ImGui::EndDisabled(); Gap(18); md::Divider(); Gap(4);
    if(stopwatch.laps.empty()) { Muted("Lap times will appear here. Start the stopwatch, then tap Lap to mark a moment."); return; }
    ImGui::TextUnformatted("Laps"); Gap(4);
    if(ImGui::BeginTable("stopwatch-laps",3,ImGuiTableFlags_SizingStretchProp)) {
        ImGui::TableSetupColumn("Lap",ImGuiTableColumnFlags_WidthStretch,.65f);
        ImGui::TableSetupColumn("Split",ImGuiTableColumnFlags_WidthStretch,1.f);
        ImGui::TableSetupColumn("Total",ImGuiTableColumnFlags_WidthStretch,1.f);
        ImGui::TableHeadersRow();
        for(int i=static_cast<int>(stopwatch.laps.size())-1;i>=0;--i) {
            ImGui::TableNextRow(0,30*Scale()); ImGui::TableNextColumn(); ImGui::Text("%02d",i+1);
            const double previous=i>0?stopwatch.laps[static_cast<std::size_t>(i-1)]:0;
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatDuration(stopwatch.laps[static_cast<std::size_t>(i)]-previous,true).c_str());
            ImGui::TableNextColumn(); ImGui::TextUnformatted(FormatDuration(stopwatch.laps[static_cast<std::size_t>(i)],true).c_str());
        }
        ImGui::EndTable();
    }
    if(stopwatch.laps.size()>=100) Muted("100 laps recorded. Reset to begin a new session.");
}
void EditAlarm(State& state,int index) {
    state.editingAlarm=index; state.alarmEditorOpen=true;
    if(index>=0) {
        const auto& alarm=state.alarms[static_cast<std::size_t>(index)];
        state.draftHour=alarm.hour; state.draftMinute=alarm.minute; state.draftEnabled=alarm.enabled;
        std::snprintf(state.draftLabel,sizeof(state.draftLabel),"%s",alarm.label.c_str());
    } else {
        state.draftHour=7; state.draftMinute=0; state.draftEnabled=true;
        std::snprintf(state.draftLabel,sizeof(state.draftLabel),"New alarm");
    }
}
void AlarmDialog(State& state) {
    if(!md::BeginDialog("alarm-editor",&state.alarmEditorOpen,
                        state.editingAlarm<0?"Add alarm":"Edit alarm",false,360)) return;
    std::array<std::array<char,3>,60> values{}; std::array<const char*,60> labels{};
    for(int i=0;i<60;++i) { std::snprintf(values[i].data(),values[i].size(),"%02d",i); labels[i]=values[i].data(); }
    const float field=(Width()-12)*.5f;
    md::Select("Hour##alarm-hour",&state.draftHour,labels.data(),24,field);
    ImGui::SameLine(0,12*Scale()); md::Select("Minute##alarm-minute",&state.draftMinute,labels.data(),60,field);
    Gap(8);
    md::TextField("Label##alarm-label",state.draftLabel,sizeof(state.draftLabel),md::TextFieldStyle::Outlined,Width());
    Gap(8); md::Switch("Enabled##alarm-draft-enabled",&state.draftEnabled,false); Gap(6);
    if(md::Button("Save##alarm-save",md::ButtonStyle::Filled)) {
        Alarm alarm{state.draftHour,state.draftMinute,state.draftEnabled,state.draftLabel[0]?state.draftLabel:"Alarm"};
        if(state.editingAlarm<0) state.alarms.push_back(alarm);
        else if(static_cast<std::size_t>(state.editingAlarm)<state.alarms.size()) state.alarms[static_cast<std::size_t>(state.editingAlarm)]=alarm;
        state.alarmEditorOpen=false; ImGui::CloseCurrentPopup();
    }
    ImGui::SameLine();
    if(md::Button("Cancel##alarm-cancel",md::ButtonStyle::Text)) { state.alarmEditorOpen=false; ImGui::CloseCurrentPopup(); }
    md::EndDialog();
}
void AlarmPage(State& state) {
    const float width=Width();
    for(std::size_t i=0;i<state.alarms.size();++i) {
        auto& alarm=state.alarms[i]; ImGui::PushID(static_cast<int>(i)); ImGui::BeginGroup();
        const auto start=ImGui::GetCursorScreenPos(); const bool wide=width>=300;
        char time[12]; std::snprintf(time,sizeof(time),"%02d:%02d",alarm.hour,alarm.minute);
        ImGui::PushFont(ImGui::GetFont(),32*Scale()); ImGui::TextUnformatted(time); ImGui::PopFont();
        // Clip long caller-owned labels before the right-hand controls.
        const float labelWidth=std::max(1.f,width-(wide?140.f:64.f))*Scale();
        ImGui::PushStyleColor(ImGuiCol_Text,md::GetTheme().onSurfaceVariant);
        const auto labelPos=ImGui::GetCursorScreenPos();
        ImGui::PushClipRect(labelPos,ImVec2(labelPos.x+labelWidth,labelPos.y+20*Scale()),true);
        ImGui::TextUnformatted(alarm.label.c_str()); ImGui::PopClipRect(); ImGui::PopStyleColor();
        ImGui::SetCursorScreenPos(ImVec2(start.x+(width-52)*Scale(),start.y+6*Scale()));
        md::Switch("##alarm-enabled",&alarm.enabled,false);
        if(wide) ImGui::SetCursorScreenPos(ImVec2(start.x+(width-128)*Scale(),start.y+10*Scale()));
        else ImGui::SetCursorScreenPos(ImVec2(start.x,start.y+62*Scale()));
        if(md::Button("Edit##alarm-edit",md::ButtonStyle::Text,ImVec2(64,40))) EditAlarm(state,static_cast<int>(i));
        ImGui::SetCursorScreenPos(ImVec2(start.x,start.y+(wide?76:110)*Scale()));
        ImGui::Dummy(ImVec2(width*Scale(),0)); ImGui::EndGroup(); ImGui::PopID(); md::Divider(); Gap(6);
    }
    Gap(4); ImGui::BeginDisabled(state.alarms.size()>=12);
    if(md::Button("Add alarm##alarm-add",md::ButtonStyle::Tonal,ImVec2(0,44),md::icons::Plus)) EditAlarm(state,-1);
    ImGui::EndDisabled(); Gap(8);
    Muted("Saved for this session. This example does not schedule Android alarms.");
    AlarmDialog(state);
}
} // namespace

void DrawPage(State& state) {
    switch(state.page) {
    case Page::Alarm: AlarmPage(state); break;
    case Page::Clock: ClockPage(state); break;
    case Page::Timer: TimerPage(state); break;
    case Page::Stopwatch: StopwatchPage(state); break;
    }
}
const char* PageTitle(Page page) {
    switch(page) {
    case Page::Alarm: return "Alarm";
    case Page::Clock: return "Clock";
    case Page::Timer: return "Timer";
    case Page::Stopwatch: return "Stopwatch";
    }
    return "Clock";
}
const char* PageSubtitle(Page page) {
    switch(page) {
    case Page::Alarm: return "A little structure for your day";
    case Page::Clock: return "Your time, at a glance";
    case Page::Timer: return "A little space to focus";
    case Page::Stopwatch: return "Keep track of the moments";
    }
    return "";
}
} // namespace clock_example
