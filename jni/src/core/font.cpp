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

// Ordered by likelihood per OEM. AOSP fonts come last because on heavily
// re-skinned Chinese OEM ROMs (ColorOS, MIUI, HarmonyOS, ...) the original
// NotoSansCJK file is occasionally a stripped stub that stb_truetype can no
// longer parse — we'd rather use a working OEM font in that case.
constexpr const char* kKnownNames[] = {
    // OPPO / OnePlus (ColorOS / OxygenOS)
    "OPlusSans3-Regular.ttf",
    "OPlusSans-Regular.ttf",
    "OPlusSans.ttf",
    "OPPOSans-Regular.ttf",
    "OPPOSans-R.ttf",
    "OnePlusSans-Regular.ttf",
    "OPSansCN-Regular.ttf",
    "OPSans-Regular.ttf",
    "OPSansVF.ttf",
    // Xiaomi / Redmi (MIUI / HyperOS)
    "MiSans-Regular.ttf",
    "MiSans-Normal.ttf",
    "MiSans-Medium.ttf",
    "MiSans-Regular.otf",
    "MiSansVF.ttf",
    "MiSans-VF.ttf",
    "MiLanProVF.ttf",
    "MiLanPro_VF.ttf",
    // Huawei / Honor (HarmonyOS / MagicOS)
    "HarmonyOS_Sans_SC_Regular.ttf",
    "HarmonyOS_Sans_SC_Medium.ttf",
    "HarmonyOS_Sans_Regular.ttf",
    "HwChinese-Medium.ttf",
    "HwChinese-Regular.ttf",
    "HONOR Sans CN-Regular.ttf",
    "HONOR Sans-Regular.ttf",
    // Vivo (OriginOS / FuntouchOS)
    "VivoSansCN-Regular.ttf",
    "VivoFontTW-Regular.ttf",
    "HYQiHei.ttf",
    "HYQiHei-65.ttf",
    // Samsung
    "SamsungOneUI-Regular.ttf",
    "SECCJK-Regular.ttc",
    // AOSP / Pixel (last because of stub-file risk on OEM ROMs)
    "NotoSansCJK-Regular.ttc",
    "NotoSerifCJK-Regular.ttc",
    "NotoSansCJKsc-Regular.otf",
    "NotoSansCJKjp-Regular.otf",
    "NotoSansSC-Regular.otf",
    "NotoSansSC-Regular.ttf",
    "NotoSerifSC-Regular.otf",
    // Legacy
    "DroidSansFallback.ttf",
    "DroidSansFallbackFull.ttf",
};

// Substrings hinting that a font file likely contains CJK glyphs. Kept
// specific to avoid matching Latin-only siblings like NotoSans-Regular.ttf,
// DroidSans.ttf or HarmonyOS_Sans (the non-SC variant).
constexpr const char* kCJKHints[] = {
    "CJK", "cjk",
    "NotoSansSC", "NotoSerifSC", "NotoSansTC", "NotoSerifTC",
    "NotoSansHK", "NotoSerifHK",
    "MiSans", "misans", "MiLan",
    "HwChinese",
    "HarmonyOS_Sans_SC", "HarmonyOS_Sans_TC",
    "OPlusSans", "OPPOSans", "OnePlusSans", "OPSansCN",
    "VivoSansCN", "VivoFontTW",
    "Hans",  // SourceHanSans, NotoSansHans
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

// Skip obviously-bogus stubs (< 200KB can't fit any reasonable CJK font).
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
    // Merged behind the embedded Latin face, so this font only ever supplies
    // the codepoints that one does not have — in practice, Chinese. Device
    // fonts vary wildly in quality and metrics; letting one of them set the
    // Latin text of the whole UI is what made the type look different per phone.
    cfg.MergeMode   = merge;
    // OEM fonts routinely put their own artwork in the Private Use Area, and
    // being merged ahead of the icon font they would win any codepoint they
    // happen to share with it — a settings gear replaced by some vendor's
    // emoji. Keep that range off this source entirely.
    static const ImWchar kIconExclude[] = {
        aimgui::font_data::kIconFirst, aimgui::font_data::kIconLast, 0 };
    if (merge) cfg.GlyphExcludeRanges = kIconExclude;
    cfg.Flags      |= ImFontFlags_NoLoadError; // don't assert on parse failure

    ImFont* f = ImGui::GetIO().Fonts->AddFontFromFileTTF(path, SizePixels, &cfg);
    if (!f) {
        LOGW("AddFontFromFileTTF rejected %s", path);
        return false;
    }
    LOGI("loaded CJK font: %s @ %.1fpx (dynamic atlas)", path, SizePixels);
    return true;
}

// Try every known filename across every dir. First one that ImGui actually
// accepts wins.
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

// Last-resort: scan every font dir for any .ttc/.ttf/.otf whose name looks
// CJK, and try to load them in turn.
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

} // namespace

namespace ImGui {

// Walks the common Android font directories and asks ImGui to load each
// known-CJK font in turn until one is accepted by the stb_truetype loader.
// ImGui 1.92's dynamic font atlas (ImGuiBackendFlags_RendererHasTextures,
// which both imgui_impl_opengl3 and imgui_impl_vulkan set) rasterizes glyphs
// on demand, so no glyph_ranges argument is necessary — any Unicode codepoint
// present in the font's cmap will render.
bool My_Android_LoadSystemFont(float SizePixels, bool merge) {
    if (TryKnownNames(SizePixels, merge)) return true;
    if (TryDirScan(SizePixels, merge))    return true;
    LOGW("no loadable CJK font found in any of /system/fonts /system/font "
         "/data/fonts /product/fonts /system_ext/fonts /mnt/system/system/fonts");
    return false;
}

} // namespace ImGui

namespace aimgui {

ImFont* LoadDefaultAndSystemCJKFont(float size_pixels) {
    ImGuiIO& io = ImGui::GetIO();

    // Three sources merged into one font, in the order they are asked.
    //
    //  1. Inter, embedded. It owns Latin, so numerals, labels and units look
    //     the same on every device instead of inheriting whatever the OEM
    //     happened to ship.
    //  2. The device's CJK font, which is only ever reached for the codepoints
    //     Inter does not have — Chinese. Embedding one is not an option: a
    //     usable CJK face is tens of megabytes against an 857KB binary.
    //  3. Font Awesome, embedded, for jni/include/ui/icons.h.
    ImFontConfig ui;
    ui.SizePixels = size_pixels;
    ui.PixelSnapH = true;
    // The arrays are constexpr, so the atlas must not try to free them. Since
    // 1.92 the data also has to outlive the atlas, which static storage does.
    ui.FontDataOwnedByAtlas = false;
    io.Fonts->AddFontFromMemoryTTF((void*)font_data::kUiFont,
                                   (int)sizeof(font_data::kUiFont),
                                   size_pixels, &ui);

    if (!ImGui::My_Android_LoadSystemFont(size_pixels, /*merge=*/true))
        LOGW("no CJK font on this device; Chinese text will not render");

    ImFontConfig ic;
    ic.MergeMode            = true;
    ic.FontDataOwnedByAtlas = false;
    ic.PixelSnapH           = true;
    // Icons are drawn a little smaller than the text and dropped onto its
    // baseline, and given a fixed advance so a column of them lines up.
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

} // namespace aimgui
