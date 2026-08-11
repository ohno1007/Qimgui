#include "text_outline.h"

#include "imgui.h"

#include <cstdint>
#include <vector>

namespace aimgui {
namespace {

// Eight directions closes the ring. Four leaves notches on the diagonals of a
// stroke, which show up as a ragged edge at this font size. Cost is linear in
// this count, so halving it is the first thing to try if it ever matters.
constexpr int kDirCount = 8;
constexpr float kDiag = 0.70710678f;
const ImVec2 kDir[kDirCount] = {
    ImVec2( 1.0f,  0.0f), ImVec2(-1.0f,  0.0f),
    ImVec2( 0.0f,  1.0f), ImVec2( 0.0f, -1.0f),
    ImVec2( kDiag,  kDiag), ImVec2(-kDiag,  kDiag),
    ImVec2( kDiag, -kDiag), ImVec2(-kDiag, -kDiag),
};

// One rebuilt list per source list, kept across frames so the vertex storage is
// allocated once rather than per frame.
std::vector<ImDrawList*> g_pool;

} // namespace

void OutlineText(ImDrawData* dd, float radius, unsigned char alpha) {
    if (!dd || dd->CmdListsCount <= 0 || radius <= 0.0f) return;

    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    ImDrawListSharedData* shared = ImGui::GetDrawListSharedData();
    if (!shared) return;
    // A UV test alone is not enough to find glyphs. Anything drawn from another
    // texture — the exit animation's particles, each carrying a patch of the
    // scene snapshot — also samples away from the white pixel, and outlining
    // fourteen thousand of those would cost more than the whole animation.
    // Only the font atlas can contain a glyph.
    const ImTextureID atlas = ImGui::GetIO().Fonts->TexRef.GetTexID();

    // ImDrawIdx is 16 bits unless the build says otherwise, and a list may only
    // pass 64k vertices if the backend can offset them — which the GL ES 3.0
    // path cannot, since glDrawElementsBaseVertex only arrives in ES 3.2. The
    // outline multiplies text geometry ninefold, so stop adding to it well short
    // of the wrap and let the rest of the frame render un-outlined. Bare text is
    // a blemish; wrapped indices are scrambled geometry.
    const bool can_offset =
        (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0;
    const int vtx_budget =
        (sizeof(ImDrawIdx) > 2 || can_offset) ? INT32_MAX : 48000;

    while ((int)g_pool.size() < dd->CmdListsCount)
        g_pool.push_back(IM_NEW(ImDrawList)(shared));

    int total_vtx = 0, total_idx = 0;

    for (int n = 0; n < dd->CmdListsCount; ++n) {
        ImDrawList* src = dd->CmdLists[n];

        // Two reasons to leave a list exactly as it came.
        //
        // A user callback cannot be reproduced by replaying geometry.
        //
        // And a texture queued for destruction cannot be pushed onto a draw
        // list at all: PushTexture asserts on it. The texture is not actually
        // dead — ImTextureData says of that flag "may still be used in the
        // current frame", and this frame's own commands are still drawing with
        // it — but the assert cannot tell replaying finished commands from
        // recording new ones, and it aborts the process rather than complains.
        // The atlas queues a texture whenever it outgrows itself, which here
        // means whenever a Chinese glyph nobody has typed yet gets rasterised,
        // so this is not rare. One frame without outlines is not noticeable.
        // An empty list is skipped too: there is nothing to rebuild, and the
        // seed texture it would be given comes from the atlas rather than from
        // a command, so it is not covered by the scan below.
        bool skip = src->CmdBuffer.Size == 0;
        for (const ImDrawCmd& cmd : src->CmdBuffer) {
            if (cmd.UserCallback) { skip = true; break; }
            if (cmd.TexRef._TexData && cmd.TexRef._TexData->WantDestroyNextFrame) {
                skip = true;
                break;
            }
        }
        if (skip) {
            total_vtx += src->VtxBuffer.Size;
            total_idx += src->IdxBuffer.Size;
            continue;
        }

        ImDrawList* out = g_pool[n];
        out->_ResetForNewFrame();
        out->PushClipRect(ImVec2(-8192.0f, -8192.0f), ImVec2(8192.0f, 8192.0f), false);
        // Safe by the scan above: the list is non-empty and none of its
        // textures is queued for destruction.
        out->PushTexture(src->CmdBuffer[0].TexRef);

        for (const ImDrawCmd& cmd : src->CmdBuffer) {
            if (cmd.ElemCount == 0) continue;
            const bool font_cmd = (cmd.GetTexID() == atlas);
            out->PushClipRect(ImVec2(cmd.ClipRect.x, cmd.ClipRect.y),
                              ImVec2(cmd.ClipRect.z, cmd.ClipRect.w), false);
            out->PushTexture(cmd.TexRef);

            const ImDrawIdx*  idx = src->IdxBuffer.Data + cmd.IdxOffset;
            const ImDrawVert* vtx = src->VtxBuffer.Data + cmd.VtxOffset;

            for (unsigned int e = 0; e + 2 < cmd.ElemCount; e += 3) {
                const ImDrawVert& a = vtx[idx[e + 0]];
                const ImDrawVert& b = vtx[idx[e + 1]];
                const ImDrawVert& c = vtx[idx[e + 2]];

                const bool is_text = font_cmd &&
                    ((a.uv.x != white.x || a.uv.y != white.y) ||
                     (b.uv.x != white.x || b.uv.y != white.y) ||
                     (c.uv.x != white.x || c.uv.y != white.y));

                // The outline goes down immediately before the glyph it belongs
                // to rather than ahead of the whole list. Putting it at the
                // front would bury it under anything opaque drawn later in the
                // same list — a combo popup's own background, most of all.
                if (is_text && out->VtxBuffer.Size < vtx_budget) {
                    for (int d = 0; d < kDirCount; ++d) {
                        const float dx = kDir[d].x * radius;
                        const float dy = kDir[d].y * radius;
                        out->PrimReserve(3, 3);
                        const unsigned int i0 = out->_VtxCurrentIdx;
                        const ImDrawVert* tri[3] = { &a, &b, &c };
                        for (int k = 0; k < 3; ++k) {
                            const ImDrawVert& v = *tri[k];
                            // Follow the glyph's own alpha, so text that ImGui
                            // is fading out takes its outline with it instead
                            // of leaving a black ghost behind.
                            const unsigned int sa = (v.col >> IM_COL32_A_SHIFT) & 0xFFu;
                            const unsigned int oa = sa * (unsigned int)alpha / 255u;
                            out->PrimWriteVtx(ImVec2(v.pos.x + dx, v.pos.y + dy),
                                              v.uv, IM_COL32(0, 0, 0, oa));
                        }
                        out->PrimWriteIdx((ImDrawIdx)(i0 + 0));
                        out->PrimWriteIdx((ImDrawIdx)(i0 + 1));
                        out->PrimWriteIdx((ImDrawIdx)(i0 + 2));
                    }
                }

                out->PrimReserve(3, 3);
                const unsigned int i0 = out->_VtxCurrentIdx;
                out->PrimWriteVtx(a.pos, a.uv, a.col);
                out->PrimWriteVtx(b.pos, b.uv, b.col);
                out->PrimWriteVtx(c.pos, c.uv, c.col);
                out->PrimWriteIdx((ImDrawIdx)(i0 + 0));
                out->PrimWriteIdx((ImDrawIdx)(i0 + 1));
                out->PrimWriteIdx((ImDrawIdx)(i0 + 2));
            }

            out->PopTexture();
            out->PopClipRect();
        }

        out->PopTexture();
        out->PopClipRect();

        dd->CmdLists[n] = out;
        total_vtx += out->VtxBuffer.Size;
        total_idx += out->IdxBuffer.Size;
    }

    // The backends size their vertex and index buffers from these, so a rebuild
    // that forgets them writes past the end of both.
    dd->TotalVtxCount = total_vtx;
    dd->TotalIdxCount = total_idx;
}

} // namespace aimgui
