#include "ui/md3.h"
#include "ui/md3_showcase.h"
#include "ui/md3_motion.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include "md3_expressive_test_backend.h"

#include <cassert>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>

void RunMd3ToolbarSplitTests();
void RunMd3ButtonGroupTests();
void RunMd3ProgressTests();

namespace md3expressivetest {
ItemObserver item_observer = nullptr;
}

namespace {
namespace md = aimgui::md3;
const char* body_font = "tests/md3_web/fonts/Roboto-400.ttf";
const char* label_font = "tests/md3_web/fonts/Roboto-500.ttf";

void Configure(ImVec2 display) {
    md3test::ConfigureContext();
    auto& io = ImGui::GetIO();
    io.DisplaySize = display;
    ImFont* body = md3test::LoadComparisonFont(body_font);
    ImFont* label = md3test::LoadComparisonFont(label_font);
    io.FontDefault = body;
    md::SetFonts(body,label);
}

void GalleryFrame() {
    ImGui::NewFrame();
    md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0,0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,ImVec2(32,28));
    ImGui::Begin("Material 3 Expressive",nullptr,
                 ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    md::Showcase(7);
    ImGui::End();
    ImGui::PopStyleVar();
    md::PopTheme();
    ImGui::Render();
    md3test::ValidateDrawData();
    md3test::UploadTextures();
}

void Gallery() {
    assert(md::ShowcasePageCount >= 8);
    for (const auto display : {ImVec2(360,640),ImVec2(640,360),ImVec2(1280,1000)}) {
        Configure(display);
        for (bool dark : {false,true}) {
            md::SetTheme(dark);
            for(int frame=0;frame<3;++frame) GalleryFrame();
            assert(ImGui::GetDrawData()->TotalVtxCount > 0);
            const auto* gallery = ImGui::FindWindowByName("Material 3 Expressive");
            assert(gallery && gallery->ScrollMax.x <= 1.f);
        }
        md3test::DestroyContext();
    }
    std::puts("PASS: Expressive gallery renders in narrow, landscape and full-size contexts with both themes");
}

void MotionPreferences() {
    md3test::ConfigureContext();
    ImGuiContext* first=ImGui::GetCurrentContext();
    assert(!md::GetMotionSettings().reduced && md::GetMotionSettings().durationScale==1.f);
    md::SetMotionSettings({true,2.f});
    md::SetTheme(true,1.5f);
    assert(md::GetMotionSettings().reduced && md::GetMotionSettings().durationScale==2.f);
    // CreateContext restores an already-current context. Clear it explicitly
    // so the common single-context helper configures the new one.
    ImGui::SetCurrentContext(nullptr);
    md3test::ConfigureContext();
    assert(!md::GetMotionSettings().reduced && md::GetMotionSettings().durationScale==1.f);
    md::SetMotionSettings({false,-1.f});
    assert(md::GetMotionSettings().durationScale==0.f);
    md::SetMotionSettings({false,100.f});
    assert(md::GetMotionSettings().durationScale==10.f);
    md::SetMotionSettings({false,std::numeric_limits<float>::quiet_NaN()});
    assert(md::GetMotionSettings().durationScale==1.f);
    md3test::DestroyContext();
    ImGui::SetCurrentContext(first);
    assert(md::GetMotionSettings().reduced && md::GetMotionSettings().durationScale==2.f);
    md3test::DestroyContext();
    std::puts("PASS: motion preferences survive palette changes, remain context-local and sanitize duration inputs");
}

void Capture(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for(bool dark : {false,true}) {
        Configure(ImVec2(1280,1000));
        md::SetTheme(dark);
        for(int frame=0;frame<24;++frame) GalleryFrame();
        const std::string name = dark ? "md3-expressive-dark.draw" : "md3-expressive-light.draw";
        md3test::DumpDrawData((directory/name).string());
        md3test::DestroyContext();
    }
    std::printf("Captured native Expressive component gallery in %s\n",directory.string().c_str());
}
} // namespace

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context,ImGuiID id,const ImRect& bounds,
                               const ImGuiLastItemData* data) {
    if(md3expressivetest::item_observer)
        md3expressivetest::item_observer(context,id,bounds,data);
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID,const char*,ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID) { return nullptr; }

int main(int argc,char** argv) {
    std::setvbuf(stdout,nullptr,_IONBF,0);
    if(argc>1) {
        assert((argc==3||argc==5)&&std::strcmp(argv[1],"--capture")==0);
        if(argc==5) { body_font=argv[3];label_font=argv[4]; }
    }
    RunMd3ToolbarSplitTests();
    RunMd3ButtonGroupTests();
    RunMd3ProgressTests();
    MotionPreferences();
    Gallery();
    if(argc>1) Capture(argv[2]);
}
