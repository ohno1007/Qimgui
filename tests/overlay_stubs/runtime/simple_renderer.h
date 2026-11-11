#pragma once
#include "imgui.h"
struct ANativeWindow;
namespace qimgui::android::detail {
class SimpleRenderer {
public:
    bool Init(ANativeWindow*, int, int);
    void NewFrame(ImVec2);
    bool EndFrame();
    void Shutdown();
};
}
