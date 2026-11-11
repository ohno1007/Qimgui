#include "app.hpp"
#include <qimgui/android_overlay.hpp>
#include <chrono>
#include <ctime>
namespace {
bool Frame(qimgui::android::Frame& frame, void* user) {
    auto& app = *static_cast<clock_example::App*>(user);
    const auto now = std::chrono::steady_clock::now().time_since_epoch();
    clock_example::Update(app.state,std::chrono::duration<double>(now).count(),std::time(nullptr));
    qimgui::md3::SetFonts(frame.bodyFont,frame.labelFont);
    const bool running = clock_example::Draw(app,frame.visible,frame.displaySize);
    frame.inputBounds = app.inputBounds;
    return running;
}
}
int main() {
    clock_example::App app;
    return qimgui::android::RunOverlay({"MD3Clock"},Frame,&app);
}
