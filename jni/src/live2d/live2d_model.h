#pragma once

#include <CubismFramework.hpp>
#include <Model/CubismUserModel.hpp>
#include <ICubismModelSetting.hpp>
#include <Rendering/Vulkan/CubismRenderer_Vulkan.hpp>
#include <Rendering/Vulkan/CubismClass_Vulkan.hpp>
#include <Type/csmVector.hpp>
#include <Math/CubismMatrix44.hpp>

#include "core/live2d_vk_bridge.h"

namespace aimgui {
namespace live2d {

namespace Csm = Live2D::Cubism::Framework;

class Model : public Csm::CubismUserModel {
public:
    Model();
    ~Model() override;

    bool LoadAssets(const Live2DVkContext& ctx, const char* dir,
                    const char* model3json, int width, int height, bool embedded);

    void Update(float dt, float dragX, float dragY, float reaction, float lipRms);

    void Draw(Csm::CubismMatrix44& matrix);

    bool Loaded() const { return _loaded; }
    bool HasModel() const { return _model != nullptr; }
    int  TextureCount() const { return static_cast<int>(_textures.GetSize()); }

private:
    void SetupModel(Csm::ICubismModelSetting* setting);
    void SetupTextures();
    void ReleaseTextures();

    Csm::csmByte* ReadModelFile(const Csm::csmString& relpath, Csm::csmSizeInt* outSize);

    Live2DVkContext _ctx{};
    Csm::ICubismModelSetting* _setting = nullptr;
    Csm::csmString _dir;
    bool _embedded = false;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds;
    Csm::csmVector<Csm::CubismImageVulkan> _textures;
    bool _loaded = false;
};

}
}
