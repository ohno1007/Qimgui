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

    // Load a model and all referenced assets. When embedded==true, files are
    // read from the binary (see live2d_embedded.h) and `dir` is ignored;
    // otherwise from <dir>/<...>. width/height size the clipping-mask target.
    bool LoadAssets(const char* dir, const char* model3json, int width, int height, bool embedded);

    // Advance motion / expression / physics / breath / blink by dt seconds.
    void Update(float dt);

    // Draw using the given projection*view matrix (already fit to the surface).
    void Draw(Csm::CubismMatrix44& matrix);

    bool Loaded() const { return _loaded; }

private:
    void SetupModel(Csm::ICubismModelSetting* setting);
    void SetupTextures();
    void ReleaseTextures();
    // Read a model file by path relative to the model dir, from the embedded
    // blob or from disk. Caller frees with CSM_FREE.
    Csm::csmByte* ReadModelFile(const Csm::csmString& relpath, Csm::csmSizeInt* outSize);

    Csm::ICubismModelSetting* _setting = nullptr;
    Csm::csmString _dir;
    bool _embedded = false;
    Csm::csmVector<Csm::CubismIdHandle> _eyeBlinkIds;
    Csm::csmVector<Csm::CubismIdHandle> _lipSyncIds;
    Csm::csmVector<unsigned int> _textures; // GL texture ids
    bool _loaded = false;
};

} // namespace live2d
} // namespace aimgui
