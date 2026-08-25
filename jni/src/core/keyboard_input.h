#pragma once

namespace aimgui::kbd_input {

void Init();
void Shutdown();

void Flush();

int ConsumeVolumePresses();

}
