#pragma once

#include "imgui.h"

namespace aimgui::md3::icons {

// Semantic tokens accepted by the MD3 components' icon parameters. These
// native vector symbols follow a 24-unit grid and need no icon font.
inline constexpr const char* Home = "@home";
inline constexpr const char* Widgets = "@widgets";
inline constexpr const char* Settings = "@settings";
inline constexpr const char* Speed = "@speed";
inline constexpr const char* Info = "@info";
inline constexpr const char* Menu = "@menu";
inline constexpr const char* Close = "@close";
inline constexpr const char* Minimize = "@minimize";
inline constexpr const char* Sun = "@sun";
inline constexpr const char* Moon = "@moon";
inline constexpr const char* Expand = "@expand";
inline constexpr const char* FormatBold = "@format_bold";
inline constexpr const char* FormatItalic = "@format_italic";
inline constexpr const char* FormatUnderlined = "@format_underlined";
inline constexpr const char* FormatColorText = "@format_color_text";
inline constexpr const char* FormatColorFill = "@format_color_fill";
inline constexpr const char* PersonAdd = "@person_add";
inline constexpr const char* Share = "@share";
inline constexpr const char* Alarm = "@alarm";
inline constexpr const char* Clock = "@clock";
// Filled hourglass, matching the timer destination in a clock navigation rail.
inline constexpr const char* Timer = "@timer";
inline constexpr const char* Stopwatch = "@stopwatch";
inline constexpr const char* Play = "@play";
inline constexpr const char* Pause = "@pause";
inline constexpr const char* Reset = "@reset";
inline constexpr const char* Plus = "@plus";

// size is the icon box in pixels, independently of the current theme scale.
// Unknown tokens return false without drawing, allowing a text/glyph fallback.
bool DrawSymbol(ImDrawList* draw, ImVec2 center, float size, ImU32 color,
                const char* token);

} // namespace aimgui::md3::icons
