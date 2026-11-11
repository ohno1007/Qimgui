#include "ui/md3.h"
#include "ui/md3_button_group.h"
#include "md3_backend.h"
#include "md3_expressive_test_backend.h"
#include "imgui_internal.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <unordered_map>

namespace {
namespace md = aimgui::md3;
using Draw = std::function<void()>;
std::unordered_map<ImGuiID, ImRect> observed;
std::array<ImGuiID, 64> ids{};
ImVec2 origin, group_size;
ImGuiWindow* group_window = nullptr;

void Observe(ImGuiContext*, ImGuiID id, const ImRect& rect, const ImGuiLastItemData*) {
    if (id) observed[id] = rect;
}
void Configure() {
    md3test::ConfigureContext(); md::SetTheme(false);
    GImGui->TestEngineHookItems = true;
}
void Frame(const Draw& draw) {
    observed.clear();
    ImGui::NewFrame(); md::PushTheme();
    ImGui::SetNextWindowPos(ImVec2(0, 0));
    ImGui::SetNextWindowSize(ImGui::GetIO().DisplaySize);
    ImGui::Begin("Expressive button groups", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoMove);
    draw();
    ImGui::End(); md::PopTheme(); ImGui::Render();
    md3test::ValidateDrawData(); md3test::UploadTextures();
}
void Settle(const Draw& draw) { Frame(draw); Frame(draw); Frame(draw); }
void CaptureIds(const char* id, int count) {
    group_window = ImGui::GetCurrentWindow(); origin = ImGui::GetCursorScreenPos();
    ImGui::PushID(id);
    for (int i = 0; i < count; ++i) {
        ImGui::PushID(i); ids[i] = ImGui::GetID("button"); ImGui::PopID();
    }
    ImGui::PopID();
}
ImRect Bounds(int index) {
    const auto found = observed.find(ids[index]);
    assert(found != observed.end()); return found->second;
}
void Click(const Draw& draw, int index) {
    const ImVec2 center = Bounds(index).GetCenter();
    auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y); Frame(draw);
    io.AddMouseButtonEvent(0, true); Frame(draw);
    io.AddMouseButtonEvent(0, false); Frame(draw);
}
void Key(const Draw& draw, ImGuiKey key) {
    auto& io = ImGui::GetIO();
    io.AddKeyEvent(key, true); Frame(draw); io.AddKeyEvent(key, false); Frame(draw);
}
void SingleSelection() {
    Configure();
    md::ButtonGroupItem items[] = {{"Day"}, {"Disabled", nullptr, false}, {"Month"}, {"Year"}};
    md::ButtonGroupOptions options; options.width = 440.f;
    bool disabled = false; int selected = 0, changes = 0; ImGuiID focus_scope = 0;
    const Draw draw = [&] {
        CaptureIds("schedule", 4);
        focus_scope = ImGui::GetID("group-focus-scope"); ImGui::PushFocusScope(focus_scope);
        ImGui::BeginDisabled(disabled);
        if (md::ButtonGroup("schedule", &selected, items, 4, options)) ++changes;
        ImGui::EndDisabled(); ImGui::PopFocusScope();
        group_size = ImGui::GetItemRectSize();
    };
    Settle(draw); assert(selected == 0 && changes == 0);
    Click(draw, 1); assert(selected == 0 && changes == 0);
    Click(draw, 0); Key(draw, ImGuiKey_RightArrow);
    assert(selected == 2 && changes == 1 && GImGui->NavFocusScopeId == focus_scope);
    Key(draw, ImGuiKey_RightArrow); assert(selected == 3 && changes == 2);
    Key(draw, ImGuiKey_RightArrow); assert(selected == 0 && changes == 3);
    Key(draw, ImGuiKey_LeftArrow); assert(selected == 3 && changes == 4);
    Key(draw, ImGuiKey_Home); assert(selected == 0 && changes == 5);
    Key(draw, ImGuiKey_End); assert(selected == 3 && changes == 6);
    disabled = true; Key(draw, ImGuiKey_Home); Click(draw, 2);
    assert(selected == 3 && changes == 6);
    disabled = false; Click(draw, 2); assert(selected == 2 && changes == 7);
    selected = -1; Settle(draw); assert(selected == -1 && changes == 7);
    ImGui::SetFocusID(ids[0], group_window); ImGui::SetNavCursorVisibleAfterMove();
    Key(draw, ImGuiKey_Space);
    assert(selected == 0 && changes == 8);
    assert(std::abs(group_size.x - options.width) < .01f);
    md3test::DestroyContext();
    std::puts("PASS: connected group click/Space, arrow wrapping, Home/End, focus scopes and disabled selection");
}
void MultipleSelection() {
    Configure();
    md::ButtonGroupItem items[] = {{"Repeat"}, {"Repeat"}, {"Disabled", nullptr, false}};
    md::ButtonGroupOptions options; options.width = 330.f;
    std::uint64_t selected = 1; bool allow_empty = false; int changes = 0;
    const Draw draw = [&] {
        CaptureIds("formats", 3);
        if (md::MultiButtonGroup("formats", &selected, items, 3, allow_empty, options)) ++changes;
    };
    Settle(draw); assert(ids[0] != ids[1]);
    Click(draw, 0); assert(selected == 1 && changes == 0);
    Click(draw, 1); assert(selected == 3 && changes == 1);
    Key(draw, ImGuiKey_RightArrow); assert(selected == 3 && GImGui->NavId == ids[0]);
    Key(draw, ImGuiKey_Space); assert(selected == 2 && changes == 2);
    Key(draw, ImGuiKey_End); assert(selected == 2 && GImGui->NavId == ids[1]);
    Key(draw, ImGuiKey_Enter); assert(selected == 2 && changes == 2);
    allow_empty = true; Key(draw, ImGuiKey_Enter); assert(selected == 0 && changes == 3);
    selected = (std::uint64_t(1) << 63) | 1; allow_empty = false;
    Click(draw, 0); assert(selected == ((std::uint64_t(1) << 63) | 1));
    Click(draw, 1); assert(selected == ((std::uint64_t(1) << 63) | 3));
    Click(draw, 2); assert(selected == ((std::uint64_t(1) << 63) | 3));
    std::array<md::ButtonGroupItem, 64> many{};
    for (auto& item : many) item.label = "x";
    selected = 0; options.width = 768.f;
    const Draw boundary = [&] {
        CaptureIds("all-bits", 64);
        md::MultiButtonGroup("all-bits", &selected, many.data(), 64, true, options);
    };
    Settle(boundary); Click(boundary, 63); assert(selected == (std::uint64_t(1) << 63));
    Key(boundary, ImGuiKey_RightArrow); assert(GImGui->NavId == ids[0]);
    Key(boundary, ImGuiKey_Space); assert(selected == ((std::uint64_t(1) << 63) | 1));
    md3test::DestroyContext();
    std::puts("PASS: multi-group focus-only navigation, toggles, non-empty constraint, repeated labels and preserved external bits");
}
void AssertGeometry(int count, float width, float gap) {
    float sum = 0.f;
    for (int i = 0; i < count; ++i) {
        const auto rect = Bounds(i); sum += rect.GetWidth();
        assert(rect.GetWidth() > 0.f && std::isfinite(rect.Min.x) && std::isfinite(rect.Max.x));
        assert(rect.Min.x >= origin.x - .01f && rect.Max.x <= origin.x + width + .01f);
        if (i) assert(std::abs(rect.Min.x - Bounds(i - 1).Max.x - gap) < .01f);
    }
    assert(std::abs(sum + gap * (count - 1) - width) < .05f);
}
void WidthMotion() {
    Configure();
    const md::ButtonGroupItem items[] = {{"Day"}, {"Week"}, {"Month"}};
    md::ButtonGroupOptions options; options.width = 360.f;
    int selected = 0;
    const Draw draw = [&] {
        CaptureIds("motion", 3); md::ButtonGroup("motion", &selected, items, 3, options);
        group_size = ImGui::GetItemRectSize();
    };
    Settle(draw); const float resting = Bounds(1).GetWidth();
    const ImVec2 center = Bounds(1).GetCenter(); auto& io = ImGui::GetIO();
    io.AddMousePosEvent(center.x, center.y); Frame(draw);
    io.AddMouseButtonEvent(0, true); Frame(draw);
    for (int i = 0; i < 12; ++i) { Frame(draw); AssertGeometry(3, options.width, options.spacing); }
    assert(Bounds(1).GetWidth() > resting + 10.f);
    assert(Bounds(0).GetWidth() < resting && Bounds(2).GetWidth() < resting);
    assert(group_size.x == options.width && group_size.y == options.height);
    io.AddMouseButtonEvent(0, false); Frame(draw); assert(selected == 1);
    for (int i = 0; i < 40; ++i) Frame(draw);
    assert(std::abs(Bounds(1).GetWidth() - resting) < .01f);
    options.animated = false; Settle(draw);
    const ImVec2 first = Bounds(0).GetCenter();
    io.AddMousePosEvent(first.x, first.y); Frame(draw);
    io.AddMouseButtonEvent(0, true); Frame(draw); Frame(draw);
    AssertGeometry(3, options.width, options.spacing);
    assert(Bounds(0).GetWidth() > resting && Bounds(1).GetWidth() < resting);
    io.AddMouseButtonEvent(0, false); Frame(draw); Frame(draw);
    assert(std::abs(Bounds(0).GetWidth() - resting) < .01f);
    md3test::DestroyContext();
    std::puts("PASS: press expansion borrows adjacent widths without overlap or group resize and returns to rest");
}
void GeometryAndInvalidInput() {
    for (float scale : {.75f, 1.f, 2.f}) {
        Configure(); md::SetTheme(true, scale);
        const md::ButtonGroupItem items[] = {{"A label far wider than the available button", "@home"},
                                           {"Second label", "@settings"}, {"Third", nullptr, false}};
        int selected = 0;
        for (const float width : {12.f, 48.f, 120.f, 360.f})
            for (const auto style : {md::ButtonGroupStyle::Filled, md::ButtonGroupStyle::Tonal, md::ButtonGroupStyle::Outlined}) {
                md::ButtonGroupOptions options; options.width = width; options.style = style;
                const Draw draw = [&] {
                    CaptureIds("narrow", 3);
                    auto* list = ImGui::GetWindowDrawList(); const int first = list->VtxBuffer.Size;
                    md::ButtonGroup("narrow", &selected, items, 3, options);
                    group_size = ImGui::GetItemRectSize();
                    for (int i = first; i < list->VtxBuffer.Size; ++i) {
                        const auto pos = list->VtxBuffer[i].pos;
                        assert(std::isfinite(pos.x) && std::isfinite(pos.y));
                        assert(pos.x >= origin.x - 2.f && pos.x <= origin.x + width * scale + 2.f);
                    }
                };
                Settle(draw);
                AssertGeometry(3, width * scale, std::min(2.f * scale, width * scale / 6.f));
                assert(std::abs(group_size.y - options.height * scale) < .01f);
            }
        const Draw invalid = [&] {
            md::ButtonGroupOptions options;
            const ImVec2 start = ImGui::GetCursorScreenPos();
            assert(!md::ButtonGroup(nullptr, &selected, items, 3, options));
            assert(!md::ButtonGroup("invalid", nullptr, items, 3, options));
            assert(!md::ButtonGroup("invalid", &selected, nullptr, 3, options));
            assert(!md::ButtonGroup("invalid", &selected, items, 0, options));
            assert(!md::ButtonGroup("invalid", &selected, items, 65, options));
            options.width = std::numeric_limits<float>::quiet_NaN();
            assert(!md::ButtonGroup("invalid", &selected, items, 3, options));
            options.width = 100.f; options.height = -1.f;
            assert(!md::ButtonGroup("invalid", &selected, items, 3, options));
            assert(start.x == ImGui::GetCursorScreenPos().x && start.y == ImGui::GetCursorScreenPos().y);
        };
        Frame(invalid); md3test::DestroyContext();
    }
    std::puts("PASS: connected-group dp scaling, long-label clipping, narrow geometry, styles and invalid input");
}
} // namespace

void RunMd3ButtonGroupTests() {
    md3expressivetest::ScopedItemObserver observer(Observe);
    SingleSelection(); MultipleSelection(); WidthMotion(); GeometryAndInvalidInput();
}
