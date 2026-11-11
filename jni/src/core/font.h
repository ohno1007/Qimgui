#pragma once

struct ImFont;

namespace ImGui {

bool My_Android_LoadSystemFont(float SizePixels, bool merge = false);

}

namespace aimgui {

ImFont* LoadDefaultAndSystemCJKFont(float size_pixels = 25.0f);

struct Md3Fonts {
    ImFont* body = nullptr;
    ImFont* label = nullptr;
};

// Embedded Roboto 400/500, byte-identical to the comparison fixture, with
// device CJK fallbacks. Does not change io.FontDefault or the shell's fonts.
Md3Fonts LoadMd3Fonts(float size_pixels = 14.0f);

}
