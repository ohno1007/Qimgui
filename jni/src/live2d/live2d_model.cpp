#include "live2d/live2d_model.h"
#include "live2d/live2d_texture.h"

#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Motion/CubismEyeBlink.hpp>
#include <Motion/CubismBreath.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Utils/CubismString.hpp>

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <vector>

#define LOG_TAG "AImGui_Live2D"
#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, LOG_TAG, __VA_ARGS__)
#define LOGW(...) __android_log_print(ANDROID_LOG_WARN, LOG_TAG, __VA_ARGS__)

using namespace Live2D::Cubism::Framework;
using namespace Live2D::Cubism::Framework::DefaultParameterId;

namespace aimgui {
namespace live2d {

namespace {
// Read an entire file into a heap buffer allocated with the Cubism allocator.
csmByte* ReadFile(const csmString& path, csmSizeInt* outSize) {
    std::FILE* fp = std::fopen(path.GetRawString(), "rb");
    if (!fp) { LOGW("open failed: %s", path.GetRawString()); return nullptr; }
    std::fseek(fp, 0, SEEK_END);
    long size = std::ftell(fp);
    std::fseek(fp, 0, SEEK_SET);
    if (size <= 0) { std::fclose(fp); return nullptr; }
    csmByte* buf = static_cast<csmByte*>(CSM_MALLOC(static_cast<csmSizeType>(size)));
    if (buf) {
        if (std::fread(buf, 1, static_cast<size_t>(size), fp) != static_cast<size_t>(size)) {
            CSM_FREE(buf);
            buf = nullptr;
        }
    }
    std::fclose(fp);
    if (buf) *outSize = static_cast<csmSizeInt>(size);
    return buf;
}
} // namespace

Model::Model() : CubismUserModel() {}

Model::~Model() {
    ReleaseTextures();
    if (_setting) { CSM_DELETE(_setting); _setting = nullptr; }
}

bool Model::LoadAssets(const char* dir, const char* model3json) {
    _dir = dir;
    _dir += "/";
    csmString jsonPath(_dir);
    jsonPath += model3json;

    csmSizeInt size = 0;
    csmByte* buf = ReadFile(jsonPath, &size);
    if (!buf) return false;

    ICubismModelSetting* setting = CSM_NEW CubismModelSettingJson(buf, size);
    CSM_FREE(buf);
    if (!setting) return false;

    SetupModel(setting);
    if (_model == nullptr) { LOGW("moc load failed"); return false; }

    CreateRenderer();
    SetupTextures();

    _loaded = true;
    LOGI("live2d model loaded: %s", jsonPath.GetRawString());
    return true;
}

void Model::SetupModel(ICubismModelSetting* setting) {
    _setting = setting;
    _updating = true;
    _initialized = false;

    csmSizeInt size = 0;
    csmByte* buf = nullptr;

    // .moc3
    if (std::strlen(setting->GetModelFileName()) > 0) {
        csmString path(_dir); path += setting->GetModelFileName();
        buf = ReadFile(path, &size);
        if (buf) { LoadModel(buf, size, /*shouldCheckMocConsistency=*/true); CSM_FREE(buf); }
    }

    // physics
    if (std::strlen(setting->GetPhysicsFileName()) > 0) {
        csmString path(_dir); path += setting->GetPhysicsFileName();
        buf = ReadFile(path, &size);
        if (buf) { LoadPhysics(buf, size); CSM_FREE(buf); }
    }

    // pose
    if (std::strlen(setting->GetPoseFileName()) > 0) {
        csmString path(_dir); path += setting->GetPoseFileName();
        buf = ReadFile(path, &size);
        if (buf) { LoadPose(buf, size); CSM_FREE(buf); }
    }

    // eye blink
    if (setting->GetEyeBlinkParameterCount() > 0) {
        _eyeBlink = CubismEyeBlink::Create(setting);
    }

    // breath
    _breath = CubismBreath::Create();
    csmVector<CubismBreath::BreathParameterData> breathParams;
    breathParams.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamAngleX), 0.0f, 15.0f, 6.5345f, 0.5f));
    breathParams.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamAngleY), 0.0f, 8.0f, 3.5345f, 0.5f));
    breathParams.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamAngleZ), 0.0f, 10.0f, 5.5345f, 0.5f));
    breathParams.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamBodyAngleX), 0.0f, 4.0f, 15.5345f, 0.5f));
    breathParams.PushBack(CubismBreath::BreathParameterData(
        CubismFramework::GetIdManager()->GetId(ParamBreath), 0.5f, 0.5f, 3.2345f, 0.5f));
    _breath->SetParameters(breathParams);

    // eye-blink target ids
    for (csmInt32 i = 0; i < setting->GetEyeBlinkParameterCount(); ++i) {
        _eyeBlinkIds.PushBack(setting->GetEyeBlinkParameterId(i));
    }

    _modelMatrix->SetWidth(2.0f);
    _updating = false;
    _initialized = true;
}

void Model::SetupTextures() {
    Rendering::CubismRenderer_OpenGLES2* renderer =
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer || !_setting) return;

    for (csmInt32 i = 0; i < _setting->GetTextureCount(); ++i) {
        if (std::strlen(_setting->GetTextureFileName(i)) == 0) continue;
        csmString path(_dir); path += _setting->GetTextureFileName(i);
        GLuint tex = LoadTexture(path.GetRawString());
        _textures.PushBack(tex);
        renderer->BindTexture(static_cast<csmUint32>(i), tex);
    }
    renderer->IsPremultipliedAlpha(true);
}

void Model::ReleaseTextures() {
    for (csmUint32 i = 0; i < _textures.GetSize(); ++i) {
        if (_textures[i]) { GLuint t = _textures[i]; glDeleteTextures(1, &t); }
    }
    _textures.Clear();
}

void Model::Update(float dt) {
    if (!_loaded || _model == nullptr) return;

    _model->LoadParameters();     // restore last frame's saved state
    _model->SaveParameters();

    if (_eyeBlink) _eyeBlink->UpdateParameters(_model, dt);
    if (_breath)   _breath->UpdateParameters(_model, dt);
    if (_physics)  _physics->Evaluate(_model, dt);
    if (_pose)     _pose->UpdateParameters(_model, dt);

    _model->Update();
}

void Model::Draw(CubismMatrix44& matrix) {
    if (!_loaded || _model == nullptr) return;
    Rendering::CubismRenderer_OpenGLES2* renderer =
        GetRenderer<Rendering::CubismRenderer_OpenGLES2>();
    if (!renderer) return;

    matrix.MultiplyByMatrix(_modelMatrix);
    renderer->SetMvpMatrix(&matrix);
    renderer->DrawModel();
}

} // namespace live2d
} // namespace aimgui
