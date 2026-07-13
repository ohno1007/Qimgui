// Public entry point for the optional Live2D layer (compiled only when the
// project is configured with -DAIMGUI_LIVE2D=ON). Everything here is a no-op /
// unavailable in the default build, so callers can reference it unconditionally
// behind #ifdef AIMGUI_LIVE2D.
#pragma once

namespace aimgui {
namespace live2d {

// Boot the Cubism Framework (once). Returns false if startup fails.
bool Init();

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

// Draw the model into the currently-bound GL framebuffer. Call between the
// GL renderer's NewFrame() and ImGui's draw so the UI overlays on top.
void Draw();

void Shutdown();

} // namespace live2d
} // namespace aimgui
