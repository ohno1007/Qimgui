// All sidebar nav entries and per-page body content live here, separate
// from the window framework in ui.cpp. To add a new page:
//   1. Append a value to enum class Page in ui/main_ui.h
//   2. Append a { Page::Foo, u8"标签" } row to kPages below
//   3. Write Draw<Foo>() and add a case in DrawPage().
//
// Page bodies render straight into the content child (caller already
// pushed it), so they can use ImGui::* layout APIs freely.

#include <string>
#include "ui/main_ui.h"

#include "ui/ui.h"          // UiState, aimgui::ripple::TouchLastItem
#include "ui/icons.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "core/screen_mirror.h"

#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <cfloat>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstring>

namespace aimgui {

// ─── Nav entries ─────────────────────────────────────────────────────────
const PageItem kPages[] = {
    { Page::Dashboard,   ICON_FA_GAUGE       u8"   概览" },
    { Page::Widgets,     ICON_FA_SLIDERS     u8"   控件" },
    { Page::Window,      ICON_FA_WINDOW      u8"   窗口" },
    { Page::Performance, ICON_FA_BOLT        u8"   性能" },
    { Page::About,       ICON_FA_CIRCLE_INFO u8"   关于" },
};
const int kPagesCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

namespace {

// ─── Page-local helpers ──────────────────────────────────────────────────
constexpr int kFpsPresets[] = { 0, 30, 60, 90, 120, 144 };
constexpr const char* kFpsLabels =
    u8"垂直同步\0" "30\0" "60\0" "90\0" "120\0" "144\0";

int FpsToIndex(int fps) {
    for (int i = 0; i < IM_ARRAYSIZE(kFpsPresets); ++i)
        if (kFpsPresets[i] == fps) return i;
    return 0;
}

// SliderFloat with a pill-shaped grab whose width hugs the formatted value
// text. The default rectangular grab is suppressed; we draw our own pill
// on top and center the number inside it.
bool SliderFloatGrabValue(const char* label, float* v, float v_min, float v_max,
                          const char* fmt = "%.3f") {
    ImGui::PushStyleColor(ImGuiCol_SliderGrab,       IM_COL32(0, 0, 0, 0));
    ImGui::PushStyleColor(ImGuiCol_SliderGrabActive, IM_COL32(0, 0, 0, 0));
    bool changed = ImGui::SliderFloat(label, v, v_min, v_max, "");
    ImGui::PopStyleColor(2);

    const ImVec2 totalMin = ImGui::GetItemRectMin();
    const ImVec2 totalMax = ImGui::GetItemRectMax();

    const char* hash = std::strstr(label, "##");
    const char* visible_end = hash ? hash : label + std::strlen(label);
    const ImVec2 label_size = ImGui::CalcTextSize(label, visible_end);
    const float  inner      = ImGui::GetStyle().ItemInnerSpacing.x;
    const float  bar_w      = (totalMax.x - totalMin.x) -
                              (label_size.x > 0.0f ? label_size.x + inner : 0.0f);
    const ImVec2 barMin = totalMin;
    const ImVec2 barMax(totalMin.x + bar_w, totalMax.y);

    char buf[32];
    std::snprintf(buf, sizeof(buf), fmt, *v);
    const ImVec2 ts = ImGui::CalcTextSize(buf);

    const float pad_x  = 18.0f;
    const float min_w  = ImGui::GetStyle().GrabMinSize;
    const float grab_w = (ts.x + pad_x > min_w) ? ts.x + pad_x : min_w;
    const float grab_h = ts.y + 10.0f;

    float t = (v_max != v_min) ? (*v - v_min) / (v_max - v_min) : 0.0f;
    if (t < 0.0f) t = 0.0f; else if (t > 1.0f) t = 1.0f;
    const float xL = barMin.x + grab_w * 0.5f;
    const float xR = barMax.x - grab_w * 0.5f;
    const float cx = xL + t * (xR - xL);
    const float cy = (barMin.y + barMax.y) * 0.5f;
    const ImVec2 gMin(cx - grab_w * 0.5f, cy - grab_h * 0.5f);
    const ImVec2 gMax(cx + grab_w * 0.5f, cy + grab_h * 0.5f);

    // The track is the step; the grab is the one solid thing on it, which is
    // where this material puts its highlights.
    chrome::Rect(barMin, barMax, -1.0f,
                 ImGui::IsItemHovered(), ImGui::IsItemActive());

    const ImU32 col = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(gMin, gMax, col, grab_h * 0.5f);
    // The pill is near-white now, so its own text has to be the dark one.
    dl->AddText(ImVec2(cx - ts.x * 0.5f, cy - ts.y * 0.5f),
                IM_COL32(14, 16, 20, 255), buf);

    return changed;
}

void KV(const char* key, const char* fmt, ...) {
    ImGui::TextDisabled("%s", key);
    ImGui::SameLine(200.0f);
    va_list args;
    va_start(args, fmt);
    ImGui::TextV(fmt, args);
    va_end(args);
}

// ─── Page bodies ─────────────────────────────────────────────────────────
void DrawDashboard(const UiState* state) {
    ImGui::SeparatorText(u8"概览");

    KV(u8"渲染后端",   "%s", state->renderer_name ? state->renderer_name : "?");
    KV(u8"ImGui 版本", "%s", ImGui::GetVersion());
    KV(u8"帧率",       "%.1f FPS  (%.2f ms)",
                       ImGui::GetIO().Framerate,
                       1000.0f / ImGui::GetIO().Framerate);
    KV(u8"防录屏",     "%s", state->permeate_record ? u8"已开启" : u8"已关闭");

    ImGui::Spacing();
    ImGui::SeparatorText(u8"提示");
    ImGui::TextWrapped(u8"按音量键折叠成灵动岛，再按一次展开。");
}

void DrawWidgets() {
    ImGui::SeparatorText(u8"基础控件");

    static int    counter = 0;
    static float  slider  = 0.5f;
    static bool   toggle  = false;
    static ImVec4 tint(0.40f, 0.70f, 1.00f, 1.0f);

    const bool hit = ImGui::Button(ICON_FA_WAND u8"  点我");
    chrome::LastItem();
    if (hit) counter++;
    ripple::TouchLastItem();
    ImGui::SameLine();
    ImGui::Text(u8"计数 = %d", counter);

    SliderFloatGrabValue(u8"滑块", &slider, 0.0f, 1.0f, "%.3f");
    ImGui::Checkbox  (u8"开关",   &toggle);
    chrome::LastItemFrame(u8"开关"); ripple::TouchLastItem();
    // ColorEdit's four drag fields are the one place a cleared FrameBg does not
    // work: they sit shoulder to shoulder, and with no fill at all they merge
    // into a single strip. Give them back the faintest of grounds locally.
    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(1, 1, 1, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.11f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(1, 1, 1, 0.15f));
    ImGui::ColorEdit4(u8"取色器", (float*)&tint);
    ImGui::PopStyleColor(3);

    ImGui::Spacing();

    // Collapsible list with animated expand/collapse + shared-element row
    // transition. The header drives an exponential-eased "open factor" t in
    // [0..1]; the items are then wrapped in a BeginChild whose height is
    // `t * full_height` so they slide in/out under the header. A single
    // rounded highlight rect lerps from the old selected row to the new one
    // and is drawn beneath the row text via DrawList channels.
    {
        static bool   list_open       = true;
        static float  list_t          = 1.0f;
        static float  list_full_h     = 160.0f;
        static int    selected         = 0;
        static ImVec2 anim_min         = ImVec2(0, 0);
        static ImVec2 anim_max         = ImVec2(0, 0);
        static bool   anim_initialized = false;

        ImGui::SetNextItemOpen(list_open, ImGuiCond_FirstUseEver);
        ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
        ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0, 0, 0, 0));
        list_open = ImGui::CollapsingHeader(u8"列表");
        ImGui::PopStyleColor(3);
        chrome::LastItem();
        ripple::TouchLastItem();

        const float dt    = ImGui::GetIO().DeltaTime;
        const float alpha = 1.0f - std::exp(-14.0f * dt);
        list_t += ((list_open ? 1.0f : 0.0f) - list_t) * alpha;

        if (list_t > 0.005f) {
            const float child_h = list_full_h * list_t;
            ImGui::BeginChild("##list_content", ImVec2(0, child_h),
                              ImGuiChildFlags_None,
                              ImGuiWindowFlags_NoScrollbar);

            const char* items[] = { u8"第一项", u8"第二项", u8"第三项", u8"第四项" };
            const int N = IM_ARRAYSIZE(items);

            ImDrawList* dl = ImGui::GetWindowDrawList();
            dl->ChannelsSplit(2);
            dl->ChannelsSetCurrent(1);

            ImGui::PushStyleColor(ImGuiCol_Header,        IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderHovered, IM_COL32(0, 0, 0, 0));
            ImGui::PushStyleColor(ImGuiCol_HeaderActive,  IM_COL32(0, 0, 0, 0));

            const float y_start = ImGui::GetCursorPosY();
            ImVec2 sel_min(0, 0), sel_max(0, 0);
            for (int i = 0; i < N; ++i) {
                if (ImGui::Selectable(items[i], selected == i)) selected = i;
                ripple::TouchLastItem();
                if (selected == i) {
                    sel_min = ImGui::GetItemRectMin();
                    sel_max = ImGui::GetItemRectMax();
                }
            }
            const float y_end = ImGui::GetCursorPosY();

            ImGui::PopStyleColor(3);

            dl->ChannelsSetCurrent(0);
            if (sel_max.y > sel_min.y) {
                if (!anim_initialized) {
                    anim_min = sel_min;
                    anim_max = sel_max;
                    anim_initialized = true;
                } else {
                    const float a2 = 1.0f - std::exp(-15.0f * dt);
                    anim_min.x += (sel_min.x - anim_min.x) * a2;
                    anim_min.y += (sel_min.y - anim_min.y) * a2;
                    anim_max.x += (sel_max.x - anim_max.x) * a2;
                    anim_max.y += (sel_max.y - anim_max.y) * a2;
                }
                // Same capsule the nav column uses: the accent lives inside the
                // step rather than being the step. Channel 0 already puts this
                // under the labels, so the wash cannot touch them.
                const float rr = (anim_max.y - anim_min.y) * 0.5f;
                dl->AddRectFilled(anim_min, anim_max,
                                  ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 0.18f)), rr);
                chrome::Rect(anim_min, anim_max, -1.0f, false, true);
            }
            dl->ChannelsMerge();

            if (list_t > 0.99f && y_end > y_start) {
                list_full_h = y_end - y_start;
            }
            ImGui::EndChild();

            const float spacing_y = ImGui::GetStyle().ItemSpacing.y;
            ImGui::SetCursorPosY(ImGui::GetCursorPosY() - (1.0f - list_t) * spacing_y);
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(u8"进度");
    static float progress = 0.0f;
    progress += ImGui::GetIO().DeltaTime * 0.15f;
    if (progress > 1.0f) progress -= 1.0f;
    ImGui::ProgressBar(progress, ImVec2(-1, 0));
    chrome::LastItem();
}

void DrawWindow(UiState* state) {
    ImGui::SeparatorText(u8"窗口表面");

    bool perm = state->permeate_record;
    const bool perm_hit = ImGui::Checkbox(u8"防录屏(对屏幕录制 / 投屏隐藏)", &perm);
    chrome::LastItemFrame(u8"防录屏(对屏幕录制 / 投屏隐藏)");
    if (perm_hit) {
        state->request_permeate_toggle = true;
    }
    ripple::TouchLastItem();
    ImGui::SameLine();
    ImGui::TextDisabled("[%s]", state->permeate_record ? u8"已开启" : u8"已关闭");

    ImGui::Spacing();
    ImGui::SeparatorText(u8"辉光");
    SliderFloatGrabValue(u8"辉光强度", &state->bloom_intensity, 0.0f, 2.5f, "%.2f");

    ImGui::Spacing();
    ImGui::SeparatorText(u8"背景");
    {
        static std::string missing;
        static bool        probed = false;
        static bool        ok     = false;
        if (!probed) {
            probed = true;
            ok = android::ANativeWindowCreator::ScreenCaptureSupported(&missing);
            if (ok && !ScreenMirror::Available()) {
                ok = false;
                missing = u8"libmediandk (AImageReader)";
            }
        }
        if (ok) {
            ImGui::Checkbox(u8"液体玻璃背景", &state->screen_mirror);
            chrome::LastItemFrame(u8"液体玻璃背景");
            ripple::TouchLastItem();
            if (state->screen_mirror) {
                SliderFloatGrabValue(u8"通透度", &state->glass_clarity,
                                     0.0f, 0.35f, "%.2f");
            }
            if (state->screen_mirror_running) {
                ImGui::TextDisabled(u8"%dx%d  ·  %llu 帧",
                                    state->screen_mirror_w, state->screen_mirror_h,
                                    (unsigned long long)state->screen_mirror_frames);
            } else if (state->screen_mirror) {
                ImGui::TextColored(ImVec4(1.0f, 0.5f, 0.4f, 1.0f), u8"启动失败");
            }
        } else {
            ImGui::TextDisabled(u8"本机不可用：缺少 %s", missing.c_str());
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(u8"主题");
    static int theme = 0;
    const bool theme_hit = ImGui::Combo(u8"##theme", &theme, u8"深色\0浅色\0经典\0");
    chrome::LastItemFrame(u8"##theme");
    if (theme_hit) {
        switch (theme) {
            case 0: ImGui::StyleColorsDark();    break;
            case 1: ImGui::StyleColorsLight();   break;
            case 2: ImGui::StyleColorsClassic(); break;
        }
        // StyleColorsX puts ImGui's own slab colours back, which would leave
        // flat fills sitting under every control's step.
        ApplyGlassPalette();
    }
    ripple::TouchLastItem();

#ifdef AIMGUI_LIVE2D
    ImGui::Spacing();
    ImGui::SeparatorText(u8"Live2D 小人");
    SliderFloatGrabValue(u8"小人大小", &state->ball_scale, 0.4f, 3.0f, "%.2f");
    const bool speak = ImGui::Button(ICON_FA_COMMENT u8"  让他说话", ImVec2(-FLT_MIN, 0));
    chrome::LastItem();
    if (speak) {
        live2d::Speak();
    }
    ripple::TouchLastItem();
#endif
}

void DrawPerformance(UiState* state) {
    ImGui::SeparatorText(u8"帧率限制");

    int fps_idx = FpsToIndex(state->target_fps);
    const bool fps_hit = ImGui::Combo(u8"目标帧率", &fps_idx, kFpsLabels);
    chrome::LastItemFrame(u8"目标帧率");
    if (fps_hit) {
        state->target_fps = kFpsPresets[fps_idx];
    }
    ripple::TouchLastItem();

    ImGui::Spacing();
    KV(u8"当前帧率", "%.1f FPS", ImGui::GetIO().Framerate);
    KV(u8"帧时间",   "%.2f ms",  1000.0f / ImGui::GetIO().Framerate);

    ImGui::Spacing();
    ImGui::SeparatorText(u8"帧率曲线");

    constexpr int N = 240;
    static float history[N] = {};
    static int   offset     = 0;
    history[offset] = ImGui::GetIO().Framerate;
    offset = (offset + 1) % N;

    char overlay[32];
    std::snprintf(overlay, sizeof(overlay), "%.1f", ImGui::GetIO().Framerate);
    ImGui::PlotLines("##fps_plot", history, N, offset, overlay,
                     0.0f, 165.0f, ImVec2(-1, 90));
    chrome::LastItem(14.0f);   // a plot is a panel, not a capsule
}

void DrawAbout() {
    ImGui::SeparatorText("AImGui");
    ImGui::TextWrapped(u8"一个极简的 Dear ImGui Android ARM64 ELF —— "
                       u8"无 JNI、无 APK、无 Activity，直接以原生二进制运行在 SurfaceFlinger 之上。");

    ImGui::Spacing();
    ImGui::SeparatorText(u8"特性");
    ImGui::BulletText(u8"Dear ImGui %s", ImGui::GetVersion());
    ImGui::BulletText(u8"Vulkan + OpenGL ES 3 自动回落");
    ImGui::BulletText(u8"VSync 锁帧的弹簧帧率器，几乎不烧 CPU");
    ImGui::BulletText(u8"只读触摸 (不创建 /dev/uinput，不影响系统)");
    ImGui::BulletText(u8"防录屏 SurfaceFlinger 标志");
    ImGui::BulletText(u8"音量键折叠/展开灵动岛");
    ImGui::BulletText(u8"自动加载系统中文字体 (NotoSansCJK / MiSans / HwChinese ...)");
}

} // namespace

void DrawPage(UiState* state, Page page) {
    switch (page) {
        case Page::Dashboard:   DrawDashboard(state);   break;
        case Page::Widgets:     DrawWidgets();          break;
        case Page::Window:      DrawWindow(state);      break;
        case Page::Performance: DrawPerformance(state); break;
        case Page::About:       DrawAbout();            break;
    }
}

} // namespace aimgui
