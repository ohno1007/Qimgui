#include <string>
#include "ui/main_ui.h"

#include "ui/ui.h"
#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "imgui.h"
#include "platform/ANativeWindowCreator.h"
#include "core/clipboard.h"
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

constexpr int kFpsPresets[] = { 0, 30, 60, 90, 120, 144 };
constexpr const char* kFpsLabels =
    u8"垂直同步\0" "30\0" "60\0" "90\0" "120\0" "144\0";

int FpsToIndex(int fps) {
    for (int i = 0; i < IM_ARRAYSIZE(kFpsPresets); ++i)
        if (kFpsPresets[i] == fps) return i;
    return 0;
}

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

    ImGui::PushStyleColor(ImGuiCol_FrameBg,        ImVec4(1, 1, 1, 0.07f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgHovered, ImVec4(1, 1, 1, 0.11f));
    ImGui::PushStyleColor(ImGuiCol_FrameBgActive,  ImVec4(1, 1, 1, 0.15f));
    ImGui::ColorEdit4(u8"取色器", (float*)&tint);
    ImGui::PopStyleColor(3);

    ImGui::Spacing();

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

                // Deliberately not persisted: it drives a second render pass,
                // and a setting that can only be reached through the UI it might
                // break is a setting that has to come back on its own.
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
    ImGui::SeparatorText(u8"剪贴板");
    {
        static char buf[512] = "";
        static std::string incoming;
        static bool        paste_pending = false;
        static int         last_len      = -1;

        struct Pending { std::string* text; bool* flag; };
        static Pending pending{&incoming, &paste_pending};
        auto on_edit = [](ImGuiInputTextCallbackData* d) -> int {
            Pending* p = (Pending*)d->UserData;
            if (*p->flag) {
                *p->flag = false;
                d->DeleteChars(0, d->BufTextLen);
                if (!p->text->empty()) d->InsertChars(0, p->text->c_str());
            }
            return 0;
        };

        ImGui::SetNextItemWidth(-FLT_MIN);
        ImGui::InputTextWithHint("##clip", u8"在这里编辑文本", buf, sizeof(buf),
                                 ImGuiInputTextFlags_CallbackAlways, on_edit, &pending);

        paste_pending = false;
        chrome::LastItem(14.0f);

        const bool pasted = ImGui::Button(ICON_FA_DOWNLOAD u8"  粘贴系统剪贴板",
                                          ImVec2(-FLT_MIN, 0));
        chrome::LastItem();
        ripple::TouchLastItem();
        if (pasted) {
            const char* t = clipboard::Get();
            incoming      = t ? t : "";
            last_len      = (int)incoming.size();
            paste_pending = true;
            std::snprintf(buf, sizeof(buf), "%s", incoming.c_str());
        }

        if (last_len >= 0) {
            if (clipboard::UsedSystem()) {
                if (last_len == 0) ImGui::TextDisabled(u8"系统剪贴板是空的");
                else               ImGui::TextDisabled(u8"系统剪贴板  ·  %d 个字符", last_len);
            } else {
                const char* why = clipboard::SystemError();
                ImGui::TextDisabled(u8"来自文件  ·  %d 个字符", last_len);
                if (why && *why) ImGui::TextDisabled(u8"系统剪贴板不可用：%s", why);
                ImGui::TextDisabled("%s", clipboard::Path());
            }
        } else {
            ImGui::TextDisabled(u8"从别处复制后点这里取过来");
        }
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
