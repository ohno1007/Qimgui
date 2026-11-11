#pragma once
#include "imgui.h"
#include <cstdint>

namespace aimgui::md3 {

enum class CardStyle { Elevated, Filled, Outlined };
enum class NavigationStyle { Bar, Rail, Drawer };
enum class AppBarStyle { Small, CenterAligned, Medium, Large };
enum class SheetSide { Bottom, Left, Right };

struct NavigationItem {
    const char* label;
    const char* icon = nullptr;
    int badge = 0; // 0 = none; negative = a dot; positive = count.
    bool enabled = true;
};

// All dimensions use logical dp; zero width fills the current content region.
// As with ImGui::BeginChild, always EndCard after BeginCard, even if it is false.
bool BeginCard(const char* id, ImVec2 size, CardStyle style = CardStyle::Filled);
void EndCard();
// The clickable alternative to a content card; returns true on activation.
bool Card(const char* id, const char* headline, const char* supporting = nullptr,
          ImVec2 size = ImVec2(0, 120), CardStyle style = CardStyle::Filled,
          bool enabled = true);
void Divider(float width = 0, float inset = 0);
bool ListItem(const char* headline, const char* supporting = nullptr,
              const char* trailing = nullptr, const char* leading = nullptr,
              bool selected = false, bool enabled = true, int lines = 1,
              float width = 0);
// Draw on the preceding item's top-right corner without replacing its item ID.
void Badge(int count = -1);

// Left/Right select the next enabled tab (wrapping); Home/End select the ends.
bool Tabs(const char* id, int* selected, const char* const* labels, int count,
          float width = 0, bool primary = true, const bool* enabled = nullptr);
bool SegmentedButtons(const char* id, int* selected, const char* const* labels,
                      int count, float width = 0, const bool* enabled = nullptr);
// Each bit selects one segment; 1..64 segments. allowEmpty controls deselection.
bool MultiSegmentedButtons(const char* id, std::uint64_t* selected,
                           const char* const* labels, int count, float width = 0,
                           bool allowEmpty = true, const bool* enabled = nullptr);
bool Navigation(const char* id, int* selected, const NavigationItem* items,
                int count, NavigationStyle style = NavigationStyle::Bar,
                float width = 0);
// -1 = no action, -2 = navigation button, otherwise the trailing action index.
int TopAppBar(const char* id, const char* title,
              AppBarStyle style = AppBarStyle::Small,
              const char* navigationIcon = nullptr,
              const char* const* actions = nullptr, int actionCount = 0,
              float width = 0);

// A negative value selects the indeterminate animation. Other values clamp 0..1.
void LinearProgress(const char* id, float value, float width = 0);
void CircularProgress(const char* id, float value, float diameter = 48);
void Tooltip(const char* text); // Tooltip for the preceding item, with delay.
// Caller owns open/time state. Set remainingSeconds on each new notification.
// Zero or negative time means persistent; hover/keyboard focus pause countdown.
// Action activation returns true. Narrow surfaces clip action labels.
bool Snackbar(const char* id, const char* message, bool* open,
              float* remainingSeconds = nullptr, const char* action = nullptr,
              float width = 0, bool dismissible = true);

// open is a controlled state: setting true opens it, X/Escape sets it false.
// EndDialog/EndSheet ONLY after a true Begin. Escape closes one popup level.
// A basic dialog is centered; fullscreen uses the current viewport work area.
bool BeginDialog(const char* id, bool* open, const char* title,
                 bool fullscreen = false, float width = 360,
                 bool dismissible = true);
void EndDialog();
bool BeginSheet(const char* id, bool* open, SheetSide side = SheetSide::Bottom,
                float extent = 320, bool dismissible = true);
void EndSheet();

void DrawLayoutGallery();
}
