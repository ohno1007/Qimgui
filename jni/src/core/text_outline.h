#pragma once

struct ImDrawData;

namespace aimgui {

// Puts a dark outline behind every glyph in the frame, in place, between
// ImGui::Render() and the backend's RenderDrawData().
//
// It has to work at this level rather than at the call sites. Text arrives from
// everywhere — Text, TextDisabled, button and selectable labels, SeparatorText,
// combo entries, the value inside a slider grab — and a helper covering the
// ones we happen to remember would leave the rest bare, which is worse than
// none at all. Here every glyph in the frame is caught by construction.
//
// Glyph triangles are told apart from filled shapes by their UV: ImGui gives
// every solid primitive the atlas's single white pixel, so anything sampling
// elsewhere in a font texture is text.
//
// Cost is linear in the number of outline directions; see kDirCount.
void OutlineText(ImDrawData* dd, float radius, unsigned char alpha = 235);

// How far the outline reaches, in px, at the UI's 25px font. Wide enough to
// survive a bright patch of refracted desktop under a glyph, narrow enough that
// counters in dense CJK characters do not fill in.
constexpr float kTextOutlineRadius = 1.5f;

// Releases the rebuilt draw lists. Must run before ImGui::DestroyContext().
//
// ImDrawListSharedData keeps a register of every ImDrawList made against it and
// asserts on destruction that they have all been returned. The pool here is
// allocated once and kept for the life of the process, so without this the last
// thing the app does is abort — which is exactly what pressing 退出 did.
void ShutdownTextOutline();

} // namespace aimgui
