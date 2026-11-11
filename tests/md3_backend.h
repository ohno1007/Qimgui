#pragma once
#include "imgui.h"
#include <cassert>
#include <cstdint>
#include <fstream>
#include <string>
namespace md3test {
inline void UploadTextures() {
    for (auto* t : ImGui::GetPlatformIO().Textures) {
        if (t->Status == ImTextureStatus_WantCreate || t->Status == ImTextureStatus_WantUpdates) {
            t->SetTexID(static_cast<ImTextureID>(t->UniqueID + 1)); t->SetStatus(ImTextureStatus_OK);
        } else if (t->Status == ImTextureStatus_WantDestroy) {
            t->SetTexID(ImTextureID_Invalid); t->SetStatus(ImTextureStatus_Destroyed);
        }
    }
}
inline void ValidateDrawData() {
    auto* d = ImGui::GetDrawData(); assert(d && d->Valid);
    for (auto* list : d->CmdLists) for (const auto& cmd : list->CmdBuffer) {
        if (cmd.UserCallback) continue;
        assert(cmd.IdxOffset + cmd.ElemCount <= static_cast<unsigned>(list->IdxBuffer.Size));
        for (unsigned i=0;i<cmd.ElemCount;++i)
            assert(cmd.VtxOffset + list->IdxBuffer[cmd.IdxOffset+i] < static_cast<unsigned>(list->VtxBuffer.Size));
    }
}
inline void DestroyContext() {
    for (auto* t : ImGui::GetPlatformIO().Textures) {
        t->SetTexID(ImTextureID_Invalid); t->SetStatus(ImTextureStatus_Destroyed);
    }
    ImGui::DestroyContext();
}
// CSS font-size uses units-per-em; stb_truetype uses ascent-minus-descent.
// The pinned Roboto fonts have hhea span 2400 and unitsPerEm 2048.
inline ImFont* LoadComparisonFont(const char* path) {
    ImFontConfig config;config.ExtraSizeScale=2400.f/2048.f;
    auto* font=ImGui::GetIO().Fonts->AddFontFromFileTTF(path,14,&config);
    assert(font);return font;
}
inline void ConfigureContext(const char* font = nullptr) {
    ImGui::CreateContext(); auto& io=ImGui::GetIO();
    io.IniFilename=nullptr; io.LogFilename=nullptr; io.DisplaySize=ImVec2(1000,1000); io.DeltaTime=1.f/60;
    io.ConfigFlags |= ImGuiConfigFlags_NavEnableKeyboard;
    io.BackendFlags |= ImGuiBackendFlags_RendererHasTextures | ImGuiBackendFlags_RendererHasVtxOffset;
    if(font) LoadComparisonFont(font);
}
template<class T> inline void Write(std::ofstream& f,T value) { f.write(reinterpret_cast<const char*>(&value),sizeof(value)); }
inline void DumpDrawData(const std::string& path) {
    UploadTextures(); auto* d=ImGui::GetDrawData(); std::ofstream f(path,std::ios::binary); assert(f.is_open());
    f.write("MD3DRAW1",8); Write(f,(uint32_t)d->DisplaySize.x); Write(f,(uint32_t)d->DisplaySize.y);
    uint32_t count=0; for(auto* t:ImGui::GetPlatformIO().Textures) if(t->Status==ImTextureStatus_OK && t->Pixels) ++count;
    Write(f,count);
    for(auto* t:ImGui::GetPlatformIO().Textures) {
        if(t->Status!=ImTextureStatus_OK || !t->Pixels) continue;
        Write(f,(uint64_t)t->TexID); Write(f,(uint32_t)t->Width); Write(f,(uint32_t)t->Height); Write(f,(uint32_t)t->BytesPerPixel);
        f.write(reinterpret_cast<const char*>(t->Pixels),t->Width*t->Height*t->BytesPerPixel);
    }
    Write(f,(uint32_t)d->CmdLists.Size);
    for(auto* list:d->CmdLists) {
        Write(f,(uint32_t)list->VtxBuffer.Size);
        for(const auto& v:list->VtxBuffer) { Write(f,v.pos.x-d->DisplayPos.x); Write(f,v.pos.y-d->DisplayPos.y); Write(f,v.uv.x); Write(f,v.uv.y); Write(f,v.col); }
        Write(f,(uint32_t)list->IdxBuffer.Size); for(auto i:list->IdxBuffer) Write(f,(uint32_t)i);
        Write(f,(uint32_t)list->CmdBuffer.Size);
        for(const auto& c:list->CmdBuffer) {
            assert(!c.UserCallback); Write(f,(uint64_t)c.GetTexID()); Write(f,c.ElemCount); Write(f,c.IdxOffset); Write(f,c.VtxOffset);
            Write(f,c.ClipRect.x-d->DisplayPos.x); Write(f,c.ClipRect.y-d->DisplayPos.y); Write(f,c.ClipRect.z-d->DisplayPos.x); Write(f,c.ClipRect.w-d->DisplayPos.y);
        }
    }
    assert(f.good());
}
}
