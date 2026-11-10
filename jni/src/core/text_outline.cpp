#include "text_outline.h"

#include "imgui.h"
#include "imgui_internal.h"

#include <cstdint>
#include <vector>

namespace aimgui {
namespace {

constexpr int kDirCount = 8;
constexpr float kDiag = 0.70710678f;
const ImVec2 kDir[kDirCount] = {
    ImVec2( 1.0f,  0.0f), ImVec2(-1.0f,  0.0f),
    ImVec2( 0.0f,  1.0f), ImVec2( 0.0f, -1.0f),
    ImVec2( kDiag,  kDiag), ImVec2(-kDiag,  kDiag),
    ImVec2( kDiag, -kDiag), ImVec2(-kDiag, -kDiag),
};

std::vector<ImDrawList*> g_pool;

}

void OutlineText(ImDrawData* dd, float radius, unsigned char alpha) {
    if (!dd || dd->CmdLists.Size <= 0 || radius <= 0.0f) return;

    const ImVec2 white = ImGui::GetFontTexUvWhitePixel();
    ImDrawListSharedData* shared = ImGui::GetDrawListSharedData();
    if (!shared) return;

    ImFontAtlas* atlas = ImGui::GetIO().Fonts;
    // 1.93 also places vector strokes and rounded corners in the atlas.
    // Exclude those regions; a non-white UV no longer identifies a glyph.
    ImFontAtlasRect primitive_rects[4];
    int primitive_count = 0;
    if (atlas->Builder) {
        const ImFontAtlasRectId ids[] = {
            atlas->Builder->PackIdMouseCursors, atlas->Builder->PackIdLinesTexData,
            atlas->Builder->PackIdLineFractTexData, atlas->Builder->PackIdCornersTexData,
        };
        for (auto id : ids)
            if (id != ImFontAtlasRectId_Invalid &&
                atlas->GetCustomRect(id, &primitive_rects[primitive_count])) ++primitive_count;
    }
    auto primitive_uv = [&](const ImVec2& uv) {
        for (int i = 0; i < primitive_count; ++i) {
            const auto& r = primitive_rects[i];
            if (uv.x >= r.uv0.x && uv.x <= r.uv1.x &&
                uv.y >= r.uv0.y && uv.y <= r.uv1.y) return true;
        }
        return false;
    };

    const bool can_offset =
        (ImGui::GetIO().BackendFlags & ImGuiBackendFlags_RendererHasVtxOffset) != 0;
    const int vtx_budget =
        (sizeof(ImDrawIdx) > 2 || can_offset) ? INT32_MAX : 48000;

    while ((int)g_pool.size() < dd->CmdLists.Size)
        g_pool.push_back(IM_NEW(ImDrawList)(shared));

    int total_vtx = 0, total_idx = 0;

    for (int n = 0; n < dd->CmdLists.Size; ++n) {
        ImDrawList* src = dd->CmdLists[n];

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

        out->PushTexture(src->CmdBuffer[0].TexRef);

        for (const ImDrawCmd& cmd : src->CmdBuffer) {
            if (cmd.ElemCount == 0) continue;
            // Textures may not have a GPU ID until RenderDrawData uploads them.
            const bool font_cmd = cmd.TexRef._TexData == atlas->TexData;
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
                    !primitive_uv(a.uv) && !primitive_uv(b.uv) && !primitive_uv(c.uv) &&
                    ((a.uv.x != white.x || a.uv.y != white.y) ||
                     (b.uv.x != white.x || b.uv.y != white.y) ||
                     (c.uv.x != white.x || c.uv.y != white.y));

                if (is_text && out->VtxBuffer.Size + 3 * (kDirCount + 1) <= vtx_budget) {
                    for (int d = 0; d < kDirCount; ++d) {
                        const float dx = kDir[d].x * radius;
                        const float dy = kDir[d].y * radius;
                        out->PrimReserve(3, 3);
                        const unsigned int i0 = out->_VtxCurrentIdx;
                        const ImDrawVert* tri[3] = { &a, &b, &c };
                        for (int k = 0; k < 3; ++k) {
                            const ImDrawVert& v = *tri[k];

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

    dd->TotalVtxCount = total_vtx;
    dd->TotalIdxCount = total_idx;
}

void ShutdownTextOutline() {
    for (ImDrawList* dl : g_pool) IM_DELETE(dl);
    g_pool.clear();
}

}
