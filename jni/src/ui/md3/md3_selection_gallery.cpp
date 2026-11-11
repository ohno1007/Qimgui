#include "ui/md3.h"
#include "ui/md3_selection.h"
namespace aimgui::md3 {
void DrawSelectionGallery() {
    static int select=1;
    static const char* options[]={"Apple","Banana","Cherry","Dragon fruit"};
    Select("Fruit",&select,options,4,280,true,true,"Choose a fruit"); ImGui::SameLine();
    Select("Filled select",&select,options,4,280,false);
    static char search[128]="";
    static const char* suggestions[]={"Material Design","Native ImGui","Date picker","Navigation rail"};
    SearchBar("Search components",search,sizeof(search),suggestions,4,400);
    if(Button("Open menu",ButtonStyle::Outlined)) OpenMenu("gallery-menu");
    if(BeginMenuPopup("gallery-menu")) {
        MenuItem("New document","Ctrl+N");
        if(BeginSubMenu("Export")) {
            MenuItem("PNG image"); MenuItem("SVG image");
            if(BeginSubMenu("More formats")) { MenuItem("PDF document"); MenuItem("JPEG image"); EndSubMenu(); }
            EndSubMenu();
        }
        MenuDivider(); MenuItem("Unavailable",nullptr,false,false); EndMenuPopup();
    }
    ImGui::SeparatorText("Date and time");
    static Date date{2026,11,11}; static DateRange range{{2026,11,11},{2026,11,18}}; static Time time{13,45}; static bool hours24=true;
    const float available=ImGui::GetContentRegionAvail().x;
    ImGui::BeginGroup(); DatePicker("date",&date); ImGui::EndGroup();
    if(available>600*GetTheme().scale) ImGui::SameLine(0,24*GetTheme().scale);
    ImGui::BeginGroup(); DateRangePicker("range",&range); ImGui::EndGroup();
    Switch("24-hour time",&hours24); TimePicker("time",&time,hours24);
}
}
