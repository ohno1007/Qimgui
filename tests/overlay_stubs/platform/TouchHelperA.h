#pragma once
#include "imgui.h"
namespace Touch {
bool Init(ImVec2, bool);
void setOrientation(int);
void Heartbeat();
void SetBlockRegion(float,float,float,float,bool);
void EmergencyRelease();
}
