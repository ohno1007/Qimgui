#include "ui/ui.h"
#include "ui/main_ui.h"
#include "ui/icons.h"

#include "imgui.h"
#include "platform/ANativeWindowCreator.h"

#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

namespace aimgui {

// ─── MD3 ripple manager ──────────────────────────────────────────────────
// Captures the tap position and rect of the last-drawn ImGui item when it
// becomes active, then paints an expanding clipped white tint on the
// foreground draw list. DrawAll() is called once per frame at the end of
// the UI to advance and render all live ripples; TouchLastItem is exposed
// via ui.h so per-page content in main_ui.cpp can record its own ripples.
namespace ripple {

namespace {
struct Entry {
    ImGuiID id;
    ImVec2  origin;
    ImVec2  rect_min;
    ImVec2  rect_max;
    float   start;
};
std::vector<Entry> g_ripples;
} // namespace

void TouchLastItem() {
    if (!ImGui::IsItemActivated()) return;
    Entry e;
    e.id       = ImGui::GetItemID();
    e.origin   = ImGui::GetIO().MousePos;
    e.rect_min = ImGui::GetItemRectMin();
    e.rect_max = ImGui::GetItemRectMax();
    e.start    = (float)ImGui::GetTime();
    g_ripples.push_back(e);
}

void DrawAll() {
    const float now      = (float)ImGui::GetTime();
    const float duration = 0.55f;

    auto& v = g_ripples;
    for (auto it = v.begin(); it != v.end(); ) {
        const float t = (now - it->start) / duration;
        if (t >= 1.0f) { it = v.erase(it); continue; }

        const float ease   = 1.0f - (1.0f - t) * (1.0f - t); // quadratic ease-out
        const float dx     = it->rect_max.x - it->rect_min.x;
        const float dy     = it->rect_max.y - it->rect_min.y;
        const float max_r  = std::sqrt(dx * dx + dy * dy);
        const float radius = ease * max_r;
        const float alpha  = (1.0f - t) * 0.22f;

        ImDrawList* dl = ImGui::GetForegroundDrawList();
        dl->PushClipRect(it->rect_min, it->rect_max, true);
        dl->AddCircleFilled(it->origin, radius,
                            ImGui::GetColorU32(ImVec4(1.0f, 1.0f, 1.0f, alpha)),
                            48);
        dl->PopClipRect();
        ++it;
    }
}

} // namespace ripple

namespace {

// ─── Exit dissolve ───────────────────────────────────────────────────────
// The window comes apart into a cloud of small particles that drift up and
// outward while fading, in the manner of the delete animation on Huawei's
// launcher — the surface turns to dust and is carried off, rather than
// breaking into slabs and dropping.
//
// The distinction that matters is where the energy comes from. Falling shards
// read as gravity acting on something solid; dust reads as the thing ceasing
// to be solid at all, which is the right feeling for deleting something. So
// gravity is weak and mostly sideways drift, particles are small and many,
// each shrinks as it goes, and they leave in a wave from one corner rather
// than all at once.
namespace dissolve {

struct Particle {
    ImVec2 pos;
    ImVec2 vel;
    float  size;
    float  delay;     // staggered so the cloud peels away rather than bursting
    float  spin;
    float  rot;
    float  sway;      // phase of the lateral drift, so no two wander alike
    ImVec2 uv0, uv1;  // patch of the snapshot this particle carries
    ImU32  color;     // fallback if there is no snapshot to sample
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

    // Particle size is fixed rather than scaled to the window: dust should look
    // the same regardless of how big the thing that turned into it was.
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

            // The wave runs from the bottom-left to the top-right, so the
            // window visibly comes apart in a direction instead of everywhere
            // at once.
            const float u = (float)i / (float)nx;
            const float v = (float)j / (float)ny;
            p.delay = (u * 0.55f + (1.0f - v) * 0.45f) * 0.34f + Frand(0.0f, 0.05f);

            // Outward from the centre in every direction, with a mild upward
            // bias. Throwing everything upwards and letting gravity bring it
            // back is what made this read as debris being tossed; dust leaves
            // in the direction it happened to be facing and simply keeps
            // going, slower and slower.
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

// Advance and draw. `t01` runs 0..1 over the animation; `snapshot_tex`, when
// present, lets each particle carry the piece of UI it was cut from.
void Step(float dt, float t01, ImTextureID snapshot_tex) {
    // Barely there. Enough that the cloud settles rather than expanding
    // forever, far too little to pull anything back down — the moment
    // particles visibly fall, this stops being dust and becomes debris.
    constexpr float kGravity = 42.0f;

    ImDrawList* dl = ImGui::GetForegroundDrawList();
    if (snapshot_tex) dl->PushTexture(ImTextureRef(snapshot_tex));

    for (auto& p : g_parts) {
        const float local = t01 - p.delay;
        if (local <= 0.0f) {
            // Not yet gone: still part of the intact surface.
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
        // Heavy drag: particles shed most of their speed in the first moments
        // and then hang, drifting. That deceleration is the whole read — it is
        // what says the pieces are light enough for the air to hold them.
        const float drag = std::exp(-3.2f * dt);
        p.vel.x *= drag;
        p.vel.y *= drag;
        // A slow lateral wander on top, each particle on its own phase, so the
        // cloud keeps moving after it has stopped travelling.
        p.sway += dt * 1.7f;
        p.pos.x += (p.vel.x + std::sin(p.sway) * 22.0f) * dt;
        p.pos.y += (p.vel.y + std::cos(p.sway * 0.7f) * 9.0f) * dt;
        p.rot   += p.spin * dt;

        // Shrink and fade together over the particle's own lifetime, so it
        // thins out to nothing rather than blinking off at full size.
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

} // namespace dissolve

} // namespace  (anonymous)

namespace chrome {
namespace {
// A draw list cannot blur, so the contact shadow is a few offset copies. Three
// is enough for the falloff to read as soft at control sizes.
constexpr int   kShadowLayers = 3;
constexpr float kShadowStep   = 2.0f;
constexpr float kRimThickness = 1.5f;
} // namespace

void Rect(const ImVec2& a, const ImVec2& b, float rounding,
          bool hovered, bool active) {
    const float h = b.y - a.y;
    const float w = b.x - a.x;
    if (h < 2.0f || w < 2.0f) return;
    float r = rounding < 0.0f ? h * 0.5f : rounding;
    const float rmax = (h < w ? h : w) * 0.5f;
    if (r > rmax) r = rmax;

    ImDrawList* dl = ImGui::GetWindowDrawList();

    // Lift it off the sheet. Spread sideways much less than downwards, so it
    // reads as a shallow step rather than a floating card.
    for (int i = kShadowLayers; i >= 1; --i) {
        const float o = (float)i * kShadowStep;
        dl->AddRectFilled(ImVec2(a.x - o * 0.3f, a.y + o * 0.4f),
                          ImVec2(b.x + o * 0.3f, b.y + o),
                          IM_COL32(0, 0, 0, 11), r + o * 0.3f);
    }

    const float fill = active ? 0.13f : (hovered ? 0.09f : 0.05f);
    dl->AddRectFilled(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, fill)), r);

    // The rim is one path stroked twice under complementary clips, so the two
    // halves join without a seam where they meet.
    const float mid = (a.y + b.y) * 0.5f;
    const float top = active ? 0.34f : (hovered ? 0.28f : 0.20f);
    const ImVec2 pad(2.0f, 2.0f);
    dl->PushClipRect(ImVec2(a.x - pad.x, a.y - pad.y), ImVec2(b.x + pad.x, mid), true);
    dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, top)), r, kRimThickness);
    dl->PopClipRect();
    dl->PushClipRect(ImVec2(a.x - pad.x, mid), ImVec2(b.x + pad.x, b.y + pad.y), true);
    dl->AddRect(a, b, ImGui::GetColorU32(ImVec4(1, 1, 1, top * 0.35f)), r, kRimThickness);
    dl->PopClipRect();
}

void LastItem(float rounding) {
    Rect(ImGui::GetItemRectMin(), ImGui::GetItemRectMax(), rounding,
         ImGui::IsItemHovered(), ImGui::IsItemActive());
}

void LastItemFrame(const char* label, float rounding) {
    ImVec2 a = ImGui::GetItemRectMin();
    ImVec2 b = ImGui::GetItemRectMax();
    if (label) {
        const char* hash = std::strstr(label, "##");
        const char* end  = hash ? hash : label + std::strlen(label);
        const ImVec2 ls  = ImGui::CalcTextSize(label, end);
        if (ls.x > 0.0f) b.x -= ls.x + ImGui::GetStyle().ItemInnerSpacing.x;
    }
    Rect(a, b, rounding, ImGui::IsItemHovered(), ImGui::IsItemActive());
}

} // namespace chrome

void ApplyGlassPalette() {
    auto& s = ImGui::GetStyle();
    const ImVec4 clear(0, 0, 0, 0);
    auto white = [](float alpha) { return ImVec4(1, 1, 1, alpha); };

    // Every control background is drawn by chrome::, which puts the shape on
    // its edge instead of in a fill. ImGui's own fills would sit underneath as
    // flat slabs, so they are cleared outright rather than tuned down.
    s.Colors[ImGuiCol_FrameBg]        = clear;
    s.Colors[ImGuiCol_FrameBgHovered] = clear;
    s.Colors[ImGuiCol_FrameBgActive]  = clear;
    s.Colors[ImGuiCol_Button]         = clear;
    s.Colors[ImGuiCol_ButtonHovered]  = clear;
    s.Colors[ImGuiCol_ButtonActive]   = clear;

    // What is left is only ever white at some strength. A saturated fill reads
    // as a sticker laid on the sheet; this material's own accents are the light
    // it concentrates at an edge, and that light has no hue of its own. Colour
    // is kept for state — the selected nav entry — and nothing else.
    // Header is not cleared: inside a popup it is the only cue for which entry
    // is current, and there is no glass behind a popup to carry the state. The
    // two lists that draw their own capsule push a transparent Header locally.
    s.Colors[ImGuiCol_Header]           = white(0.14f);
    s.Colors[ImGuiCol_HeaderHovered]    = white(0.20f);
    s.Colors[ImGuiCol_HeaderActive]     = white(0.26f);

    s.Colors[ImGuiCol_CheckMark]        = white(0.92f);
    s.Colors[ImGuiCol_SliderGrab]       = white(0.92f);
    s.Colors[ImGuiCol_SliderGrabActive] = white(1.00f);
    s.Colors[ImGuiCol_Border]           = white(0.14f);
    s.Colors[ImGuiCol_BorderShadow]     = clear;
    s.Colors[ImGuiCol_Separator]        = white(0.10f);
    s.Colors[ImGuiCol_SeparatorHovered] = white(0.20f);
    s.Colors[ImGuiCol_SeparatorActive]  = white(0.30f);
    s.Colors[ImGuiCol_PlotHistogram]    = white(0.55f);
    s.Colors[ImGuiCol_PlotLines]        = white(0.70f);
    s.Colors[ImGuiCol_ResizeGrip]        = white(0.10f);
    s.Colors[ImGuiCol_ResizeGripHovered] = white(0.22f);
    s.Colors[ImGuiCol_ResizeGripActive]  = white(0.34f);

    // Grey text goes muddy against a background the shader is already pulling
    // down per pixel; white at low alpha holds its contrast wherever it lands.
    s.Colors[ImGuiCol_TextDisabled] = white(0.45f);

    // Popups float clear of the sheet with nothing refracted behind them, so
    // they are the one thing that still needs a ground of its own.
    s.Colors[ImGuiCol_PopupBg] = ImVec4(0.07f, 0.08f, 0.10f, 0.96f);
}

namespace {

void ApplyStyleOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& s = ImGui::GetStyle();
    s.WindowRounding          = 12.0f;
    s.ChildRounding           = 10.0f;
    // Capsules. ImDrawList clamps the radius to half the shorter side, so a
    // large number here just means "as round as it goes" and every control
    // ends up with the fully-rounded ends this material uses.
    s.FrameRounding           = 999.0f;
    s.GrabRounding            = 999.0f;
    s.PopupRounding           = 6.0f;
    s.ScrollbarRounding       = 10.0f;
    s.WindowBorderSize        = 1.0f;
    s.FrameBorderSize         = 0.0f;
    s.WindowPadding           = ImVec2(0, 0);
    s.ItemSpacing             = ImVec2(14, 10);
    s.ItemInnerSpacing        = ImVec2(8, 6);
    s.FramePadding            = ImVec2(14, 10);
    s.ScrollbarSize           = 26.0f;
    s.GrabMinSize             = 16.0f;
    s.SeparatorTextBorderSize = 3.0f;
    s.SeparatorTextPadding    = ImVec2(28, 8);

    // Keep the title bar painted with the focused/active color even when the
    // window loses focus (we only have one window).
    s.Colors[ImGuiCol_TitleBg]          = s.Colors[ImGuiCol_TitleBgActive];
    s.Colors[ImGuiCol_TitleBgCollapsed] = s.Colors[ImGuiCol_TitleBgActive];

    ApplyGlassPalette();
}

// ─── Page contents live in main_ui.cpp ──────────────────────────────────
// Width of the nav column. Named so the glass pane behind it and the child
// itself cannot drift apart.
constexpr float kSidebarW = 250.0f;

// The nav column is a slab in its own right now, but its left and bottom edges
// still sat flush against the window's while every other edge in the layout has
// a slot. Inset them by the same amount so the column is separated on all four
// sides rather than jammed into the corner.
constexpr float kNavInset = 14.0f;

// How far the nav column's pane and the title/content pane are held apart, over
// what radius the field between them is smoothed, and the strand left spanning
// the slot.
//
// The smooth union of two edges a gap g apart closes at the midline only when
// the merge radius exceeds 2g — below that the slot stays genuinely open, which
// is what is wanted here. So merge is deliberately under 2*gap and the slot
// does not heal on its own; a third small shape straddling the divide is what
// connects the two, and the same smoothing flares it into a proper neck where
// it lands on each side. Slot open above and below, one strand across.
// The slot at rest, and the surface tension holding the sheet together. The
// merge radius is a constant on purpose: it is a property of the material, not
// something to animate. A smooth union closes over only once the radius passes
// twice the gap, so with 30 here anything under 15px apart simply runs
// together — that threshold is what makes proximity do the work.
constexpr float kGlassGap      = 18.0f;
constexpr float kGlassMerge    = 30.0f;
constexpr float kGlassStrandAt = 0.46f;   // down the column, 0..1
constexpr float kGlassStrandH  = 96.0f;
constexpr float kStrandGrip    = 26.0f;   // how far the strand reaches into each side
// A liquid bridge thins as the bodies pull apart and lets go once tension can
// no longer hold it. Gap px at which it starts to neck, and where it snaps.
constexpr float kBridgeHold = 20.0f;
constexpr float kBridgeSnap = 31.0f;   // reachable: max retreat opens the slot to 32

// The nav column is its own body, so it does not follow the window instantly:
// this is the fraction of each frame's motion it fails to keep up with, and
// the spring in UpdateSpring is what brings it back.
//
// The whole column moves — pane and labels together — which is what lets the
// travel be symmetric. An earlier version moved only the pane's two inner
// edges, so retreating slid the pane out from under its own labels and the
// outward throw had to be capped at half the inward one. Carrying the widgets
// along removes that constraint entirely: the column is a slab that lags and
// springs back, and nothing it holds can come off it.
constexpr float kNavFollow = 0.85f;
constexpr float kNavLagMax = 34.0f;

// ─── Sidebar ─────────────────────────────────────────────────────────────
void DrawSidebar(Page& current, bool* keep_running, UiState* state) {
    // Wide enough that the labels clear the lensed band on both sides. The
    // right edge is the tighter of the two now that the slot eats half its
    // width out of this column, so the padding is set by that side and the
    // left simply inherits it.
    // Measured from the pane's edge, not the child's: the two differ by the
    // window padding on the left and by the inset on the right, and it is the
    // pane the eye reads the text against.
    constexpr float kInnerPadX     = 38.0f;
    // The nav column's pane starts below the title bar with a slot between
    // them, so its top edge is lensed too and the first entry has to clear it.
    // The lag needs no allowance here: the labels move with the pane.
    constexpr float kInnerPadY     = 44.0f;
    constexpr float kSelectableH   = 48.0f;
    // Inside a row, measured from the capsule's own edge. SelectableTextAlign
    // cannot express this: it is a fraction of the *leftover* space, so the
    // inset moves with the label's width — for these labels it worked out at
    // about 7px, which is why the icons sat against the capsule. The row lays
    // itself out instead.
    constexpr float kRowPadX       = 24.0f;
    constexpr float kRowIconGap    = 16.0f;
    constexpr float kFooterH       = 150.0f;
    constexpr float kBottomMargin  = 36.0f;

    // The selected entry is drawn below as a capsule, so ImGui's own Header
    // fills stay out of it entirely — a slab of flat blue was the single most
    // out-of-place thing on the sheet.
    const ImVec4 sel_bg(0, 0, 0, 0);
    // This child's own fill is opaque, and it is pushed after the glass code
    // has cleared ChildBg — so it was painting the nav column solid black over
    // the pane behind it. The pane is the background whenever there is one.
    ImGui::PushStyleColor(ImGuiCol_ChildBg,
                          state->screen_texture_id ? ImVec4(0, 0, 0, 0)
                                                   : ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,        sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  sel_bg);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,       ImVec2(kInnerPadX, kInnerPadY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,         ImVec2(0, 8));
    // Unused now that the row draws its own content, but the Selectable still
    // reads it, so keep it neutral.
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.0f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,        ImVec2(0, 6));

    // Ride the column's lag, so the labels stay put on the pane while the whole
    // slab trails the window and springs back. Without this the pane would slide
    // out from under them and the outward throw would have to be capped short.
    const ImVec2 nav_origin = ImGui::GetCursorScreenPos();
    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + state->glass_nav_offset.x,
                                     nav_origin.y + state->glass_nav_offset.y));

    // The border is a rectangle around the child, which cuts across the pane
    // and re-draws the seam the parting just removed.
    ImGui::BeginChild("##sidebar", ImVec2(kSidebarW, 0),
                      (state->screen_texture_id ? 0 : ImGuiChildFlags_Borders) |
                          ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    const ImU32 accent = ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 1.0f));
    for (int i = 0; i < kPagesCount; ++i) {
        const PageItem& p = kPages[i];
        const bool selected = (current == p.id);
        // The Selectable is the hit area and nothing else — an empty visible
        // label, because the row places its own icon and text below.
        ImGui::PushID(i);
        if (ImGui::Selectable("##nav", selected, 0, ImVec2(0, kSelectableH))) {
            current = p.id;
        }
        ripple::TouchLastItem();

        // A capsule rather than a filled bar, and the accent lives *in* it
        // instead of as a stripe alongside — a separate bar would collide with
        // the capsule's rounded end, and the colour reads better as the pill
        // being lit than as a marker stuck to its edge.
        const ImVec2 a = ImGui::GetItemRectMin();
        const ImVec2 b = ImGui::GetItemRectMax();
        const bool hovered = ImGui::IsItemHovered();
        if (selected) {
            ImGui::GetWindowDrawList()->AddRectFilled(
                a, b, (accent & ~IM_COL32_A_MASK) | (46u << IM_COL32_A_SHIFT),
                (b.y - a.y) * 0.5f);
            chrome::Rect(a, b, -1.0f, false, true);
        } else if (hovered) {
            chrome::Rect(a, b, -1.0f, true, false);
        }

        // Icon and label on a fixed pixel grid, both centred on the row's
        // middle so a tall icon and a short label share one baseline.
        ImDrawList* dl = ImGui::GetWindowDrawList();
        const float  cy  = (a.y + b.y) * 0.5f;
        const ImU32  col = ImGui::GetColorU32(
            ImVec4(1, 1, 1, selected ? 1.0f : (hovered ? 0.92f : 0.78f)));
        float x = a.x + kRowPadX;
        if (p.icon && *p.icon) {
            const ImVec2 is = ImGui::CalcTextSize(p.icon);
            dl->AddText(ImVec2(x, cy - is.y * 0.5f), col, p.icon);
            x += is.x + kRowIconGap;
        }
        const ImVec2 ls = ImGui::CalcTextSize(p.label);
        dl->AddText(ImVec2(x, cy - ls.y * 0.5f), col, p.label);
        ImGui::PopID();
    }

    float remaining = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - kFooterH - kBottomMargin;
    if (remaining > 0) ImGui::Dummy(ImVec2(0, remaining));

    // The footer sits on the same grid as the rows: the rule and the backend
    // name line up with the icons above them rather than starting at the
    // column's edge, and the rule gets air on both sides instead of being
    // sandwiched between the last row and the text under it.
    ImGui::Indent(kRowPadX);
    ImGui::Dummy(ImVec2(0, 6));
    ImGui::Separator();
    ImGui::Dummy(ImVec2(0, 10));
    ImGui::TextDisabled("%s", state->renderer_name ? state->renderer_name : "?");
    ImGui::Unindent(kRowPadX);
    ImGui::Dummy(ImVec2(0, 14));

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 14));
    const bool exit_pressed = ImGui::Button(ICON_FA_POWER u8"  退出", ImVec2(-1, 0));
    chrome::LastItem();
    if (exit_pressed) {
        if (!state->exit_anim_active) {
            // UV normalisation must use the *snapshot texture* dimensions —
            // which equal io.DisplaySize because the renderer sizes its
            // scene image to that. state->display_w/h are the physical
            // screen dimensions and would mis-map most particles off-frame.
            const ImGuiIO& io2 = ImGui::GetIO();
            dissolve::Begin(state->last_full_pos, state->last_full_size,
                           io2.DisplaySize.x, io2.DisplaySize.y);
            state->exit_anim_active      = true;
            state->exit_anim_first_frame = true;
            state->exit_anim_start       = (float)ImGui::GetTime();
        }
    }
    ripple::TouchLastItem();
    ImGui::PopStyleVar();

    ImGui::Dummy(ImVec2(0, kBottomMargin));

    ImGui::EndChild();

    // Hand the cursor back where the content expects it. The lag is this
    // column's alone, so leaving it in the cursor would drag the content pane
    // along with it and there would be no relative motion at all.
    ImGui::SetCursorScreenPos(ImVec2(nav_origin.x + kSidebarW, nav_origin.y));

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

// Forward decl — body lives further down, but DrawContent invokes it.
void DrawResizeGrip(const UiState* state);

// ─── Content gestures ────────────────────────────────────────────────────
// One drag in the content can mean two things — scroll the page, or move the
// window — so decide which from the gesture itself rather than reserving a
// strip of chrome for one of them. Restricting window moves to the title bar
// worked but made a large window awkward to reposition, since the only handle
// was a thin bar that might be anywhere on screen.
//
// The finger is given a few pixels of travel before anything happens, and what
// it does in those pixels decides the rest of the gesture:
//
//   mostly vertical, and the page has somewhere to scroll  -> scroll
//   anything else                                          -> move the window
//
// So a page that fits entirely drags the window from anywhere in it, a long
// page scrolls, and a sideways drag moves the window even on a long page. The
// choice is made once and held until release, because a gesture that changes
// meaning halfway through feels broken.
void ContentGesture(const char* id, UiState* state) {
    enum class Mode { Undecided, Scroll, Move, Widget };

    struct Drag {
        bool   active   = false;
        Mode   mode     = Mode::Undecided;
        ImVec2 start    = ImVec2(0, 0);
        float  velocity = 0.0f;   // scroll momentum, px/s
    };
    // Keyed by id: the sidebar and the content pane both scroll and must not
    // share momentum or a decision.
    static std::vector<std::pair<const char*, Drag>> states;
    Drag* d = nullptr;
    for (auto& e : states) if (e.first == id) { d = &e.second; break; }
    if (!d) { states.push_back({id, Drag{}}); d = &states.back().second; }

    ImGuiIO&    io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;
    const bool  hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                 ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (io.MouseDown[0] && (d->active || hovered)) {
        if (!d->active) {
            d->active = true;
            d->start  = io.MousePos;
            // A press that lands on a widget belongs to that widget for the
            // whole gesture; taking it back partway would need ClearActiveID,
            // which is not in the vendored public headers, and stealing a
            // slider's drag halfway would be worse than not scrolling.
            d->mode   = ImGui::IsAnyItemActive() ? Mode::Widget : Mode::Undecided;
        }

        const float dx = io.MousePos.x - d->start.x;
        const float dy = io.MousePos.y - d->start.y;

        if (d->mode == Mode::Undecided && (std::fabs(dx) > 6.0f || std::fabs(dy) > 6.0f)) {
            const bool vertical  = std::fabs(dy) > std::fabs(dx);
            const bool canScroll = ImGui::GetScrollMaxY() > 1.0f;
            d->mode = (vertical && canScroll) ? Mode::Scroll : Mode::Move;
        }

        if (d->mode == Mode::Scroll) {
            ImGui::SetScrollY(ImGui::GetScrollY() - io.MouseDelta.y);
            // Smoothed, so one jittery frame cannot define the throw.
            d->velocity = d->velocity * 0.65f + (-io.MouseDelta.y / dt) * 0.35f;
        } else if (d->mode == Mode::Move) {
            state->last_full_pos.x += io.MouseDelta.x;
            state->last_full_pos.y += io.MouseDelta.y;
            state->content_moving = true;
        }
    } else {
        state->content_moving = false;
        if (d->active && d->mode == Mode::Scroll) {
            // Released: glide on, shedding speed exponentially, coming to rest
            // in about a second so it reads as friction rather than the list
            // being yanked away.
            d->velocity *= std::exp(-4.5f * dt);
            if (std::fabs(d->velocity) > 8.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + d->velocity * dt);
            } else {
                d->velocity = 0.0f;
                d->active   = false;
                d->mode     = Mode::Undecided;
            }
        } else {
            d->active   = false;
            d->mode     = Mode::Undecided;
            d->velocity = 0.0f;
        }
    }
}

void DrawContent(UiState* state, Page page) {
    // The left edge is the tight one: the slot takes half its width out of
    // this side, so the body text would otherwise start inside the lensing.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(34, 20));

    ImGui::BeginChild("##content", ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    // Per-page body lives in main_ui.cpp.
    DrawPage(state, page);
    // Pips + preview frame only (input handled before Begin in DrawUi).
    DrawResizeGrip(state);
    ContentGesture("##content", state);
    ImGui::EndChild();

    ImGui::PopStyleVar();
}

// What the middle rest shows: enough to answer "is it running and how fast"
// without opening the window, and a line saying the next tap opens it.
void DrawCardContent(const UiState* state) {
    ImGuiIO& io = ImGui::GetIO();

    // The card has no title bar or child to inherit padding from, so its text
    // sat flush against the lensed rim — where the refraction is strongest and
    // least readable. Inset it clear of that band.
    constexpr float kCardPadX = 30.0f;
    constexpr float kCardPadY = 24.0f;
    ImGui::Indent(kCardPadX);
    ImGui::Dummy(ImVec2(0, kCardPadY));
    ImGui::PushFont(nullptr, 34.0f);
    if (state->card_icon) { ImGui::TextUnformatted(state->card_icon); ImGui::SameLine(0, 14); }
    ImGui::TextUnformatted(state->card_title ? state->card_title : "AImGui");
    ImGui::PopFont();
    ImGui::Spacing();

    // A caller-supplied body replaces the status block outright rather than
    // adding to it — the card is small, and a card showing both would overflow
    // rather than look full.
    if (state->card_body) {
        ImGui::PushTextWrapPos(0.0f);
        ImGui::TextUnformatted(state->card_body);
        ImGui::PopTextWrapPos();
        ImGui::Unindent(kCardPadX);
        return;
    }

    ImGui::Text(u8"%.0f FPS   ·   %.2f ms", io.Framerate, 1000.0f / io.Framerate);
    ImGui::TextDisabled("%s", state->renderer_name ? state->renderer_name : "?");

    ImGui::Spacing();
    ImGui::Separator();
    ImGui::Spacing();

    if (state->screen_mirror_running) {
        ImGui::TextDisabled(u8"液体玻璃   %dx%d", state->screen_mirror_w,
                            state->screen_mirror_h);
    } else {
        ImGui::TextDisabled(u8"液体玻璃   未开启");
    }
    ImGui::TextDisabled(u8"防录屏   %s", state->permeate_record ? u8"已开启" : u8"已关闭");

    ImGui::Dummy(ImVec2(0, 8));
    ImGui::TextDisabled(u8"再点一次展开窗口   ·   上滑收起");
    ImGui::Unindent(kCardPadX);
}

void DrawIslandContent(const UiState* state) {
    char buf[96];
    if (state->island_text) {
        std::snprintf(buf, sizeof(buf), "%s%s%s",
                      state->island_icon ? state->island_icon : "",
                      state->island_icon ? "  " : "",
                      state->island_text);
    } else {
        std::snprintf(buf, sizeof(buf), "%s%s%.0f FPS",
                      state->island_icon ? state->island_icon : "",
                      state->island_icon ? "  " : "",
                      ImGui::GetIO().Framerate);
    }

    const ImVec2 ts = ImGui::CalcTextSize(buf);
    const ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - ts.x) * 0.5f, (ws.y - ts.y) * 0.5f));
    ImGui::TextUnformatted(buf);
}

// The dot sits outside the ImGui window, so its content goes on the foreground
// list rather than through the layout.
void DrawDotContent(const UiState* state, float alpha) {
    if (state->dot_radius < 6.0f || alpha <= 0.01f) return;
    const char* txt = state->dot_text ? state->dot_text : state->island_icon;
    if (!txt || !*txt) return;
    const ImVec2 ts = ImGui::CalcTextSize(txt);
    ImGui::GetForegroundDrawList()->AddText(
        ImVec2(state->dot_center.x - ts.x * 0.5f,
               state->dot_center.y - ts.y * 0.5f),
        ImGui::GetColorU32(ImVec4(1, 1, 1, alpha)), txt);
}

// Bottom-right grip rect (in screen coords) anchored to the main window.
ImVec2 GripMin(const UiState* state) {
    constexpr float kGrip = 40.0f;
    return ImVec2(state->last_full_pos.x + state->last_full_size.x - kGrip,
                  state->last_full_pos.y + state->last_full_size.y - kGrip);
}
ImVec2 GripMax(const UiState* state) {
    return ImVec2(state->last_full_pos.x + state->last_full_size.x,
                  state->last_full_pos.y + state->last_full_size.y);
}

// Resize input is detected BEFORE Begin so that the ImGuiWindowFlags_NoMove
// can be applied this very frame to stop ImGui from also interpreting the
// touch as a window-drag-start. Without this, a press on the grip would
// kick off both the grip drag (our code) and the main window's title-bar
// move (ImGui's built-in), and the window would slide around under the
// finger as the size grew.
//
// Hit-test is done with raw math (not ImGui::IsMouseHoveringRect) — that
// helper defaults to clipping against the *current window*'s ClipRect, and
// we're called outside any Begin/End so its clip rect is empty / wrong,
// which silently produces "no hit" forever.
void HandleResizeInput(UiState* state, const ImGuiIO& io) {
    const ImVec2 grip_min = GripMin(state);
    const ImVec2 grip_max = GripMax(state);

    const bool inside = io.MousePos.x >= grip_min.x && io.MousePos.x < grip_max.x &&
                        io.MousePos.y >= grip_min.y && io.MousePos.y < grip_max.y;

    if (io.MouseClicked[0] && !state->resizing && inside) {
        state->resizing                = true;
        state->resize_drag_start_mouse = io.MousePos;
        state->resize_drag_start_size  = state->last_full_size;
        state->resize_target_size      = state->last_full_size;
    }
    if (state->resizing && io.MouseDown[0]) {
        const ImVec2 d(io.MousePos.x - state->resize_drag_start_mouse.x,
                       io.MousePos.y - state->resize_drag_start_mouse.y);
        state->resize_target_size = ImVec2(
            std::max(700.0f, state->resize_drag_start_size.x + d.x),
            std::max(560.0f, state->resize_drag_start_size.y + d.y));
    }
    if (state->resizing && !io.MouseDown[0]) {
        state->resizing        = false;
        state->resize_anim_vel = ImVec2(0, 0);
    }
}

// Visual-only: pips at the corner + preview frame while resizing. Input
// is handled by HandleResizeInput at the top of DrawUi.
void DrawResizeGrip(const UiState* state) {
    const ImVec2 grip_min = GripMin(state);
    const ImVec2 grip_max = GripMax(state);

    const bool inside = ImGui::IsMouseHoveringRect(grip_min, grip_max);
    ImDrawList* fg = ImGui::GetForegroundDrawList();
    const ImU32 col = ImGui::GetColorU32(
        state->resizing ? ImGuiCol_ResizeGripActive
                        : (inside ? ImGuiCol_ResizeGripHovered : ImGuiCol_ResizeGrip));

    for (int i = 0; i < 3; ++i) {
        const float o = 8.0f + i * 7.0f;
        fg->AddLine(ImVec2(grip_max.x - o, grip_max.y - 5),
                    ImVec2(grip_max.x - 5, grip_max.y - o),
                    col, 3.0f);
    }

    if (state->resizing) {
        const ImVec2 a = state->last_full_pos;
        const ImVec2 b(a.x + state->resize_target_size.x,
                       a.y + state->resize_target_size.y);
        fg->AddRect(a, b,
                    ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 0.95f)),
                    12.0f, 5.0f, 0);
    }
}

// Critically-ish damped spring with mild overshoot for the "灵动" feel.
// Under-damped on purpose: at 0.82 the window arrived and stopped, which is
// correct and lifeless. At 0.52 it overshoots slightly and settles back, which
// is what reads as elastic — the overshoot is the whole effect, and the squash
// applied from this spring's velocity is the other half of it.
void UpdateSpring(float* pos, float* vel, float target, float dt) {
    constexpr float kOmega = 7.2f;
    constexpr float kZeta  = 0.58f;
    const float diff  = target - *pos;
    const float accel = kOmega * kOmega * diff - 2.0f * kZeta * kOmega * (*vel);
    *vel += accel * dt;
    *pos += (*vel) * dt;
    if (std::abs(diff) < 0.001f && std::abs(*vel) < 0.005f) {
        *pos = target;
        *vel = 0.0f;
    }
}

} // namespace


// Liquid glass now lives in the renderers (core/glass_{gl,vk}.cpp, from
// core/shaders/glass.frag). Drawing it here meant displacing the UVs of a
// tessellated quad per-vertex, which cannot express dispersion — each colour
// channel needs its own bend — and quantised the lensing to the grid exactly
// where it varies fastest, at the border. The UI now just says where the panes
// are and the backend refracts them per-pixel.

void DrawUi(UiState* state, bool* keep_running) {
    ApplyStyleOnce();

    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    // Past the click frame the entire main window stops rendering — the
    // dissolve particles, which sample the frozen pre-click scene snapshot,
    // visually replace the UI. Where a chip has flown off, the rest of
    // the system surface shows through (no mask, no leftover frame).
    if (state->exit_anim_active && !state->exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - state->exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);

        // This path returns before the pane submission below ever runs, so the
        // count has to be cleared here too — leaving it alone meant the
        // renderer kept drawing whatever was submitted on the last normal
        // frame, which is the slab of glass still sitting there after the
        // window had come apart.
        state->glass_count = 0;

        ripple::DrawAll();
        dissolve::Step(dt, clamped,
                      (ImTextureID)(uintptr_t)state->scene_snapshot_id);

        if (t01 >= 1.0f) {
            state->exit_anim_active = false;
            *keep_running = false;
        }
        return;
    }

    // When the Live2D character is loaded it becomes the collapsed visual: a
    // draggable floating "ball". A small press-release on it is a tap (expand
    // the window + poke); a larger move drags the ball around. An open window
    // shows no model.
    bool l2d_active = false;
#ifdef AIMGUI_LIVE2D
    l2d_active = live2d::IsLoaded();
    if (l2d_active) {
        const float bdw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;
        const float bdh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        constexpr float kBallHalf = 100.0f;               // keep-on-screen margin
        if (state->ball_pos.x < 0.0f)                     // first-use placement
            state->ball_pos = ImVec2(bdw * 0.18f, bdh * 0.28f);
        auto clampf = [](float v, float lo, float hi) { return v < lo ? lo : (v > hi ? hi : v); };
        state->ball_pos.x = clampf(state->ball_pos.x, kBallHalf, bdw - kBallHalf);
        state->ball_pos.y = clampf(state->ball_pos.y, kBallHalf, bdh - kBallHalf);

        static bool   s_dragging = false, s_moved = false;
        static ImVec2 s_press(0, 0), s_off(0, 0);
        if (state->collapsed) {
            if (ImGui::IsMouseClicked(0) && live2d::HitCollapsed(io.MousePos.x, io.MousePos.y)) {
                s_dragging = true; s_moved = false; s_press = io.MousePos;
                s_off = ImVec2(state->ball_pos.x - io.MousePos.x, state->ball_pos.y - io.MousePos.y);
            }
            if (s_dragging && ImGui::IsMouseDown(0)) {
                float mdx = io.MousePos.x - s_press.x, mdy = io.MousePos.y - s_press.y;
                if (mdx * mdx + mdy * mdy > 24.0f * 24.0f) s_moved = true;
                if (s_moved) state->ball_pos = ImVec2(io.MousePos.x + s_off.x,
                                                      io.MousePos.y + s_off.y);
            }
            if (s_dragging && ImGui::IsMouseReleased(0)) {
                s_dragging = false;
                if (!s_moved) {
                    // One step per tap: ball -> card -> window.
                    if (state->stage < UiState::StageWindow) ++state->stage;
                    live2d::Poke();
                }
            }
        } else {
            s_dragging = false;
        }
    }
#endif

    // Handle grip press/drag/release first so the NoMove flag below sees
    // an up-to-date state->resizing this frame.
    HandleResizeInput(state, io);

    // Resize spring: when no drag is in progress, ease last_full_size
    // toward whatever target the previous drag (if any) parked it at.
    if (!state->resizing) {
        UpdateSpring(&state->last_full_size.x, &state->resize_anim_vel.x,
                     state->resize_target_size.x, dt);
        UpdateSpring(&state->last_full_size.y, &state->resize_anim_vel.y,
                     state->resize_target_size.y, dt);
    } else {
        // Hold the live window at the size it had when the drag started;
        // the preview frame reads from resize_target_size.
        state->last_full_size = state->resize_drag_start_size;
    }

    state->collapsed = (state->stage == UiState::StageIsland);
    const float target = (float)state->stage * 0.5f;
    UpdateSpring(&state->expand, &state->expand_vel, target, dt);
    const float t = state->expand;
    const float lt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    constexpr float kIslandW   = 280.0f;
    constexpr float kIslandH   = 56.0f;
    constexpr float kIslandTop = 28.0f;
    // The companion circle. The gap is deliberately under half the merge
    // radius, because a smooth union closes at the midline only past 2*gap —
    // so at rest the two are joined by a thread rather than being separate,
    // and it is the drag pulling them apart that breaks it.
    // How far the island slides at a full lean. Big enough that tipping the
    // phone visibly carries it, small enough that it cannot be tipped off the
    // screen — and it is only ever applied while the island is the island, so
    // an open window never wanders.
    constexpr float kTiltRange = 70.0f;
    constexpr float kDotD      = 56.0f;
    constexpr float kDotGap    = 12.0f;
    constexpr float kDotMerge  = 30.0f;

    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;
    // With the Live2D ball active the window springs from wherever the ball
    // sits (so collapsing returns to the ball's dragged position); otherwise
    // it uses the fixed top-centre island spot.
    // The dot hangs off the capsule's right, so what should sit centred on
    // screen is the pair. The offset fades out with the stage, since the dot
    // is gone by the time the card is open.
    const float dot_t = lt < 0.22f ? 1.0f - lt / 0.22f : 0.0f;
    const float pair_shift = (kDotD + kDotGap) * 0.5f * dot_t;

    // Lean carries the island. Faded out well before the window opens, and run
    // through a spring so it arrives with some weight rather than tracking the
    // sensor. Because this moves win_pos, the frame-to-frame velocity it
    // produces feeds the same lag the drag does — so the companion dot swings
    // behind the tilt too, with no extra machinery.
    const float tilt_w = 1.0f - (lt < 0.55f ? lt / 0.55f : 1.0f);
    UpdateSpring(&state->island_tilt.x, &state->island_tilt_vel.x,
                 state->tilt_x * kTiltRange * tilt_w, dt);
    UpdateSpring(&state->island_tilt.y, &state->island_tilt_vel.y,
                 state->tilt_y * kTiltRange * tilt_w, dt);

    const ImVec2 island_base = l2d_active
        ? ImVec2(state->ball_pos.x - kIslandW * 0.5f,
                 state->ball_pos.y - kIslandH * 0.5f)
        : ImVec2(dw * 0.5f - kIslandW * 0.5f - pair_shift, kIslandTop);

    // Hold the lean inside the screen, dot included, and drop the spring's
    // velocity at the stop so it does not wind up against the edge and fire
    // the island across the display the moment the phone comes back level.
    // The island rests near the top, so in practice only upward travel is
    // limited — which is what a thing already resting against a wall does.
    {
        const float dh_ = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
        const float margin = 12.0f;
        const float pair_w = kIslandW + (kDotGap + kDotD) * dot_t;
        auto hold = [](float* v, float* vel, float lo, float hi) {
            if (lo > hi) { *v = 0.0f; *vel = 0.0f; return; }
            if (*v < lo) { *v = lo; if (*vel < 0.0f) *vel = 0.0f; }
            if (*v > hi) { *v = hi; if (*vel > 0.0f) *vel = 0.0f; }
        };
        hold(&state->island_tilt.x, &state->island_tilt_vel.x,
             margin - island_base.x, dw - margin - pair_w - island_base.x);
        hold(&state->island_tilt.y, &state->island_tilt_vel.y,
             margin - island_base.y, dh_ - margin - kIslandH - island_base.y);
    }

    const ImVec2 island_pos(island_base.x + state->island_tilt.x,
                            island_base.y + state->island_tilt.y);
    const ImVec2 island_size(kIslandW, kIslandH);

    // The card: big enough to read, small enough to still feel like the island
    // opened rather than the window arrived. It grows from the island's centre
    // and is kept on screen, so opening a ball dragged to a corner does not put
    // the card half off the edge.
    const float dh = state->display_h > 0 ? (float)state->display_h : io.DisplaySize.y;
    const ImVec2 card_size(560.0f, 360.0f);
    ImVec2 card_pos(island_pos.x + kIslandW * 0.5f - card_size.x * 0.5f,
                    island_pos.y + kIslandH * 0.5f - card_size.y * 0.35f);
    if (card_pos.x < 16.0f) card_pos.x = 16.0f;
    if (card_pos.y < 16.0f) card_pos.y = 16.0f;
    if (card_pos.x + card_size.x > dw - 16.0f) card_pos.x = dw - 16.0f - card_size.x;
    if (card_pos.y + card_size.y > dh - 16.0f) card_pos.y = dh - 16.0f - card_size.y;

    auto lerp = [](ImVec2 a, ImVec2 b, float u) {
        return ImVec2(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u);
    };
    // Rotation swaps the display's dimensions, and a window that was fine in
    // portrait can end up entirely off-screen in landscape with no way to drag
    // it back. Keep a usable margin of it reachable whenever the display size
    // changes under it.
    if (state->display_w > 0 && state->display_h > 0) {
        const float keep = 120.0f;   // enough of the title bar to grab
        const float max_x = (float)state->display_w - keep;
        const float max_y = (float)state->display_h - keep;
        if (state->last_full_pos.x > max_x) state->last_full_pos.x = max_x;
        if (state->last_full_pos.y > max_y) state->last_full_pos.y = max_y;
        if (state->last_full_pos.x < 0.0f)  state->last_full_pos.x = 0.0f;
        if (state->last_full_pos.y < 0.0f)  state->last_full_pos.y = 0.0f;
        // A window wider or taller than the screen cannot be dragged back into
        // view either, so bring the size in as well.
        if (state->last_full_size.x > (float)state->display_w)
            state->last_full_size.x = (float)state->display_w;
        if (state->last_full_size.y > (float)state->display_h)
            state->last_full_size.y = (float)state->display_h;
    }

    // Two segments rather than one: island -> card over the first half of the
    // spring, card -> window over the second.
    ImVec2 win_pos, win_size;
    if (lt <= 0.5f) {
        const float k = lt * 2.0f;
        win_pos  = lerp(island_pos,  card_pos,  k);
        win_size = lerp(island_size, card_size, k);
    } else {
        const float k = (lt - 0.5f) * 2.0f;
        win_pos  = lerp(card_pos,  state->last_full_pos,  k);
        win_size = lerp(card_size, state->last_full_size, k);
    }

    // Squash and stretch, taken straight from the spring's velocity: opening
    // stretches along the direction of growth and pinches across it, closing
    // does the reverse, and it vanishes the moment the spring settles. Without
    // this the overshoot alone just looks like a bounce; the deformation is
    // what makes the thing feel soft rather than rigid.
    {
        float j = state->expand_vel * 0.055f;
        if (j >  0.16f) j =  0.16f;
        if (j < -0.16f) j = -0.16f;
        const ImVec2 c(win_pos.x + win_size.x * 0.5f, win_pos.y + win_size.y * 0.5f);
        win_size.x *= (1.0f + j * 0.55f);
        win_size.y *= (1.0f - j * 0.85f);
        win_pos.x = c.x - win_size.x * 0.5f;
        win_pos.y = c.y - win_size.y * 0.5f;
    }

    // Above the card's rest, and clear of it: the spring is deliberately
    // under-damped and overshoots past 0.5 on its way to the card, so a
    // threshold close to it would flash the title bar during the bounce.
    const bool show_chrome    = (lt > 0.75f);
    const bool overriding_pos = (lt < 0.999f);

    if (overriding_pos) {
        ImGui::SetNextWindowPos (win_pos);
        ImGui::SetNextWindowSize(win_size);
    } else {
        // A drag in the content moves the window, so on those frames the
        // position here is authoritative and has to be pushed every frame;
        // otherwise ImGui owns it and this is just the initial placement.
        ImGui::SetNextWindowPos(state->last_full_pos,
                                state->content_moving ? ImGuiCond_Always
                                                      : ImGuiCond_FirstUseEver);
        // last_full_size is now driven by the custom resize spring, so
        // push it every frame instead of only once.
        ImGui::SetNextWindowSize(state->last_full_size, ImGuiCond_Always);
        ImGui::SetNextWindowSizeConstraints(ImVec2(700, 560), ImVec2(FLT_MAX, FLT_MAX));
    }

    // ImGui fills the title bar and the sidebar child with its own colours,
    // which sit directly on top of the panes drawn for them and hide the glass
    // completely. Clear those fills while the glass is up — the pane is the
    // background now.
    int pushed_glass_text = 0;
    if (state->screen_texture_id) {
        // One sheet, so one fill — the same colour and the same alpha on the
        // window body and the title bar, and nothing at all on the children.
        //
        // The content area looked greyer than the title bar because it was:
        // a child paints ChildBg *over* the window's WindowBg, so that region
        // carried two fills where the title bar, which ImGui paints separately,
        // carried one. Matching the two colours is not enough while one side is
        // doubled; the children have to contribute nothing.
        // Nothing at all, now that the panes can be parted: any fill ImGui
        // paints is a window-shaped rectangle, so it would bridge the slot
        // between them with a flat wash and undo the parting. The body the
        // fill used to provide is the shader's own wash instead — one wash on
        // the real silhouette rather than two on different shapes — which is
        // what glass_clarity's default accounts for.
        const ImVec4 kNone(0, 0, 0, 0);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,         kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBg,          kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    kNone);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kNone);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,          kNone);
        pushed_glass_text = 5;
    }
    const float rounding = (kIslandH * 0.5f) * (1.0f - lt) + 12.0f * lt;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowRounding, rounding);

    // Live screen behind the window. Drawn into the window's own draw list
    // beneath its contents, sampling the region of the mirror that sits behind
    // where the window actually is, so it reads as glass rather than a picture.
    // Panes for this frame: the window, its title bar and the sidebar. Handed
    // to the renderer, which refracts them before ImGui's widgets are drawn on
    // top. The title bar and sidebar get a tighter edge and a gentler bend so
    // they read as thinner pieces set into the same pane.
    // A single pane for the whole window. The title bar and sidebar used to get
    // panes of their own, but every pane carries its own lensed rim, caustic
    // and specular — so their borders ran straight through the middle of the
    // sheet and read as seams between separate pieces of glass. They are
    // regions of one sheet, not three sheets, so they are differentiated by
    // density instead (the fills pushed below) and the glass stays continuous.
    // win_pos/win_size already interpolate all the way down to the collapsed
    // pill, and `rounding` follows them, so the island is the same pane at a
    // different size — it only ever looked bare because this was gated on the
    // window being expanded, and faded out with it.
    // Nothing to submit once the window is coming apart: the particles carry
    // the glass with them. The snapshot they sample is the scene image, and
    // the panes are drawn into that before ImGui's widgets — so the refraction
    // is already baked into every particle. Leaving the pane submitted just
    // left a rectangle of glass hanging where the window used to be.
    state->glass_count = 0;
    if (state->screen_texture_id && !state->exit_anim_active) {
        GlassRect base{};
        base.rounding = rounding;
        base.alpha    = 1.0f;
        base.tintA    = state->glass_clarity;
        base.lightX   = state->glass_light_x;
        base.lightY   = state->glass_light_y;
        // The pill is small, so its rim would otherwise reach most of the way
        // across it; scale the lensing down with the shorter side.
        const float minSide = win_size.x < win_size.y ? win_size.x : win_size.y;
        if (minSide < 200.0f) {
            base.edgeWidth = minSide * 0.30f;
            base.blur      = 5.0f;
        }

        // Below the full-window stage there is nothing to part: the island and
        // the card are a single body, and win_pos/win_size already interpolate
        // all the way down to the pill.
        // The nav column is left behind by the window's motion and springs back
        // after it. Each frame's movement is subtracted from where the body has
        // got to, then the spring pulls it home — under-damped, so it comes
        // back through the rest position and briefly presses into its
        // neighbour before settling. Nothing here touches the merge; all of it
        // is distance, and the merge threshold turns that into contact.
        {
            const float dt = ImGui::GetIO().DeltaTime;
            if (state->glass_pos_valid) {
                state->glass_nav_lag.x -= (win_pos.x - state->glass_prev_pos.x) * kNavFollow;
                state->glass_nav_lag.y -= (win_pos.y - state->glass_prev_pos.y) * kNavFollow;
            }
            state->glass_prev_pos  = win_pos;
            state->glass_pos_valid = true;
            UpdateSpring(&state->glass_nav_lag.x, &state->glass_nav_lag_vel.x, 0.0f, dt);
            UpdateSpring(&state->glass_nav_lag.y, &state->glass_nav_lag_vel.y, 0.0f, dt);
            // Bounded so a fling cannot throw the column clean off the window.
            // The velocity is dropped at the stop too, or the spring winds up
            // against the cap and fires the body across the slot the moment the
            // drag ends.
            auto hold = [](float* v, float* vel, float lim) {
                if (*v < -lim) { *v = -lim; if (*vel < 0.0f) *vel = 0.0f; }
                if (*v >  lim) { *v =  lim; if (*vel > 0.0f) *vel = 0.0f; }
            };
            hold(&state->glass_nav_lag.x, &state->glass_nav_lag_vel.x, kNavLagMax);
            hold(&state->glass_nav_lag.y, &state->glass_nav_lag_vel.y, kNavLagMax);
        }

        const float split = lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f;
        GlassRect a = base, b = base, strand = base, title = base;
        bool parted = false;
        if (split > 0.01f) {
            // The title bar belongs to the content, not to the nav column: it
            // is its own full-width shape butted against the content's top edge
            // so the two are one body, and the nav column is the thing that
            // comes away. Making the nav column full height instead put the
            // window's title on it, which read as the title belonging to the
            // menu — the opposite of what the split is for.
            //
            // The gaps open from zero with the stage, so the parting grows out
            // of the single pane rather than appearing on top of it; at zero
            // every shape meets its neighbour exactly and the smoothing fills
            // the notches their rounded corners would leave along the seams.
            const ImGuiStyle& stl = ImGui::GetStyle();
            const float divide  = win_pos.x + stl.WindowPadding.x + kSidebarW;
            const float title_h = ImGui::GetFontSize() + stl.FramePadding.y * 2.0f;
            const float gap     = kGlassGap * split;
            const float win_r   = win_pos.x + win_size.x;
            const float win_b   = win_pos.y + win_size.y;
            // Faded in with the stage and published for DrawSidebar, so the
            // widgets and the pane they sit on move by exactly one value.
            const ImVec2 lag(state->glass_nav_lag.x * split, state->glass_nav_lag.y * split);
            state->glass_nav_offset = lag;

            // A little past the title bar so it overlaps the content and the
            // two are unambiguously one body. Kept small: the nav column's slot
            // is measured from this edge, not from the title bar's, and every
            // pixel of overlap is a pixel the slot loses.
            constexpr float kTitleOverlap = 4.0f;
            title.x = win_pos.x;
            title.y = win_pos.y;
            title.w = win_size.x;
            title.h = title_h + kTitleOverlap;

            b.x = divide + gap * 0.5f;
            b.y = win_pos.y + title_h;
            b.w = win_r - b.x;
            b.h = win_b - b.y;

            // The whole column carries the lag, size fixed — it is a slab that
            // trails the window, not a shape that stretches. DrawSidebar moves
            // its widgets by the same offset, so the labels ride along and the
            // throw can be as far in one direction as the other.
            const float nav_y0 = win_pos.y + title_h + kTitleOverlap + gap;
            a.x = win_pos.x + kNavInset + lag.x;
            a.y = nav_y0 + lag.y;
            a.w = (divide - gap * 0.5f) - (win_pos.x + kNavInset);
            a.h = (win_b - kNavInset) - nav_y0;

            // The strand is what is left of the join once the bodies are too
            // far apart for the merge alone. It necks down as the slot opens
            // and lets go at kBridgeSnap — and because the slot's width is now
            // a real distance rather than a parameter, so is the moment it
            // breaks and the moment it reforms.
            const float nav_r = a.x + a.w;
            const float slot  = b.x - nav_r;
            const float u     = (slot - kBridgeHold) / (kBridgeSnap - kBridgeHold);
            const float uc    = u < 0.0f ? 0.0f : (u > 1.0f ? 1.0f : u);
            const float neck  = 1.0f - uc * uc * (3.0f - 2.0f * uc);
            strand.w = slot + kStrandGrip * neck;
            strand.h = kGlassStrandH * neck;
            strand.x = nav_r - kStrandGrip * neck * 0.5f;
            strand.y = a.y + (win_b - a.y) * kGlassStrandAt - strand.h * 0.5f;

            a.merge = b.merge = strand.merge = title.merge = kGlassMerge;
            parted = a.w > 2.0f && b.w > 2.0f && a.h > 2.0f;
        }
        if (parted) {
            state->glass_rects[state->glass_count++] = title;
            state->glass_rects[state->glass_count++] = b;
            state->glass_rects[state->glass_count++] = a;
            state->glass_rects[state->glass_count++] = strand;
        } else {
            state->glass_nav_offset = ImVec2(0, 0);
            GlassRect r = base;
            r.x = win_pos.x; r.y = win_pos.y; r.w = win_size.x; r.h = win_size.y;
            if (dot_t > 0.02f) {
                // Its own body, and it lags the way the nav column does — so
                // the thread between the two stretches when the island is
                // thrown one way and the two run together when it is thrown
                // the other. Nothing about the join is animated; only where
                // the dot is.
                GlassRect dot = base;
                dot.w = dot.h = kDotD * dot_t;
                dot.x = win_pos.x + win_size.x + kDotGap
                        + state->glass_nav_lag.x * dot_t;
                dot.y = win_pos.y + win_size.y * 0.5f - dot.h * 0.5f
                        + state->glass_nav_lag.y * dot_t;
                r.merge = dot.merge = kDotMerge;
                state->dot_center = ImVec2(dot.x + dot.w * 0.5f, dot.y + dot.h * 0.5f);
                state->dot_radius = dot.w * 0.5f;
                state->glass_rects[state->glass_count++] = r;
                state->glass_rects[state->glass_count++] = dot;
            } else {
                state->dot_radius = 0.0f;
                state->glass_rects[state->glass_count++] = r;
            }
        }
    }

    // With the Live2D character as the collapsed visual, fade the window
    // background + border in as it expands so only the character shows when
    // collapsed (no stray pill box around the tiny character).
    const bool l2d_hidden_chrome = l2d_active && lt < 0.999f;
    // ImGui's border is a rectangle around the window, so once the panes are
    // parted it would run straight across the slot. The glass draws its own
    // edge stroke on the real silhouette anyway.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize,
                        (l2d_hidden_chrome || state->screen_texture_id) ? 0.0f : 1.0f);
    if (l2d_hidden_chrome) {
        ImGui::SetNextWindowBgAlpha(lt);
    } else if (state->screen_texture_id) {
        // WindowBg is pushed explicitly above so it can match the title bar
        // exactly; overriding its alpha here as well would undo that.
    }

    // Suppress ImGui's built-in resize handle: the custom DrawResizeGrip
    // (with preview-then-animate behaviour) owns resizing. NoMove is also
    // applied during a grip drag to stop ImGui from interpreting the same
    // touch as a window-drag-start and sliding the window under the finger.
    ImGuiWindowFlags flags = ImGuiWindowFlags_NoCollapse |
                             ImGuiWindowFlags_NoSavedSettings |
                             ImGuiWindowFlags_NoResize;
    if (!show_chrome || state->resizing) {
        flags |= ImGuiWindowFlags_NoMove;
    }
    if (!show_chrome) {
        flags |= ImGuiWindowFlags_NoTitleBar;
    }

    char title[64];
    std::snprintf(title, sizeof(title), "AImGui  v%s###aimgui_main", ImGui::GetVersion());

    if (ImGui::Begin(title, show_chrome ? keep_running : nullptr, flags)) {
        if (lt >= 0.999f && state->stage == UiState::StageWindow) {
            state->last_full_pos  = ImGui::GetWindowPos();
            state->last_full_size = ImGui::GetWindowSize();
        }

        // The FPS pill is only the collapsed visual when there's no character;
        // with Live2D the tiny character replaces it (and its own tap handler,
        // above, drives the expand).
        const float island_alpha = 1.0f - (lt < 0.30f ? lt / 0.30f : 1.0f);
        if (!l2d_active && island_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, island_alpha);
            DrawIslandContent(state);
            DrawDotContent(state, island_alpha);
            ImGui::PopStyleVar();

            if (!show_chrome && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                if (state->stage < UiState::StageWindow) ++state->stage;
            }
        }

        // The card. It occupies the gap the two fades above and below leave —
        // the island's content is gone by 0.30 and the window's does not arrive
        // until 0.70, so without this the middle rest is an empty sheet of
        // glass. A glance's worth of information and an invitation to open the
        // rest.
        const float card_alpha = (lt <= 0.30f || lt >= 0.72f) ? 0.0f
                               : (lt < 0.46f ? (lt - 0.30f) / 0.16f
                                             : (lt > 0.62f ? (0.72f - lt) / 0.10f : 1.0f));
        if (card_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, card_alpha);
            DrawCardContent(state);
            ImGui::PopStyleVar();

            // Tap opens the next rest; a flick upwards closes back to the
            // island. Up is the direction the card came from, so sending it
            // back that way is the gesture that already means "put it away".
            static bool   s_press   = false;
            static ImVec2 s_from    = ImVec2(0, 0);
            static bool   s_handled = false;
            if (ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                s_press = true; s_handled = false; s_from = io.MousePos;
            }
            if (s_press && io.MouseDown[0] && !s_handled) {
                if (io.MousePos.y - s_from.y < -46.0f) {
                    state->stage = UiState::StageIsland;
                    s_handled = true;
                }
            }
            if (s_press && !io.MouseDown[0]) {
                const float dy = io.MousePos.y - s_from.y;
                const float dx = io.MousePos.x - s_from.x;
                if (!s_handled && std::fabs(dy) < 18.0f && std::fabs(dx) < 18.0f &&
                    state->stage < UiState::StageWindow) {
                    ++state->stage;
                }
                s_press = false;
            }
        }

        const float full_alpha = lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f;
        if (full_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, full_alpha);

            static Page page = Page::Dashboard;
            // No SameLine: DrawSidebar hands the cursor back itself, and
            // SameLine would recompute it from the child's own advance —
            // which carries the column's lag and would drag the content
            // along with it.
            DrawSidebar(page, keep_running, state);
            DrawContent(state, page);

            ImGui::PopStyleVar();
        }
    }
    ImGui::End();

    ImGui::PopStyleVar(2);   // WindowRounding + WindowBorderSize
    if (pushed_glass_text) ImGui::PopStyleColor(pushed_glass_text);

    // Foreground overlays: ripples on every clickable widget.
    ripple::DrawAll();

    // On the click frame the particles still need advancing/drawing so the
    // visual is continuous with the next frame, but the UI under them is
    // still the real one (so the user perceives the surface itself
    // coming apart). After this frame the early-return path takes over.
    if (state->exit_anim_active && state->exit_anim_first_frame) {
        const float now     = (float)ImGui::GetTime();
        const float t01     = (now - state->exit_anim_start) / 1.35f;
        const float clamped = t01 < 0.0f ? 0.0f : (t01 > 1.0f ? 1.0f : t01);
        dissolve::Step(dt, clamped,
                      (ImTextureID)(uintptr_t)state->scene_snapshot_id);
        state->exit_anim_first_frame = false;
    }
}

} // namespace aimgui
