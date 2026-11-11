#pragma once
namespace aimgui::md3 {
inline constexpr int ShowcasePageCount = 6;
// -1 shows page selector; 0..5 render a component category.
// Caller controls SetTheme/PushTheme/PopTheme.
void Showcase(int page = -1);
}
