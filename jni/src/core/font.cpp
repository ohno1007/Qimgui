#include "font.h"

#include "font_data.h"
#include "imgui.h"

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <sys/stat.h>
#include <unistd.h>

#define LOG_TAG "AImGui_Font"
#define LOGI(fmt, ...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, fmt, ##__VA_ARGS__)
#define LOGW(fmt, ...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, fmt, ##__VA_ARGS__)

namespace {

constexpr const char* kFontDirs[] = {
    "/system/fonts",
    "/system/font",
    "/data/fonts",
    "/product/fonts",
    "/system_ext/fonts",
    "/mnt/system/system/fonts",
};

constexpr const char* kKnownNames[] = {

    "OPlusSans3-Regular.ttf",
    "OPlusSans-Regular.ttf",
    "OPlusSans.ttf",
    "OPPOSans-Regular.ttf",
    "OPPOSans-R.ttf",
    "OnePlusSans-Regular.ttf",
    "OPSansCN-Regular.ttf",
    "OPSans-Regular.ttf",
    "OPSansVF.ttf",

    "MiSans-Regular.ttf",
    "MiSans-Normal.ttf",
    "MiSans-Medium.ttf",
    "MiSans-Regular.otf",
    "MiSansVF.ttf",
    "MiSans-VF.ttf",
    "MiLanProVF.ttf",
    "MiLanPro_VF.ttf",

    "HarmonyOS_Sans_SC_Regular.ttf",
    "HarmonyOS_Sans_SC_Medium.ttf",
    "HarmonyOS_Sans_Regular.ttf",
    "HwChinese-Medium.ttf",
    "HwChinese-Regular.ttf",
    "HONOR Sans CN-Regular.ttf",
    "HONOR Sans-Regular.ttf",

    "VivoSansCN-Regular.ttf",
    "VivoFontTW-Regular.ttf",
    "HYQiHei.ttf",
    "HYQiHei-65.ttf",

    "SamsungOneUI-Regular.ttf",
    "SECCJK-Regular.ttc",

    "NotoSansCJK-Regular.ttc",
    "NotoSerifCJK-Regular.ttc",
    "NotoSansCJKsc-Regular.otf",
    "NotoSansCJKjp-Regular.otf",
    "NotoSansSC-Regular.otf",
    "NotoSansSC-Regular.ttf",
    "NotoSerifSC-Regular.otf",

    "DroidSansFallback.ttf",
    "DroidSansFallbackFull.ttf",
};

constexpr const char* kCJKHints[] = {
    "CJK", "cjk",
    "NotoSansSC", "NotoSerifSC", "NotoSansTC", "NotoSerifTC",
    "NotoSansHK", "NotoSerifHK",
    "MiSans", "misans", "MiLan",
    "HwChinese",
    "HarmonyOS_Sans_SC", "HarmonyOS_Sans_TC",
    "OPlusSans", "OPPOSans", "OnePlusSans", "OPSansCN",
    "VivoSansCN", "VivoFontTW",
    "Hans",
    "HYQiHei", "FangZhengHei",
    "DroidSansFallback",
    "Chinese", "chinese",
};

bool NameLooksCJK(const char* name) {
    for (const char* hint : kCJKHints) {
        if (std::strstr(name, hint)) return true;
    }
    return false;
}

bool LooksLikeRealFont(const char* path) {
    struct stat st;
    if (stat(path, &st) != 0) return false;
    return st.st_size >= 200 * 1024;
}

bool TryAddFontFromFile(const char* path, float SizePixels, bool merge) {
    if (!LooksLikeRealFont(path)) {
        LOGW("skip %s (too small to be a CJK font)", path);
        return false;
    }
    ImFontConfig cfg;
    cfg.SizePixels  = SizePixels;
    cfg.PixelSnapH  = true;
    cfg.OversampleH = 1;
    cfg.OversampleV = 1;

    cfg.MergeMode   = merge;

    static const ImWchar kIconExclude[] = {
        aimgui::font_data::kIconFirst, aimgui::font_data::kIconLast, 0 };
    if (merge) cfg.GlyphExcludeRanges = kIconExclude;
    cfg.Flags      |= ImFontFlags_NoLoadError;

    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, SizePixels, &cfg);
    if (!f) {
        LOGW("AddFontFromFileTTF rejected %s", path);
        return false;
    }
    LOGI("loaded CJK font: %s @ %.1fpx (dynamic atlas)", path, SizePixels);
    return true;
}

bool TryKnownNames(float SizePixels, bool merge) {
    char path[256];
    for (const char* dir : kFontDirs) {
        if (access(dir, R_OK) != 0) continue;
        for (const char* name : kKnownNames) {
            std::snprintf(path, sizeof(path), "%s/%s", dir, name);
            if (access(path, R_OK) != 0) continue;
            if (TryAddFontFromFile(path, SizePixels, merge)) return true;
        }
    }
    return false;
}

bool TryDirScan(float SizePixels, bool merge) {
    char path[256];
    for (const char* dir : kFontDirs) {
        DIR* d = opendir(dir);
        if (!d) continue;
        while (dirent* e = readdir(d)) {
            const char* n = e->d_name;
            if (n[0] == '.') continue;
            const char* dot = std::strrchr(n, '.');
            if (!dot) continue;
            if (std::strcmp(dot, ".ttc") != 0 &&
                std::strcmp(dot, ".ttf") != 0 &&
                std::strcmp(dot, ".otf") != 0) continue;
            if (!NameLooksCJK(n)) continue;
            std::snprintf(path, sizeof(path), "%s/%s", dir, n);
            if (access(path, R_OK) != 0) continue;
            if (TryAddFontFromFile(path, SizePixels, merge)) {
                closedir(d);
                return true;
            }
        }
        closedir(d);
    }
    return false;
}

}

namespace ImGui {

bool My_Android_LoadSystemFont(float SizePixels, bool merge) {
    if (TryKnownNames(SizePixels, merge)) return true;
    if (TryDirScan(SizePixels, merge))    return true;
    LOGW("no loadable CJK font found in any of /system/fonts /system/font "
         "/data/fonts /product/fonts /system_ext/fonts /mnt/system/system/fonts");
    return false;
}

}

namespace aimgui {

ImFont* LoadDefaultAndSystemCJKFont(float size_pixels) {
    ImGuiIO& io = ImGui::GetIO();

    ImFontConfig ui;
    ui.SizePixels = size_pixels;
    ui.PixelSnapH = true;

    ui.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)font_data::kUiFont,
                                   (int)sizeof(font_data::kUiFont),
                                   size_pixels, &ui);

    if (!ImGui::My_Android_LoadSystemFont(size_pixels, true))
        LOGW("no CJK font on this device; Chinese text will not render");

    ImFontConfig ic;
    ic.MergeMode            = true;
    ic.FontDataOwnedByAtlas = false;
    ic.PixelSnapH           = true;

    const float icon_px     = size_pixels * 0.84f;
    ic.SizePixels           = icon_px;
    ic.GlyphOffset          = ImVec2(0.0f, size_pixels * 0.11f);
    ic.GlyphMinAdvanceX     = icon_px;
    static const ImWchar kIconRange[] = {
        aimgui::font_data::kIconFirst, aimgui::font_data::kIconLast, 0 };
    ic.GlyphRanges = kIconRange;
    io.Fonts->AddFontFromMemoryTTF((void*)font_data::kIconFont,
                                   (int)sizeof(font_data::kIconFont),
                                   icon_px, &ic);

    LOGI("font: Inter %.0fpx + system CJK + Font Awesome %.0fpx",
         size_pixels, icon_px);
    return io.Fonts->Fonts.back();
}

}
