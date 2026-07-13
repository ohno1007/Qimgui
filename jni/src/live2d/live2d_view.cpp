#include "live2d/live2d_view.h"
#include "live2d/live2d_allocator.h"
#include "live2d/live2d_model.h"
#include "live2d/live2d_embedded.h"

#include <CubismFramework.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include <Math/CubismMatrix44.hpp>

#include <android/log.h>
#include <dirent.h>
#include <cmath>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

// Diagnostics to a file (logcat filtering is unreliable on some ROMs).
static void L2DDiag(const char* fmt, ...) {
    FILE* f = std::fopen("/data/local/tmp/aimgui_live2d.txt", "a");
    if (!f) return;
    va_list ap; va_start(ap, fmt); std::vfprintf(f, fmt, ap); va_end(ap);
    std::fputc('\n', f);
    std::fclose(f);
}

#define LOG_TAG "AImGui_Live2D"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)

using namespace Live2D::Cubism::Framework;

namespace aimgui {
namespace live2d {

namespace {
Allocator      g_allocator;
CubismFramework::Option g_option;
Model*         g_model = nullptr;
bool           g_started = false;
Live2DVkContext g_ctx{};
int            g_width = 1;
int            g_height = 1;

// Presentation / interaction state.
float          g_expandT   = 1.0f;   // 0 = tiny ball, 1 = full character
float          g_lookX = 0, g_lookY = 0;   // gaze target (screen px)
bool           g_lookActive = false;
float          g_dragX = 0, g_dragY = 0;    // smoothed gaze in [-1,1]
float          g_reaction = 0.0f;           // seconds of tap-reaction left

constexpr float kReactionDur = 0.6f;
constexpr float kBallPx      = 200.0f;  // on-screen model height when collapsed
constexpr float kBallCenterY = 150.0f;  // screen-y of the collapsed character

inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}

// The character's on-screen centre (visible-region px) at expand factor t.
void ModelCenterScreen(float t, float* scx, float* scy) {
    *scx = g_width * 0.5f;   // horizontally centred in both states
    float big = g_height * 0.5f;
    *scy = kBallCenterY + (big - kBallCenterY) * t;
}
} // namespace

// Serve embedded assets (the Cubism Vulkan renderer's compiled SPIR-V shaders)
// by basename. The renderer requests paths like "FrameworkShaders/xxx.spv";
// the SDK's CreateShaderModule is patched to call this instead of std::ifstream
// (see the transient build patch). Returns nullptr if not embedded.
extern "C" const unsigned char* aimgui_l2d_load_asset(const char* path, unsigned* outSize) {
    const char* base = path;
    if (const char* slash = std::strrchr(path, '/')) base = slash + 1;
    unsigned sz = 0;
    const unsigned char* p = EmbeddedGet(base, &sz);
    if (!p) { L2DDiag("asset MISS: %s", path); if (outSize) *outSize = 0; return nullptr; }
    if (outSize) *outSize = sz;
    return p;
}

bool VkInit(const Live2DVkContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE) return false;
    if (g_started) return true;
    g_ctx = *ctx;
    { FILE* f = std::fopen("/data/local/tmp/aimgui_live2d.txt", "w"); if (f) std::fclose(f); }
    g_option.LogFunction = [](const char* msg) {
        __android_log_print(ANDROID_LOG_INFO, "AImGui_Cubism", "%s", msg);
        L2DDiag("cubism: %s", msg);
    };
    g_option.LoggingLevel = CubismFramework::Option::LogLevel_Verbose;

    CubismFramework::StartUp(&g_allocator, &g_option);
    CubismFramework::Initialize();

    // One-time renderer configuration, before any model is created.
    Rendering::CubismRenderer_Vulkan::SetConstantSettings(
        g_ctx.device, g_ctx.physicalDevice, g_ctx.commandPool, g_ctx.queue,
        g_ctx.imageCount, g_ctx.extent, g_ctx.modelView, g_ctx.colorFormat,
        g_ctx.depthFormat);
    // We render the model into an offscreen image that the UI composites over.
    Rendering::CubismRenderer_Vulkan::EnableChangeRenderTarget();

    g_started = true;
    LOGI("cubism framework started (vulkan)");
    L2DDiag("init: framework started (vulkan) extent=%ux%u fmt=%d depth=%d imgs=%u",
            g_ctx.extent.width, g_ctx.extent.height, (int)g_ctx.colorFormat,
            (int)g_ctx.depthFormat, g_ctx.imageCount);
    return true;
}

bool LoadModel(const char* dir, const char* model3json) {
    if (!g_started) return false;
    delete g_model;
    g_model = new Model();
    if (!g_model->LoadAssets(g_ctx, dir, model3json, g_width, g_height, /*embedded=*/false)) {
        delete g_model;
        g_model = nullptr;
        return false;
    }
    return true;
}

bool LoadEmbedded() {
    if (!g_started) return false;
    const char* m3 = EmbeddedFindModel3();
    L2DDiag("embedded model3 = %s (surface %dx%d)", m3 ? m3 : "<none>", g_width, g_height);
    if (!m3) return false;  // no model compiled in — fall back to disk
    delete g_model;
    g_model = new Model();
    bool ok = g_model->LoadAssets(g_ctx, nullptr, m3, g_width, g_height, /*embedded=*/true);
    L2DDiag("embedded load %s (hasModel=%d textures=%d)", ok ? "OK" : "FAILED",
            g_model->HasModel(), g_model->TextureCount());
    if (!ok) {
        delete g_model;
        g_model = nullptr;
        return false;
    }
    return true;
}

bool IsLoaded() { return g_model && g_model->Loaded(); }

void Note(const char* msg) { L2DDiag("%s", msg); }

namespace {
// Find the first "*.model3.json" directly inside `dir`. Returns "" if none.
std::string FindModel3(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        const char* n = e->d_name;
        const char* dot = std::strstr(n, ".model3.json");
        if (dot && dot[std::strlen(".model3.json")] == '\0') { found = n; break; }
    }
    closedir(d);
    return found;
}
} // namespace

bool AutoLoad(const char* root) {
    // Case 1: root itself contains the *.model3.json.
    std::string j = FindModel3(root);
    if (!j.empty()) return LoadModel(root, j.c_str());

    // Case 2: scan immediate subdirectories.
    DIR* d = opendir(root);
    if (!d) { LOGI("live2d: no model dir at %s", root); return false; }
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string sub = std::string(root) + "/" + e->d_name;
        std::string js = FindModel3(sub);
        if (!js.empty()) { closedir(d); return LoadModel(sub.c_str(), js.c_str()); }
    }
    closedir(d);
    LOGI("live2d: no *.model3.json found under %s", root);
    return false;
}

void Resize(int width, int height) {
    g_width = width > 0 ? width : 1;
    g_height = height > 0 ? height : 1;
}

void SetView(float expandT) { g_expandT = ClampF(expandT, 0.0f, 1.0f); }

void SetLookScreen(float x, float y, bool active) {
    g_lookX = x; g_lookY = y; g_lookActive = active;
}

void Poke() { g_reaction = kReactionDur; }

bool HitCollapsed(float x, float y) {
    float cx = g_width * 0.5f, cy = kBallCenterY;
    float hw = kBallPx * 0.40f, hh = kBallPx * 0.55f;
    return x >= cx - hw && x <= cx + hw && y >= cy - hh && y <= cy + hh;
}

void Update(float dt) {
    // Ease the gaze toward the tap point (or back to centre when inactive).
    float tx = 0.0f, ty = 0.0f;
    if (g_lookActive) {
        float scx, scy; ModelCenterScreen(g_expandT, &scx, &scy);
        float range = 0.5f * (g_width < g_height ? g_width : g_height);
        if (range < 1.0f) range = 1.0f;
        tx = ClampF((g_lookX - scx) / range, -1.0f, 1.0f);
        ty = ClampF((scy - g_lookY) / range, -1.0f, 1.0f);  // screen y is down
    }
    float k = ClampF(dt * 8.0f, 0.0f, 1.0f);
    g_dragX += (tx - g_dragX) * k;
    g_dragY += (ty - g_dragY) * k;

    if (g_reaction > 0.0f) g_reaction = g_reaction - dt < 0.0f ? 0.0f : g_reaction - dt;
    float react01 = g_reaction / kReactionDur;

    if (g_model) g_model->Update(dt, g_dragX, g_dragY, react01);
}

void Draw() {
    static int s_diagFrames = 0;
    if (!g_model || !g_model->Loaded()) return;

    // Point the (static) Cubism renderer at our offscreen model image. Done
    // every frame because SetRenderTarget mutates shared global state.
    Rendering::CubismRenderer_Vulkan::SetRenderTarget(
        g_ctx.modelImage, g_ctx.modelView, g_ctx.colorFormat, g_ctx.extent);

    // The model image is a square (side × side), but only the central
    // g_width × g_height of it is on screen. Cubism's VK vertex shader already
    // flips Y for Vulkan (pos.y = -pos.y), so the GL-style projection below
    // renders upright. The model lerps between a tiny "ball" at the top
    // (collapsed) and the full-screen character (expanded) via g_expandT.
    float side = static_cast<float>(g_ctx.extent.width > g_ctx.extent.height
                                        ? g_ctx.extent.width : g_ctx.extent.height);
    if (side <= 0.0f) side = 1.0f;

    const float kFill    = 0.85f;   // model fills ~85% of the shorter screen side
    const float kOffsetY = 0.0f;    // + up / - down nudge; tune on device
    float visMin = static_cast<float>(g_width < g_height ? g_width : g_height);
    if (visMin <= 0.0f) visMin = side;

    const float t = ClampF(g_expandT, 0.0f, 1.0f);
    float sBig   = kFill * visMin / side;          // on-screen height ≈ s*side px
    float sSmall = kBallPx / side;
    float s = sSmall + (sBig - sSmall) * t;

    // A little "pop" while reacting to a tap.
    float react01 = g_reaction / kReactionDur;
    float bounce = react01 > 0.0f ? 1.0f + 0.18f * std::sin((1.0f - react01) * 3.14159265f)
                                  : 1.0f;
    s *= bounce;

    float scx, scy; ModelCenterScreen(t, &scx, &scy);
    float cx = 2.0f * scx / side - 1.0f;
    float cy = 1.0f - 2.0f * scy / side;

    CubismMatrix44 projection;
    projection.Scale(s, s);
    projection.Translate(cx, cy + kOffsetY);

    if (s_diagFrames < 3) {
        L2DDiag("draw#%d side=%.0f vis=%dx%d s=%.3f c=(%.3f,%.3f)",
                s_diagFrames, side, g_width, g_height, s, cx, cy);
        s_diagFrames++;
    }

    g_model->Draw(projection);
}

void Shutdown() {
    // The model owns Vulkan texture images; make sure the GPU is done with them
    // before their destructors free the handles.
    if (g_started && g_ctx.device != VK_NULL_HANDLE) vkDeviceWaitIdle(g_ctx.device);
    delete g_model;
    g_model = nullptr;
    if (g_started) {
        CubismFramework::Dispose();
        g_started = false;
    }
}

} // namespace live2d
} // namespace aimgui
