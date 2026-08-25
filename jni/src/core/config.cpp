#include "config.h"

#include "ui/main_ui.h"
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
    F(mirror_hides_window, Kind::Bool),
    F(stage,            Kind::Int),
    F(nav_page,         Kind::Int),
    F(ball_scale,       Kind::Float),
};
#undef F

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

}

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

    if (state->last_full_size.x < 700.0f) state->last_full_size.x = 700.0f;
    if (state->last_full_size.y < 560.0f) state->last_full_size.y = 560.0f;
    if (state->stage < UiState::StageIsland) state->stage = UiState::StageIsland;
    if (state->stage > UiState::StageWindow) state->stage = UiState::StageWindow;

    if (state->nav_page < 0 || state->nav_page >= kPagesCount) state->nav_page = 0;

    state->expand = (float)state->stage * 0.5f;

    Remember(state);

    LOGI("[config] loaded %s: mirror=%d permeate=%d fps=%d stage=%d page=%d haptics=%d",
         kPath, state->screen_mirror ? 1 : 0, state->permeate_record ? 1 : 0,
         state->target_fps, state->stage, state->nav_page,
         state->haptics_enabled ? 1 : 0);
}

void Save(const UiState* state) {

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

}
}
