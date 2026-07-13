#include "live2d/live2d_view.h"
#include "live2d/live2d_allocator.h"
#include "live2d/live2d_model.h"
#include "live2d/live2d_embedded.h"

#include <CubismFramework.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Math/CubismMatrix44.hpp>

#include <GLES3/gl3.h>
#include <android/log.h>
#include <dirent.h>
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
int            g_width = 1;
int            g_height = 1;

// Cubism SDK 5's GL renderer loads its shaders through this file loader. We
// serve them from the embedded blob, matching by basename (the renderer may
// request "FrameworkShaders/xxx.frag" or just "xxx.frag").
csmByte* FileLoader(const std::string filePath, csmSizeInt* outSize) {
    std::string base = filePath;
    size_t slash = base.find_last_of("/\\");
    if (slash != std::string::npos) base = base.substr(slash + 1);
    unsigned sz = 0;
    const unsigned char* p = EmbeddedGet(base.c_str(), &sz);
    if (!p) { L2DDiag("fileloader MISS: %s", filePath.c_str()); if (outSize) *outSize = 0; return nullptr; }
    csmByte* buf = static_cast<csmByte*>(std::malloc(sz));
    if (buf) { std::memcpy(buf, p, sz); if (outSize) *outSize = static_cast<csmSizeInt>(sz); }
    return buf;
}
void BytesReleaser(csmByte* b) { std::free(b); }
} // namespace

bool Init() {
    if (g_started) return true;
    { FILE* f = std::fopen("/data/local/tmp/aimgui_live2d.txt", "w"); if (f) std::fclose(f); }
    g_option.LogFunction = [](const char* msg) {
        __android_log_print(ANDROID_LOG_INFO, "AImGui_Cubism", "%s", msg);
        L2DDiag("cubism: %s", msg);
    };
    g_option.LoggingLevel = CubismFramework::Option::LogLevel_Verbose;
    g_option.LoadFileFunction = &FileLoader;       // serve embedded GL shaders
    g_option.ReleaseBytesFunction = &BytesReleaser;

    CubismFramework::StartUp(&g_allocator, &g_option);
    CubismFramework::Initialize();
    g_started = true;
    LOGI("cubism framework started");
    L2DDiag("init: framework started");
    return true;
}

bool LoadModel(const char* dir, const char* model3json) {
    if (!g_started && !Init()) return false;
    delete g_model;
    g_model = new Model();
    if (!g_model->LoadAssets(dir, model3json, g_width, g_height, /*embedded=*/false)) {
        delete g_model;
        g_model = nullptr;
        return false;
    }
    return true;
}

bool LoadEmbedded() {
    if (!g_started && !Init()) return false;
    const char* m3 = EmbeddedFindModel3();
    L2DDiag("embedded model3 = %s (surface %dx%d)", m3 ? m3 : "<none>", g_width, g_height);
    if (!m3) return false;  // no model compiled in — fall back to disk
    delete g_model;
    g_model = new Model();
    bool ok = g_model->LoadAssets(nullptr, m3, g_width, g_height, /*embedded=*/true);
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

void Update(float dt) {
    if (g_model) g_model->Update(dt);
}

void Draw() {
    static int s_diagFrames = 0;
    if (s_diagFrames < 3) {
        GLint fbo = 0, vp[4] = {0,0,0,0};
        glGetIntegerv(GL_FRAMEBUFFER_BINDING, &fbo);
        glGetIntegerv(GL_VIEWPORT, vp);
        L2DDiag("draw#%d loaded=%d fbo=%d vp=%dx%d glErr=0x%x",
                s_diagFrames, (g_model && g_model->Loaded()) ? 1 : 0,
                fbo, vp[2], vp[3], glGetError());
        s_diagFrames++;
    }
    if (!g_model || !g_model->Loaded()) return;

    // Fit the model (normalised device coords, ~2 units tall) into the surface,
    // preserving aspect. Portrait surfaces show the full body; wide surfaces are
    // letterboxed horizontally.
    CubismMatrix44 projection;
    const float aspect = static_cast<float>(g_width) / static_cast<float>(g_height);
    if (g_width < g_height) {
        projection.Scale(1.0f, aspect);
    } else {
        projection.Scale(1.0f / aspect, 1.0f);
    }

    g_model->Draw(projection);
}

void Shutdown() {
    delete g_model;
    g_model = nullptr;
    if (g_started) {
        CubismFramework::Dispose();
        g_started = false;
    }
}

} // namespace live2d
} // namespace aimgui
