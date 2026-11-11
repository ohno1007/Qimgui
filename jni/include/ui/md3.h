#pragma once
#include "imgui.h"
#include <cstddef>
namespace aimgui::md3 {
struct Theme {
    ImVec4 primary, onPrimary, primaryContainer, onPrimaryContainer;
    ImVec4 secondaryContainer, onSecondaryContainer;
    ImVec4 surface, surfaceContainer, surfaceContainerLow, surfaceContainerHigh,
        surfaceContainerHighest, surfaceVariant;
    ImVec4 onSurface, onSurfaceVariant, outline, outlineVariant;
    ImVec4 error, onError, inverseSurface, inverseOnSurface, inversePrimary;
    float scale = 1.0f;
    bool dark = false;
};
Theme &GetTheme();
void SetTheme(bool dark, float scale = 1.0f);
void SetFonts(ImFont *body, ImFont *label = nullptr);
void PushTheme();
void PopTheme();
enum class ButtonStyle { Filled, Tonal, Outlined, Text, Elevated };
enum class IconButtonStyle { Standard, Filled, Tonal, Outlined };
enum class FabSize { Small, Medium, Large };
enum class ChipStyle { Assist, Filter, Input, Suggestion };
enum class TextFieldStyle { Filled, Outlined };
bool Button(const char *label, ButtonStyle style = ButtonStyle::Filled, ImVec2 size = ImVec2(0, 0),
            const char *icon = nullptr);
bool IconButton(const char *id, const char *icon, IconButtonStyle style = IconButtonStyle::Standard,
                bool *selected = nullptr);
bool Fab(const char *id, const char *icon, FabSize size = FabSize::Medium,
         const char *label = nullptr);
bool Checkbox(const char *label, bool *value, bool mixed = false);
bool RadioButton(const char *label, int *value, int option);
bool Switch(const char *label, bool *value, bool show_icons = true);
bool Chip(const char *label, ChipStyle style = ChipStyle::Assist, bool *selected = nullptr,
          bool *removed = nullptr, const char *icon = nullptr);
bool Slider(const char *label, float *value, float minimum, float maximum, float width = 240.f,
            int steps = 0, const char *format = "%.0f");
bool RangeSlider(const char *label, float *lower, float *upper, float minimum, float maximum,
                 float width = 240.f, int steps = 0);
bool TextField(const char *label, char *buffer, std::size_t capacity,
               TextFieldStyle style = TextFieldStyle::Outlined, float width = 280.f,
               const char *supporting_text = nullptr, bool error = false,
               ImGuiInputTextFlags flags = 0);
} // namespace aimgui::md3
