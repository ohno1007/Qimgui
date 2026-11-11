#pragma once
#include "model.hpp"
namespace clock_example {
// The caller owns the window, navigation rail, theme scope and frame lifecycle.
void DrawPage(State& state);
const char* PageTitle(Page page);
const char* PageSubtitle(Page page);
}
