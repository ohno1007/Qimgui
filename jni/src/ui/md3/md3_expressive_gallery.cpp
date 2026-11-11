#include "ui/md3_showcase.h"
#include "ui/md3_toolbar.h"
#include "ui/md3_split_button.h"
#include "ui/md3_button_group.h"
#include "ui/md3_progress.h"
#include "ui/md3_motion.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "md3_internal.h"
#include <algorithm>

namespace aimgui::md3 {
namespace {
using namespace detail;
struct GalleryState {
    ImGuiContext* owner = nullptr;
    GalleryState* next = nullptr;
    bool bold = true, italic = false, underline = false;
    bool disabled = false, indeterminate = false, splitOpen[3]{};
    int period = 0, primaryActions = 0, toolbarAction = -1;
    std::uint64_t destinations = 1;
    float progress = 45.f, amplitude = 1.f, wavelength = 40.f, thickness = 4.f;
    const char* menuAction = "None";
};
GalleryState* states = nullptr;
void Release(ImGuiContext* owner, ImGuiContextHook*) {
    for (auto** it = &states; *it; it = &(*it)->next) {
        if ((*it)->owner != owner) continue;
        auto* dead = *it; *it = dead->next; IM_DELETE(dead); return;
    }
}
GalleryState& State() {
    for (auto* s = states; s; s = s->next) if (s->owner == GImGui) return *s;
    auto* s = IM_NEW(GalleryState)();
    s->owner = GImGui; s->next = states; states = s;
    ImGuiContextHook hook; hook.Type = ImGuiContextHookType_Shutdown; hook.Callback = Release;
    ImGui::AddContextHook(GImGui, &hook);
    return *s;
}
void Title(const char* text) {
    ImGui::PushFont(Font(true), Dp(20));
    ImGui::TextWrapped("%s", text);
    ImGui::PopFont();
    ImGui::Dummy(ImVec2(0, Dp(4)));
}
float Width() { return std::max(1.f, ImGui::GetContentRegionAvail().x / GetTheme().scale); }
void Toolbars(GalleryState& s) {
    Title("Toolbar");
    ToolbarItem format[] = {
        {"format-bold", "@format_bold", nullptr, &s.bold, true, "Bold"},
        {"format-italic", "@format_italic", nullptr, &s.italic, true, "Italic"},
        {"format-underline", "@format_underlined", nullptr, &s.underline, true, "Underline"},
        {"format-color", "@format_color_text", nullptr, nullptr, true, "Text color"},
        {"format-fill", "@format_color_fill", nullptr, nullptr, true, "Highlight"},
    };
    const ToolbarItem actions[] = {
        {"tools-menu", "@menu", "Menu"}, {"tools-share", "@share", "Share"},
        {"tools-settings", "@settings", "Settings"},
    };
    ImGui::BeginDisabled(s.disabled);
    const int formatAction = Toolbar("format-tools", format, 5);
    if (formatAction >= 0) s.toolbarAction = formatAction;
    const int action = Toolbar("action-tools", actions, 3, ToolbarOrientation::Horizontal, ToolbarStyle::Standard);
    if (action >= 0) s.toolbarAction = action + 5;
    ImGui::EndDisabled();
    const char* actionNames[] = {"Bold", "Italic", "Underline", "Text color", "Highlight", "Menu", "Share", "Settings"};
    ImGui::TextWrapped("Last action: %s", s.toolbarAction >= 0 ? actionNames[s.toolbarAction] : "None");
}
void Split(GalleryState& s, int index, ButtonStyle style) {
    ImGui::PushID(index);
    s.splitOpen[index] = ImGui::IsPopupOpen("share-options");
    const auto action = SplitButton("share-button", "Share", &s.splitOpen[index], style,
                                    "@person_add", std::min(240.f, Width()), !s.disabled);
    if (action == SplitButtonAction::Primary) ++s.primaryActions;
    if (action == SplitButtonAction::ToggleMenu && s.splitOpen[index]) OpenMenu("share-options");
    if (BeginMenuPopup("share-options")) {
        ImGui::BeginDisabled(s.disabled);
        if (MenuItem("Copy link")) s.menuAction = "Copy link";
        if (MenuItem("Invite people")) s.menuAction = "Invite people";
        MenuDivider();
        if (MenuItem("Export a copy")) s.menuAction = "Export a copy";
        ImGui::EndDisabled();
        EndMenuPopup();
    }
    s.splitOpen[index] = ImGui::IsPopupOpen("share-options");
    ImGui::PopID();
}
void Splits(GalleryState& s) {
    Title("Split button");
    Split(s, 0, ButtonStyle::Filled);
    Split(s, 1, ButtonStyle::Tonal);
    Split(s, 2, ButtonStyle::Outlined);
    ImGui::TextWrapped("Primary actions: %d / Menu: %s", s.primaryActions, s.menuAction);
}
void Groups(GalleryState& s) {
    Title("Connected button group");
    const ButtonGroupItem periods[] = {{"Day"}, {"Week"}, {"Month"}};
    const ButtonGroupItem destinations[] = {{"Work"}, {"Home"}, {"Travel"}};
    ImGui::BeginDisabled(s.disabled);
    ButtonGroup("period-group", &s.period, periods, 3);
    ImGui::TextWrapped("Single selection: %s", periods[s.period].label);
    ButtonGroupOptions tonal; tonal.style = ButtonGroupStyle::Tonal;
    MultiButtonGroup("destination-group", &s.destinations, destinations, 3, false, tonal);
    ImGui::TextWrapped("Multiple selection / keep at least one");
    ImGui::EndDisabled();
    ImGui::BeginDisabled();
    ButtonGroupOptions outlined; outlined.style = ButtonGroupStyle::Outlined;
    ButtonGroup("disabled-group", &s.period, periods, 3, outlined);
    ImGui::EndDisabled();
    ImGui::TextWrapped("Press a button to expand it and compress its neighbors.");
}
void Progress(GalleryState& s) {
    Title("Wavy progress");
    WavyProgressStyle style;
    style.amplitude = s.amplitude; style.wavelength = s.wavelength;
    style.strokeWidth = style.trackWidth = s.thickness;
    const float value = s.indeterminate ? -1.f : s.progress / 100.f;
    ImGui::Dummy(ImVec2(0, Dp(8)));
    WavyLinearProgress("expressive-linear", value, 0, style);
    ImGui::Dummy(ImVec2(0, Dp(12)));
    WavyCircularProgress("expressive-circular", value, 64, style);
    if (Width() >= 148.f) ImGui::SameLine(0, Dp(20));
    WavyCircularProgress("expressive-unknown", -1, 64, style);
    ImGui::BeginDisabled(s.disabled);
    Switch("Indeterminate##expressive-indeterminate", &s.indeterminate);
    ImGui::BeginDisabled(s.indeterminate);
    Slider("Progress##expressive-progress", &s.progress, 0, 100, Width(), 0, "%.0f%%");
    ImGui::EndDisabled();
    ImGui::EndDisabled();
}
} // namespace

void DrawExpressiveGallery() {
    auto& s = State();
    ImGui::PushID("md3-expressive");
    Title("Material 3 Expressive");
    const float available = Width();
    const bool twoColumns = available >= 760.f;
    const float cardWidth = twoColumns ? (available - 16.f) * .5f : available;
    if (BeginCard("toolbar-card", {cardWidth, 244})) Toolbars(s);
    EndCard();
    if (twoColumns) ImGui::SameLine(0, Dp(16));
    if (BeginCard("split-card", {cardWidth, 244})) Splits(s);
    EndCard();
    if (BeginCard("group-card", {cardWidth, 328})) Groups(s);
    EndCard();
    if (twoColumns) ImGui::SameLine(0, Dp(16));
    if (BeginCard("progress-card", {cardWidth, 328})) Progress(s);
    EndCard();
    auto motion = GetMotionSettings();
    if (Switch("Reduce motion##expressive-motion", &motion.reduced)) SetMotionSettings(motion);
    if (available >= 600.f) ImGui::SameLine(0, Dp(24));
    Switch("Disable controls##expressive-disabled", &s.disabled);
    ImGui::BeginDisabled(s.disabled);
    const float controlWidth = std::min(320.f, twoColumns ? (available - 32.f) / 3.f : available);
    ImGui::BeginGroup(); Slider("Wave amplitude##expressive-amplitude", &s.amplitude, 0, 1, controlWidth, 0, "%.2f"); ImGui::EndGroup();
    if (twoColumns) ImGui::SameLine(0, Dp(16));
    ImGui::BeginGroup(); Slider("Wave length##expressive-wavelength", &s.wavelength, 12, 80, controlWidth, 0, "%.0f dp"); ImGui::EndGroup();
    if (twoColumns) ImGui::SameLine(0, Dp(16));
    ImGui::BeginGroup(); Slider("Stroke width##expressive-thickness", &s.thickness, 2, 8, controlWidth, 0, "%.0f dp"); ImGui::EndGroup();
    ImGui::EndDisabled();
    ImGui::PopID();
}
} // namespace aimgui::md3
