#pragma once
#include "imgui.h"

namespace aimgui::md3 {

enum class ToolbarOrientation { Horizontal, Vertical };
enum class ToolbarStyle { Standard, Vibrant };

struct ToolbarItem {
    const char* id;
    const char* icon = nullptr;
    const char* label = nullptr;
    bool* selected = nullptr; // Non-null makes this item an independent toggle.
    bool enabled = true;
    const char* tooltip = nullptr;
};

// Material floating toolbar: 64dp container, 48dp items, 8dp end padding.
// Returns the activated item index, or -1. State stays with the caller.
// extent is maximum width (horizontal) or height (vertical), in dp; zero uses
// natural size. Horizontal bars also fit the current content width. Constrained
// slots shrink together and text is clipped; use fewer items on narrow layouts.
// Arrows along the bar, Home and End move focus, skipping disabled items.
// Enter/Space activates the focused action. BeginDisabled is respected.
// id/items must be non-null; 1..64 items are accepted.
int Toolbar(const char* id, const ToolbarItem* items, int count,
            ToolbarOrientation orientation = ToolbarOrientation::Horizontal,
            ToolbarStyle style = ToolbarStyle::Vibrant, float extent = 0.f);

} // namespace aimgui::md3
