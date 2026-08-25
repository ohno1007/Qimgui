#include "ui/ui_internal.h"
#include "ui/icons.h"
#include "core/haptics.h"

#include "imgui.h"

#include <cmath>
#include <string>

namespace aimgui {
namespace dialog {
namespace {

constexpr float kBodyWMin   = 300.0f;
constexpr float kBodyWMax   = 660.0f;
constexpr float kSideMargin = 26.0f;
constexpr float kFieldW     = 400.0f;
constexpr float kBtnPadX    = 26.0f;
constexpr float kBtnH       = 54.0f;
constexpr float kPadX       = 26.0f;
constexpr float kPadY       = 20.0f;
constexpr float kTitleSize  = 27.0f;
constexpr float kTitleGap   = 10.0f;
constexpr float kHangGap    = 22.0f;

constexpr float kMerge  = 34.0f;
constexpr float kRowGap = 26.0f;
constexpr float kBtnGap = 34.0f;

constexpr float kClarityAlone = 0.45f;
constexpr float kClarity      = 0.62f;

constexpr float kTiltRangeDlg = 60.0f;
constexpr float kTiltX[3] = { 1.00f, 1.16f, 0.84f };
constexpr float kTiltY[3] = { 1.00f, 1.22f, 1.22f };
constexpr float kOmega[3] = { 9.0f, 6.5f, 11.5f };

struct State {
    bool        open  = false;
    int         kind  = KindConfirm;
    std::string title, body, ok, cancel;
    char        input[256] = "";
    float       t = 0.0f, vel = 0.0f;
    int         result = ResultNone;

    float       w = 0.0f, w_vel = 0.0f;
    float       h = 0.0f, h_vel = 0.0f;

    float       swap = 0.0f;
    bool        pending = false;
    int         p_kind = KindConfirm;
    std::string p_title, p_body, p_ok, p_cancel;

    ImVec2      off[3]     = {};
    ImVec2      off_vel[3] = {};

    float       anchor = 0.0f, anchor_vel = 0.0f;
    bool        anchor_valid = false;

    bool        joined  = false;
    bool        regroup = false;

    bool        room = false;

    bool        held[2]      = {};
    float       press[2]     = {};
    float       press_vel[2] = {};
};
State g;

ImVec4 Lerp(const ImVec4& a, const ImVec4& b, float u) {
    return ImVec4(a.x + (b.x - a.x) * u, a.y + (b.y - a.y) * u,
                  a.z + (b.z - a.z) * u, a.w + (b.w - a.w) * u);
}

void SpringTo(float* pos, float* vel, float target, float dt, float omega,
              float zeta = 0.62f) {
    const float diff  = target - *pos;
    const float accel = omega * omega * diff - 2.0f * zeta * omega * (*vel);
    *vel += accel * dt;
    *pos += (*vel) * dt;
    if (std::fabs(diff) < 0.01f && std::fabs(*vel) < 0.05f) { *pos = target; *vel = 0.0f; }
}

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
    g.held[0] = g.held[1] = false;
    haptic::Step();
}

}

void Open(Kind kind, const char* title, const char* body,
          const char* ok, const char* cancel) {
    const std::string t  = title  ? title  : "";
    const std::string b  = body   ? body   : "";
    const std::string o  = ok     ? ok     : (kind == KindLicense ? u8"粘贴" : u8"确定");
    const std::string c  = cancel ? cancel : u8"取消";

    g.input[0] = '\0';
    g.result   = ResultNone;

    if (IsOpen()) {

        g.pending = true;
        g.p_kind = kind; g.p_title = t; g.p_body = b; g.p_ok = o; g.p_cancel = c;
        g.swap = 1.0f;
    } else {
        Commit(kind, t, b, o, c);

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

void BlendMaterial(GlassRect* r) {
    const float u = Openness();
    if (!g.joined || u <= 0.001f) return;
    auto mix = [u](float a, float b) { return a + (b - a) * u; };
    r->rounding  = mix(r->rounding,  kBtnH * 0.5f);

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

    if (StageMismatch(state->stage)) g.regroup = true;
    if (g.regroup && g.t < 0.002f) {
        g.joined  = state->stage != UiState::StageWindow;
        g.regroup = false;
        g.anchor_valid = false;
    }

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
    if (!IsOpen()) { g_ui.modal_rect = ImVec4(0, 0, 0, 0); return; }

    const float u  = g.t < 0.0f ? 0.0f : (g.t > 1.0f ? 1.0f : g.t);
    const float dw = state->display_w > 0 ? (float)state->display_w : io.DisplaySize.x;

    ImGui::PushFont(nullptr, kTitleSize);
    const ImVec2 titleSz = ImGui::CalcTextSize(g.title.c_str());
    ImGui::PopFont();
    const float natBody = (g.kind == KindLicense)
        ? kFieldW
        : ImGui::CalcTextSize(g.body.c_str(), nullptr, false, 0.0f).x;

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
    if (g.w <= 0.0f) g.w = wantW;
    SpringTo(&g.w, &g.w_vel, wantW, dt, 10.0f);
    const float bodyW = g.w;

    const float wrapW = bodyW - kPadX * 2.0f;
    ImGui::PushFont(nullptr, kTitleSize);
    const float titleH = ImGui::CalcTextSize(g.title.c_str(), nullptr, false, wrapW).y;
    ImGui::PopFont();
    const float contentH = (g.kind == KindLicense)
        ? ImGui::GetFrameHeight()
        : ImGui::CalcTextSize(g.body.c_str(), nullptr, false, wrapW).y;
    const float wantH = kPadY * 2.0f + titleH + kTitleGap + contentH;
    if (g.h <= 0.0f) g.h = wantH;
    SpringTo(&g.h, &g.h_vel, wantH, dt, 10.0f);
    const float bodyH = g.h;

    const ImVec2 lean(state->tilt_x * kTiltRangeDlg, state->tilt_y * kTiltRangeDlg);
    for (int i = 0; i < 3; ++i) {
        SpringTo(&g.off[i].x, &g.off_vel[i].x, lean.x * kTiltX[i], dt, kOmega[i]);
        SpringTo(&g.off[i].y, &g.off_vel[i].y, lean.y * kTiltY[i], dt, kOmega[i]);
    }

    const float x0 = (dw - bodyW) * 0.5f;

    const float dh_m  = state->display_h > 0 ? (float)state->display_h
                                             : io.DisplaySize.y;
    const ImVec4& sh  = g_ui.shell_rect;
    const ImVec4& sr  = g_ui.shell_rest_rect;
    const float rest  = kIslandTop + kIslandH;
    const float total = bodyH + kRowGap + kBtnH;
    float want = rest;
    if (g.joined) {
        const float span = (sr.z < bodyW ? sr.z : bodyW);
        const float lo   = (sr.x > x0 ? sr.x : x0);
        const float hi   = (sr.x + sr.z < x0 + bodyW ? sr.x + sr.z : x0 + bodyW);
        float cover = (span > 1.0f) ? (hi - lo) / span : 0.0f;
        if (cover < 0.0f) cover = 0.0f;
        if (cover > 1.0f) cover = 1.0f;
        want = rest + ((sr.y + sr.w) - rest) * cover;

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

    for (int i = 0; i < 2; ++i)
        SpringTo(&g.press[i], &g.press_vel[i], g.held[i] ? 1.0f : 0.0f, dt, 26.0f, 1.0f);

    const ImVec4 fBody (x0 + g.off[0].x, y0 + g.off[0].y, bodyW, bodyH);
    const ImVec4 fLeft  = Squash(ImVec4(x0 + g.off[1].x, btnY + g.off[1].y, btnW, kBtnH),
                                 g.press[0]);
    const ImVec4 fRight = Squash(ImVec4(x0 + bodyW - btnW + g.off[2].x,
                                        btnY + g.off[2].y, btnW, kBtnH), g.press[1]);

    const ImVec4 seed = (g_ui.island_rect.z > 2.0f)
        ? g_ui.island_rect
        : ImVec4(dw * 0.5f - kIslandW * 0.5f + g_ui.island_tilt.x,
                 kIslandTop + g_ui.island_tilt.y, kIslandW, kIslandH);

    const ImVec4 rBody  = Lerp(seed, fBody,  u);
    const ImVec4 rLeft  = Lerp(seed, fLeft,  u);
    const ImVec4 rRight = Lerp(seed, fRight, u);

    const ImVec4 bs[3] = { rBody, rLeft, rRight };
    ImVec2 bmin(bs[0].x, bs[0].y), bmax(bs[0].x + bs[0].z, bs[0].y + bs[0].w);
    for (int i = 1; i < 3; ++i) {
        if (bs[i].x < bmin.x) bmin.x = bs[i].x;
        if (bs[i].y < bmin.y) bmin.y = bs[i].y;
        if (bs[i].x + bs[i].z > bmax.x) bmax.x = bs[i].x + bs[i].z;
        if (bs[i].y + bs[i].w > bmax.y) bmax.y = bs[i].y + bs[i].w;
    }

    g_ui.modal_rect = ImVec4(bmin.x, bmin.y, bmax.x - bmin.x, bmax.y - bmin.y);

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
        base.rounding  = kBtnH * 0.5f;
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

    ImGui::PushStyleVar(ImGuiStyleVar_WindowBorderSize, 0.0f);

    if (!ImGui::IsAnyItemActive()) ImGui::SetNextWindowFocus();
    ImGui::Begin("##modal", nullptr,
                 ImGuiWindowFlags_NoDecoration | ImGuiWindowFlags_NoMove |
                 ImGuiWindowFlags_NoSavedSettings | ImGuiWindowFlags_NoNav |
                 ImGuiWindowFlags_NoScrollbar |
                 ImGuiWindowFlags_NoScrollWithMouse |
                 ImGuiWindowFlags_NoFocusOnAppearing);

    if (!state->screen_texture_id) {
        ImDrawList* bg = ImGui::GetWindowDrawList();
        const float a  = u < 0.10f ? u / 0.10f : 1.0f;
        const ImU32 c  = IM_COL32(22, 24, 28, (int)(232.0f * a));
        const ImVec4 bodies[3] = { rBody, rLeft, rRight };
        for (const ImVec4& r : bodies)
            bg->AddRectFilled(ImVec2(r.x, r.y), ImVec2(r.x + r.z, r.y + r.w),
                              c, kBtnH * 0.5f);
    }

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

        g.held[0] = g.held[1] = false;
    }

    ImGui::End();
    ImGui::PopStyleVar(2);
    ImGui::PopStyleColor();
}

}
}
