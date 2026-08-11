#include "ui/ui.h"
#include "ui/main_ui.h"

#include "imgui.h"
#include "platform/ANativeWindowCreator.h"

#ifdef AIMGUI_LIVE2D
#include "live2d/live2d_view.h"
#endif

#include <cmath>
#include <cstdint>
#include <cstdio>
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


void ApplyStyleOnce() {
    static bool done = false;
    if (done) return;
    done = true;
    auto& s = ImGui::GetStyle();
    s.WindowRounding          = 12.0f;
    s.ChildRounding           = 10.0f;
    s.FrameRounding           = 6.0f;
    s.GrabRounding            = 6.0f;
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
}

// ─── Page contents live in main_ui.cpp ──────────────────────────────────
// Width of the nav column. Named so the glass pane behind it and the child
// itself cannot drift apart.
constexpr float kSidebarW = 230.0f;

// ─── Sidebar ─────────────────────────────────────────────────────────────
void DrawSidebar(Page& current, bool* keep_running, UiState* state) {
    constexpr float kInnerPadX     = 18.0f;
    constexpr float kInnerPadY     = 14.0f;
    constexpr float kSelectableH   = 44.0f;
    constexpr float kAccentInset   = 10.0f;
    constexpr float kAccentW       = 4.0f;
    constexpr float kFooterH       = 110.0f;
    constexpr float kBottomMargin  = 16.0f;

    // Click on a sidebar entry should land directly on the selected color —
    // no intermediate hover-gray or transient pressed-blue. Push the same
    // color into all three slots.
    const ImVec4 sel_bg(0.22f, 0.40f, 0.78f, 0.55f);
    ImGui::PushStyleColor(ImGuiCol_ChildBg,       ImVec4(0.07f, 0.08f, 0.10f, 1.0f));
    ImGui::PushStyleColor(ImGuiCol_Header,        sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderHovered, sel_bg);
    ImGui::PushStyleColor(ImGuiCol_HeaderActive,  sel_bg);

    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding,       ImVec2(kInnerPadX, kInnerPadY));
    ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing,         ImVec2(0, 4));
    // Inset the label inside the highlight rect so text doesn't touch the
    // selected (blue) background's left edge.
    ImGui::PushStyleVar(ImGuiStyleVar_SelectableTextAlign, ImVec2(0.08f, 0.5f));
    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding,        ImVec2(0, 6));

    ImGui::BeginChild("##sidebar", ImVec2(kSidebarW, 0),
                      ImGuiChildFlags_Borders | ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);

    const ImU32 accent = ImGui::GetColorU32(ImVec4(0.30f, 0.62f, 1.0f, 1.0f));
    for (int i = 0; i < kPagesCount; ++i) {
        const PageItem& p = kPages[i];
        bool selected = (current == p.id);
        if (ImGui::Selectable(p.label, selected, 0, ImVec2(0, kSelectableH))) {
            current = p.id;
        }
        ripple::TouchLastItem();
        if (selected) {
            ImVec2 a = ImGui::GetItemRectMin();
            ImVec2 b = ImGui::GetItemRectMax();
            ImGui::GetWindowDrawList()->AddRectFilled(
                ImVec2(a.x - kAccentInset,            a.y + 8),
                ImVec2(a.x - kAccentInset + kAccentW, b.y - 8),
                accent, kAccentW * 0.5f);
        }
    }

    float remaining = ImGui::GetWindowHeight() - ImGui::GetCursorPosY() - kFooterH - kBottomMargin;
    if (remaining > 0) ImGui::Dummy(ImVec2(0, remaining));

    ImGui::Separator();
    ImGui::Spacing();
    ImGui::TextDisabled("%s", state->renderer_name ? state->renderer_name : "?");
    ImGui::Spacing();

    ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(0, 12));
    if (ImGui::Button(u8"退出", ImVec2(-1, 0))) {
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

    ImGui::PopStyleVar(4);
    ImGui::PopStyleColor(4);
}

// Forward decl — body lives further down, but DrawContent invokes it.
void DrawResizeGrip(const UiState* state);

// ─── Touch scrolling ─────────────────────────────────────────────────────
// Drag anywhere in the content to scroll it, with the throw-and-glide the
// gesture implies. Call once inside the child, after its contents.
//
// A scrollbar is a mouse affordance: it asks for a precise grab on a 26 px
// target, which is the wrong thing to hand a finger on a surface this size.
// Dragging the content itself is how every touch UI does this.
void TouchScroll(const char* id) {
    // Per-child state, keyed by id — the sidebar and the content pane both
    // scroll and must not share momentum.
    struct Scroll {
        bool  dragging   = false;
        bool  cancelled  = false;   // gesture claimed by a widget
        float start_y    = 0.0f;
        float velocity   = 0.0f;
    };
    static std::vector<std::pair<const char*, Scroll>> states;
    Scroll* st = nullptr;
    for (auto& e : states) if (e.first == id) { st = &e.second; break; }
    if (!st) { states.push_back({id, Scroll{}}); st = &states.back().second; }

    ImGuiIO&   io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    const bool hovered = ImGui::IsWindowHovered(ImGuiHoveredFlags_ChildWindows |
                                                ImGuiHoveredFlags_AllowWhenBlockedByActiveItem);

    if (io.MouseDown[0] && hovered && !st->cancelled) {
        if (!st->dragging) {
            st->dragging = true;
            st->start_y  = io.MousePos.y;
            // A press that lands on a widget belongs to that widget for the
            // whole gesture. Taking it back partway would need ClearActiveID,
            // which lives in imgui_internal.h and is not vendored here — and
            // handing a slider's drag to the scroller halfway through would be
            // worse than not scrolling. Pressing anywhere else scrolls.
            st->cancelled = ImGui::IsAnyItemActive();
        }
        if (!st->cancelled && std::fabs(io.MousePos.y - st->start_y) > 2.0f) {
            const float d = io.MouseDelta.y;
            ImGui::SetScrollY(ImGui::GetScrollY() - d);
            // Smoothed so a jittery last frame doesn't define the throw.
            st->velocity = st->velocity * 0.65f + (-d / dt) * 0.35f;
        }
    } else {
        if (st->dragging && !st->cancelled) {
            // Released: glide on, shedding speed exponentially. 4.5/s reaches a
            // stop in roughly a second, which reads as friction rather than as
            // the list being yanked away.
            st->velocity *= std::exp(-4.5f * dt);
            if (std::fabs(st->velocity) > 8.0f) {
                ImGui::SetScrollY(ImGui::GetScrollY() + st->velocity * dt);
            } else {
                st->velocity = 0.0f;
                st->dragging = false;
            }
        } else {
            st->dragging  = false;
            st->cancelled = false;
            st->velocity  = 0.0f;
        }
    }
}

void DrawContent(UiState* state, Page page) {
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(22, 18));

    ImGui::BeginChild("##content", ImVec2(0, 0),
                      ImGuiChildFlags_AlwaysUseWindowPadding,
                      ImGuiWindowFlags_NoScrollbar);
    // Per-page body lives in main_ui.cpp.
    DrawPage(state, page);
    // Pips + preview frame only (input handled before Begin in DrawUi).
    DrawResizeGrip(state);
    TouchScroll("##content");
    ImGui::EndChild();

    ImGui::PopStyleVar();
}

void DrawIslandContent() {
    ImGuiIO& io = ImGui::GetIO();
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.0f FPS", io.Framerate);

    ImVec2 ts = ImGui::CalcTextSize(buf);
    ImVec2 ws = ImGui::GetWindowSize();
    ImGui::SetCursorPos(ImVec2((ws.x - ts.x) * 0.5f,
                               (ws.y - ts.y) * 0.5f));
    ImGui::TextUnformatted(buf);
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
void UpdateSpring(float* pos, float* vel, float target, float dt) {
    constexpr float kOmega = 12.0f;
    constexpr float kZeta  = 0.82f;
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
                if (!s_moved) { state->collapsed = false; live2d::Poke(); }
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

    const float target = state->collapsed ? 0.0f : 1.0f;
    UpdateSpring(&state->expand, &state->expand_vel, target, dt);
    const float t = state->expand;
    const float lt = t < 0.0f ? 0.0f : (t > 1.0f ? 1.0f : t);

    constexpr float kIslandW   = 280.0f;
    constexpr float kIslandH   = 56.0f;
    constexpr float kIslandTop = 28.0f;

    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;
    // With the Live2D ball active the window springs from wherever the ball
    // sits (so collapsing returns to the ball's dragged position); otherwise
    // it uses the fixed top-centre island spot.
    const ImVec2 island_pos = l2d_active
        ? ImVec2(state->ball_pos.x - kIslandW * 0.5f, state->ball_pos.y - kIslandH * 0.5f)
        : ImVec2(dw * 0.5f - kIslandW * 0.5f, kIslandTop);
    const ImVec2 island_size(kIslandW, kIslandH);

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

    const ImVec2 win_pos  = lerp(island_pos,  state->last_full_pos,  lt);
    const ImVec2 win_size = lerp(island_size, state->last_full_size, lt);

    const bool show_chrome    = (lt > 0.55f);
    const bool overriding_pos = (lt < 0.999f);

    if (overriding_pos) {
        ImGui::SetNextWindowPos (win_pos);
        ImGui::SetNextWindowSize(win_size);
    } else {
        ImGui::SetNextWindowPos (state->last_full_pos,  ImGuiCond_FirstUseEver);
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
        const ImVec4 kSheet(1, 1, 1, 0.05f);
        ImGui::PushStyleColor(ImGuiCol_WindowBg,         kSheet);
        ImGui::PushStyleColor(ImGuiCol_TitleBg,          kSheet);
        ImGui::PushStyleColor(ImGuiCol_TitleBgActive,    kSheet);
        ImGui::PushStyleColor(ImGuiCol_TitleBgCollapsed, kSheet);
        ImGui::PushStyleColor(ImGuiCol_ChildBg,          ImVec4(0, 0, 0, 0));
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
        GlassRect r{};
        r.x = win_pos.x; r.y = win_pos.y; r.w = win_size.x; r.h = win_size.y;
        r.rounding = rounding;
        r.alpha = 1.0f;
        r.tintA = 0.06f;
        // The pill is small, so its rim would otherwise reach most of the way
        // across it; scale the lensing down with the shorter side.
        const float minSide = win_size.x < win_size.y ? win_size.x : win_size.y;
        if (minSide < 200.0f) {
            r.edgeWidth = minSide * 0.30f;
            r.blur      = 5.0f;
        }
        state->glass_rects[state->glass_count++] = r;
    }

    // Frosted-glass backdrop: hand SurfaceFlinger the window's current rect
    // and let the compositor blur what is behind it. Follows the collapse /
    // drag / resize animation for free because win_pos/win_size are already
    // the animated values. Costs nothing per frame when unchanged — the
    // helper only sends a transaction when the rect or radius moves.
    state->backdrop_blur_supported = android::ANativeWindowCreator::BlurAvailable();
    if (state->backdrop_blur_supported) {
        const int radius = state->backdrop_blur ? (int)state->backdrop_blur_radius : 0;
        android::detail::ui::Rect r{
            (int32_t)win_pos.x,
            (int32_t)win_pos.y,
            (int32_t)(win_pos.x + win_size.x),
            (int32_t)(win_pos.y + win_size.y),
        };
        android::ANativeWindowCreator::SetBackdropBlur(state->display_w > state->display_h
                                                           ? state->display_w : state->display_h,
                                                       radius, r);
    }

    // With the Live2D character as the collapsed visual, fade the window
    // background + border in as it expands so only the character shows when
    // collapsed (no stray pill box around the tiny character).
    const bool l2d_hidden_chrome = l2d_active && lt < 0.999f;
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, l2d_hidden_chrome ? 0.0f : 1.0f);
    if (l2d_hidden_chrome) {
        ImGui::SetNextWindowBgAlpha(lt);
    } else if (state->screen_texture_id) {
        // WindowBg is pushed explicitly above so it can match the title bar
        // exactly; overriding its alpha here as well would undo that.
    } else if (state->backdrop_blur && state->backdrop_blur_supported) {
        // Same reasoning for SurfaceFlinger's own blur, which is also behind us.
        ImGui::SetNextWindowBgAlpha(0.45f);
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
        if (lt >= 0.999f && !state->collapsed) {
            state->last_full_pos  = ImGui::GetWindowPos();
            state->last_full_size = ImGui::GetWindowSize();
        }

        // The FPS pill is only the collapsed visual when there's no character;
        // with Live2D the tiny character replaces it (and its own tap handler,
        // above, drives the expand).
        const float island_alpha = 1.0f - (lt < 0.30f ? lt / 0.30f : 1.0f);
        if (!l2d_active && island_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, island_alpha);
            DrawIslandContent();
            ImGui::PopStyleVar();

            if (!show_chrome && ImGui::IsWindowHovered() && ImGui::IsMouseClicked(0)) {
                state->collapsed = false;
            }
        }

        const float full_alpha = lt > 0.70f ? (lt - 0.70f) / 0.30f : 0.0f;
        if (full_alpha > 0.01f) {
            ImGui::PushStyleVar(ImGuiStyleVar_Alpha, full_alpha);

            static Page page = Page::Dashboard;
            DrawSidebar(page, keep_running, state);
            ImGui::SameLine(0, 0);
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
