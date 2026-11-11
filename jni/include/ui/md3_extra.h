#pragma once
#include "imgui.h"
namespace aimgui::md3 {
struct CarouselItem {
    const char* title;
    const char* supporting = nullptr;
    ImTextureRef image = ImTextureRef();
};
// Click/Enter selects; swipe scrolls; Left/Right moves selection. Images remain caller-owned.
bool Carousel(const char* id, int* selected, const CarouselItem* items, int count,
              float width = 0, float height = 200, float itemWidth = 240);
// Returns action index, actionCount for FAB, or -1.
int BottomAppBar(const char* id, const char* const* actions, int actionCount,
                 const char* fabIcon = nullptr, float width = 0);
// Rich tooltip for preceding item. Returns true on action.
bool RichTooltip(const char* id, const char* title, const char* supporting,
                 const char* action = nullptr, float width = 280);
}
