#include "config.h"

#include "ui/ui.h"

#include <android/log.h>
#include <cstdio>
#include <cstdlib>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AImGui", __VA_ARGS__)

namespace aimgui {
namespace config {
namespace {

constexpr const char* kPath = "/data/local/tmp/aimgui.conf";

// The persisted set, in one place. Everything reads and writes through this
// table, so adding a setting is one row rather than three edits that can drift
// apart — a key saved but never loaded is silent and easy to miss.
enum class Kind { Int, Float, Bool };

struct Field {
    const char* key;
    Kind        kind;
    size_t      offset;
};

#define F(name, kind) { #name, kind, offsetof(UiState, name) }
const Field kFields[] = {
    F(target_fps,       Kind::Int),
    F(glass_clarity,    Kind::Float),
    F(bloom_intensity,  Kind::Float),
    F(screen_mirror,    Kind::Bool),
    F(permeate_record,  Kind::Bool),
    F(haptics_enabled,  Kind::Bool),
    F(stage,            Kind::Int),
    F(nav_page,         Kind::Int),
    F(ball_scale,       Kind::Float),
};
#undef F

// last_full_pos / last_full_size are ImVec2 and do not fit the table, so they
// are handled by hand below rather than bent into it.

char* At(UiState* s, size_t off) { return reinterpret_cast<char*>(s) + off; }
const char* At(const UiState* s, size_t off) {
    return reinterpret_cast<const char*>(s) + off;
}

struct Snapshot {
    UiState  values;
    bool     valid = false;
};
Snapshot g_saved;

bool SameAsSaved(const UiState* s) {
    if (!g_saved.valid) return false;
    for (const Field& f : kFields) {
        const char* a = At(s, f.offset);
        const char* b = At(&g_saved.values, f.offset);
        switch (f.kind) {
        case Kind::Int:   if (*(const int*)a   != *(const int*)b)   return false; break;
        case Kind::Float: if (*(const float*)a != *(const float*)b) return false; break;
        case Kind::Bool:  if (*(const bool*)a  != *(const bool*)b)  return false; break;
        }
    }
    return s->last_full_pos.x  == g_saved.values.last_full_pos.x &&
           s->last_full_pos.y  == g_saved.values.last_full_pos.y &&
           s->last_full_size.x == g_saved.values.last_full_size.x &&
           s->last_full_size.y == g_saved.values.last_full_size.y;
}

void Remember(const UiState* s) {
    for (const Field& f : kFields) {
        char* dst = At(&g_saved.values, f.offset);
        const char* src = At(s, f.offset);
        switch (f.kind) {
        case Kind::Int:   *(int*)dst   = *(const int*)src;   break;
        case Kind::Float: *(float*)dst = *(const float*)src; break;
        case Kind::Bool:  *(bool*)dst  = *(const bool*)src;  break;
        }
    }
    g_saved.values.last_full_pos  = s->last_full_pos;
    g_saved.values.last_full_size = s->last_full_size;
    g_saved.valid = true;
}

} // namespace

const char* Path() { return kPath; }

void Load(UiState* state) {
    FILE* f = std::fopen(kPath, "r");
    if (!f) return;

    char line[160];
    while (std::fgets(line, sizeof(line), f)) {
        char* eq = std::strchr(line, '=');
        if (!eq) continue;
        *eq = '\0';
        const char* key = line;
        const char* val = eq + 1;

        if (std::strcmp(key, "last_full_pos") == 0) {
            float x, y;
            if (std::sscanf(val, "%f %f", &x, &y) == 2)
                state->last_full_pos = ImVec2(x, y);
            continue;
        }
        if (std::strcmp(key, "last_full_size") == 0) {
            float x, y;
            if (std::sscanf(val, "%f %f", &x, &y) == 2)
                state->last_full_size = ImVec2(x, y);
            continue;
        }
        for (const Field& fl : kFields) {
            if (std::strcmp(key, fl.key) != 0) continue;
            char* dst = At(state, fl.offset);
            switch (fl.kind) {
            case Kind::Int:   *(int*)dst   = std::atoi(val);              break;
            case Kind::Float: *(float*)dst = (float)std::atof(val);       break;
            case Kind::Bool:  *(bool*)dst  = std::atoi(val) != 0;         break;
            }
            break;
        }
    }
    std::fclose(f);

    // A size below what the window can actually be constrained to would leave
    // it stuck at a size it cannot represent, and a stage outside the enum is
    // read straight into an array-free switch that would simply do nothing.
    if (state->last_full_size.x < 700.0f) state->last_full_size.x = 700.0f;
    if (state->last_full_size.y < 560.0f) state->last_full_size.y = 560.0f;
    if (state->stage < UiState::StageIsland) state->stage = UiState::StageIsland;
    if (state->stage > UiState::StageWindow) state->stage = UiState::StageWindow;
    // The spring has to start where the stage says, or the window plays its
    // whole opening animation every launch.
    state->expand = (float)state->stage * 0.5f;
    state->resize_target_size = state->last_full_size;

    Remember(state);
    LOGI("[config] loaded %s", kPath);
}

void Save(const UiState* state) {
    // Written whole and replaced, so a kill part-way through leaves the old
    // file rather than half of the new one.
    char tmp[128];
    std::snprintf(tmp, sizeof(tmp), "%s.tmp", kPath);
    FILE* f = std::fopen(tmp, "w");
    if (!f) return;

    for (const Field& fl : kFields) {
        const char* src = At(state, fl.offset);
        switch (fl.kind) {
        case Kind::Int:   std::fprintf(f, "%s=%d\n", fl.key, *(const int*)src);   break;
        case Kind::Float: std::fprintf(f, "%s=%.4f\n", fl.key, *(const float*)src); break;
        case Kind::Bool:  std::fprintf(f, "%s=%d\n", fl.key, *(const bool*)src ? 1 : 0); break;
        }
    }
    std::fprintf(f, "last_full_pos=%.1f %.1f\n",
                 state->last_full_pos.x, state->last_full_pos.y);
    std::fprintf(f, "last_full_size=%.1f %.1f\n",
                 state->last_full_size.x, state->last_full_size.y);
    std::fclose(f);
    std::rename(tmp, kPath);

    Remember(state);
}

bool Dirty(const UiState* state) { return !SameAsSaved(state); }

} // namespace config
} // namespace aimgui
