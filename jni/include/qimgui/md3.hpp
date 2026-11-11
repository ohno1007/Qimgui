#pragma once

// Public component-library entry point. Platform input, window creation and
// renderer backends remain the responsibility of the application.
#include "ui/md3.h"
#include "ui/md3_layout.h"
#include "ui/md3_selection.h"
#include "ui/md3_extra.h"
#include "ui/md3_shapes.h"
#include "ui/md3_icons.h"
#include "ui/md3_motion.h"
#include "ui/md3_toolbar.h"
#include "ui/md3_split_button.h"
#include "ui/md3_button_group.h"
#include "ui/md3_progress.h"

namespace aimgui::md3 {
inline constexpr const char* LibraryVersion = "0.1.0";

// Use within one live ImGui context, after SetTheme/SetFonts. The scope restores
// ImGui's style/font stacks; it does not own or change the selected MD3 palette.
class ScopedTheme final {
public:
    ScopedTheme() { PushTheme(); }
    ~ScopedTheme() { PopTheme(); }
    ScopedTheme(const ScopedTheme&) = delete;
    ScopedTheme& operator=(const ScopedTheme&) = delete;
};
} // namespace aimgui::md3

namespace qimgui {
namespace md3 = ::aimgui::md3;
}
