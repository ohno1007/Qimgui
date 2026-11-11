// Compile the production RunOverlay loop against fake Android services and the
// real ImGui core. Platform ABI/layer ownership has its own recording tests.
#include "qimgui/android_overlay.hpp"
#include "core/crash_diagnostics.h"
#include "core/font.h"
#include "core/keyboard_input.h"
#include "runtime/simple_renderer.h"
#include "platform/ANativeWindowCreator.h"
#include "platform/TouchHelperA.h"
#include "md3_backend.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <sys/wait.h>
#include <unistd.h>

namespace {
ANativeWindow window;
int completed=0, callbacks=0, polls=0, created=0, removed=0, reportFd=-1;
bool frameStarted=false, externalTarget=false, mirror=false;
bool keyboardStopped=false, rendererStopped=false, inputReleased=false;
const char* stage="";
bool Application(qimgui::android::Frame& frame, void*) {
    ++callbacks;
    assert(frameStarted && polls==callbacks && completed==callbacks-1);
    assert(frame.bodyFont && frame.labelFont);
    assert(frame.displaySize.x==1080 && frame.displaySize.y==2400);
    frame.inputBounds=ImVec4(10,20,500,400);
    if(callbacks==1) {
        assert(frame.visible && !mirror);
        // A recorder starts after startup; the next runtime pass must see it.
        externalTarget=true; frame.visible=false;
    } else if(callbacks==2) {
        assert(!frame.visible && mirror && created==1);
        externalTarget=false; frame.visible=true;
    } else if(callbacks==3) {
        assert(frame.visible && !mirror && removed==1);
        externalTarget=true;
    } else {
        assert(callbacks==4 && frame.visible && mirror && created==2);
        return false;
    }
    return true;
}
}

namespace android {
ANativeWindowCreator::DisplayInfo ANativeWindowCreator::GetDisplayInfo() { return {0,1080,2400}; }
ANativeWindow* ANativeWindowCreator::Create(const char*,int width,int height,bool excluded) {
    assert(width==2400 && height==2400 && !excluded);return &window;
}
void ANativeWindowCreator::ProcessMirrorDisplay() {
    assert(!frameStarted && polls==completed);
    assert(std::strcmp(stage,"overlay-recording-mirror")==0);
    ++polls;
    if(externalTarget && !mirror) { mirror=true;++created; }
    if(!externalTarget && mirror) { mirror=false;++removed; }
}
void ANativeWindowCreator::Destroy(ANativeWindow* value) {
    assert(value==&window && completed==4 && callbacks==4 && polls==4);
    assert(created==2 && removed==1 && mirror);
    assert(keyboardStopped && rendererStopped && inputReleased && !frameStarted);
    mirror=false; // The actual Creator's mirror cleanup is tested separately.
    const char passed='P';assert(write(reportFd,&passed,1)==1);
}
}
namespace aimgui::diagnostics {
void InstallCrashHandler(void (*release)()) { assert(release==Touch::EmergencyRelease); }
void RefreshLoadedModules() {}
void SetStage(const char* value) { stage=value; }
}
namespace aimgui {
Md3Fonts LoadMd3Fonts() { auto* font=ImGui::GetIO().Fonts->AddFontDefault();return {font,font}; }
}
namespace aimgui::kbd_input {
void Init() {}
void Shutdown() { keyboardStopped=true; }
void Flush() { assert(!frameStarted && polls==completed+1); }
int ConsumeVolumePresses() { return 0; }
}
namespace Touch {
bool Init(ImVec2,bool) { return true; }
void setOrientation(int orientation) { assert(orientation==0); }
void Heartbeat() { assert(!frameStarted); }
void SetBlockRegion(float,float,float width,float height,bool active) {
    if(callbacks==1 || callbacks==4) assert(!active && width==0 && height==0);
}
void EmergencyRelease() { inputReleased=true; }
}
namespace qimgui::android::detail {
bool SimpleRenderer::Init(ANativeWindow*,int,int) {
    ImGui::GetIO().BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    return true;
}
void SimpleRenderer::NewFrame(ImVec2 size) {
    assert(!frameStarted && polls==completed+1);
    frameStarted=true;ImGui::GetIO().DisplaySize=size;
}
bool SimpleRenderer::EndFrame() {
    assert(frameStarted);ImGui::Render();md3test::ValidateDrawData();md3test::UploadTextures();
    frameStarted=false;++completed;return true;
}
void SimpleRenderer::Shutdown() {
    assert(!frameStarted);rendererStopped=true;md3test::DestroyContext();
}
}
extern "C" int __system_property_get(const char* key,char* output) {
    const char* value=std::strstr(key,"sdk")?"37":"17";std::strcpy(output,value);return std::strlen(value);
}
int main() {
    int pipefd[2];assert(pipe(pipefd)==0);
    const auto child=fork();assert(child>=0);
    if(child==0) {
        close(pipefd[0]);reportFd=pipefd[1];
        qimgui::android::RunOverlay({"recording-runtime-test"},Application);
        _Exit(90); // A started process host must shut down through its exit path.
    }
    close(pipefd[1]);char passed=0;const auto count=read(pipefd[0],&passed,1);close(pipefd[0]);
    int status=0;assert(waitpid(child,&status,0)==child);
    assert(count==1 && passed=='P' && WIFEXITED(status) && WEXITSTATUS(status)==0);
    std::puts("PASS: production SDK loop pumps recording discovery before frames, including hidden/later recorder changes, then shuts down cleanly");
}
