#include "core/config.h"
#include "ui/ui.h"
#include "ui/main_ui.h"
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <fstream>
#include <string>
#include <unistd.h>

namespace aimgui { const int kPagesCount = 5; }
extern "C" int __android_log_print(int, const char*, const char*, ...) { return 0; }

int main() {
    char directory[] = "build/config-checks/preferences-XXXXXX";
    assert(mkdtemp(directory));
    const std::string file = std::string(directory) + "/aimgui.conf";
    {
        std::ofstream out(file);
        out << "screen_mirror=1\nnav_page=3\nstage=2\nglass_clarity=0.15\n"
               "last_full_pos=80 140\nlast_full_size=960 700\nunknown_old_key=1\n";
    }
    aimgui::UiState state;
    aimgui::config::Load(&state, file.c_str());
    assert(state.screen_mirror && state.nav_page == 3);
    assert(!state.md3_dark && state.ui_scale == 0 && state.block_touch);
    assert(state.last_full_size.x == 960 && state.last_full_size.y == 700);
    assert(!aimgui::config::Dirty(&state));

    state.md3_dark = true;
    state.ui_scale = 1.75f;
    state.block_touch = false;
    assert(aimgui::config::Dirty(&state));
    aimgui::config::Save(&state, file.c_str());
    assert(!aimgui::config::Dirty(&state));
    aimgui::UiState restored;
    aimgui::config::Load(&restored, file.c_str());
    assert(restored.screen_mirror && restored.md3_dark);
    assert(restored.ui_scale == 1.75f && !restored.block_touch && restored.nav_page == 3);
    restored.screen_mirror = false;
    aimgui::config::Save(&restored, file.c_str());
    aimgui::UiState flat;
    aimgui::config::Load(&flat, file.c_str());
    assert(!flat.screen_mirror && flat.md3_dark && flat.nav_page == 3);

    flat.md3_dark = false;
    aimgui::config::Save(&flat, (file + "/missing/preferences").c_str());
    assert(aimgui::config::Dirty(&flat));
    {
        std::ofstream out(file);
        out << "ui_scale=nan\nglass_clarity=nan\nnav_page=99\nstage=-9\n"
               "last_full_pos=inf -inf\nlast_full_size=nan 0\n";
    }
    aimgui::UiState invalid;
    aimgui::config::Load(&invalid, file.c_str());
    assert(invalid.ui_scale == 0 && std::isfinite(invalid.glass_clarity));
    assert(invalid.nav_page == 0 && invalid.stage == aimgui::UiState::StageIsland);
    assert(std::isfinite(invalid.last_full_pos.x) && std::isfinite(invalid.last_full_size.x));
    assert(invalid.last_full_size.x > 0 && invalid.last_full_size.y > 0);
    std::remove(file.c_str());
    assert(rmdir(directory) == 0);
    std::puts("PASS: legacy glass preference migration, MD3 theme/scale persistence, toggle round trip and failed-save handling");
}
