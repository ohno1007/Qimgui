#pragma once
#include "model.hpp"
#include <qimgui/md3.hpp>
namespace clock_example {
struct App {
    State state;
    ImVec2 position{};
    ImVec2 size{};
    ImVec4 inputBounds{};
    bool positioned = false;
    int previousPage = -1;
};
bool Draw(App& app, bool& visible, ImVec2 displaySize);
}
