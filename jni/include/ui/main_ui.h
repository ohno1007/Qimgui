#pragma once

namespace aimgui {

struct UiState;

enum class Page {
    Dashboard,
    Widgets,
    Window,
    Performance,
    About,
};

struct PageItem {
    Page        id;
    const char* icon;
    const char* label;
};

extern const PageItem kPages[];
extern const int      kPagesCount;

void DrawPage(UiState* state, Page page);

}
