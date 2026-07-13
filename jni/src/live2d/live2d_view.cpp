#include "live2d/live2d_view.h"
#include "live2d/live2d_allocator.h"
#include "live2d/live2d_model.h"
#include "live2d/live2d_embedded.h"
#include "live2d/live2d_audio.h"

#include <CubismFramework.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include <Math/CubismMatrix44.hpp>

#include <android/log.h>
#include <dirent.h>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>

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

// The character is only shown as the collapsed floating "ball": it lives at a
// draggable screen position and fades out as the window expands.
float          g_expandT   = 0.0f;   // 0 = ball shown, 1 = window (no model)
float          g_ballX = 140.0f, g_ballY = 260.0f;  // ball centre (screen px)
float          g_ballScale = 1.0f;                  // UI size multiplier
float          g_lookX = 0, g_lookY = 0;   // gaze target (screen px)
bool           g_lookActive = false;
float          g_dragX = 0, g_dragY = 0;    // smoothed gaze in [-1,1]
float          g_reaction = 0.0f;           // seconds of tap-reaction left

constexpr float kReactionDur = 0.6f;
constexpr float kBallPx      = 200.0f;  // on-screen model height (the ball)

inline float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : (v > hi ? hi : v);
}
inline float SmoothStep(float e0, float e1, float x) {
    float u = ClampF((x - e0) / (e1 - e0), 0.0f, 1.0f);
    return u * u * (3.0f - 2.0f * u);
}
} // namespace

// The Cubism Vulkan renderer loads its compiled SPIR-V by relative path; the
// SDK is patched to call this, serving the embedded blobs by basename.
extern "C" const unsigned char* aimgui_l2d_load_asset(const char* path, unsigned* outSize) {
    const char* base = path;
    if (const char* slash = std::strrchr(path, '/')) base = slash + 1;
    unsigned sz = 0;
    const unsigned char* p = EmbeddedGet(base, &sz);
    if (outSize) *outSize = p ? sz : 0;
    return p;
}

bool VkInit(const Live2DVkContext* ctx) {
    if (!ctx || ctx->device == VK_NULL_HANDLE) return false;
    if (g_started) return true;
    g_ctx = *ctx;
    g_option.LogFunction = [](const char* msg) {
        __android_log_print(ANDROID_LOG_INFO, "AImGui_Cubism", "%s", msg);
    };
    g_option.LoggingLevel = CubismFramework::Option::LogLevel_Warning;

    CubismFramework::StartUp(&g_allocator, &g_option);
    CubismFramework::Initialize();

    // One-time renderer configuration, before any model is created. The model
    // is rendered into an offscreen image that the UI composites over.
    // (Cubism 5 SDK r.2+ renamed SetConstantSettings → InitializeConstantSettings;
    // the argument list is unchanged.)
    Rendering::CubismRenderer_Vulkan::InitializeConstantSettings(
        g_ctx.device, g_ctx.physicalDevice, g_ctx.commandPool, g_ctx.queue,
        g_ctx.imageCount, g_ctx.extent, g_ctx.modelView, g_ctx.colorFormat,
        g_ctx.depthFormat);
    Rendering::CubismRenderer_Vulkan::EnableChangeRenderTarget();

    g_started = true;
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
    if (!m3) return false;  // no model compiled in — fall back to disk
    delete g_model;
    g_model = new Model();
    if (!g_model->LoadAssets(g_ctx, nullptr, m3, g_width, g_height, /*embedded=*/true)) {
        delete g_model;
        g_model = nullptr;
        return false;
    }
    return true;
}

bool IsLoaded() { return g_model && g_model->Loaded(); }

namespace {
// First "*.model3.json" directly inside `dir`, or "" if none.
std::string FindModel3(const std::string& dir) {
    DIR* d = opendir(dir.c_str());
    if (!d) return "";
    std::string found;
    while (dirent* e = readdir(d)) {
        const char* dot = std::strstr(e->d_name, ".model3.json");
        if (dot && dot[std::strlen(".model3.json")] == '\0') { found = e->d_name; break; }
    }
    closedir(d);
    return found;
}
} // namespace

bool AutoLoad(const char* root) {
    std::string j = FindModel3(root);
    if (!j.empty()) return LoadModel(root, j.c_str());

    DIR* d = opendir(root);
    if (!d) return false;
    while (dirent* e = readdir(d)) {
        if (e->d_name[0] == '.') continue;
        std::string sub = std::string(root) + "/" + e->d_name;
        std::string js = FindModel3(sub);
        if (!js.empty()) { closedir(d); return LoadModel(sub.c_str(), js.c_str()); }
    }
    closedir(d);
    return false;
}

void Resize(int width, int height) {
    g_width = width > 0 ? width : 1;
    g_height = height > 0 ? height : 1;
}

void SetView(float expandT) { g_expandT = ClampF(expandT, 0.0f, 1.0f); }
void SetBall(float x, float y) { g_ballX = x; g_ballY = y; }
void SetBallScale(float scale) { g_ballScale = ClampF(scale, 0.1f, 5.0f); }
void SetLookScreen(float x, float y, bool active) {
    g_lookX = x; g_lookY = y; g_lookActive = active;
}

// Play a voice line. Disk clips in /data/local/tmp/live2d_voice/*.wav override
// (rotating through them); otherwise the voice embedded in the binary is used.
void Speak() {
    std::vector<std::string> wavs;
    if (DIR* d = opendir("/data/local/tmp/live2d_voice")) {
        while (dirent* e = readdir(d)) {
            size_t ln = std::strlen(e->d_name);
            if (ln > 4 && (std::strcmp(e->d_name + ln - 4, ".wav") == 0 ||
                           std::strcmp(e->d_name + ln - 4, ".WAV") == 0))
                wavs.push_back(std::string("/data/local/tmp/live2d_voice/") + e->d_name);
        }
        closedir(d);
    }
    if (!wavs.empty()) {
        static unsigned s_i = 0;
        audio::PlayFile(wavs[s_i++ % wavs.size()].c_str());
        return;
    }
    unsigned sz = 0;
    if (const unsigned char* p = EmbeddedGet("voice.wav", &sz)) audio::PlayMemory(p, sz);
}

void Poke() {
    g_reaction = kReactionDur;
    Speak();
}

bool HitCollapsed(float x, float y) {
    float hw = kBallPx * g_ballScale * 0.40f, hh = kBallPx * g_ballScale * 0.55f;
    return x >= g_ballX - hw && x <= g_ballX + hw &&
           y >= g_ballY - hh && y <= g_ballY + hh;
}

void Update(float dt) {
    // Ease the gaze toward the tap point (or back to centre when inactive).
    float tx = 0.0f, ty = 0.0f;
    if (g_lookActive) {
        float range = 0.35f * (g_width < g_height ? g_width : g_height);
        if (range < 1.0f) range = 1.0f;
        tx = ClampF((g_lookX - g_ballX) / range, -1.0f, 1.0f);
        ty = ClampF((g_ballY - g_lookY) / range, -1.0f, 1.0f);  // screen y is down
    }
    float k = ClampF(dt * 8.0f, 0.0f, 1.0f);
    g_dragX += (tx - g_dragX) * k;
    g_dragY += (ty - g_dragY) * k;

    if (g_reaction > 0.0f) g_reaction = g_reaction - dt < 0.0f ? 0.0f : g_reaction - dt;

    if (g_model) g_model->Update(dt, g_dragX, g_dragY, g_reaction / kReactionDur, audio::Rms());
}

void Draw() {
    if (!g_model || !g_model->Loaded()) return;

    // Point the (static) Cubism renderer at our offscreen model image each
    // frame (SetRenderTarget mutates shared global state).
    Rendering::CubismRenderer_Vulkan::SetRenderTarget(
        g_ctx.modelImage, g_ctx.modelView, g_ctx.colorFormat, g_ctx.extent);

    // The model image is a square (side × side); Cubism's VK vertex shader
    // flips Y, so the GL-style projection renders upright. The character sits
    // at the draggable ball position and shrinks away as the window expands
    // (Cubism clears the target each frame, so at ~0 scale it reads empty).
    float side = static_cast<float>(g_ctx.extent.width > g_ctx.extent.height
                                        ? g_ctx.extent.width : g_ctx.extent.height);
    if (side <= 0.0f) side = 1.0f;

    float fade = 1.0f - SmoothStep(0.0f, 0.6f, g_expandT);  // 1 collapsed → 0 open
    float s = (kBallPx * g_ballScale * (fade > 0.001f ? fade : 0.001f)) / side;

    float react01 = g_reaction / kReactionDur;             // tap "pop"
    if (react01 > 0.0f) s *= 1.0f + 0.18f * std::sin((1.0f - react01) * 3.14159265f);

    float cx = 2.0f * g_ballX / side - 1.0f;
    float cy = 1.0f - 2.0f * g_ballY / side;

    CubismMatrix44 projection;
    projection.Scale(s, s);
    projection.Translate(cx, cy);
    g_model->Draw(projection);
}

void Shutdown() {
    audio::Stop();
    // Make sure the GPU is done with the model's textures before they free.
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
