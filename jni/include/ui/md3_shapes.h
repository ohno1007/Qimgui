#pragma once

#include "imgui.h"

namespace aimgui::md3 {

// AndroidX MaterialShapes preset names, in the order of the upstream catalog.
enum class MaterialShape {
    Circle, Square, Slanted, Arch, Fan, Arrow, SemiCircle, Oval, Pill,
    Triangle, Diamond, ClamShell, Pentagon, Gem, Sunny, VerySunny,
    Cookie4Sided, Cookie6Sided, Cookie7Sided, Cookie9Sided, Cookie12Sided,
    Ghostish, Clover4Leaf, Clover8Leaf, Burst, SoftBurst, Boom, SoftBoom,
    Flower, Puffy, PuffyDiamond, PixelCircle, PixelTriangle, Bun, Heart,
    Count
};
inline constexpr int MaterialShapeCount = static_cast<int>(MaterialShape::Count);

struct ShapeCubic { ImVec2 start, control1, control2, end; };
struct ShapeGeometry {
    const char* name;
    const ShapeCubic* cubics;
    int cubic_count;
    ImVec2 center;
};

// Immutable normalized curves exported from AndroidX RoundedPolygon. These
// remain valid for the lifetime of the process, without an ImGui context.
// Invalid enum values return an empty geometry and an empty name.
const ShapeGeometry& GetShapeGeometry(MaterialShape shape);
const char* ShapeName(MaterialShape shape);

enum class ShapeFit { Contain, Stretch };

// Physical pixels and explicit colors; zero alpha disables fill/stroke.
// Rotation is clockwise in degrees around the upstream polygon centroid
// (ordinary rotation, not Compose startAngle anchor alignment);
// afterwards the actual curve bounds are centered in the target box.
// Contain: uniform min(width,height) scaling of the normalized coordinates,
// reduced only when the rotated geometry would exceed the box. This preserves
// the source's normalization and aspect ratio instead of expanding tight bounds.
// Stretch: independent width/height scaling. This uses actual curve bounds to
// center, unlike Compose toShape's control-point bounds. Rotated geometry may
// extend beyond the box; use a draw-list clip rect if needed.
// A visible stroke subtracts half its width from each side before scaling.
// Anti-aliasing may extend by one pixel. The caller's unfinished draw-list path
// is preserved. No clipping or input handling is installed. Invalid shapes,
// non-finite parameters, non-positive sizes and fully clipped bounds draw
// nothing. Adaptive curve tessellation has a bounded vertex budget.
void DrawShape(ImDrawList* draw, MaterialShape shape, ImVec2 originPx,
               ImVec2 sizePx, ImU32 fill, float rotationDegrees = 0.f,
               ImU32 stroke = 0, float strokeWidthPx = 1.f,
               ShapeFit fit = ShapeFit::Contain);

// Non-interactive ImGui layout item using the current primary color and scale.
// Reserves sizeDp. Outlined shapes use a 2dp stroke; filled shapes have no stroke.
// Use DrawShape for custom colors, combined fill/stroke or embedding in controls.
void Shape(const char* id, MaterialShape shape, ImVec2 sizeDp = ImVec2(96.f, 96.f),
           float rotationDegrees = 0.f, bool outlined = false,
           ShapeFit fit = ShapeFit::Contain);

} // namespace aimgui::md3
