#pragma once

#include <string>

namespace aimgui::java_bridge {

// Uses the already-running Android VM when the binary is hosted by app_process.
// A plain shell-launched ELF has no VM and reports the service as unavailable.
bool Available();
const char* LastError();
bool SetClipboard(const char* text);
bool GetClipboard(std::string* text);
// Ask the currently focused Java View to show the system IME.
// Returns false when this native process has no hosted Java View.
bool ShowInputMethod();

}
