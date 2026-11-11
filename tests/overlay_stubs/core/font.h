#pragma once
struct ImFont;
namespace aimgui {
struct Md3Fonts { ImFont* body; ImFont* label; };
Md3Fonts LoadMd3Fonts();
}
