// Liquid-glass modal.
//
// Three bodies, not one panel: a capsule for the text and two smaller ones
// beneath it for the answers. They are drawn out of the Dynamic Island's capsule
// and hang below it. Whether they *merge* with the shell depends on what the
// shell is: beside the island or the card they share its pane group and neck to
// it; over a window they cannot, because a smooth union swallows a shape that
// lies inside another one, and a window contains the island's spot.

#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "core/haptics.h"

#include "imgui.h"

#include <cmath>
#include <string>

namespace aimgui {
namespace dialog {
namespace {

// Width is measured from the words, between these bounds. The two answers split
// it, so it also sets how wide a button gets. The floor keeps a short question
// reading as a dialog rather than a tooltip; the cap is what makes long text
// wrap instead of running the width of the phone.
constexpr float kBodyWMin   = 300.0f;
constexpr float kBodyWMax   = 660.0f;
constexpr float kSideMargin = 26.0f;
constexpr float kFieldW     = 400.0f;   // a licence field has no text to measure
constexpr float kBtnPadX    = 26.0f;    // slack around an answer's label
constexpr float kBtnH       = 54.0f;
constexpr float kPadX       = 26.0f;
constexpr float kPadY       = 20.0f;
constexpr float kTitleSize  = 27.0f;
constexpr float kTitleGap   = 10.0f;
constexpr float kHangGap    = 22.0f;    // below the island it hangs from

// Surface tension, and the gaps set against it. A smooth union closes at the
// midline only once the merge radius passes twice the gap, so at rest — every
// gap here above 17 — the bodies are apart. The lean is what joins them: each
// body takes a different share of it, so tilting changes the distances inside
// the group rather than sliding the group about, and the field answers.
constexpr float kMerge  = 34.0f;
constexpr float kRowGap = 26.0f;   // needs 9px of approach to bridge
constexpr float kBtnGap = 34.0f;   // needs 17px

// Wash, as a share of the window's. Alone it is its own sheet and can be
// properly thin; sharing the island's body it cannot be thinner than what it is
// part of, because a group gets one material, so the pair thins together.
constexpr float kClarityAlone = 0.45f;
constexpr float kClarity      = 0.62f;

// The lean's share per body, per axis. The row gap answers to the vertical lean
// and the gap between the answers to the horizontal, so one share per body
// cannot serve both. The two answers share a vertical share so they stay on a
// line. Its own range rather than the island's, which is clamped on screen and
// so nearly one-sided. Stiffnesses differ so threads form during the movement
// as well as at its ends.
constexpr float kTiltRangeDlg = 60.0f;
constexpr float kTiltX[3] = { 1.00f, 1.16f, 0.84f };
constexpr float kTiltY[3] = { 1.00f, 1.22f, 1.22f };
constexpr float kOmega[3] = { 9.0f, 6.5f, 11.5f };

struct State {
    bool        open  = false;
    int         kind  = KindConfirm;
    std::string title, body, ok, cancel;
    char        input[256] = "";
    float       t = 0.0f, vel = 0.0f;   // 0 collapsed on the island, 1 open
    int         result = ResultNone;

    // Both dimensions are content-dependent. Springing them is what makes one
    // dialog become another rather than be replaced by it.
    float       w = 0.0f, w_vel = 0.0f;
    float       h = 0.0f, h_vel = 0.0f;

    // Cross-fade for a switch while one is up. Runs 1 -> 0, content committed at
    // the midpoint so the old text leaves before the new arrives.
    float       swap = 0.0f;
    bool        pending = false;
    int         p_kind = KindConfirm;
    std::string p_title, p_body, p_ok, p_cancel;

    // Where the lean has carried each body.
    ImVec2      off[3]     = {};
    ImVec2      off_vel[3] = {};

    // The line it hangs from: the island's edge plus whatever the shell presses
    // down with. Under-damped, so the shell moving leaves the modal behind and
    // closes the gap past the merge threshold, and the spring coming back
    // through opens it again.
    float       anchor = 0.0f, anchor_vel = 0.0f;
    bool        anchor_valid = false;

    // Whether the shell shares this body, and the retract that lets that change
    // unseen. The volume key flips the stage from outside ImGui, so it can
    // change mid-modal; `regroup` drives the openness to zero, the swap is made
    // at the bottom where the modal draws as nothing, and it springs back out on
    // the other side.
    bool        joined  = false;
    bool        regroup = false;

    // Whether the shared body had room for three more shapes last frame. The
    // opening waits for it, or the four-shape cap would drop one.
    bool        room = false;

    // Press, read a frame late: the panes are submitted before the hit areas
    // exist. The price of the glass being the button's background.
    bool        held[2]      = {};
    float       press[2]     = {};
    float       press_vel[2] = {};
};
State g;

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float u) {
    return ImVec4(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                  a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u);
}

// UpdateSpring's stiffness is fixed; these need one each.
void SpringTo(float* pos, float* vel, float target, float dt, float omega,
              float zeta = 0.62f) {
    const float diff  = target - *pos;
    const float accel = omega * omega * diff - 2.0f * zeta * omega * (*vel);
    *vel += accel * dt;
    *pos += (*vel) * dt;
    if (std::fabs(diff) < 0.01f && std::fabs(*vel) < 0.05f) { *pos = target; *vel = 0.0f; }
}

// The press feedback, in full. The shader takes its material from one pane per
// group, so there is no per-button colour to change; shape is what is left.
// Five percent — three and a half was under two pixels on a 54px capsule.
ImVec4 Squash(const ImVec4& r, float p) {
    const float s  = 1.0f - 0.05f * p;
    const float cx = r.x + r.z * 0.5f;
    const float cy = r.y + r.w * 0.5f;
    return ImVec4(cx - r.z * s * 0.5f, cy - r.w * s * 0.5f, r.z * s, r.w * s);
}

void Commit(int kind, const std::string& title, const std::string& body,
            const std::string& ok, const std::string& cancel) {
    g.kind = kind; g.title = title; g.body = body; g.ok = ok; g.cancel = cancel;
}

void Answer(int result) {
    g.result = result;
    g.open   = false;
    g.held[0] = g.held[1] = false;   // or it collapses with a button still down
    haptic::Step();
}

} // namespace

void Open(Kind kind, const char* title, const char* body,
          const char* ok, const char* cancel) {
    const std::string t  = title  ? title  : "";
    const std::string b  = body   ? body   : "";
    const std::string o  = ok     ? ok     : (kind == KindLicense ? u8"粘贴" : u8"确定");
    const std::string c  = cancel ? cancel : u8"取消";

    g.input[0] = '\0';
    g.result   = ResultNone;

    if (IsOpen()) {
        // A switch, not an entrance: hold the content until the cross-fade
        // midpoint and let the size spring across.
        g.pending = true;
        g.p_kind = kind; g.p_title = t; g.p_body = b; g.p_ok = o; g.p_cancel = c;
        g.swap = 1.0f;
    } else {
        Commit(kind, t, b, o, c);
        // Hang from where the shell is now, and wait for room rather than trust
        // the last answer.
        g.anchor_valid = false;
        g.room         = false;
    }
    g.open = true;
    haptic::Step();
}

void Close()      { g.open = false; }
bool IsOpen()     { return g.open || g.t > 0.002f; }
const char* Input() { return g.input; }

float Openness()    { return g.t < 0.0f ? 0.0f : (g.t > 1.0f ? 1.0f : g.t); }
bool  JoinedShell() { return IsOpen() && g.joined; }

bool StageMismatch(int stage) {
    return IsOpen() && ((stage != UiState::StageWindow) != g.joined);
}

// The shared group's material, walked toward the modal's as it comes out. A
// group gets one set of settings and it comes from the group's first pane, which
// is the shell's — so this is the only place the modal's material can live once
// the two are one body. A no-op when they are not.
void BlendMaterial(GlassRect* r) {
    const float u = Openness();
    if (!g.joined || u <= 0.001f) return;
    auto mix = [u](float a, float b) { return a + (b - a) * u; };
    r->rounding  = mix(r->rounding,  kBtnH * 0.5f);
    // Narrowing only: ui.cpp pulls the rim in for a small shell, and 40px on a
    // 56px pill would reach in from both sides and meet in the middle.
    if (r->edgeWidth > 40.0f) r->edgeWidth = mix(r->edgeWidth, 40.0f);
    r->blur      = mix(r->blur,      5.0f);
    r->merge     = mix(r->merge,     kMerge);
    r->tintA     = mix(r->tintA,     r->tintA * kClarity);
}

Result Take() {
    const int r = g.result;
    g.result = ResultNone;
    return (Result)r;
}

void Draw(UiState* state) {
    ImGuiIO& io = ImGui::GetIO();
    const float dt = io.DeltaTime > 0.0f ? io.DeltaTime : 1.0f / 60.0f;

    // Which body the modal belongs to, decided from the stage rather than the
    // animated size so it does not chatter as the shell springs past a
    // threshold. The swap is committed at exactly zero openness, where the three
    // bodies contribute nothing: two percent out they are still an island-sized
    // capsule, which in a sheet of its own is a stray pill.
    if (StageMismatch(state->stage)) g.regroup = true;
    if (g.regroup && g.t < 0.002f) {
        g.joined  = state->stage != UiState::StageWindow;
        g.regroup = false;
        g.anchor_valid = false;   // the line it hangs from has changed meaning
    }

    // Held at zero while retracting, and until the shared body has shapes to
    // spare. Closing never waits; only the way out does.
    const bool out = g.open && g.room && !g.regroup;
    UpdateSpring(&g.t, &g.vel, out ? 1.0f : 0.0f, dt);

    if (g.swap > 0.0f) {
        g.swap -= dt * 3.2f;
        if (g.swap < 0.0f) g.swap = 0.0f;
        if (g.pending && g.swap <= 0.5f) {
            Commit(g.p_kind, g.p_title, g.p_body, g.p_ok, g.p_cancel);
            g.pending = false;
        }
    }
    if (!IsOpen()) { state->modal_rect = ImVec4(0, 0, 0, 0); return; }

    const float u  = g.t < 0.0f ? 0.0f : (g.t > 1.0f ? 1.0f : g.t);
    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;

    // ── Size ─────────────────────────────────────────────────────────────
    // Both axes measured from the words, then sprung, so switching kinds grows
    // into the new shape rather than snapping to it. A wrap width of 0 means "do
    // not wrap", so natBody is the widest line the body already has.
    ImGui::PushFont(nullptr, kTitleSize);
    const ImVec2 titleSz = ImGui::CalcTextSize(g.title.c_str());
    ImGui::PopFont();
    const float natBody = (g.kind == KindLicense)
        ? kFieldW
        : ImGui::CalcTextSize(g.body.c_str(), nullptr, false, 0.0f).x;
    // The answers set a floor; their labels are caller-supplied and so cannot be
    // assumed short.
    const float okW  = ImGui::CalcTextSize(g.ok.c_str()).x;
    const float noW  = ImGui::CalcTextSize(g.cancel.c_str()).x;
    const float btnW_min = (okW > noW ? okW : noW) + kBtnPadX * 2.0f;

    const float avail = dw - 2.0f * kSideMargin;
    float maxW = avail < kBodyWMax ? avail : kBodyWMax;
    float wantW = (titleSz.x > natBody ? titleSz.x : natBody) + kPadX * 2.0f;
    const float floorW = btnW_min * 2.0f + kBtnGap;
    if (wantW < floorW)   wantW = floorW;
    if (wantW < kBodyWMin) wantW = kBodyWMin;
    if (wantW > maxW)     wantW = maxW;
    if (g.w <= 0.0f) g.w = wantW;          // first open: no growth to animate
    SpringTo(&g.w, &g.w_vel, wantW, dt, 10.0f);
    const float bodyW = g.w;

    // Height measured, not counted in lines — and measured against the animated
    // width, or a height sized for the destination overflows the width it has.
    const float wrapW = bodyW - kPadX * 2.0f;
    ImGui::PushFont(nullptr, kTitleSize);
    const float titleH = ImGui::CalcTextSize(g.title.c_str(), nullptr, false, wrapW).y;
    ImGui::PopFont();
    const float contentH = (g.kind == KindLicense)
        ? ImGui::GetFrameHeight()
        : ImGui::CalcTextSize(g.body.c_str(), nullptr, false, wrapW).y;
    const float wantH = kPadY * 2.0f + titleH + kTitleGap + contentH;
    if (g.h <= 0.0f) g.h = wantH;          // first open: no growth to animate
    SpringTo(&g.h, &g.h_vel, wantH, dt, 10.0f);
    const float bodyH = g.h;

    // ── Place ────────────────────────────────────────────────────────────
    // Each body takes its own share of the lean through its own spring. Nothing
    // animates the merge; the distances move and the field answers.
    const ImVec2 lean(state->tilt_x * kTiltRangeDlg, state->tilt_y * kTiltRangeDlg);
    for (int i = 0; i < 3; ++i) {
        SpringTo(&g.off[i].x, &g.off_vel[i].x, lean.x * kTiltX[i], dt, kOmega[i]);
        SpringTo(&g.off[i].y, &g.off_vel[i].y, lean.y * kTiltY[i], dt, kOmega[i]);
    }

    const float x0 = (dw - bodyW) * 0.5f;

    // It hangs from the island's line. What moves it is being pressed on: while
    // the shell shares this body it cannot pass through it, so a shell resting
    // below that line carries the modal down ahead of its edge — the card
    // opening and pushing it out of the way. Only to the extent the shell is
    // overhead, and by the shell's *rest* rect: one mid-collapse is briefly huge,
    // and holding the modal clear of that would fling it down the screen and drag
    // it back. Sweeping over it instead absorbs it, which is what a passing body
    // should do to a smaller one. The shell's own lean is inside this, so the
    // modal's lean is the only thing that opens and closes the gap.
    const float dh_m  = state->display_h > 0 ? (float)state->display_h
                                             : io.DisplaySize.y;
    const ImVec4& sh  = state->shell_rect;        // live, for the swallow test
    const ImVec4& sr  = state->shell_rest_rect;   // settled, for the press
    const float rest  = kIslandTop + kIslandH;
    const float total = bodyH + kRowGap + kBtnH;   // everything below the line
    float want = rest;
    if (g.joined) {
        const float span = (sr.z < bodyW ? sr.z : bodyW);
        const float lo   = (sr.x > x0 ? sr.x : x0);
        const float hi   = (sr.x + sr.z < x0 + bodyW ? sr.x + sr.z : x0 + bodyW);
        float cover = (span > 1.0f) ? (hi - lo) / span : 0.0f;
        if (cover < 0.0f) cover = 0.0f;
        if (cover > 1.0f) cover = 1.0f;
        want = rest + ((sr.y + sr.w) - rest) * cover;
        // Rails, not design lines: no shell that can share this body reaches
        // them. They only stop a future one pressing the modal off screen.
        const float ceiling = dh_m - 16.0f - total - kHangGap;
        const float floor_  = kTiltRangeDlg - kHangGap + 16.0f;
        if (want > ceiling) want = ceiling;
        if (want < floor_)  want = floor_;
    }
    if (!g.anchor_valid) { g.anchor = want; g.anchor_valid = true; }
    SpringTo(&g.anchor, &g.anchor_vel, want, dt, 12.0f, 0.55f);

    const float y0   = g.anchor + kHangGap;
    const float btnW = (bodyW - kBtnGap) * 0.5f;
    const float btnY = y0 + bodyH + kRowGap;

    // Critically damped: overshoot on release reads as the button recoiling.
    for (int i = 0; i < 2; ++i)
        SpringTo(&g.press[i], &g.press_vel[i], g.held[i] ? 1.0f : 0.0f, dt, 26.0f, 1.0f);

    const ImVec4 fBody (x0 + g.off[0].x, y0 + g.off[0].y, bodyW, bodyH);
    const ImVec4 fLeft  = Squash(ImVec4(x0 + g.off[1].x, btnY + g.off[1].y, btnW, kBtnH),
                                 g.press[0]);
    const ImVec4 fRight = Squash(ImVec4(x0 + bodyW - btnW + g.off[2].x,
                                        btnY + g.off[2].y, btnW, kBtnH), g.press[1]);

    // At u = 0 all three bodies are the island's capsule, so the field has a
    // single body and the separation into three *is* the opening. Read from
    // where the island actually is: with Live2D it follows the dragged ball.
    const ImVec4 seed = (state->island_rect.z > 2.0f)
        ? state->island_rect
        : ImVec4(dw * 0.5f - kIslandW * 0.5f + state->island_tilt.x,
                 kIslandTop + state->island_tilt.y, kIslandW, kIslandH);

    const ImVec4 rBody  = Lerp(seed, fBody,  u);
    const ImVec4 rLeft  = Lerp(seed, fLeft,  u);
    const ImVec4 rRight = Lerp(seed, fRight, u);

    // A real union, not the body's box: the answers take a different share of
    // the lean and can stand outside it.
    const ImVec4 bs[3] = { rBody, rLeft, rRight };
    ImVec2 bmin(bs[0].x, bs[0].y), bmax(bs[0].x + bs[0].z, bs[0].y + bs[0].w);
    for (int i = 1; i < 3; ++i) {
        if (bs[i].x < bmin.x) bmin.x = bs[i].x;
        if (bs[i].y < bmin.y) bmin.y = bs[i].y;
        if (bs[i].x + bs[i].z > bmax.x) bmax.x = bs[i].x + bs[i].z;
        if (bs[i].y + bs[i].w > bmax.y) bmax.y = bs[i].y + bs[i].w;
    }
    // For the exit dissolve, so the question comes apart with the window rather
    // than blinking out beside it.
    state->modal_rect = ImVec4(bmin.x, bmin.y, bmax.x - bmin.x, bmax.y - bmin.y);

    // ── Panes ────────────────────────────────────────────────────────────
    // In the shell's group while it shares this body, which is what buys the
    // join: one distance field, so they run together and let go by distance.
    // Three of the four shapes one body gets, so the shell must be down to a
    // single shape by now — ui.cpp closes its ranks on modal_close first, and
    // until there is room the openness spring stays at zero.
    //
    // Alone it is a thinner sheet of its own. The alpha ramp matters there: at
    // the bottom of a retract the bodies are the island's capsule, which in its
    // own group would be a stray pill rather than nothing.
    int used = 0;
    const int grp = g.joined ? 0 : 1;
    for (int i = 0; i < state->glass_count; ++i)
        if (state->glass_rects[i].group == grp) ++used;
    const bool room = used + 3 <= kMaxMergedShapes &&
                      state->glass_count + 3 <= kMaxGlassRects;
    g.room = room;
    if (state->screen_texture_id && room) {
        GlassRect base{};
        base.group     = grp;
        base.alpha     = u < 0.10f ? u / 0.10f : 1.0f;
        base.rounding  = kBtnH * 0.5f;   // capsule answers, rounded body
        base.edgeWidth = 40.0f;
        base.blur      = 5.0f;
        base.merge     = kMerge;
        base.tintA     = state->glass_clarity * (g.joined ? kClarity : kClarityAlone);
        base.lightX    = state->glass_light_x;
        base.lightY    = state->glass_light_y;
        const ImVec4 rects[3] = { rBody, rLeft, rRight };
        for (const ImVec4& r : rects) {
            GlassRect p = base;
            p.x = r.x; p.y = r.y; p.w = r.z; p.h = r.w;
            state->glass_rects[state->glass_count++] = p;
        }
    }

    // ── Content and hit areas ────────────────────────────────────────────
    // A window over the three bodies and nothing else. ImGui hit-tests by window
    // rect and hover goes to exactly one window, so a screen-wide one would take
    // every press in the app; this reaches over the window instead of taking it
    // away. Sized over the settled layout as well as the animated one, because
    // ImGui clips and culls at the window rect and the text is wrapped for the
    // final width — a body two thirds of the way out is narrower than its words.
    constexpr float kClipSlack = 6.0f;
    ImVec2 wmin = bmin, wmax = bmax;
    const ImVec4 fs[3] = { fBody, fLeft, fRight };
    for (const ImVec4& r : fs) {
        if (r.x < wmin.x) wmin.x = r.x;
        if (r.y < wmin.y) wmin.y = r.y;
        if (r.x + r.z > wmax.x) wmax.x = r.x + r.z;
        if (r.y + r.w > wmax.y) wmax.y = r.y + r.w;
    }
    ImGui::SetNextWindowPos(ImVec2(wmin.x - kClipSlack, wmin.y - kClipSlack));
    ImGui::SetNextWindowSize(ImVec2((wmax.x - wmin.x) + kClipSlack * 2.0f,
                                    (wmax.y - wmin.y) + kClipSlack * 2.0f));
    ImGui::PushStyleColor(ImGuiCol_WindowBg, ImVec4(0, 0, 0, 0));
    ImGui::PushStyleVar(ImGuiStyleVar_WindowPadding, ImVec2(0, 0));
    // The style's border is 1px and ui.cpp's push of zero is long popped by now.
    // The pane draws its own edge stroke on the real silhouette.
    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    // In front, but only where saying so costs nothing.
    //
    // NoBringToFrontOnFocus is not an option here: ImGui push_front()s such a
    // window into g.Windows, that list runs back-to-front, and FocusWindow's
    // display-front call is skipped by a test on the same flag — so it pins the
    // window at the very back for good, and the answers are unhittable wherever
    // the main window covers them. Asking for focus is how the public API brings
    // a window forward, and clicking the window behind sends it back, so the ask
    // repeats — but focusing steals the active id, which would drop a slider
    // mid-drag, so not while something owns the mouse.
    if (!ImGui::IsAnyItemActive()) ImGui::SetNextWindowFocus();
    ImGui::Begin("##modal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoFocusOnAppearing);

    // No mirror, no panes — so the words would sit on nothing. Plain fills
    // instead, which also covers the frames a rotation takes to rebuild it.
    if (!state->screen_texture_id) {
        ImDrawList* bg = ImGui::GetWindowDrawList();
        const float a  = u < 0.10f ? u / 0.10f : 1.0f;
        const ImU32 c  = IM_COL32(22, 24, 28, (int)(232.0f * a));
        const ImVec4 bodies[3] = { rBody, rLeft, rRight };
        for (const ImVec4& r : bodies)
            bg->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.z, r.y + r.w),
                              c, kBtnH * 0.5f);
    }

    // Text lags the shape, so the words arrive on a surface that is already
    // there; during a switch it crosses zero exactly when the content changes
    // hands. And it goes with the body when the shell grows over it — while the
    // two are one field a body inside the shell is not a surface at all. That
    // last part only applies when they are one body: a sheet of its own sits
    // happily inside an open window's rect.
    const float open_a = u < 0.55f ? 0.0f : (u - 0.55f) / 0.45f;
    const float swap_a = g.swap > 0.0f ? std::fabs(2.0f * g.swap - 1.0f) : 1.0f;
    float sunk_a = 1.0f;
    if (g.joined && rBody.w > 1.0f) {
        sunk_a = 1.0f - (((sh.y + sh.w) - rBody.y) / rBody.w - 0.15f) / 0.45f;
        if (sunk_a < 0.0f) sunk_a = 0.0f;
        if (sunk_a > 1.0f) sunk_a = 1.0f;
    }
    const float text_a = open_a * swap_a * sunk_a;
    if (text_a > 0.01f) {
        ImGui::PushStyleVar(ImGuiStyleVar_Alpha, text_a);
        ImDrawList* dl = ImGui::GetWindowDrawList();

        ImGui::SetCursorScreenPos(ImVec2(rBody.x + kPadX, rBody.y + kPadY));
        ImGui::BeginGroup();
        // PushTextWrapPos takes a *window-local* x. Taken from the cursor, which
        // is already local and already where the text starts — a screen
        // coordinate here agrees only while the window sits at the origin.
        const float wrap_local = ImGui::GetCursorPosX() + wrapW;
        ImGui::PushFont(nullptr, kTitleSize);
        ImGui::PushTextWrapPos(wrap_local);
        ImGui::TextUnformatted(g.title.c_str());
        ImGui::PopTextWrapPos();
        ImGui::PopFont();
        ImGui::Dummy(ImVec2(0, kTitleGap - ImGui::GetStyle().ItemSpacing.y));
        if (g.kind == KindLicense) {
            ImGui::SetNextItemWidth(wrapW);
            ImGui::InputTextWithHint("##key", u8"输入卡密", g.input, sizeof(g.input));
            chrome::LastItem(14.0f);
        } else {
            ImGui::PushTextWrapPos(wrap_local);
            ImGui::TextUnformatted(g.body.c_str());
            ImGui::PopTextWrapPos();
        }
        ImGui::EndGroup();

        // The glass is already the answers' background, so each button is only a
        // hit area with its label centred on it by hand — and the label rides the
        // squash, or it floats free of the thing it is written on.
        auto answer = [&](const char* id, const ImVec4& r, const char* label,
                          int idx, int result) {
            ImGui::SetCursorScreenPos(ImVec2(r.x, r.y));
            const bool hit = ImGui::InvisibleButton(id, ImVec2(r.z, r.w));
            g.held[idx] = ImGui::IsItemActive();
            ripple::TouchLastItem();
            const ImVec2 ts = ImGui::CalcTextSize(label);
            dl->AddText(ImVec2(r.x + (r.z - ts.x) * 0.5f,
                               r.y + (r.w - ts.y) * 0.5f),
                        ImGui::GetColorU32(ImGuiCol_Text), label);
            if (hit) Answer(result);
        };
        answer("##no", rLeft, g.cancel.c_str(), 0, ResultCancel);
        answer("##yes", rRight, g.ok.c_str(), 1, ResultOk);

        ImGui::PopStyleVar();
    } else {
        // Nothing will report the finger lifting this frame, so a press held as
        // the content faded would stay squashed for as long as the modal is up.
        g.held[0] = g.held[1] = false;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);   // WindowPadding + WindowBorderSize
    ImGui::PopStyleColor();
}

} // namespace dialog
} // namespace aimgui
