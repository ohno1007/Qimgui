#include "ui/ui.h"
#include "ui/main_ui.h"
#include "ui/md3.h"
#include "imgui_internal.h"
#include "md3_backend.h"

#include <algorithm>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <string>
#include <unordered_map>

namespace md = aimgui::md3;
namespace {
struct Item {
    ImRect bounds, clip;
    ImGuiID id = 0;
    ImGuiWindow* window = nullptr;
    ImGuiItemFlags flags = 0;
};
std::unordered_map<std::string, Item> items;
const char* labels[] = {
    u8"液体玻璃##glass-toggle", "window-drag", "window-resize", "restore-window",
    u8"取消##exit-cancel", u8"退出##exit-confirm",
    u8"深色主题##setting-dark", u8"显示大小##setting-scale",
    u8"触感反馈##setting-haptics", u8"拦截窗口内触摸##setting-touch",
    u8"防录屏##setting-recording", u8"重置窗口位置##setting-center",
    u8"允许点击##component-enabled", u8"点击 +1##component-count",
    u8"重置##component-reset", u8"打开确认框##component-dialog",
    u8"取消##component-cancel", u8"确认##component-confirm",
    u8"自动", u8"小", u8"标准", u8"大",
    u8"组件分类##component-category", u8"形状", "Shape##material-shape",
    u8"表现力组件", "format-bold"
};
const char* bodyFont = "tests/md3_web/fonts/Roboto-400.ttf";
const char* labelFont = "tests/md3_web/fonts/Roboto-500.ttf";

ImFont* LoadFont(const char* path) {
    ImFont* font = md3test::LoadComparisonFont(path);
    for (const char* cjk : {"/usr/share/fonts/opentype/noto/NotoSansCJK-Regular.ttc",
                            "/usr/share/fonts/truetype/wqy/wqy-zenhei.ttc"}) {
        if (!std::filesystem::exists(cjk)) continue;
        ImFontConfig merge; merge.MergeMode = true;
        static const ImWchar exclude[] = {0x20,0x24f,0};
        merge.GlyphExcludeRanges = exclude;
        assert(ImGui::GetIO().Fonts->AddFontFromFileTTF(cjk,14,&merge));
        break;
    }
    return font;
}
void Configure(ImVec2 display) {
    md3test::ConfigureContext();
    ImGui::GetIO().DisplaySize = display;
    ImFont* body = LoadFont(bodyFont);
    ImFont* label = LoadFont(labelFont);
    ImGui::GetIO().FontDefault = body;
    md::SetFonts(body,label);
    GImGui->TestEngineHookItems = true;
}
aimgui::UiState StateFor(ImVec2 display) {
    aimgui::UiState state;
    state.display_w = static_cast<int>(display.x); state.display_h = static_cast<int>(display.y);
    state.renderer_name = "Vulkan";
    state.glass_available = true;
    state.block_touch_ok = true;
    return state;
}
void Frame(aimgui::UiState& state, bool& running) {
    items.clear();
    ImGui::GetIO().DisplaySize = ImVec2(static_cast<float>(state.display_w),static_cast<float>(state.display_h));
    ImGui::NewFrame();
    aimgui::DrawUi(&state,&running);
    ImGui::Render();
    md3test::ValidateDrawData();
    md3test::UploadTextures();
}
void Settle(aimgui::UiState& state, bool& running) {
    Frame(state,running); Frame(state,running); Frame(state,running);
}
Item Find(const std::string& label) {
    auto it = items.find(label);
    if (it == items.end()) {
        std::fprintf(stderr,"Missing main UI item: %s\n",label.c_str());
        for (const auto& entry : items) std::fprintf(stderr,"  present: %s\n",entry.first.c_str());
    }
    assert(it != items.end());
    return it->second;
}
void RevealContentItem(aimgui::UiState& state, bool& running, const std::string& label) {
    const auto visible = [&] {
        const auto it = items.find(label);
        return it != items.end() && it->second.clip.Contains(it->second.bounds.GetCenter());
    };
    if (visible()) return;
    // Dropdowns can scroll independently of the compact main content area.
    const auto found = items.find(label);
    if (found != items.end() && (found->second.window->Flags & ImGuiWindowFlags_Popup)) {
        const auto item = found->second;
        ImGui::SetScrollFromPosY(item.window, item.bounds.GetCenter().y - item.window->Pos.y, .5f);
        Settle(state,running);
        if (visible()) return;
    }
    ImGuiWindow* content = nullptr;
    const auto* root = ImGui::FindWindowByName("Qimgui###aimgui_main");
    for (auto* window : GImGui->Windows)
        if (window->Active && window->ParentWindow == root && std::strstr(window->Name,"main-content"))
            content = window;
    if (!content) return;
    // Compact floating windows intentionally scroll. Search visible content
    // instead of requiring every settings card to fit on screen at once.
    ImGui::SetScrollY(content,0);
    Settle(state,running);
    for (int i=0;i<32 && !visible();++i) {
        if (content->Scroll.y >= content->ScrollMax.y) break;
        ImGui::SetScrollY(content,content->Scroll.y+content->InnerRect.GetHeight()*.5f);
        Settle(state,running);
    }
}
void Click(aimgui::UiState& state, bool& running, const std::string& label) {
    RevealContentItem(state,running,label);
    Item item = Find(label);
    ImRect visible = item.bounds; visible.ClipWith(item.clip);
    if (visible.GetWidth() <= 0 || visible.GetHeight() <= 0)
        std::fprintf(stderr,"Main UI item outside clip: %s\n",label.c_str());
    assert(visible.GetWidth() > 0 && visible.GetHeight() > 0);
    const ImVec2 center = visible.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x,center.y); Frame(state,running);
    io.AddMouseButtonEvent(0,true); Frame(state,running);
    io.AddMouseButtonEvent(0,false); Frame(state,running);
    // Return on the release frame so material toggles are checked immediately.
}
void Drag(aimgui::UiState& state, bool& running, const std::string& label, ImVec2 delta) {
    const ImVec2 center = Find(label).bounds.GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x,center.y); Frame(state,running);
    io.AddMouseButtonEvent(0,true); Frame(state,running);
    io.AddMousePosEvent(center.x+delta.x,center.y+delta.y); Frame(state,running);
    io.AddMouseButtonEvent(0,false); Settle(state,running);
}
void AssertReachable(const std::string& label) {
    const Item item = Find(label);
    const float slack = 1.f;
    if (item.bounds.Min.x < item.clip.Min.x-slack || item.bounds.Max.x > item.clip.Max.x+slack ||
        item.bounds.Min.y < item.clip.Min.y-slack || item.bounds.Max.y > item.clip.Max.y+slack)
        std::fprintf(stderr,"Clipped main UI control %s: item (%.1f,%.1f)-(%.1f,%.1f), clip (%.1f,%.1f)-(%.1f,%.1f)\n",
                     label.c_str(),item.bounds.Min.x,item.bounds.Min.y,item.bounds.Max.x,item.bounds.Max.y,
                     item.clip.Min.x,item.clip.Min.y,item.clip.Max.x,item.clip.Max.y);
    assert(item.bounds.Min.x >= item.clip.Min.x-slack && item.bounds.Max.x <= item.clip.Max.x+slack);
    assert(item.bounds.Min.y >= item.clip.Min.y-slack && item.bounds.Max.y <= item.clip.Max.y+slack);
}
void AssertWindowBounds(const aimgui::UiState& state) {
    const auto r = state.interaction_rect;
    assert(std::isfinite(r.x) && std::isfinite(r.y) && std::isfinite(r.z) && std::isfinite(r.w));
    assert(r.x >= -.1f && r.y >= -.1f && r.z > 0 && r.w > 0);
    assert(r.x+r.z <= state.display_w+.1f && r.y+r.w <= state.display_h+.1f);
    if (state.collapsed) return;
    const auto* root = ImGui::FindWindowByName("Qimgui###aimgui_main");
    const ImGuiWindow* navigation = nullptr;
    const ImGuiWindow* content = nullptr;
    for (const ImGuiWindow* window : GImGui->Windows) {
        if (!window->Active || window->ParentWindow != root) continue;
        if (std::strstr(window->Name,"navigation-panel")) navigation = window;
        if (std::strstr(window->Name,"main-content")) content = window;
    }
    assert(navigation && content);
    assert(navigation->Pos.x + navigation->Size.x <= content->Pos.x);
    assert(std::fabs(navigation->Pos.y - content->Pos.y) <= 1.f);
    assert(std::fabs(navigation->Size.y - content->Size.y) <= 1.f);
    assert(content->ClipRect.GetWidth() > 0 && content->ClipRect.GetHeight() > 0);
    assert(content->Pos.x+content->Size.x <= state.display_w+.1f);
    assert(content->Pos.y+content->Size.y <= state.display_h+.1f);
    float previous_y = -1;
    for (int page=0;page<aimgui::kPagesCount;++page) {
        const Item item = Find("destination/"+std::to_string(page));
        assert(item.window == navigation);
        assert(item.bounds.Min.x >= navigation->Pos.x-.1f);
        assert(item.bounds.Max.x <= content->Pos.x);
        if (page > 0) assert(item.bounds.Min.y > previous_y);
        previous_y = item.bounds.Min.y;
    }
}
void AssertRootOpacity(bool opaque) {
    const auto* root = ImGui::FindWindowByName("Qimgui###aimgui_main");
    assert(root && root->DrawList->CmdBuffer.Size && root->DrawList->IdxBuffer.Size >= 3);
    // The shell paints its background in draw channel zero. Read its first
    // triangle after channel merge, independently of geometry submission order.
    const auto& command = root->DrawList->CmdBuffer[0];
    unsigned maximum_alpha = 0;
    for (unsigned i=0;i<3;++i) {
        const auto index = root->DrawList->IdxBuffer[command.IdxOffset+i] + command.VtxOffset;
        const unsigned alpha = (root->DrawList->VtxBuffer[index].col >> IM_COL32_A_SHIFT)&255;
        maximum_alpha = std::max(maximum_alpha,alpha);
    }
    assert(opaque ? maximum_alpha == 255 : maximum_alpha > 0 && maximum_alpha < 255);
}
void AllPages() {
    for (const ImVec2 display : {ImVec2(360,640),ImVec2(480,320),ImVec2(1216,2640),
                                 ImVec2(2640,1216),ImVec2(1600,1000)}) {
        Configure(display); auto state = StateFor(display); bool running = true;
        for (bool dark : {false,true}) {
            state.md3_dark = dark;
            for (int page=0;page<aimgui::kPagesCount;++page) {
                state.nav_page = page;
                Settle(state,running);
                AssertWindowBounds(state);
                AssertReachable(u8"液体玻璃##glass-toggle");
                AssertReachable("app-action/0"); AssertReachable("app-action/1");
                assert(state.glass_count == 0 && state.widget_count == 0);
                assert(running && ImGui::GetDrawData()->TotalVtxCount > 0);
            }
        }
        state.display_w = static_cast<int>(display.y); state.display_h = static_cast<int>(display.x);
        Settle(state,running); AssertWindowBounds(state);
        md3test::DestroyContext();
    }
    std::puts("PASS: left navigation and right content stay aligned across all pages/themes and phone/tablet/compact/rotated layouts");
}
void Navigation() {
    // The sidebar must stay beside content even on narrow or short displays.
    for (ImVec2 size : {ImVec2(360,640),ImVec2(480,320),ImVec2(1216,2640),
                       ImVec2(2640,1216),ImVec2(1600,1000)}) {
        Configure(size); auto state = StateFor(size);
        state.last_full_pos = ImVec2(12,12); state.last_full_size = ImVec2(size.x-24,size.y-24);
        state.ui_initialized = true; bool running = true;
        Settle(state,running);
        for (int page=0;page<aimgui::kPagesCount;++page) {
            const std::string destination = "destination/"+std::to_string(page);
            const Item item = Find(destination);
            if (item.bounds.Min.y < item.clip.Min.y || item.bounds.Max.y > item.clip.Max.y) {
                ImGui::SetScrollFromPosY(item.window,item.bounds.GetCenter().y-item.window->Pos.y,.5f);
                Settle(state,running);
            }
            AssertReachable(destination);
            Click(state,running,destination);
            assert(state.nav_page == page);
            Settle(state,running);
            AssertWindowBounds(state);
        }
        md3test::DestroyContext();
    }
    std::puts("PASS: all MD3 sidebar destinations select pages across portrait/landscape/narrow/wide windows");
}
void CompactDefault() {
    const ImVec2 display(1216,2640);
    Configure(display); auto state = StateFor(display); bool running = true;
    Settle(state,running);
    assert(state.last_full_size.x > state.last_full_size.y && state.last_full_size.y < 900.f);
    assert(std::fabs(state.last_full_pos.x-(display.x-state.last_full_size.x)*.5f) < 1.f);
    assert(std::fabs(state.last_full_pos.y-(display.y-state.last_full_size.y)*.5f) < 1.f);
    AssertWindowBounds(state);
    for (int page=0;page<aimgui::kPagesCount;++page)
        AssertReachable("destination/"+std::to_string(page));
    md3test::DestroyContext();

    Configure(display); state = StateFor(display);
    state.last_full_size = ImVec2(1156,2560);
    state.nav_page = 2; state.md3_dark = true; state.haptics_enabled = false;
    Settle(state,running);
    assert(state.last_full_size.x > state.last_full_size.y && state.last_full_size.y < 900.f);
    assert(state.nav_page == 2 && state.md3_dark && !state.haptics_enabled);
    state.last_full_size = ImVec2(900,1300);
    Settle(state,running);
    assert(state.last_full_size.y == 1300); // Migration only runs on first draw.
    md3test::DestroyContext();

    for (const ImVec2 saved_size : {ImVec2(650,480),ImVec2(900,620)}) {
        Configure(display); state = StateFor(display);
        state.last_full_pos = ImVec2(80,100); state.last_full_size = saved_size;
        Settle(state,running);
        assert(state.last_full_pos.x == 80 && state.last_full_pos.y == 100);
        assert(state.last_full_size.x == saved_size.x && state.last_full_size.y == saved_size.y);
        AssertWindowBounds(state);
        md3test::DestroyContext();
    }
    std::puts("PASS: compact centered launch, one-time tall-window migration and custom compact geometry preservation");
}
void GlassSwitch() {
    const ImVec2 display(1216,2640);
    Configure(display); auto state = StateFor(display); bool running = true;
    state.nav_page = 2; state.component_page = 3; state.md3_dark = true;
    state.screen_texture_id = 123; state.screen_mirror_running = true;
    Settle(state,running);
    assert(!state.screen_mirror && state.glass_count == 0);
    AssertRootOpacity(true);
    Click(state,running,u8"液体玻璃##glass-toggle");
    assert(state.screen_mirror && state.glass_count > 0);
    AssertRootOpacity(false);
    AssertWindowBounds(state);
    assert(state.nav_page == 2 && state.component_page == 3 && state.md3_dark);
    for (int i=0;i<state.glass_count;++i) {
        const auto& r = state.glass_rects[i];
        assert(r.w > 0 && r.h > 0 && r.alpha > 0);
        assert(std::isfinite(r.x) && std::isfinite(r.y));
    }
    Click(state,running,u8"液体玻璃##glass-toggle");
    assert(!state.screen_mirror && state.glass_count == 0 && state.widget_count == 0);
    AssertRootOpacity(true);
    AssertWindowBounds(state);
    assert(state.nav_page == 2 && state.component_page == 3 && state.md3_dark);
    state.screen_mirror = true; state.screen_texture_id = 0;
    Settle(state,running); assert(state.glass_count == 0); AssertRootOpacity(true);
    state.screen_texture_id = 123; state.glass_failed = true;
    Settle(state,running); assert(state.glass_count == 0); AssertRootOpacity(true);
    state.glass_failed = false; state.glass_available = false;
    Settle(state,running); assert(state.glass_count == 0); AssertRootOpacity(true);
    Click(state,running,u8"液体玻璃##glass-toggle");
    assert(!state.screen_mirror);
    md3test::DestroyContext();
    std::puts("PASS: one glass switch preserves page/theme/state and clears effects on the release frame; unavailable/starting/failed capture stays flat");
}
void SettingsAndDialogs() {
    const ImVec2 display(1216,2640);
    Configure(display); auto state = StateFor(display); state.nav_page = 2; bool running = true;
    Settle(state,running);
    Click(state,running,u8"深色主题##setting-dark"); assert(state.md3_dark);
    Click(state,running,u8"显示大小##setting-scale"); Settle(state,running);
    Click(state,running,u8"标准"); Settle(state,running); assert(state.ui_scale == 1.5f);
    Click(state,running,u8"显示大小##setting-scale"); Settle(state,running);
    Click(state,running,u8"自动"); Settle(state,running); assert(state.ui_scale == 0.f);
    Click(state,running,u8"触感反馈##setting-haptics"); assert(!state.haptics_enabled);
    Click(state,running,u8"拦截窗口内触摸##setting-touch"); assert(!state.block_touch);
    state.nav_page = 1; Settle(state,running);
    Click(state,running,u8"允许点击##component-enabled");
    Settle(state,running);
    assert(Find(u8"点击 +1##component-count").flags & ImGuiItemFlags_Disabled);
    state.screen_texture_id = 123;
    Click(state,running,u8"液体玻璃##glass-toggle");
    assert(state.screen_mirror && state.nav_page == 1);
    assert(Find(u8"点击 +1##component-count").flags & ImGuiItemFlags_Disabled);
    Click(state,running,u8"液体玻璃##glass-toggle");
    assert(!state.screen_mirror && state.nav_page == 1);
    assert(Find(u8"点击 +1##component-count").flags & ImGuiItemFlags_Disabled);
    Click(state,running,u8"允许点击##component-enabled"); Settle(state,running);
    assert(!(Find(u8"点击 +1##component-count").flags & ImGuiItemFlags_Disabled));
    Click(state,running,u8"打开确认框##component-dialog"); Settle(state,running);
    assert(state.modal_open);
    Click(state,running,u8"取消##component-cancel"); Settle(state,running);
    assert(!state.modal_open && running);
    Click(state,running,"app-action/0");
    Settle(state,running); assert(state.stage == aimgui::UiState::StageIsland && state.collapsed);
    AssertWindowBounds(state);
    Click(state,running,"restore-window"); Settle(state,running);
    assert(state.stage == aimgui::UiState::StageWindow && !state.collapsed && state.nav_page == 1);
    Click(state,running,"app-action/1"); Settle(state,running);
    assert(state.modal_open && running);
    Click(state,running,u8"取消##exit-cancel"); Settle(state,running);
    assert(!state.modal_open && running);
    Click(state,running,"app-action/1"); Settle(state,running);
    Click(state,running,u8"退出##exit-confirm"); assert(!running);
    md3test::DestroyContext();
    std::puts("PASS: MD3 settings, component enabled state, modal cancellation, collapse/restore and confirmed exit");
}
void WindowMovement() {
    const ImVec2 display(1100,900);
    Configure(display); auto state = StateFor(display); bool running = true;
    state.ui_scale = 1; state.ui_initialized = true;
    state.last_full_pos = ImVec2(80,70); state.last_full_size = ImVec2(800,660);
    Settle(state,running);
    const auto old_position = state.last_full_pos;
    Drag(state,running,"window-drag",ImVec2(30,40));
    assert(state.last_full_pos.x > old_position.x+20 && state.last_full_pos.y > old_position.y+30);
    AssertWindowBounds(state);
    const auto old_size = state.last_full_size;
    Drag(state,running,"window-resize",ImVec2(-100,-80));
    assert(state.last_full_size.x < old_size.x-90 && state.last_full_size.y < old_size.y-70);
    AssertWindowBounds(state);
    Drag(state,running,"window-drag",ImVec2(-1500,-1500)); AssertWindowBounds(state);
    state.nav_page = 2; Settle(state,running);
    Click(state,running,u8"重置窗口位置##setting-center"); Settle(state,running);
    assert(std::fabs(state.last_full_pos.x-(display.x-state.last_full_size.x)*.5f) < 1.f);
    assert(std::fabs(state.last_full_pos.y-(display.y-state.last_full_size.y)*.5f) < 1.f);
    md3test::DestroyContext();
    std::puts("PASS: title drag, resize, offscreen clamping and center-window setting");
}
void ShapesPage() {
    for (const ImVec2 display : {ImVec2(1216,2640), ImVec2(960,540), ImVec2(1600,1000)}) {
        Configure(display); auto state = StateFor(display); bool running = true;
        state.nav_page = 1;
        Settle(state,running);
        const auto before = state.last_full_size;
        Click(state,running,"component-mode/1"); Settle(state,running);
        Click(state,running,u8"组件分类##component-category"); Settle(state,running);
        Click(state,running,u8"形状"); Settle(state,running);
        assert(state.component_page == 6);
        assert(state.last_full_size.x == before.x && state.last_full_size.y == before.y);
        AssertWindowBounds(state);
        assert(Find("Shape##material-shape").window->ScrollMax.x < 1.f);
        assert(!state.screen_mirror && state.glass_count == 0);
        Click(state,running,u8"组件分类##component-category"); Settle(state,running);
        Click(state,running,u8"表现力组件"); Settle(state,running);
        assert(state.component_page == 7);
        assert(state.last_full_size.x == before.x && state.last_full_size.y == before.y);
        AssertWindowBounds(state);
        AssertReachable("format-bold");
        md3test::DestroyContext();
    }
    std::puts("PASS: MaterialShapes and Expressive categories are reachable and preserve compact left/right layout");
}
void Capture(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    struct Shot { const char* name; ImVec2 size; int page; bool dark; float scale; bool glass = false; bool shapes = false; bool expressive = false; };
    const Shot shots[] = {
        {"main-md3-light-portrait",{1216,2640},0,false,0},
        {"main-md3-components",{1216,2640},1,false,0},
        {"main-md3-dark-settings",{1216,2640},2,true,0},
        {"main-md3-light-wide",{1600,1000},0,false,1.5f},
        {"md3-main-glass",{1216,2640},0,false,0,true},
        {"main-md3-shapes-light",{1216,2640},1,false,0,false,true},
        {"main-md3-shapes-dark",{1600,1000},1,true,0,false,true},
        {"main-md3-expressive-light",{1216,2640},1,false,0,false,false,true},
        {"main-md3-expressive-dark",{1600,1000},1,true,0,false,false,true},
    };
    for (const auto& shot: shots) {
        Configure(shot.size); auto state = StateFor(shot.size); bool running = true;
        state.nav_page = shot.page; state.md3_dark = shot.dark; state.ui_scale = shot.scale;
        state.screen_mirror = shot.glass;
        state.screen_mirror_running = shot.glass;
        // The host supplies no capture texture. This ID only requests transparent
        // UI draw data; the compositor applies the production shader separately.
        state.screen_texture_id = shot.glass ? 123 : 0;
        if (shot.shapes || shot.expressive) {
            state.component_page = shot.expressive ? 7 : 6;
            Settle(state,running);
            Click(state,running,"component-mode/1");
        }
        for (int i=0;i<30;++i) Frame(state,running);
        md3test::DumpDrawData((directory/(std::string(shot.name)+".draw")).string());
        if (shot.glass) {
            assert(state.glass_count > 0);
            std::ofstream json(directory/(std::string(shot.name)+".glass.json"));
            assert(json.is_open());
            json << "{\"width\":" << state.display_w << ",\"height\":" << state.display_h << ",\"glass_rects\":[";
            for (int i=0;i<state.glass_count;++i) {
                const auto& r = state.glass_rects[i];
                if (i) json << ',';
                json << "{\"x\":" << r.x << ",\"y\":" << r.y << ",\"w\":" << r.w << ",\"h\":" << r.h
                     << ",\"rounding\":" << r.rounding << ",\"edgeWidth\":" << r.edgeWidth
                     << ",\"bend\":" << r.bend << ",\"alpha\":" << r.alpha
                     << ",\"tintR\":" << r.tintR << ",\"tintG\":" << r.tintG << ",\"tintB\":" << r.tintB
                     << ",\"tintA\":" << r.tintA << ",\"blur\":" << r.blur
                     << ",\"lightX\":" << r.lightX << ",\"lightY\":" << r.lightY
                     << ",\"merge\":" << r.merge << ",\"group\":" << r.group
                     << ",\"hardJoin\":" << (r.hardJoin?"true":"false") << '}';
            }
            json << "]}\n"; assert(json.good());
        }
        md3test::DestroyContext();
    }
    std::printf("Captured unified MD3 main UI in %s (glass draw data requires the separate shader compositor)\n",directory.string().c_str());
}
}

void ImGuiTestEngineHook_ItemAdd(ImGuiContext* context,ImGuiID id,const ImRect& bounds,const ImGuiLastItemData*) {
    auto* window = context->CurrentWindow;
    if (!window || window->IDStack.empty()) return;
    const Item item{bounds,window->ClipRect,id,window,context->CurrentItemFlags};
    for (const char* label: labels) {
        if (window->GetID(label) == id) items[label] = item;
        if (window->IDStack.Size >= 2 && window->GetID("##field") == id &&
            ImHashStr(label,0,window->IDStack[window->IDStack.Size-2]) == window->IDStack.back())
            items[label] = item;
    }
    if (window->IDStack.Size >= 3) {
        const ImGuiID parent = window->IDStack[window->IDStack.Size-2];
        const ImGuiID top = window->IDStack.back();
        const bool action = window->GetID("action") == id;
        const bool destination = window->GetID("destination") == id;
        const bool componentTab = window->GetID("tab") == id &&
            parent == ImHashStr("component-modes",0,window->IDStack[window->IDStack.Size-3]);
        if (componentTab) for (int i=0;i<2;++i) {
            if (ImHashData(&i,sizeof(i),parent) == top)
                items["component-mode/"+std::to_string(i)] = item;
        }
        if (action || destination) for (int i=0;i<5;++i) {
            if (ImHashData(&i,sizeof(i),parent) != top) continue;
            items[std::string(action?"app-action/":"destination/")+std::to_string(i)] = item;
        }
    }
}
void ImGuiTestEngineHook_ItemInfo(ImGuiContext*,ImGuiID,const char*,ImGuiItemStatusFlags) {}
void ImGuiTestEngineHook_Log(ImGuiContext*,const char*,...) {}
const char* ImGuiTestEngine_FindItemDebugLabel(ImGuiContext*,ImGuiID) { return nullptr; }

int main(int argc,char** argv) {
    if (argc>1) {
        assert(argc==3 || argc==5); assert(std::strcmp(argv[1],"--capture")==0);
        if(argc==5) {bodyFont=argv[3];labelFont=argv[4];}
    }
    AllPages(); Navigation(); CompactDefault(); GlassSwitch(); SettingsAndDialogs(); WindowMovement(); ShapesPage();
    if(argc>1) Capture(argv[2]);
}
