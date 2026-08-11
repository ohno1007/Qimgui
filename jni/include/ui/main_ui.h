#pragma once

namespace aimgui {

struct UiState;

// Identifier for each entry in the sidebar nav. Add a new value here, add a
// row in kPages, and handle it in DrawPage() to introduce a new page.
enum class Page {
    Dashboard,
    Widgets,
    Window,
    Performance,
    About,
};

struct PageItem {
    Page        id;
    const char* icon;    // from ui/icons.h; may be null
    const char* label;
};

// Sidebar enumerates these for the nav entries. Defined in main_ui.cpp.
extern const PageItem kPages[];
extern const int      kPagesCount;

// Renders the body of `page` into the current content child. The UI
// framework (ui.cpp) calls this from inside the content BeginChild, so
// implementations may freely call ImGui::* layout APIs.
void DrawPage(UiState* state, Page page);

} // namespace aimgui
