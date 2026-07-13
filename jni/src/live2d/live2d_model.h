// A single Cubism model built on CubismUserModel, modelled after Live2D's
// LAppModel sample but trimmed for this project. Vulkan renderer path.
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

    // Load a model and all referenced assets. When embedded==true, files are
    // read from the binary (see live2d_embedded.h) and `dir` is ignored;
    // otherwise from <dir>/<...>. width/height size the clipping-mask target.
    bool LoadAssets(const Live2DVkContext& ctx, const char* dir,
                    const char* model3json, int width, int height, bool embedded);

    // Advance motion / expression / physics / breath / blink by dt seconds.
    // dragX/dragY in [-1,1] steer the head + eyes toward the look target;
    // reaction in [0,1] adds a decaying wobble when the character was tapped;
    // lipRms in [0,1] opens the mouth for lip-sync while a voice plays.
    void Update(float dt, float dragX, float dragY, float reaction, float lipRms);

    // Draw using the given projection*view matrix (already fit to the surface).
    void Draw(Csm::CubismMatrix44& matrix);

    bool Loaded() const { return _loaded; }
    bool HasModel() const { return _model != nullptr; }
    int  TextureCount() const { return static_cast<int>(_textures.GetSize()); }

private:
    void SetupModel(Csm::ICubismModelSetting* setting);
    void SetupTextures();
    void ReleaseTextures();
    // Read a model file by path relative to the model dir, from the embedded
    // blob or from disk. Caller frees with CSM_FREE.
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

} // namespace live2d
} // namespace aimgui
