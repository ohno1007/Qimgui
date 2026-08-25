#pragma once

namespace aimgui {

namespace clipboard {

const char* Path();

bool        UsedSystem();
const char* SystemError();

void Install();

void        Set(const char* text);
const char* Get();

}
}
