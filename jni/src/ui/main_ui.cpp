#include <algorithm>
#include <string>
#include "ui/main_ui.h"
#include "ui/md3_showcase.h"
#include "ui/md3.h"

#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "core/config.h"
#include "core/haptics.h"
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

const PageItem kPages[] = {
    { Page::Dashboard,   ICON_FA_GAUGE,       u8"概览" },
    { Page::Widgets,     ICON_FA_SLIDERS,     u8"控件" },
    { Page::Window,      ICON_FA_WINDOW,      u8"窗口" },
    { Page::Performance, ICON_FA_BOLT,        u8"性能" },
    { Page::About,       ICON_FA_CIRCLE_INFO, u8"关于" },
};
const int kPagesCount = (int)(sizeof(kPages) / sizeof(kPages[0]));

namespace {

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

    chrome::Rect(barMin, barMax, -1.0f,
                 ImGui::IsItemHovered(), ImGui::IsItemActive());

    const ImU32 col = ImGui::GetColorU32(ImGuiCol_SliderGrab);
    ImDrawList* dl  = ImGui::GetWindowDrawList();
    dl->AddRectFilled(gMin, gMax, col, grab_h * 0.5f);

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
    // Keep the full gallery usable in the resizable Android content pane.
    const float available = ImGui::GetContentRegionAvail().x;
    const float logicalContentWidth = 760.0f;
    const float scale = std::max(0.65f, std::min(1.5f,
        (available - ImGui::GetStyle().ScrollbarSize) / (logicalContentWidth + 48.0f)));
    md3::SetTheme(md3::GetTheme().dark, scale);
    md3::PushTheme();
    ImGui::PushStyleColor(ImGuiCol_ChildBg, md3::GetTheme().surface);
    // Preserve readable touch targets at the minimum scale. Narrow panes can
    // scroll to every fixed-width sample instead of silently clipping controls.
    ImGui::SetNextWindowContentSize(ImVec2(logicalContentWidth * scale, 0));
    if (ImGui::BeginChild("MD3 gallery", ImVec2(0,0), ImGuiChildFlags_AlwaysUseWindowPadding,
                          ImGuiWindowFlags_HorizontalScrollbar))
        md3::Showcase();
    ImGui::EndChild();
    ImGui::PopStyleColor();
    md3::PopTheme();
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

                ImGui::BeginDisabled(!state->block_touch_ok);
                ImGui::Checkbox(u8"挡住触摸穿透", &state->block_touch);
                chrome::LastItemFrame(u8"挡住触摸穿透");
                ripple::TouchLastItem();
                ImGui::EndDisabled();
                if (!state->block_touch_ok) {
                    ImGui::TextDisabled(u8"拿不到 /dev/uinput，无法回注事件");
                } else if (state->block_touch) {
                    ImGui::TextDisabled(state->block_touch_on
                        ? u8"独占触摸屏中；点在窗口上不会穿到后面"
                        : u8"已请求，等待抓取");
                    ImGui::TextDisabled(u8"卡住 2 秒会自动放开，崩溃也会放开");
                }

                ImGui::BeginDisabled(!state->widget_glass_ok);
                ImGui::Checkbox(u8"控件也用玻璃", &state->widget_glass);
                chrome::LastItemFrame(u8"控件也用玻璃");
                ripple::TouchLastItem();
                ImGui::EndDisabled();
                if (!state->widget_glass_ok) {
                    ImGui::TextDisabled(u8"这个后端拿不到画面副本，控件维持描边");
                } else if (state->widget_glass) {
                    ImGui::TextDisabled(u8"控件折射它所在的那层玻璃，不是桌面");
                }

                ImGui::Checkbox(u8"镜像时对截屏隐藏窗口", &state->mirror_hides_window);
                chrome::LastItemFrame(u8"镜像时对截屏隐藏窗口");
                ripple::TouchLastItem();
                if (state->mirror_hides_window) {
                    ImGui::TextDisabled(u8"关掉就能截到窗口，但玻璃会套娃");
                } else {
                    ImGui::TextDisabled(u8"能被截屏了，玻璃可能发白");
                }
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
    ImGui::SeparatorText(u8"触感");
    {

        static bool has_vibrator = false;
        static bool probed        = false;
        if (!probed) {
            probed = true;
            Haptics probe;

            has_vibrator = probe.Init();
            probe.Shutdown();
        }

        ImGui::Checkbox(u8"震动反馈", &state->haptics_enabled);
        chrome::LastItemFrame(u8"震动反馈");
        ripple::TouchLastItem();
        if (!has_vibrator && state->haptics_enabled) {
            ImGui::TextDisabled(u8"本机没有可用的振动器");
        }
    }

    ImGui::Spacing();
    ImGui::SeparatorText(u8"弹窗");
    {

        static std::string last;
        const float w3 = (ImGui::GetContentRegionAvail().x -
                          ImGui::GetStyle().ItemSpacing.x * 2.0f) / 3.0f;

        if (ImGui::Button(ICON_FA_CIRCLE_INFO u8" 确认", ImVec2(w3, 0)))
            dialog::Open(dialog::KindConfirm, u8"确认操作",
                         u8"这条会以液体玻璃的形式挂在灵动岛下面，"
                         u8"由一块胶囊和两颗小胶囊组成，彼此粘连。");
        chrome::LastItem(); ripple::TouchLastItem();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_LOCK u8" 卡密", ImVec2(w3, 0)))
            dialog::Open(dialog::KindLicense, u8"输入授权卡密");
        chrome::LastItem(); ripple::TouchLastItem();
        ImGui::SameLine();
        if (ImGui::Button(ICON_FA_WAND u8" 自定义", ImVec2(w3, 0)))
            dialog::Open(dialog::KindCustom, u8"自定义弹窗",
                         u8"标题、正文和两颗按钮的文字都可以自己给。",
                         u8"好的", u8"算了");
        chrome::LastItem(); ripple::TouchLastItem();

        if (!g_ui.pending_exit) {
            const dialog::Result r = dialog::Take();
            if (r == dialog::ResultOk) {
                const char* key = dialog::Input();
                last = (key && *key) ? std::string(u8"确定 · ") + key : u8"确定";
            } else if (r == dialog::ResultCancel) {
                last = u8"取消";
            }
        }
        if (!last.empty()) ImGui::TextDisabled(u8"上次回答：%s", last.c_str());
    }

    ImGui::Spacing();
    ImGui::SeparatorText(u8"设置");
    ImGui::TextDisabled("%s", config::Path());

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
    ImGui::SeparatorText(u8"垂直同步");
    ImGui::TextDisabled(u8"由显示设备的 VSync 驱动帧率，避免软件限帧与 FIFO 冲突。");

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
    chrome::LastItem(14.0f);
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

}

void DrawPage(UiState* state, Page page) {
    switch (page) {
        case Page::Dashboard:   DrawDashboard(state);   break;
        case Page::Widgets:     DrawWidgets();          break;
        case Page::Window:      DrawWindow(state);      break;
        case Page::Performance: DrawPerformance(state); break;
        case Page::About:       DrawAbout();            break;
    }
}

}
