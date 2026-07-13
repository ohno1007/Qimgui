#include "live2d/live2d_model.h"
#include "live2d/live2d_texture.h"
#include "live2d/live2d_embedded.h"

#include <CubismDefaultParameterId.hpp>
#include <CubismModelSettingJson.hpp>
#include <Id/CubismIdManager.hpp>
#include <Effect/CubismEyeBlink.hpp>
#include <Effect/CubismBreath.hpp>
#include <Effect/CubismPose.hpp>
#include <Motion/CubismMotion.hpp>
#include <Physics/CubismPhysics.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
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

csmByte* Model::ReadModelFile(const csmString& relpath, csmSizeInt* outSize) {
    if (_embedded) {
        unsigned sz = 0;
        const unsigned char* p = EmbeddedGet(relpath.GetRawString(), &sz);
        if (!p) { LOGW("embedded missing: %s", relpath.GetRawString()); return nullptr; }
        csmByte* buf = static_cast<csmByte*>(CSM_MALLOC(sz));
        if (buf) { std::memcpy(buf, p, sz); *outSize = static_cast<csmSizeInt>(sz); }
        return buf;
    }
    csmString full(_dir); full += relpath;
    return ReadFile(full, outSize);
}

bool Model::LoadAssets(const Live2DVkContext& ctx, const char* dir, const char* model3json,
                       int width, int height, bool embedded) {
    _ctx = ctx;
    _embedded = embedded;
    _dir = "";
    if (!embedded) { _dir = dir; _dir += "/"; }

    csmSizeInt size = 0;
    csmByte* buf = ReadModelFile(csmString(model3json), &size);
    if (!buf) return false;

    ICubismModelSetting* setting = CSM_NEW CubismModelSettingJson(buf, size);
    CSM_FREE(buf);
    if (!setting) return false;

    SetupModel(setting);
    if (_model == nullptr) { LOGW("moc load failed"); return false; }

    CreateRenderer(static_cast<Csm::csmUint32>(width > 0 ? width : 1),
                   static_cast<Csm::csmUint32>(height > 0 ? height : 1));
    SetupTextures();

    _loaded = true;
    LOGI("live2d model loaded: %s (%s)", model3json, _embedded ? "embedded" : _dir.GetRawString());
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
        buf = ReadModelFile(csmString(setting->GetModelFileName()), &size);
        if (buf) { LoadModel(buf, size, /*shouldCheckMocConsistency=*/true); CSM_FREE(buf); }
    }

    // physics
    if (std::strlen(setting->GetPhysicsFileName()) > 0) {
        buf = ReadModelFile(csmString(setting->GetPhysicsFileName()), &size);
        if (buf) { LoadPhysics(buf, size); CSM_FREE(buf); }
    }

    // pose
    if (std::strlen(setting->GetPoseFileName()) > 0) {
        buf = ReadModelFile(csmString(setting->GetPoseFileName()), &size);
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
    Rendering::CubismRenderer_Vulkan* renderer =
        GetRenderer<Rendering::CubismRenderer_Vulkan>();
    if (!renderer || !_setting) return;

    const csmInt32 count = _setting->GetTextureCount();
    _textures.Resize(static_cast<csmUint32>(count));
    for (csmInt32 i = 0; i < count; ++i) {
        const csmChar* name = _setting->GetTextureFileName(i);
        if (std::strlen(name) == 0) continue;
        bool ok = false;
        if (_embedded) {
            unsigned sz = 0;
            const unsigned char* p = EmbeddedGet(name, &sz);
            ok = p && LoadTextureVkFromMemory(_ctx, p, sz, _textures[i]);
        } else {
            csmString path(_dir); path += name;
            ok = LoadTextureVk(_ctx, path.GetRawString(), _textures[i]);
        }
        if (!ok) { LOGW("texture load failed: %s", name); continue; }
        // Cubism appends textures in binding order.
        renderer->BindTexture(_textures[i]);
    }
    renderer->IsPremultipliedAlpha(true);
}

void Model::ReleaseTextures() {
    for (csmUint32 i = 0; i < _textures.GetSize(); ++i) {
        _textures[i].Destroy(_ctx.device);
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
    Rendering::CubismRenderer_Vulkan* renderer =
        GetRenderer<Rendering::CubismRenderer_Vulkan>();
    if (!renderer) return;

    matrix.MultiplyByMatrix(_modelMatrix);
    renderer->SetMvpMatrix(&matrix);
    renderer->DrawModel();
}

} // namespace live2d
} // namespace aimgui
