#pragma once

struct ImFont;

namespace ImGui {

// Walks /system/fonts -> /system/font -> /data/fonts -> /product/fonts and
// loads the first known CJK font (NotoSansCJK / NotoSerifCJK / MiSans /
// HwChinese / HarmonyOS_Sans / DroidSansFallback ...) as the primary ImGui
// font, with glyph ranges covering Latin + CJK Unified Ideographs +
// Hiragana/Katakana + half-/full-width forms. Returns true on success.
// `merge` folds it into the previous font rather than starting a new one,
// which is how it ends up supplying only the codepoints the embedded Latin
// face lacks.
bool My_Android_LoadSystemFont(float SizePixels, bool merge = false);

} // namespace ImGui

namespace aimgui {

// Convenience: calls ImGui::My_Android_LoadSystemFont and falls back to the
// embedded default font (ASCII / Latin-1 only) on failure. Returns the loaded
// ImFont*.
ImFont* LoadDefaultAndSystemCJKFont(float size_pixels = 25.0f);

} // namespace aimgui
