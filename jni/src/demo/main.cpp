#include "qimgui/android_overlay.hpp"
#include "ui/md3.h"
#include "ui/md3_demo.h"

namespace {
bool DrawDemo(qimgui::android::Frame& frame, void* userData) {
    auto& state = *static_cast<aimgui::md3demo::State*>(userData);
    state.visible = frame.visible;
    aimgui::md3::SetFonts(frame.bodyFont, frame.labelFont);
    bool running = true;
    aimgui::md3demo::Draw(state, &running, frame.displaySize);
    frame.visible = state.visible;
    frame.inputBounds = state.window_rect;
    return running;
}
} // namespace

int main() {
    aimgui::md3demo::State state;
    return qimgui::android::RunOverlay({"MD3Demo"}, DrawDemo, &state);
}
