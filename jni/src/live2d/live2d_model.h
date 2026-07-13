// A single Cubism model built on CubismUserModel, modelled after Live2D's
// LAppModel sample but trimmed for this project. NOTE: first-draft glue — it is
// compiled against the user-supplied Cubism SDK and is expected to need
// on-device iteration (see docs/LIVE2D.md).
#pragma once

#include <CubismFramework.hpp>
#include <Model/CubismUserModel.hpp>
#include <ICubismModelSetting.hpp>
#include <Rendering/OpenGL/CubismRenderer_OpenGLES2.hpp>
#include <Type/csmVector.hpp>
#include <Math/CubismMatrix44.hpp>

namespace aimgui {
namespace live2d {

namespace Csm = Live2D::Cubism::Framework;

class Model : public Csm::CubismUserModel {
public:
    Model();
    ~Model() override;

    // Load <dir>/<model3json> and all referenced assets. Returns false on error.
    bool LoadAssets(const char* dir, const char* model3json);

    // Advance motion / expression / physics / breath / blink by dt seconds.
    void Update(float dt);

    // Draw using the given projection*view matrix (already fit to the surface).
    void Draw(Csm::CubismMatrix44& matrix);

    bool Loaded() const { return _loaded; }

private:
    void SetupModel(Csm::ICubismModelSetting* setting);
    void SetupTextures();
    void ReleaseTextures();

    Csm::ICubismModelSetting* _setting = nullptr;
    Csm::csmString _dir;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds;
    Csm::csmVector<unsigned int> _textures; // GL texture ids
    bool _loaded = false;
};

} // namespace live2d
} // namespace aimgui
