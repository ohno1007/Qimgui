// Public entry point for the optional Live2D layer (compiled only when the
// project is configured with -DAIMGUI_LIVE2D=ON). Everything here is a no-op /
// unavailable in the default build, so callers can reference it unconditionally
// behind #ifdef AIMGUI_LIVE2D.
#pragma once

#ifdef AIMGUI_LIVE2D
#include "core/live2d_vk_bridge.h"
#endif

namespace aimgui {
namespace live2d {

#ifdef AIMGUI_LIVE2D
// Boot the Cubism Framework and its Vulkan renderer against the objects owned
// by the Vulkan backend (device, queue, offscreen model image, …). Call once
// before loading a model. Returns false on failure.
bool VkInit(const Live2DVkContext* ctx);
#endif

// Load a model from <dir>/<model3json> (e.g. "/data/local/tmp/live2d/Hiyori",
// "Hiyori.model3.json"). Replaces any currently-loaded model.
bool LoadModel(const char* dir, const char* model3json);

// Scan <root> (and its immediate subdirectories) for the first *.model3.json
// and load it. Returns false if none found / load failed.
bool AutoLoad(const char* root);

// Load the model embedded in the binary at build time, if any.
bool LoadEmbedded();

// Append a line to the on-device diagnostics file (/data/local/tmp/aimgui_live2d.txt).
void Note(const char* msg);

bool IsLoaded();

// Notify the current drawable surface size (used to fit the model on screen).
void Resize(int width, int height);

// Advance motion / physics / breathing by dt seconds.
void Update(float dt);

// Presentation: expandT in [0,1] — 0 shows the character as the floating
// "ball", 1 is the open window (the model shrinks away / is hidden). Driven
// by the UI's collapse/expand spring.
void SetView(float expandT);

// Screen position (visible-region px) of the draggable ball the character is
// drawn at while collapsed.
void SetBall(float x, float y);

// Size multiplier for the ball character (1 = default).
void SetBallScale(float scale);

// The character's eyes/head look toward this screen point (visible-region
// pixels) while active; when inactive the gaze eases back to centre.
void SetLookScreen(float x, float y, bool active);

// Trigger a tap reaction (a little bounce + head wobble). Also plays a voice.
void Poke();

// Play a voice line (disk clip override, else the embedded voice) with
// lip-sync. Bound to the UI "说话" button.
void Speak();

// True when the screen point lands on the collapsed (tiny) character — used
// by the UI to detect a tap on it. Only meaningful while collapsed.
bool HitCollapsed(float x, float y);

// Render the model into the Vulkan backend's offscreen model image (the
// Cubism renderer self-submits). Invoked via the renderer's scene-predraw
// hook, before the UI is composited over the result.
void Draw();

void Shutdown();

} // namespace live2d
} // namespace aimgui
