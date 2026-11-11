#pragma once

#include "imgui.h"
#include <cstdint>

namespace aimgui::md3 {

struct ButtonGroupItem {
    const char* label;
    const char* icon = nullptr;
    bool enabled = true;
};
enum class ButtonGroupStyle { Filled, Tonal, Outlined };
struct ButtonGroupOptions {
    // Logical dp. Zero width fills the available content region.
    float width = 0.f;
    float height = 40.f;
    float spacing = 2.f;
    float expandedRatio = .15f;
    ButtonGroupStyle style = ButtonGroupStyle::Filled;
    bool animated = true;
};

// Connected expressive buttons: full outer corners, small inner corners and
// fully rounded selection. Pressing expands one item and compresses neighbors.
// Caller owns selection; no automatic selection changes on focus/initialization.
// Left/Right wrap past disabled items; Home/End select first/last enabled item.
// Enter/Space activate. Supports 1..64 items. Text is clipped at narrow widths.
// Parent BeginDisabled and each item's enabled flag are both respected.
bool ButtonGroup(const char* id, int* selected, const ButtonGroupItem* items,
                 int count, const ButtonGroupOptions& options = {});

// Each bit selects an item. Arrow/Home/End keys only move focus; Enter/Space
// toggle selection. If allowEmpty is false the last selected bit is retained.
// Bits beyond count are preserved; they do not satisfy the non-empty rule.
bool MultiButtonGroup(const char* id, std::uint64_t* selected,
                      const ButtonGroupItem* items, int count,
                      bool allowEmpty = true, const ButtonGroupOptions& options = {});

} // namespace aimgui::md3
