#pragma once
namespace aimgui::md3 {
inline constexpr int ShowcasePageCount = 8;
// -1 shows page selector; 0..7 render a category (6 = shapes, 7 = expressive).
// Caller controls SetTheme/PushTheme/PopTheme.
void Showcase(int page = -1);
void DrawExpressiveGallery();
}
