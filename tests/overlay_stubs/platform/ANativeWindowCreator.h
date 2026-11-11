#pragma once
struct ANativeWindow {};
namespace android {
class ANativeWindowCreator {
public:
    struct DisplayInfo { int orientation; int width; int height; };
    static DisplayInfo GetDisplayInfo();
    static ANativeWindow* Create(const char*, int, int, bool);
    static void ProcessMirrorDisplay();
    static void Destroy(ANativeWindow*);
};
}
