#include "live2d/live2d_view.h"
#include "live2d/live2d_allocator.h"
#include "live2d/live2d_model.h"

#include <CubismFramework.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Math/CubismMatrix44.hpp>

#include <android/log.h>
#include <dirent.h>
#include <cstring>
#include <string>

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
} // namespace

bool Init() {
    if (g_started) return true;
    g_option.LogFunction = [](const char* msg) {
        __android_log_print(ANDROID_LOG_INFO, "AImGui_Cubism", "%s", msg);
    };
    g_option.LoggingLevel = CubismFramework::Option::LogLevel_Verbose;

    CubismFramework::StartUp(&g_allocator, &g_option);
    CubismFramework::Initialize();
    g_started = true;
    LOGI("cubism framework started");
    return true;
}

bool LoadModel(const char* dir, const char* model3json) {
    if (!g_started && !Init()) return false;
    delete g_model;
    g_model = new Model();
    if (!g_model->LoadAssets(dir, model3json)) {
        delete g_model;
        g_model = nullptr;
        return false;
    }
    return true;
}

bool IsLoaded() { return g_model && g_model->Loaded(); }

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
