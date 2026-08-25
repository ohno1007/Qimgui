#include "ui/ui_internal.h"

#include "imgui.h"

#include <cmath>
#include <cstdint>
#include <vector>

namespace aimgui {

namespace dissolve {

struct Particle {
    ImVec2 pos;
    ImVec2 vel;
    float  size;
    float  delay;
    float  spin;
    float  rot;
    float  sway;
    ImVec2 uv0, uv1;
    ImU32  color;
};

std::vector<Particle> g_parts;

uint32_t g_seed = 0x9e3779b9;
uint32_t Rand() {
    g_seed ^= g_seed << 13;
    g_seed ^= g_seed >> 17;
    g_seed ^= g_seed << 5;
    return g_seed;
}
float Frand(float lo, float hi) {
    return lo + ((Rand() & 0xFFFF) / 65535.0f) * (hi - lo);
}

void Begin(const ImVec2& origin, const ImVec2& size,
           float display_w, float display_h) {
    g_parts.clear();

    constexpr float kCell = 9.0f;
    const int nx = (int)(size.x / kCell) + 1;
    const int ny = (int)(size.y / kCell) + 1;
    g_parts.reserve((size_t)nx * ny);

    const ImU32 palette[3] = {
        ImGui::GetColorU32(ImGuiCol_TitleBgActive),
        ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 1.0f)),
        ImGui::GetColorU32(ImGuiCol_FrameBg),
    };

    for (int j = 0; j < ny; ++j) {
        for (int i = 0; i < nx; ++i) {
            const float x = origin.x + (float)i * kCell;
            const float y = origin.y + (float)j * kCell;

            Particle p;
            p.pos  = ImVec2(x + kCell * 0.5f, y + kCell * 0.5f);
            p.size = kCell * Frand(0.55f, 1.0f);
            p.uv0  = ImVec2(x / display_w, y / display_h);
            p.uv1  = ImVec2((x + kCell) / display_w, (y + kCell) / display_h);

            const float u = (float)i / (float)nx;
            const float v = (float)j / (float)ny;
            p.delay = (u * 0.55f + (1.0f - v) * 0.45f) * 0.34f + Frand(0.0f, 0.05f);

            const float cx = (x - (origin.x + size.x * 0.5f)) / (size.x * 0.5f);
            const float cy = (y - (origin.y + size.y * 0.5f)) / (size.y * 0.5f);
            const float spread = Frand(40.0f, 130.0f);
            p.vel  = ImVec2(cx * spread + Frand(-34.0f, 34.0f),
                            cy * spread * 0.7f + Frand(-30.0f, 30.0f) - 34.0f);
            p.spin = Frand(-2.2f, 2.2f);
            p.sway = Frand(0.0f, 6.283f);
            p.rot  = 0.0f;
            p.color = palette[(uint32_t)(x + y) % 3u];
            g_parts.push_back(p);
        }
    }
}

void Step(float dt, float t01, ImTextureID snapshot_tex) {

    constexpr float kGravity = 42.0f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (snapshot_tex) dl->PushTexture(ImTextureRef(snapshot_tex));

    for (auto& p : g_parts) {
        const float local = t01 - p.delay;
        if (local <= 0.0f) {

            if (snapshot_tex) {
                const ImVec2 a(p.pos.x - p.size * 0.5f, p.pos.y - p.size * 0.5f);
                const ImVec2 b(p.pos.x + p.size * 0.5f, p.pos.y + p.size * 0.5f);
                dl->PrimReserve(6, 4);
                const unsigned int i0 = dl->_VtxCurrentIdx;
                dl->PrimWriteVtx(a,                  p.uv0,                    IM_COL32_WHITE);
                dl->PrimWriteVtx(ImVec2(b.x, a.y),   ImVec2(p.uv1.x, p.uv0.y), IM_COL32_WHITE);
                dl->PrimWriteVtx(b,                  p.uv1,                    IM_COL32_WHITE);
                dl->PrimWriteVtx(ImVec2(a.x, b.y),   ImVec2(p.uv0.x, p.uv1.y), IM_COL32_WHITE);
                dl->PrimWriteIdx((ImDrawIdx)i0);     dl->PrimWriteIdx((ImDrawIdx)(i0 + 1));
                dl->PrimWriteIdx((ImDrawIdx)(i0+2)); dl->PrimWriteIdx((ImDrawIdx)i0);
                dl->PrimWriteIdx((ImDrawIdx)(i0+2)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
            }
            continue;
        }

        p.vel.y += kGravity * dt;

        const float drag = std::exp(-3.2f * dt);
        p.vel.x *= drag;
        p.vel.y *= drag;

        p.sway += dt * 1.7f;
        p.pos.x += (p.vel.x + std::sin(p.sway) * 22.0f) * dt;
        p.pos.y += (p.vel.y + std::cos(p.sway * 0.7f) * 9.0f) * dt;
        p.rot   += p.spin * dt;

        const float life = local / 0.95f;
        if (life >= 1.0f) continue;
        const float fade = 1.0f - life;
        const float sz   = p.size * (0.15f + 0.85f * fade);
        const uint32_t a = (uint32_t)(255.0f * fade * fade);
        if (a == 0) continue;

        const float cs = std::cos(p.rot) * sz * 0.5f;
        const float sn = std::sin(p.rot) * sz * 0.5f;
        const ImVec2 q[4] = {
            ImVec2(p.pos.x - cs + sn, p.pos.y - sn - cs),
            ImVec2(p.pos.x + cs + sn, p.pos.y + sn - cs),
            ImVec2(p.pos.x + cs - sn, p.pos.y + sn + cs),
            ImVec2(p.pos.x - cs - sn, p.pos.y - sn + cs),
        };

        if (snapshot_tex) {
            const ImU32 col = IM_COL32(255, 255, 255, a);
            dl->PrimReserve(6, 4);
            const unsigned int i0 = dl->_VtxCurrentIdx;
            dl->PrimWriteVtx(q[0], p.uv0,                    col);
            dl->PrimWriteVtx(q[1], ImVec2(p.uv1.x, p.uv0.y), col);
            dl->PrimWriteVtx(q[2], p.uv1,                    col);
            dl->PrimWriteVtx(q[3], ImVec2(p.uv0.x, p.uv1.y), col);
            dl->PrimWriteIdx((ImDrawIdx)i0);     dl->PrimWriteIdx((ImDrawIdx)(i0 + 1));
            dl->PrimWriteIdx((ImDrawIdx)(i0+2)); dl->PrimWriteIdx((ImDrawIdx)i0);
            dl->PrimWriteIdx((ImDrawIdx)(i0+2)); dl->PrimWriteIdx((ImDrawIdx)(i0 + 3));
        } else {
            dl->AddQuadFilled(q[0], q[1], q[2], q[3],
                              (p.color & ~IM_COL32_A_MASK) | (a << IM_COL32_A_SHIFT));
        }
    }

    if (snapshot_tex) dl->PopTexture();
}

}
}
