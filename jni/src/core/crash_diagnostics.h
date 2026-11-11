#pragma once

namespace aimgui::diagnostics {
// Install before starting workers; cleanup preserves the existing input release.
void InstallCrashHandler(void (*release_input)() = nullptr);
// Call after loading graphics/media libraries, outside any signal handler.
// Previously published records remain immutable for concurrent crash readers.
void RefreshLoadedModules();
// Only strings with static lifetime. This describes the main thread even when
// another thread receives the fatal signal.
void SetStage(const char* stage);
}
