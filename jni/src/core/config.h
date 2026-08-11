#pragma once

namespace aimgui {

struct UiState;

// Settings that survive a restart.
//
// ImGui's own .ini is switched off — it persists window geometry keyed by
// window name, which is the wrong shape here: the window's position and size
// are driven by a spring out of UiState, and half of what is worth keeping
// (frame cap, glass clarity, whether the mirror is running) is not ImGui state
// at all. So this is a handful of key/value lines of our own.
//
// It is best-effort in both directions. A missing or unreadable file leaves
// every default in place, and an unrecognised key is skipped rather than
// treated as an error — a config written by a newer build has to be survivable
// by an older one.
namespace config {

// Where the file lives. Under /data/local/tmp because that is where this
// binary is run from and the one place a package-less process can rely on
// being able to write.
const char* Path();

void Load(UiState* state);
void Save(const UiState* state);

// True when a persisted field has changed since the last Save. The main loop
// uses this to avoid rewriting the file every frame; nothing here changes
// faster than a finger can move a slider.
bool Dirty(const UiState* state);

} // namespace config
} // namespace aimgui
