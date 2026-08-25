#pragma once

#include <string>

namespace aimgui::sysclip {

bool ReadText(std::string* out);

const char* LastError();

int RunHelperMain(int argc, char** argv);

}
