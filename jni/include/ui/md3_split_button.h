#pragma once
#include "ui/md3.h"

namespace aimgui::md3 {

enum class SplitButtonAction { None, Primary, ToggleMenu };

// Two independent targets in one 40dp split button. The trailing target toggles
// *expanded and returns ToggleMenu. Primary never changes expanded. The caller
// opens/draws/closes its menu, and synchronizes expanded when the menu closes.
// width is the total width in dp; zero sizes to the label and available region.
// Filled/Tonal/Outlined/Text/Elevated match the normal ButtonStyle palette.
// Left/Right moves keyboard focus between the two targets. Enter/Space activates.
// Supports BeginDisabled plus the explicit enabled flag. The ID is separate
// from the visible label. Invalid pointers/non-finite widths produce no item.
SplitButtonAction SplitButton(const char* id, const char* label, bool* expanded,
                              ButtonStyle style = ButtonStyle::Filled,
                              const char* icon = nullptr, float width = 0.f,
                              bool enabled = true);

} // namespace aimgui::md3
