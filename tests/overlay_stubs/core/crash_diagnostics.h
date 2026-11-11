#pragma once
namespace aimgui::diagnostics {
void InstallCrashHandler(void (*release_input)() = nullptr);
void RefreshLoadedModules();
void SetStage(const char* stage);
}
