// Public entry point for the optional Live2D layer (built only with
// -DAIMGUI_LIVE2D=ON). Callers guard use behind #ifdef AIMGUI_LIVE2D.
#pragma once

#ifdef AIMGUI_LIVE2D
#include "core/live2d_vk_bridge.h"
#endif

namespace aimgui {
namespace live2d {

#ifdef AIMGUI_LIVE2D
// Boot the Cubism Framework + Vulkan renderer against the objects owned by the
// Vulkan backend. Call once before loading a model. Returns false on failure.
bool VkInit(const Live2DVkContext* ctx);
#endif

// Load a model from <dir>/<model3json>, replacing any current model.
bool LoadModel(const char* dir, const char* model3json);

// Scan <root> and its immediate subdirectories for the first *.model3.json.
bool AutoLoad(const char* root);

// Load the model embedded in the binary at build time, if any.
bool LoadEmbedded();

bool IsLoaded();

// Notify the current drawable surface size.
void Resize(int width, int height);

// Advance motion / physics / breathing / gaze / lip-sync by dt seconds.
void Update(float dt);

// Presentation: expandT in [0,1] — 0 shows the floating "ball", 1 is the open
// window (the model is hidden). Driven by the UI's collapse/expand spring.
void SetView(float expandT);

// Draggable ball position (visible-region px) the character is drawn at.
void SetBall(float x, float y);

// Ball size multiplier (1 = default).
void SetBallScale(float scale);

// Eyes/head look toward this screen point while active; ease to centre when not.
void SetLookScreen(float x, float y, bool active);

// Tap reaction (bounce + wobble) and play a voice.
void Poke();

// Play a voice line (disk override, else the embedded clip) with lip-sync.
void Speak();

// True when the screen point lands on the collapsed character.
bool HitCollapsed(float x, float y);

// Render the model into the backend's offscreen image (via the renderer's
// scene-predraw hook, before the UI composites over it).
void Draw();

void Shutdown();

} // namespace live2d
} // namespace aimgui
