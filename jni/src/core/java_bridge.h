#pragma once
#include <string>
namespace aimgui::java_bridge {
bool Available();
const char* LastError();
bool SetClipboard(const char* text);
bool GetClipboard(std::string* text);
bool ShowInputMethod();
bool GetInputText(std::string* text);
bool HideInputMethod();
}
