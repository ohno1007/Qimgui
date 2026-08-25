#pragma once

struct ImDrawData;

namespace aimgui {

void OutlineText(ImDrawData* dd, float radius, unsigned char alpha = 235);

constexpr float kTextOutlineRadius = 1.5f;

void ShutdownTextOutline();

}
