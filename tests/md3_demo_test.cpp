#include "ui/md3_demo.h"
#include "ui/md3.h"
#include "ui/md3_showcase.h"
#include "imgui_internal.h"
#include "md3_backend.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <string>
#include <unordered_map>

namespace demo = aimgui::md3demo;
namespace md = aimgui::md3;
namespace {
struct Item {
    ImRect bounds;
    ImRect clip;
    ImGuiID id = 0;
    ImGuiWindow* window = nullptr;
};
std::unordered_map<std::string, Item> items;
const char* labels[] = {
    "window-minimize", "window-close", u8"重置样例##samples-reset",
    u8"允许计数##sample-enabled", u8"点击 +1##sample-count",
    u8"勾选选项##sample-checked", u8"通知##sample-notifications",
    u8"强度##sample-value", u8"保存##sample-save",
    u8"确认保存##save-confirm", u8"取消##save-cancel", u8"填入示例##sample-fill", "##input",
    u8"组件分类##demo-page", u8"表现力组件"
};
const char* bodyFont = "tests/md3_web/fonts/Roboto-400.ttf";
const char* labelFont = "tests/md3_web/fonts/Roboto-500.ttf";

const char* CjkFont() {
    for (const char* path : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"})
        if (std::filesystem::exists(path)) return path;
    return nullptr;
}
ImFont* LoadFont(const char* path) {
    ImFont* result = md3test::LoadComparisonFont(path);
    if (const char* cjk = CjkFont()) {
        ImFontConfig merge;
        merge.MergeMode = true;
        static const ImWchar exclude[] = {0x20, 0x24f, 0};
        merge.GlyphExcludeRanges = exclude;
        assert(ImGui::GetIO().Fonts->AddFontFromFileTTF(cjk, 14.f, &merge));
    }
    return result;
}
void Configure(ImVec2 size) {
    md3test::ConfigureContext();
    ImGui::GetIO().DisplaySize = size;
    ImFont* body = LoadFont(bodyFont);
    ImFont* label = LoadFont(labelFont);
    ImGui::GetIO().FontDefault = body;
    md::SetFonts(body, label);
    GImGui->TestEngineHookItems = true;
}
void Frame(demo::State& state, bool& running) {
    items.clear();
    ImGui::NewFrame();
    demo::Draw(state, &running, ImGui::GetIO().DisplaySize);
    ImGui::Render();
    md3test::ValidateDrawData();
    md3test::UploadTextures();
}
void Settle(demo::State& state, bool& running) {
    Frame(state, running); Frame(state, running); Frame(state, running);
}
Item Find(const char* label) {
    auto it = items.find(label);
    if (it == items.end()) std::fprintf(stderr, "Missing item: %s\n", label);
    assert(it != items.end());
    return it->second;
}
void Click(demo::State& state, bool& running, const char* label) {
    Item item = Find(label);
    if ((item.window->Flags & ImGuiWindowFlags_Popup) &&
        !item.clip.Contains(item.bounds.GetCenter())) {
        ImGui::SetScrollFromPosY(item.window,item.bounds.GetCenter().y-item.window->Pos.y,.5f);
        Settle(state,running);
        item = Find(label);
    }
    ImRect visible = item.bounds;
    visible.ClipWith(item.clip);
    if (visible.GetWidth() <= 0 || visible.GetHeight() <= 0)
        std::fprintf(stderr, "Item outside clip: %s\n", label);
    assert(visible.GetWidth() > 0 && visible.GetHeight() > 0);
    const auto center = visible.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y); Frame(state, running);
    io.AddMouseButtonEvent(0, true); Frame(state, running);
    io.AddMouseButtonEvent(0, false); Frame(state, running);
    Frame(state, running);
}
void AssertBounds(const demo::State& state, ImVec2 display) {
    const ImVec4 r = state.window_rect;
    assert(r.x >= -.1f && r.y >= -.1f && r.z > 0 && r.w > 0);
    assert(r.x + r.z <= display.x + .1f && r.y + r.w <= display.y + .1f);
    for (const ImGuiWindow* child : GImGui->Windows) {
        if (!child->Active || !child->ParentWindow ||
            std::strcmp(child->ParentWindow->Name, "MD3Demo") != 0 ||
            !std::strstr(child->Name, "demo-content")) continue;
        if (child->Pos.y + child->Size.y > r.y + r.w + .1f)
            std::fprintf(stderr, "Demo content exceeds window: display %.0fx%.0f, child bottom %.1f > window %.1f\n",
                         display.x, display.y, child->Pos.y + child->Size.y, r.y + r.w);
        assert(child->Pos.y >= r.y && child->Pos.y + child->Size.y <= r.y + r.w + .1f);
        assert(child->ClipRect.GetHeight() > 0 && child->ClipRect.GetWidth() > 0);
    }
    for (const char* label : {"window-minimize", "window-close"}) {
        const Item item = Find(label);
        assert(item.bounds.Min.x >= r.x -.1f && item.bounds.Max.x <= r.x + r.z + .1f);
        assert(item.bounds.Min.y >= r.y -.1f && item.bounds.Max.y <= r.y + r.w + .1f);
        assert(item.bounds.Min.x >= item.clip.Min.x -.1f && item.bounds.Max.x <= item.clip.Max.x + .1f);
        assert(item.bounds.Min.y >= item.clip.Min.y -.1f && item.bounds.Max.y <= item.clip.Max.y + .1f);
    }
}
void Galleries() {
    for (const ImVec2 display : {ImVec2(360,640), ImVec2(480,320), ImVec2(720,480), ImVec2(1216,2640), ImVec2(960,540), ImVec2(1600,1000)}) {
        Configure(display);
        demo::State state; bool running = true;
        for (bool dark : {false, true}) {
            state.dark = dark;
            for (int page = 0; page < md::ShowcasePageCount; ++page) {
                state.page = page;
                Settle(state, running);
                AssertBounds(state, display);
                assert(running && ImGui::GetDrawData()->TotalVtxCount > 0);
            }
        }
        ImGui::GetIO().DisplaySize = ImVec2(display.y, display.x);
        Settle(state, running);
        AssertBounds(state, ImGui::GetIO().DisplaySize);
        md3test::DestroyContext();
    }
    std::puts("PASS: all demo gallery categories/themes render within phone and tablet windows, including rotation");
}
void WindowControls() {
    Configure(ImVec2(960,1000));
    demo::State state; bool running = true;
    Settle(state, running);
    Click(state, running, "window-minimize");
    assert(!state.visible && running);
    Frame(state, running);
    assert(state.window_rect.z == 0 && state.window_rect.w == 0);
    state.visible = true; Settle(state, running);
    assert(state.window_rect.z > 0 && state.window_rect.w > 0);
    Click(state, running, "window-close");
    assert(!running);
    md3test::DestroyContext();
    std::puts("PASS: demo minimize, restore and close controls");
}
void ExpressiveCategoryMenu() {
    assert(md::ShowcasePageCount >= 8);
    for (const ImVec2 display : {ImVec2(960,540),ImVec2(1216,2640)}) {
        Configure(display);
        demo::State state;bool running=true;
        Settle(state,running);
        const ImVec2 size = state.size;
        Click(state,running,u8"组件分类##demo-page");Settle(state,running);
        Click(state,running,u8"表现力组件");Settle(state,running);
        assert(state.page==7 && running);
        assert(state.size.x==size.x && state.size.y==size.y);
        AssertBounds(state,display);
        md3test::DestroyContext();
    }
    std::puts("PASS: Expressive category is reachable through the standalone demo menu, including a scrolling dropdown");
}
void Samples() {
    Configure(ImVec2(1216,2640));
    demo::State state; state.mode = 1; bool running = true;
    Settle(state, running);
    Click(state, running, u8"点击 +1##sample-count");
    assert(state.sample_clicks == 1);
    Click(state, running, u8"允许计数##sample-enabled");
    assert(!state.sample_enabled);
    Click(state, running, u8"点击 +1##sample-count");
    assert(state.sample_clicks == 1);
    Click(state, running, u8"允许计数##sample-enabled");
    Click(state, running, u8"点击 +1##sample-count");
    assert(state.sample_clicks == 2);

    state.sample_page = 1; Settle(state, running);
    Click(state, running, u8"勾选选项##sample-checked");
    assert(state.sample_checked);
    Click(state, running, u8"通知##sample-notifications");
    assert(!state.sample_notifications);
    const Item slider = Find(u8"强度##sample-value");
    ImGui::SetFocusID(slider.id, slider.window);
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(ImGuiKey_End, true); Frame(state, running);
    io.AddKeyEvent(ImGuiKey_End, false); Frame(state, running);
    assert(state.sample_value == 100.f);

    state.sample_page = 2; Settle(state, running);
    Click(state, running, u8"保存##sample-save");
    assert(state.sample_error && !state.sample_dialog && !state.sample_saved);
    Click(state, running, "##input");
    io.AddInputCharactersUTF8("MD3 demo"); Frame(state, running);
    assert(std::strcmp(state.sample_name, "MD3 demo") == 0);
    Click(state, running, u8"保存##sample-save");
    assert(!state.sample_error && state.sample_dialog && !state.sample_saved);
    Settle(state, running);
    Click(state, running, u8"取消##save-cancel");
    assert(!state.sample_dialog && !state.sample_saved);
    Click(state, running, u8"保存##sample-save"); Settle(state, running);
    Click(state, running, u8"确认保存##save-confirm");
    assert(!state.sample_dialog && state.sample_saved && state.snackbar_open);
    assert(std::strcmp(state.saved_name, "MD3 demo") == 0);

    Click(state, running, u8"重置样例##samples-reset");
    assert(state.sample_clicks == 0 && state.sample_enabled && !state.sample_checked);
    assert(state.sample_notifications && state.sample_value == 40.f);
    assert(!state.sample_error && !state.sample_saved && !state.sample_dialog);
    assert(state.sample_name[0] == '\0' && state.saved_name[0] == '\0');
    // The touch-only path fills sample text without an external keyboard.
    Click(state, running, u8"填入示例##sample-fill");
    assert(std::strcmp(state.sample_name, "Material 3") == 0);
    Click(state, running, u8"保存##sample-save"); Settle(state, running);
    Click(state, running, u8"确认保存##save-confirm");
    assert(state.sample_saved && std::strcmp(state.saved_name, "Material 3") == 0);
    md3test::DestroyContext();
    std::puts("PASS: demo counter/disabled states, settings, keyboard slider, form validation, confirm/cancel and reset");
}
void Capture(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    struct Shot { const char* name; ImVec2 size; bool dark; int mode, page; };
    const Shot shots[] = {
        {"md3-demo-light-portrait", {1216,2640}, false,0,0},
        {"md3-demo-dark-landscape", {960,540}, true,0,1},
        {"md3-demo-interactive-form", {1216,2640}, false,1,2},
        {"md3-demo-shapes-light", {1216,2640}, false,0,6},
        {"md3-demo-shapes-dark", {1600,1000}, true,0,6},
        {"md3-demo-expressive-light", {1216,2640}, false,0,7},
        {"md3-demo-expressive-dark", {1600,1000}, true,0,7},
    };
    for (const auto& shot : shots) {
        Configure(shot.size);
        demo::State state; bool running = true;
        state.dark = shot.dark; state.mode = shot.mode;
        state.page = shot.page; state.sample_page = shot.page;
        // A manual 1dp scale makes the shape controls and initial catalog rows
        // visible together; auto-scale remains covered by Galleries().
        if (shot.mode == 0 && shot.page >= 6) state.scale = 1.f;
        if (shot.mode == 1) {
            std::strcpy(state.sample_name, "Material Design 3");
            std::strcpy(state.saved_name, "Material Design 3");
            state.sample_saved = true;
        }
        for (int i = 0; i < 24; ++i) Frame(state, running);
        md3test::DumpDrawData((directory / (std::string(shot.name) + ".draw")).string());
        md3test::DestroyContext();
    }
    std::printf("Captured native MD3 demo draws in %s\n", directory.string().c_str());
}
}

// Standard ImGui hooks capture real layout; tests do not duplicate positioning.
void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context, ImGuiID id, const ImRect& bounds,
                               const ImGuiLastItemData*) {
    ImGuiWindow* window = context->CurrentWindow;
    if (!window || window->IDStack.empty()) return;
    for (const char* label : labels) {
        if (window->GetID(label) == id)
            items[std::string(label)] = {bounds, window->ClipRect, id, window};
        if (window->IDStack.Size >= 2 && window->GetID("##field") == id &&
            ImHashStr(label,0,window->IDStack[window->IDStack.Size-2]) == window->IDStack.back())
            items[std::string(label)] = {bounds, window->ClipRect, id, window};
    }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*, ImGuiID, const char*, ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*, const char*, ...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*, ImGuiID) { return nullptr; }

int main(int argc, char** argv) {
    if (argc > 1) {
        assert(argc == 3 || argc == 5);
        assert(std::strcmp(argv[1], "--capture") == 0);
        if (argc == 5) { bodyFont = argv[3]; labelFont = argv[4]; }
    }
    Galleries(); WindowControls(); Samples(); ExpressiveCategoryMenu();
    if (argc > 1) Capture(argv[2]);
}
