#include "ui/md3.h"
#include "ui/md3_selection.h"
#include "imgui_internal.h"
#include "md3_internal.h"
#include <algorithm>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <cctype>

namespace aimgui::md3 {
namespace {
float S(float value) { return value * GetTheme().scale; }
ImU32 C(const ImVec4& value, float alpha = 1.0f) {
    ImVec4 c = value; c.w *= alpha * ImGui::GetStyle().Alpha;
    return ImGui::ColorConvertFloat4ToU32(c);
}
void Label(ImVec2 pos, const char* value, const ImVec4& color, float size = 14) {
    const char* end = std::strstr(value, "##");
    ImGui::GetWindowDrawList()->AddText(detail::Font(), S(size), pos, C(color), value, end);
}
void Center(ImVec2 min, ImVec2 max, const char* value, const ImVec4& color, float size = 14) {
    const char* end = std::strstr(value, "##");
    const ImVec2 text = detail::Font()->CalcTextSizeA(S(size), FLT_MAX, 0, value, end);
    Label(ImVec2((min.x + max.x - text.x) / 2, (min.y + max.y - text.y) / 2), value, color, size);
}
void PopupStyle(bool menu = false) {
    const auto& t = GetTheme();
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .38f);
    ImGui::PushStyleVar(ImGuiStyleVar_PopupRounding, S(4));
    ImGui::PushStyleVar(ImGuiStyleVar_PopupBorderSize, 0);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(S(12), S(menu ? 24 : 12)));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(menu ? 32 : 8)));
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0, .5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(S(12), S(12)));
    ImGui::PushStyleColor(ImGuiCol_PopupBg, t.surfaceContainer);
    ImGui::PushStyleColor(ImGuiCol_Text, t.onSurface);
    ImGui::PushStyleColor(ImGuiCol_Header, t.secondaryContainer);
    ImVec4 hover = t.onSurface; hover.w = .08f;
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, hover);
    hover.w = .12f; ImGui::PushStyleColor(ImGuiCol_HeaderActive, hover);
    ImGui::PushFont(detail::Font(), S(16));
}
void EndPopupStyle() { ImGui::PopFont(); ImGui::PopStyleColor(5); ImGui::PopStyleVar(7); }
bool Action(const char* label, ImVec2 size, bool selected = false) {
    if (ImGui::GetCurrentWindow()->SkipItems) return false;
    const auto& t = GetTheme();
    const ImVec2 min = ImGui::GetCursorScreenPos();
    const bool pressed = ImGui::InvisibleButton(label, ImVec2(S(size.x), S(size.y)), ImGuiButtonFlags_EnableNav);
    const ImVec2 max = ImGui::GetItemRectMax(); auto* draw = ImGui::GetWindowDrawList();
    if (selected) draw->AddRectFilled(min, max, C(t.primary), S(size.y / 2));
    if (ImGui::IsItemHovered() || ImGui::IsItemActive()) {
        const ImVec4& color = selected ? t.onPrimary : t.onSurface;
        draw->AddRectFilled(min, max, C(color, ImGui::IsItemActive() ? .12f : .08f), S(size.y / 2));
    }
    if (ImGui::IsItemFocused()) draw->AddRect(min, max, C(t.primary), S(size.y / 2), S(2), 0);
    Center(min, max, label, selected ? t.onPrimary : t.onSurface); return pressed;
}
int Weekday(Date date) {
    static const int offsets[] = {0,3,2,5,0,3,5,1,4,6,2,4};
    int year = date.year - (date.month < 3);
    return (year + year/4 - year/100 + year/400 + offsets[date.month-1] + date.day) % 7;
}
void FormatDate(Date date, char* out, std::size_t size) {
    std::snprintf(out, size, "%04d-%02d-%02d", date.year, date.month, date.day);
}
void MoveMonth(int& year, int& month, int delta) {
    const int serial = std::clamp((year - 1) * 12 + month - 1 + delta, 0, 9999 * 12 - 1);
    year = serial / 12 + 1; month = serial % 12 + 1;
}
bool Calendar(const char* id, Date selected, const DateRange* range, Date* picked, bool enabled) {
    ImGui::PushID(id); const auto& t = GetTheme(); auto* storage = ImGui::GetStateStorage();
    const ImGuiID monthId = ImGui::GetID("view-month"), yearId = ImGui::GetID("view-year"), modeId = ImGui::GetID("year-mode");
    int month = storage->GetInt(monthId, selected.month), year = storage->GetInt(yearId, selected.year);
    bool yearMode = storage->GetBool(modeId, false), changed = false;
    ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .38f);
    ImGui::BeginDisabled(!enabled);
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(S(4), S(4))); ImGui::BeginGroup();
    char title[48];
    static const char* months[] = {"January","February","March","April","May","June", "July","August","September","October","November","December"};
    std::snprintf(title, sizeof(title), "%s %d##month", months[month-1], year);
    if (Action(title, ImVec2(192, 40))) yearMode = !yearMode;
    ImGui::SameLine(); ImGui::BeginDisabled(year == 1 && month == 1);
    if (Action("<##previous", ImVec2(40,40))) MoveMonth(year, month, -1);
    ImGui::EndDisabled(); ImGui::SameLine(); ImGui::BeginDisabled(year == 9999 && month == 12);
    if (Action(">##next", ImVec2(40,40))) MoveMonth(year, month, 1);
    ImGui::EndDisabled();
    if (yearMode) {
        ImGui::SetNextItemWidth(S(276)); ImGui::InputInt("##year", &year, 1, 10); year = std::clamp(year, 1, 9999);
        for (int m = 1; m <= 12; ++m) {
            if (m % 3 != 1) ImGui::SameLine();
            ImGui::PushID(m);
            if (Action(months[m-1], ImVec2(89.3f,48), month == m)) { month = m; yearMode = false; }
            ImGui::PopID();
        }
    } else {
        ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(4)));
        const char* weekdays[] = {"S","M","T","W","T","F","S"};
        for (int i = 0; i < 7; ++i) {
            if (i) ImGui::SameLine();
            ImVec2 p = ImGui::GetCursorScreenPos();
            ImGui::Dummy(ImVec2(S(40), S(32))); Center(p, ImGui::GetItemRectMax(), weekdays[i], t.onSurfaceVariant);
        }
        const int start = Weekday({year, month, 1}), days = DaysInMonth(year, month);
        for (int cell = 0; cell < 42; ++cell) {
            if (cell % 7) ImGui::SameLine();
            const int day = cell - start + 1;
            if (day < 1 || day > days) { ImGui::Dummy(ImVec2(S(40), S(40))); continue; }
            const Date current{year, month, day};
            const bool endpoint = range ? CompareDates(current, range->start) == 0 || CompareDates(current, range->end) == 0 : CompareDates(current, selected) == 0;
            const bool inRange = range && CompareDates(range->start, range->end) < 0 &&
                                 CompareDates(current, range->start) >= 0 && CompareDates(current, range->end) <= 0;
            ImVec2 p = ImGui::GetCursorScreenPos();
            if (inRange) {
                const float left = p.x + (CompareDates(current, range->start) == 0 ? S(20) : 0);
                const float right = p.x + (CompareDates(current, range->end) == 0 ? S(20) : S(40));
                ImGui::GetWindowDrawList()->AddRectFilled(ImVec2(left,p.y),ImVec2(right,p.y+S(40)),C(t.secondaryContainer));
            }
            char label[20]; std::snprintf(label,sizeof(label),"%d##day%d",day,day);
            if (Action(label,ImVec2(40,40),endpoint)) { *picked=current; changed=true; }
        }
        ImGui::PopStyleVar();
    }
    ImGui::EndGroup(); ImGui::PopStyleVar(); ImGui::EndDisabled(); ImGui::PopStyleVar();
    storage->SetInt(monthId,month); storage->SetInt(yearId,year); storage->SetBool(modeId,yearMode); ImGui::PopID(); return changed;
}
bool ContainsInsensitive(const char* haystack, const char* needle) {
    if (!needle || !*needle) return true;
    for (; *haystack; ++haystack) {
        const unsigned char* a=reinterpret_cast<const unsigned char*>(haystack);
        const unsigned char* b=reinterpret_cast<const unsigned char*>(needle);
        while(*a&&*b&&std::tolower(*a)==std::tolower(*b)) { ++a; ++b; }
        if(!*b) return true;
    }
    return false;
}
}
bool IsLeapYear(int year) { return year>=1&&year<=9999&&year%4==0&&(year%100!=0||year%400==0); }
int DaysInMonth(int year,int month) {
    static const int days[]={31,28,31,30,31,30,31,31,30,31,30,31};
    if(year<1||year>9999||month<1||month>12) return 0;
    return days[month-1]+(month==2&&IsLeapYear(year)?1:0);
}
bool IsValidDate(Date date) { return date.day>=1&&date.day<=DaysInMonth(date.year,date.month); }
int CompareDates(Date lhs,Date rhs) {
    if(lhs.year!=rhs.year) return lhs.year<rhs.year?-1:1;
    if(lhs.month!=rhs.month) return lhs.month<rhs.month?-1:1;
    return lhs.day==rhs.day?0:lhs.day<rhs.day?-1:1;
}
Date ClampDate(Date date) {
    date.year=std::clamp(date.year,1,9999); date.month=std::clamp(date.month,1,12);
    date.day=std::clamp(date.day,1,DaysInMonth(date.year,date.month)); return date;
}
bool Select(const char* label,int* selected,const char* const* options,int count,float width,bool outlined,bool enabled,const char* supporting,bool error) {
    if(!selected||!options||count<=0||ImGui::GetCurrentWindow()->SkipItems) return false;
    const auto& t=GetTheme(); bool changed=false; ImGui::PushID(label); ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .38f);
    ImGui::BeginDisabled(!enabled);
    const ImVec2 pos=ImGui::GetCursorScreenPos();
    const float w=std::max(S(112),std::min(S(width),ImGui::GetContentRegionAvail().x));
    const bool hit=ImGui::InvisibleButton("##field",ImVec2(w,S(56)),ImGuiButtonFlags_EnableNav);
    const bool focus=ImGui::IsItemFocused(); const ImVec2 end=ImGui::GetItemRectMax(); auto* draw=ImGui::GetWindowDrawList();
    const ImVec4 edge=error?t.error:focus?t.primary:t.outline;
    if(!outlined) draw->AddRectFilled(pos,end,C(t.surfaceContainerHigh),S(4),ImDrawFlags_RoundTop);
    if(outlined) draw->AddRect(pos,end,C(edge),S(4),S(focus?2:1),0);
    else draw->AddLine(ImVec2(pos.x,end.y),end,C(edge),S(focus?2:1));
    if(ImGui::IsItemHovered()) draw->AddRectFilled(pos,end,C(t.onSurface,.08f),S(4));
    if (outlined) {
        const ImVec2 labelSize = detail::Measure(label, 12);
        draw->AddRectFilled(ImVec2(pos.x+S(12),pos.y-S(7)),
                            ImVec2(pos.x+S(20)+labelSize.x,pos.y+S(7)),C(t.surface));
    }
    Label(ImVec2(pos.x+S(16),pos.y+S(outlined ? -6 : 7)),label,error?t.error:t.onSurfaceVariant,12);
    const char* value=*selected>=0&&*selected<count?options[*selected]:"Select an option";
    draw->PushClipRect(ImVec2(pos.x+S(16),pos.y),ImVec2(end.x-S(40),end.y),true);
    Label(ImVec2(pos.x+S(16),pos.y+S(outlined ? 20 : 27)),value?value:"",t.onSurface,16); draw->PopClipRect();
    const ImVec2 arrow(end.x-S(24),pos.y+S(28));
    draw->AddTriangleFilled(ImVec2(arrow.x-S(5),arrow.y-S(2)),ImVec2(arrow.x+S(5),arrow.y-S(2)),ImVec2(arrow.x,arrow.y+S(3)),C(t.onSurfaceVariant));
    if(hit) ImGui::OpenPopup("options");
    if(supporting&&*supporting) {
        ImVec2 p=ImGui::GetCursorScreenPos(); Label(ImVec2(p.x+S(16),p.y),supporting,error?t.error:t.onSurfaceVariant,12); ImGui::Dummy(ImVec2(w,S(16)));
    }
    ImGui::SetNextWindowPos(ImVec2(pos.x,end.y+S(4)),ImGuiCond_Appearing);
    ImGui::SetNextWindowSizeConstraints(ImVec2(w,0),ImVec2(w,S(288))); PopupStyle();
    if(ImGui::BeginPopup("options")) {
        for(int i=0;i<count;++i) {
            ImGui::PushID(i);
            if(ImGui::Selectable(options[i]?options[i]:"",i==*selected,0,ImVec2(0,S(40)))) { changed=*selected!=i; *selected=i; }
            if(i==*selected) ImGui::SetItemDefaultFocus();
            ImGui::PopID();
        }
        ImGui::EndPopup();
    }
    EndPopupStyle(); ImGui::EndDisabled(); ImGui::PopStyleVar(); ImGui::PopID(); return changed;
}
bool SearchBar(const char* label,char* buffer,std::size_t capacity,const char* const* suggestions,int count,float width,bool enabled) {
    if(!buffer||capacity==0||ImGui::GetCurrentWindow()->SkipItems) return false;
    ImGui::PushID(label); ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .38f);
    ImGui::BeginDisabled(!enabled); const auto& t=GetTheme();
    const float w=std::max(S(112),std::min(S(width),ImGui::GetContentRegionAvail().x));
    const ImVec2 pos=ImGui::GetCursorScreenPos(); auto* draw=ImGui::GetWindowDrawList();
    draw->AddRectFilled(pos,ImVec2(pos.x+w,pos.y+S(56)),C(t.surfaceContainerHigh),S(28));
    draw->AddCircle(ImVec2(pos.x+S(26),pos.y+S(25)),S(7),C(t.onSurfaceVariant),0,S(2));
    draw->AddLine(ImVec2(pos.x+S(31),pos.y+S(30)),ImVec2(pos.x+S(37),pos.y+S(36)),C(t.onSurfaceVariant),S(2));
    ImGui::SetCursorScreenPos(ImVec2(pos.x+S(48),pos.y+S(8)));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(0,S(11))); ImGui::PushStyleVar(ImGuiStyleVar_FrameBorderSize,0);
    ImGui::PushStyleColor(ImGuiCol_FrameBg,ImVec4(0,0,0,0)); ImGui::PushStyleColor(ImGuiCol_Text,t.onSurface);
    ImGui::PushFont(detail::Font(),S(16)); ImGui::SetNextItemWidth(std::max(S(16),w-S(96)));
    bool changed=ImGui::InputTextWithHint("##query",label,buffer,capacity); const bool active=ImGui::IsItemActive();
    ImGui::PopFont(); ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    ImGui::SetCursorScreenPos(ImVec2(pos.x+w-S(48),pos.y+S(8)));
    if(Action("x##clear",ImVec2(40,40))) { changed=buffer[0]!='\0'; buffer[0]='\0'; }
    ImGui::SetCursorScreenPos(pos); ImGui::Dummy(ImVec2(w,S(56)));
    auto* storage=ImGui::GetStateStorage(); const ImGuiID viewId=ImGui::GetID("expanded");
    bool expanded=storage->GetBool(viewId,false); if(active) expanded=true;
    if(!enabled||(expanded&&ImGui::IsKeyPressed(ImGuiKey_Escape))) expanded=false;
    if(expanded&&suggestions&&count>0) {
        ImGui::PushStyleColor(ImGuiCol_ChildBg,t.surfaceContainerHigh); ImGui::PushStyleVar(ImGuiStyleVar_ChildRounding,S(16));
        if(ImGui::BeginChild("suggestions",ImVec2(w,S(160)),ImGuiChildFlags_Borders)) {
            for(int i=0;i<count;++i) {
                if(!suggestions[i]||!ContainsInsensitive(suggestions[i],buffer)) continue;
                ImGui::PushID(i);
                if(ImGui::Selectable(suggestions[i],false,0,ImVec2(0,S(40)))) {
                    const std::size_t n=std::min(capacity-1,std::strlen(suggestions[i])); std::size_t safe=n;
                    if(suggestions[i][n]) while(safe&&(static_cast<unsigned char>(suggestions[i][safe])&0xc0)==0x80) --safe;
                    changed|=std::strlen(buffer)!=safe||std::strncmp(buffer,suggestions[i],safe)!=0;
                    std::memcpy(buffer,suggestions[i],safe); buffer[safe]='\0'; expanded=false;
                }
                ImGui::PopID();
            }
        }
        ImGui::EndChild(); ImGui::PopStyleVar(); ImGui::PopStyleColor();
    }
    storage->SetBool(viewId,expanded); ImGui::EndDisabled(); ImGui::PopStyleVar(); ImGui::PopID(); return changed;
}
void OpenMenu(const char* id) { ImGui::OpenPopup(id); }
bool BeginMenuPopup(const char* id,float minimumWidth) {
    PopupStyle(true); ImGui::SetNextWindowSizeConstraints(ImVec2(S(minimumWidth),0),ImVec2(FLT_MAX,FLT_MAX));
    if(ImGui::BeginPopup(id)) return true;
    EndPopupStyle(); return false;
}
void EndMenuPopup() { ImGui::EndPopup(); EndPopupStyle(); }
bool BeginSubMenu(const char* label,bool enabled) { return ImGui::BeginMenu(label,enabled); }
void EndSubMenu() { ImGui::EndMenu(); }
bool MenuItem(const char* label,const char* shortcut,bool selected,bool enabled) { return ImGui::MenuItem(label,shortcut,selected,enabled); }
void MenuDivider() {
    ImGui::SetCursorPosY(ImGui::GetCursorPosY() - S(8));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(0, S(24)));
    ImGui::PushStyleColor(ImGuiCol_Separator,GetTheme().outlineVariant);
    ImGui::Separator(); ImGui::PopStyleColor(); ImGui::PopStyleVar();
}
bool DatePicker(const char* id,Date* date,bool enabled) {
    if(!date||ImGui::GetCurrentWindow()->SkipItems) return false;
    Date value=ClampDate(*date),picked=value; ImGui::PushID(id); ImGui::PushFont(detail::Font(),S(14)); char heading[32]; FormatDate(value,heading,sizeof(heading));
    ImGui::TextUnformatted("Select date"); ImGui::PushFont(detail::Font(),S(28)); ImGui::TextUnformatted(heading); ImGui::PopFont();
    const bool hit=Calendar("calendar",value,nullptr,&picked,enabled); ImGui::PopFont(); ImGui::PopID();
    if(hit&&CompareDates(picked,*date)!=0) { *date=picked; return true; } return false;
}
bool DateRangePicker(const char* id,DateRange* range,bool enabled) {
    if(!range||ImGui::GetCurrentWindow()->SkipItems) return false;
    ImGui::PushID(id); ImGui::PushFont(detail::Font(),S(14));
    DateRange value{ClampDate(range->start),ClampDate(range->end)};
    if(CompareDates(value.start,value.end)>0) std::swap(value.start,value.end);
    auto* storage=ImGui::GetStateStorage(); const ImGuiID pendingId=ImGui::GetID("pending-end"); bool pending=storage->GetBool(pendingId,false);
    char first[24],last[24]; FormatDate(value.start,first,sizeof(first)); FormatDate(value.end,last,sizeof(last));
    ImGui::TextUnformatted(pending?"Select end date":"Select date range"); ImGui::Text("%s  -  %s",first,last);
    Date picked=value.start; bool changed=false;
    if(Calendar("calendar",value.start,&value,&picked,enabled)) {
        if(!pending) { value.start=value.end=picked; pending=true; }
        else { value.end=picked; if(CompareDates(value.start,value.end)>0) std::swap(value.start,value.end); pending=false; }
        changed=CompareDates(value.start,range->start)!=0||CompareDates(value.end,range->end)!=0; *range=value;
    }
    storage->SetBool(pendingId,pending); ImGui::PopFont(); ImGui::PopID(); return changed;
}
bool TimePicker(const char* id,Time* time,bool use24Hour,bool enabled) {
    if(!time||ImGui::GetCurrentWindow()->SkipItems) return false;
    ImGui::PushID(id); ImGui::PushFont(detail::Font(),S(14)); ImGui::PushStyleVar(ImGuiStyleVar_DisabledAlpha, .38f);
    ImGui::BeginDisabled(!enabled);
    const auto& t=GetTheme(); auto* storage=ImGui::GetStateStorage(); const ImGuiID modeId=ImGui::GetID("minutes-mode");
    bool minutes=storage->GetBool(modeId,false); Time value{std::clamp(time->hour,0,23),std::clamp(time->minute,0,59)}; bool interacted=false;
    ImGui::TextUnformatted("Select time"); int displayHour=use24Hour?value.hour:((value.hour+11)%12+1); const bool pm=value.hour>=12;
    ImGui::PushStyleVar(ImGuiStyleVar_FrameRounding,S(8)); ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,ImVec2(S(12),S(12)));
    ImGui::PushStyleColor(ImGuiCol_FrameBg,minutes?t.surfaceContainerHigh:t.primaryContainer); ImGui::PushStyleColor(ImGuiCol_Text,minutes?t.onSurface:t.onPrimaryContainer);
    ImGui::PushFont(detail::Font(),S(28)); ImGui::SetNextItemWidth(S(96));
    if(ImGui::InputInt("##hour",&displayHour,0,0)) {
        displayHour=std::clamp(displayHour,use24Hour?0:1,use24Hour?23:12); value.hour=use24Hour?displayHour:displayHour%12+(pm?12:0); interacted=true;
    }
    if(ImGui::IsItemActivated()) minutes=false;
    ImGui::PopStyleColor(2);
    ImGui::SameLine(); ImGui::TextUnformatted(":"); ImGui::SameLine();
    ImGui::PushStyleColor(ImGuiCol_FrameBg,minutes?t.primaryContainer:t.surfaceContainerHigh); ImGui::PushStyleColor(ImGuiCol_Text,minutes?t.onPrimaryContainer:t.onSurface);
    ImGui::SetNextItemWidth(S(96));
    if(ImGui::InputInt("##minute",&value.minute,0,0)) { value.minute=std::clamp(value.minute,0,59); interacted=true; }
    if(ImGui::IsItemActivated()) minutes=true;
    ImGui::PopFont(); ImGui::PopStyleColor(2); ImGui::PopStyleVar(2);
    if(!use24Hour) {
        if(Action("AM",ImVec2(96,40),value.hour<12)) { value.hour%=12; interacted=true; } ImGui::SameLine();
        if(Action("PM",ImVec2(96,40),value.hour>=12)) { value.hour=value.hour%12+12; interacted=true; }
    }
    const ImVec2 origin=ImGui::GetCursorScreenPos(); const float face=S(256),radius=S(108); const ImVec2 center(origin.x+face/2,origin.y+face/2);
    auto* draw=ImGui::GetWindowDrawList(); draw->AddCircleFilled(center,face/2,C(t.surfaceContainerHigh),64);
    const bool drawMinutes=minutes;
    const int count=drawMinutes?12:(use24Hour?24:12),active=minutes?value.minute:(use24Hour?value.hour:value.hour%12);
    const float angle=static_cast<float>(active*(minutes?6:30)-90)*3.14159265f/180;
    const float handRadius=!minutes&&use24Hour&&(value.hour==0||value.hour>12)?S(72):radius;
    const ImVec2 hand(center.x+std::cos(angle)*handRadius,center.y+std::sin(angle)*handRadius);
    draw->AddLine(center,hand,C(t.primary),S(2)); draw->AddCircleFilled(center,S(4),C(t.primary));
    for(int i=0;i<count;++i) {
        const int number=drawMinutes?i*5:(use24Hour?i:((i+11)%12+1)),clockIndex=drawMinutes?i:use24Hour?i%12:number%12;
        const float a=(clockIndex*30.0f-90)*3.14159265f/180,r=!drawMinutes&&use24Hour&&(number==0||number>12)?S(72):radius;
        ImGui::SetCursorScreenPos(ImVec2(center.x+std::cos(a)*r-S(18),center.y+std::sin(a)*r-S(18)));
        char text[24]; std::snprintf(text,sizeof(text),"%02d##clock%d",number,i);
        const bool selected=drawMinutes?value.minute==number:use24Hour?value.hour==number:value.hour%12==number%12;
        if(Action(text,ImVec2(36,36),selected)) {
            if(drawMinutes) value.minute=number;
            else { value.hour=use24Hour?number:number%12+(value.hour>=12?12:0); minutes=true; }
            interacted=true;
        }
    }
    ImGui::SetCursorScreenPos(origin); ImGui::Dummy(ImVec2(face,face)); storage->SetBool(modeId,minutes); ImGui::EndDisabled(); ImGui::PopStyleVar(); ImGui::PopFont(); ImGui::PopID();
    if(interacted&&(time->hour!=value.hour||time->minute!=value.minute)) { *time=value; return true; } return false;
}
}
