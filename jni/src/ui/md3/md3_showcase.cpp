#include "ui/md3_showcase.h"
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_extra.h"
#include "ui/md3_shapes.h"
#include "md3_internal.h"
#include <algorithm>
#include <cstdint>

namespace aimgui::md3 {
namespace {
using namespace detail;
struct GalleryState {
    ImGuiContext* owner = nullptr;
    GalleryState* next = nullptr;
    int page = 0, clicks = 0, radio = 0, selectedFruit = 0, tab = 0;
    int secondaryTab = 0, segment = 0, navigation = 0, rail = 0, drawer = 0, carousel = 0;
    std::uint64_t multi = 3;
    bool check = true, mixed = true, toggle = true, iconSelected[4] = {false,true,true,false};
    bool chipFilter = true, chipInput = false, chipRemoved = false;
    bool dialog = false, fullscreen = false, bottomSheet = false, sideSheet = false;
    bool snackbar = true, use24Hour = true, menuChecked = true;
    float slider = 40, discrete = 40, lower = 20, upper = 70, snackSeconds = 0;
    char filled[128] = "Input text", outlined[128] = "Input text";
    char error[128] = "Input text", password[128] = "password";
    char disabled[128] = "Input text", readonly[128] = "Input text", search[128] = "";
    Date date{2026,11,11};
    DateRange range{{2026,11,11},{2026,11,18}};
    Time time{13,30};
    int shape = static_cast<int>(MaterialShape::Heart);
    float shapeSize = 112.f, shapeRotation = 0.f;
    bool shapeOutlined = false;
};
GalleryState* galleryStates = nullptr;
void ReleaseGallery(ImGuiContext* context, ImGuiContextHook*) {
    GalleryState** it = &galleryStates;
    while (*it) {
        if ((*it)->owner == context) {
            GalleryState* dead = *it; *it = dead->next; IM_DELETE(dead); return;
        }
        it = &(*it)->next;
    }
}
GalleryState& State() {
    for (GalleryState* value = galleryStates; value; value = value->next)
        if (value->owner == GImGui) return *value;
    GalleryState* value = IM_NEW(GalleryState)();
    value->owner = GImGui; value->next = galleryStates; galleryStates = value;
    ImGuiContextHook hook; hook.Type = ImGuiContextHookType_Shutdown;
    hook.Callback = ReleaseGallery; ImGui::AddContextHook(GImGui, &hook);
    return *value;
}
void Heading(const char* title, const char* caption) {
    Text(ImGui::GetCursorScreenPos(), title, GetTheme().onSurface, 28);
    ImGui::Dummy(ImVec2(0, Dp(36)));
    Text(ImGui::GetCursorScreenPos(), caption, GetTheme().onSurfaceVariant, 14);
    ImGui::Dummy(ImVec2(0, Dp(24)));
}
void Section(const char* title) {
    ImGui::Dummy(ImVec2(0, Dp(4)));
    Text(ImGui::GetCursorScreenPos(), title, GetTheme().onSurface, 16, true);
    ImGui::Dummy(ImVec2(0, Dp(24)));
}
void Note(const char* text) {
    Text(ImGui::GetCursorScreenPos(), text, GetTheme().onSurfaceVariant, 14);
    ImGui::Dummy(ImVec2(0,Dp(20)));
}
void NextColumn() { ImGui::SameLine(0,Dp(24)); }
void Actions(GalleryState& s) {
    Heading("Actions", "Buttons, icon buttons, floating action buttons and chips");
    Section("Buttons");
    constexpr ButtonStyle styles[] = {ButtonStyle::Filled, ButtonStyle::Tonal,
        ButtonStyle::Outlined, ButtonStyle::Text, ButtonStyle::Elevated};
    const char* ids[] = {"Button##filled", "Button##tonal", "Button##outlined", "Button##text", "Button##elevated"};
    for (int row = 0; row < 2; ++row) {
        ImGui::PushID(row); ImGui::BeginDisabled(row == 1);
        for (int i = 0; i < 5; ++i) {
            if (i) ImGui::SameLine(0,Dp(16));
            if (Button(ids[i], styles[i], ImVec2(112,40))) ++s.clicks;
        }
        ImGui::EndDisabled(); ImGui::PopID();
    }
    const char* names[] = {"Filled","Tonal","Outlined","Text","Elevated"};
    for(int i=0;i<5;++i) {
        if(i) ImGui::SameLine(0,Dp(16));
        const ImVec2 pos=ImGui::GetCursorScreenPos();
        ImGui::Dummy(ImVec2(Dp(112),Dp(20)));
        CenterText(ImRect(pos,ImGui::GetItemRectMax()),names[i],GetTheme().onSurfaceVariant,12);
    }
    Section("Icon buttons");
    constexpr IconButtonStyle iconStyles[] = {IconButtonStyle::Standard, IconButtonStyle::Filled,
        IconButtonStyle::Tonal, IconButtonStyle::Outlined};
    for (int i = 0; i < 4; ++i) {
        ImGui::PushID(i); if (i) ImGui::SameLine(0,Dp(24));
        IconButton("icon", "+", iconStyles[i], &s.iconSelected[i]); ImGui::PopID();
    }
    ImGui::SameLine(0,Dp(32)); ImGui::BeginDisabled();
    IconButton("disabled-icon", "+", IconButtonStyle::Filled); ImGui::EndDisabled();
    Section("Floating action buttons");
    Fab("small-fab", "+", FabSize::Small); ImGui::SameLine(0,Dp(24));
    Fab("medium-fab", "+", FabSize::Medium); ImGui::SameLine(0,Dp(24));
    Fab("large-fab", "+", FabSize::Large); ImGui::SameLine(0,Dp(24));
    if (Fab("extended-fab", "+", FabSize::Medium, "Create")) ++s.clicks;
    Section("Chips");
    Chip("Assist",ChipStyle::Assist); ImGui::SameLine();
    Chip("Filter",ChipStyle::Filter,&s.chipFilter); ImGui::SameLine();
    if (!s.chipRemoved) {
        Chip("Input",ChipStyle::Input,&s.chipInput,&s.chipRemoved); ImGui::SameLine();
    }
    Chip("Suggestion",ChipStyle::Suggestion);
    ImGui::BeginDisabled();
    Chip("Assist##disabled",ChipStyle::Assist); ImGui::SameLine();
    Chip("Filter##disabled",ChipStyle::Filter,&s.chipFilter); ImGui::SameLine();
    Chip("Input##disabled",ChipStyle::Input,&s.chipInput,&s.chipRemoved); ImGui::SameLine();
    Chip("Suggestion##disabled",ChipStyle::Suggestion);
    ImGui::EndDisabled();
    if (s.chipRemoved && Button("Restore input chip",ButtonStyle::Text)) s.chipRemoved=false;
}
void Inputs(GalleryState& s) {
    Heading("Inputs", "Selection controls, sliders, text fields and search");
    Section("Selection controls");
    Checkbox("Checkbox", &s.check); ImGui::SameLine(0,Dp(20));
    Checkbox("Mixed", &s.mixed, true); ImGui::SameLine(0,Dp(20));
    RadioButton("Radio", &s.radio, 0); ImGui::SameLine(0,Dp(20));
    Switch("Switch", &s.toggle);
    ImGui::PushID("disabled-selection"); ImGui::BeginDisabled();
    Checkbox("Checkbox", &s.check); ImGui::SameLine(0,Dp(20));
    RadioButton("Radio", &s.radio, 1); ImGui::SameLine(0,Dp(20));
    Switch("Switch", &s.toggle);
    ImGui::EndDisabled(); ImGui::PopID();
    Section("Sliders");
    ImGui::BeginGroup(); Slider("Continuous",&s.slider,0,100,280); ImGui::EndGroup();
    NextColumn(); ImGui::BeginGroup(); Slider("Discrete",&s.discrete,0,100,280,10); ImGui::EndGroup();
    RangeSlider("Range",&s.lower,&s.upper,0,100,584);
    Section("Text fields");
    ImGui::BeginGroup(); TextField("Label##filled",s.filled,sizeof(s.filled),TextFieldStyle::Filled,280,"Supporting text"); ImGui::EndGroup();
    NextColumn(); ImGui::BeginGroup(); TextField("Label##outlined",s.outlined,sizeof(s.outlined),TextFieldStyle::Outlined,280,"Supporting text"); ImGui::EndGroup();
    ImGui::BeginGroup(); TextField("Error",s.error,sizeof(s.error),TextFieldStyle::Outlined,280,"Error message",true); ImGui::EndGroup();
    NextColumn(); ImGui::BeginGroup(); TextField("Password",s.password,sizeof(s.password),TextFieldStyle::Filled,280,"Supporting text",false,ImGuiInputTextFlags_Password); ImGui::EndGroup();
    ImGui::BeginGroup(); ImGui::BeginDisabled(); TextField("Disabled",s.disabled,sizeof(s.disabled),TextFieldStyle::Filled,280,"Supporting text"); ImGui::EndDisabled(); ImGui::EndGroup();
    NextColumn(); ImGui::BeginGroup(); TextField("Read only",s.readonly,sizeof(s.readonly),TextFieldStyle::Outlined,280,"Supporting text",false,ImGuiInputTextFlags_ReadOnly); ImGui::EndGroup();
    Section("Select and search");
    const char* options[] = {"Apple","Banana","Cherry"};
    ImGui::BeginGroup(); Select("Fruit",&s.selectedFruit,options,3,280,true,true,"Choose a fruit"); ImGui::EndGroup();
    NextColumn(); ImGui::BeginGroup(); SearchBar("Search",s.search,sizeof(s.search),options,3,280); ImGui::EndGroup();
}
void Navigations(GalleryState& s) {
    Heading("Navigation", "App bars, tabs, segmented buttons and destinations");
    const char* actions[] = {"+","..."};
    TopAppBar("app-bar","Qimgui",AppBarStyle::Small,"=",actions,2,680);
    Section("Primary and secondary tabs");
    const char* tabs[] = {"Overview","Activity","Settings"};
    Tabs("primary",&s.tab,tabs,3,680,true);
    Tabs("secondary",&s.secondaryTab,tabs,3,680,false);
    Section("Segmented buttons");
    const char* segments[] = {"Day","Week","Month"};
    SegmentedButtons("single",&s.segment,segments,3,320); ImGui::SameLine(0,Dp(24));
    MultiSegmentedButtons("multi",&s.multi,segments,3,320);
    Section("Navigation bar");
    const NavigationItem destinations[] = {{"Home","H",0,true},{"Activity","A",3,true},{"Settings","S",0,true}};
    Navigation("bar",&s.navigation,destinations,3,NavigationStyle::Bar,680);
    Section("Navigation rail and drawer");
    ImGui::BeginGroup(); Navigation("rail",&s.rail,destinations,3,NavigationStyle::Rail); ImGui::EndGroup();
    ImGui::SameLine(0,Dp(40)); ImGui::BeginGroup(); Navigation("drawer",&s.drawer,destinations,3,NavigationStyle::Drawer,360); ImGui::EndGroup();
}
void Surfaces(GalleryState& s) {
    Heading("Surfaces and feedback", "Cards, lists, progress, dialogs, sheets and notifications");
    Section("Cards");
    Card("filled","Filled card","Supporting text",ImVec2(208,112),CardStyle::Filled); ImGui::SameLine(0,Dp(16));
    Card("outlined","Outlined card","Supporting text",ImVec2(208,112),CardStyle::Outlined); ImGui::SameLine(0,Dp(16));
    Card("elevated","Elevated card","Supporting text",ImVec2(208,112),CardStyle::Elevated);
    Section("Lists and badges");
    ListItem("Headline##one",nullptr,"100+","A",false,true,1,656);
    ListItem("Headline##two","Supporting text","24","B",true,true,2,656);
    ListItem("Headline##three","Supporting text\nSecond supporting line","56","C",false,true,3,656);
    Divider(656);
    Section("Progress indicators");
    LinearProgress("determinate",.4f,320); ImGui::SameLine(0,Dp(24)); LinearProgress("indeterminate",-1,312);
    CircularProgress("circle",.75f); ImGui::SameLine(0,Dp(24)); CircularProgress("spinner",-1);
    ImGui::SameLine(0,Dp(32)); IconButton("badge-button","+"); Badge(3);
    Section("Dialogs and sheets");
    if(Button("Dialog",ButtonStyle::Outlined)) s.dialog=true; ImGui::SameLine();
    if(Button("Full screen",ButtonStyle::Outlined)) s.fullscreen=true; ImGui::SameLine();
    if(Button("Bottom sheet",ButtonStyle::Outlined)) s.bottomSheet=true; ImGui::SameLine();
    if(Button("Side sheet",ButtonStyle::Outlined)) s.sideSheet=true;
    if(Button("Show snackbar",ButtonStyle::Text)) {s.snackbar=true;s.snackSeconds=6;}
    Snackbar("snackbar","Changes saved",&s.snackbar,&s.snackSeconds,"Undo",656,true);
    if(BeginDialog("basic-dialog",&s.dialog,"Save changes?")) {
        ImGui::TextWrapped("Your changes will be saved to this project.");
        if(Button("Cancel",ButtonStyle::Text)) s.dialog=false; ImGui::SameLine();
        if(Button("Save")) {s.dialog=false;s.snackbar=true;s.snackSeconds=6;}
        EndDialog();
    }
    if(BeginDialog("fullscreen-dialog",&s.fullscreen,"Edit project",true)) {
        ImGui::TextWrapped("Full-screen dialogs keep long editing tasks in one place.");
        TextField("Project name",s.filled,sizeof(s.filled));
        if(Button("Done")) s.fullscreen=false;
        EndDialog();
    }
    if(BeginSheet("bottom-sheet",&s.bottomSheet,SheetSide::Bottom,320)) {
        ImGui::TextUnformatted("Bottom sheet");
        ListItem("Share","Choose how to share this item",nullptr,"+");
        ListItem("Download","Save a local copy",nullptr,"+");
        if(Button("Close",ButtonStyle::Text)) s.bottomSheet=false;
        EndSheet();
    }
    if(BeginSheet("side-sheet",&s.sideSheet,SheetSide::Right,360)) {
        ImGui::TextUnformatted("Details");
        TextField("Name",s.outlined,sizeof(s.outlined));
        if(Button("Done")) s.sideSheet=false;
        EndSheet();
    }
}
void Pickers(GalleryState& s) {
    Heading("Date and time", "Gregorian date, date range and clock selection");
    if(BeginCard("date-card",ImVec2(328,448),CardStyle::Filled)) DatePicker("date",&s.date);
    EndCard(); ImGui::SameLine(0,Dp(24));
    if(BeginCard("range-card",ImVec2(328,448),CardStyle::Filled)) DateRangePicker("range",&s.range);
    EndCard();
    Section("Time picker");
    ImGui::BeginGroup(); TimePicker("time",&s.time,s.use24Hour); ImGui::EndGroup();
    ImGui::SameLine(0,Dp(48)); ImGui::BeginGroup();
    Switch("24-hour clock",&s.use24Hour);
    Note("Choose hours, then minutes.");
    Note("The fields also accept keyboard input.");
    ImGui::EndGroup();
}
void Additional(GalleryState& s) {
    Heading("Media and contextual actions", "Carousel, bottom app bar, tooltips and nested menus");
    Section("Carousel");
    const CarouselItem cards[] = {{"Mountains","Explore nature"},{"City","Find a new perspective"},
        {"Coast","Take a break"},{"Forest","Follow the trail"}};
    Carousel("carousel",&s.carousel,cards,4,680,200,224);
    Section("Bottom app bar");
    const char* actions[] = {"+","*","..."};
    BottomAppBar("bottom-appbar",actions,3,"+",680);
    Section("Tooltips");
    Button("Plain tooltip",ButtonStyle::Outlined); Tooltip("A short explanation");
    ImGui::SameLine(0,Dp(16)); Button("Rich tooltip",ButtonStyle::Outlined);
    RichTooltip("rich","Helpful details","Rich tooltips can provide a title, supporting text and an action.","Learn more");
    Section("Menus");
    if(Button("Open menu",ButtonStyle::Outlined)) OpenMenu("menu");
    if(BeginMenuPopup("menu")) {
        MenuItem("New item","Ctrl+N");
        if(MenuItem("Show details",nullptr,s.menuChecked)) s.menuChecked=!s.menuChecked;
        MenuDivider();
        if(BeginSubMenu("More actions")) {
            MenuItem("Duplicate","Ctrl+D");
            if(BeginSubMenu("Export")) {
                MenuItem("PNG image"); MenuItem("PDF document"); EndSubMenu();
            }
            MenuItem("Unavailable",nullptr,false,false);
            EndSubMenu();
        }
        MenuItem("Delete"); EndMenuPopup();
    }
    Note("Escape closes the deepest menu first.");
    Note("Tab and arrow keys move focus; Enter activates.");
}
}
namespace {
void Shapes(GalleryState& s) {
    const auto& theme = GetTheme();
    const float available = ImGui::GetContentRegionAvail().x / theme.scale;
    Section("Material Shapes / 35");
    const char* names[MaterialShapeCount];
    for (int i = 0; i < MaterialShapeCount; ++i)
        names[i] = ShapeName(static_cast<MaterialShape>(i));
    s.shape = std::clamp(s.shape, 0, MaterialShapeCount - 1);
    // A fixed preview box keeps controls still while the shape changes size.
    const bool sideBySide = available >= 280.f;
    const float preview = std::min(144.f, available);
    const ImVec2 previewPos = ImGui::GetCursorScreenPos();
    ImGui::BeginGroup();
    ImGui::Dummy(ImVec2(Dp(preview), Dp(preview)));
    auto* draw = ImGui::GetWindowDrawList();
    draw->AddRectFilled(previewPos, Add(previewPos, ImVec2(Dp(preview), Dp(preview))),
                        Color(theme.surfaceContainerHigh), Dp(24));
    const float side = std::min(s.shapeSize, preview - 16.f);
    const ImVec2 origin = Add(previewPos, ImVec2(Dp((preview-side)*.5f), Dp((preview-side)*.5f)));
    DrawShape(draw, static_cast<MaterialShape>(s.shape), origin, ImVec2(Dp(side), Dp(side)),
              s.shapeOutlined ? 0 : Color(theme.primary), s.shapeRotation,
              s.shapeOutlined ? Color(theme.primary) : 0, Dp(2));
    ImGui::EndGroup();
    if (sideBySide) ImGui::SameLine(0, Dp(20));
    ImGui::BeginGroup();
    const float controlsWidth = std::max(80.f, std::min(280.f, available - (sideBySide ? preview + 20.f : 0.f)));
    Select("Shape##material-shape", &s.shape, names, MaterialShapeCount, controlsWidth);
    Switch("Outline##shape-outline", &s.shapeOutlined);
    if (Button("Reset##shape-reset", ButtonStyle::Text)) {
        s.shapeSize = 112.f; s.shapeRotation = 0.f; s.shapeOutlined = false;
    }
    ImGui::EndGroup();
    Slider("Size##shape-size", &s.shapeSize, 48.f, 128.f, std::min(available, 360.f), 0, "%.0f dp");
    Slider("Rotation##shape-rotation", &s.shapeRotation, -180.f, 180.f, std::min(available, 360.f), 0, "%.0f deg");

    Section("All shapes");
    Note("Select a shape to preview it above.");
    constexpr float gap = 12.f;
    const int columns = std::max(1, static_cast<int>((available + gap) / (128.f + gap)));
    const float tileWidth = std::max(1.f, (available - gap * (columns - 1)) / columns);
    for (int i = 0; i < MaterialShapeCount; ++i) {
        if (i % columns) ImGui::SameLine(0, Dp(gap));
        ImGui::PushID(i);
        const auto tile = Item("shape-tile", ImVec2(Dp(tileWidth), Dp(132)));
        if (tile.pressed) s.shape = i;
        if (tile.visible) {
            const bool selected = s.shape == i;
            draw->AddRectFilled(tile.rect.Min, tile.rect.Max,
                               Color(selected ? theme.secondaryContainer : theme.surfaceContainer), Dp(16));
            StateLayer(tile, selected ? theme.onSecondaryContainer : theme.onSurface, Dp(16));
            if (selected) draw->AddRect(tile.rect.Min, tile.rect.Max, Color(theme.primary), Dp(16), Dp(2));
            const float edge = std::min(76.f, tileWidth - 16.f);
            const ImVec2 tileOrigin = Add(tile.rect.Min, ImVec2(Dp((tileWidth-edge)*.5f), Dp(14)));
            DrawShape(draw, static_cast<MaterialShape>(i), tileOrigin, ImVec2(Dp(edge), Dp(edge)),
                      Color(selected ? theme.onSecondaryContainer : theme.primary));
            CenterText(ImRect(Add(tile.rect.Min, ImVec2(0, Dp(96))), tile.rect.Max), names[i],
                       selected ? theme.onSecondaryContainer : theme.onSurface, 12, true);
        }
        ImGui::PopID();
    }
}
}
void Showcase(int page) {
    GalleryState& state=State();
    ImGui::PushID("md3-showcase");
    if(page<0) {
        bool dark=GetTheme().dark;
        if(Switch("Dark theme",&dark)) SetTheme(dark,GetTheme().scale);
        const char* pages[] = {"Actions","Inputs","Navigation","Surfaces","Pickers","More","Shapes","Expressive"};
        Tabs("categories",&state.page,pages,ShowcasePageCount,0,false);
        ImGui::Dummy(ImVec2(0,Dp(8)));
        page=state.page;
    }
    switch(std::clamp(page,0,ShowcasePageCount-1)) {
        case 0: Actions(state); break;
        case 1: Inputs(state); break;
        case 2: Navigations(state); break;
        case 3: Surfaces(state); break;
        case 4: Pickers(state); break;
        case 5: Additional(state); break;
        case 6: Shapes(state); break;
        case 7: DrawExpressiveGallery(); break;
    }
    ImGui::PopID();
}
}
