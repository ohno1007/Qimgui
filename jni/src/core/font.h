#pragma once

struct ImFont;

namespace ImGui {

bool My_Android_LoadSystemFont(float SizePixels, bool merge = false);

}

namespace aimgui {

ImFont* LoadDefaultAndSystemCJKFont(float size_pixels = 25.0f);

}
