#include "ui/md3.h"
#include "ui/md3_motion.h"
#include "ui/md3_progress.h"
#include "imgui_internal.h"
#include "md3_backend.h"

#include <cassert>
#include <cmath>
#include <cstdio>
#include <functional>
#include <limits>
#include <vector>

namespace md=aimgui::md3;
namespace {
struct Snapshot {
    ImRect bounds;
    ImGuiID id=0;
    std::vector<ImDrawVert> vertices;
};
Snapshot Frame(const std::function<void()>& draw,double time=3.25) {
    ImGui::NewFrame();
    GImGui->Time=time;
    md::PushTheme();
    ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({1000,1000});
    ImGui::Begin("progress-test",nullptr,ImGuiWindowFlags_NoDecoration|ImGuiWindowFlags_NoSavedSettings);
    ImGui::SetCursorScreenPos({40,40});
    auto* list=ImGui::GetWindowDrawList();
    const int first=list->VtxBuffer.Size,clips=list->_ClipRectStack.Size;
    draw();
    Snapshot result;
    result.bounds=ImRect(ImGui::GetItemRectMin(),ImGui::GetItemRectMax());
    result.id=ImGui::GetItemID();
    result.vertices.assign(list->VtxBuffer.begin()+first,list->VtxBuffer.end());
    assert(list->_ClipRectStack.Size==clips);
    ImGui::Dummy({0,0}); // Invalid/no-op calls still follow a manual cursor position.
    ImGui::End();md::PopTheme();ImGui::Render();
    md3test::ValidateDrawData();md3test::UploadTextures();
    return result;
}
bool Same(const Snapshot& a,const Snapshot& b) {
    if(a.vertices.size()!=b.vertices.size()) return false;
    for(std::size_t i=0;i<a.vertices.size();++i) {
        const auto& x=a.vertices[i];const auto& y=b.vertices[i];
        if(x.pos.x!=y.pos.x||x.pos.y!=y.pos.y||x.col!=y.col) return false;
    }
    return true;
}
void InBounds(const Snapshot& s) {
    assert(!s.vertices.empty());
    for(const auto& v:s.vertices) {
        assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y));
        assert(v.pos.x>=s.bounds.Min.x-1.5f&&v.pos.x<=s.bounds.Max.x+1.5f);
        assert(v.pos.y>=s.bounds.Min.y-1.5f&&v.pos.y<=s.bounds.Max.y+1.5f);
    }
}
int ColorCount(const Snapshot& s,ImU32 color) {
    int count=0;
    for(const auto& v:s.vertices) if(v.col==color) ++count;
    return count;
}
void ProgressGeometry() {
    md3test::ConfigureContext();
    for(float scale:{.75f,1.f,2.f}) for(bool dark:{false,true}) {
        md::SetTheme(dark,scale);
        for(float size:{1.f,4.f,12.f,48.f,240.f})
            for(float value:{-1.f,0.f,.05f,.5f,.96f,1.f,2.f}) {
                const auto linear=Frame([&]{md::WavyLinearProgress("linear",value,size);});
                const auto circle=Frame([&]{md::WavyCircularProgress("circle",value,size);});
                assert(std::abs(linear.bounds.GetWidth()-size*scale)<.01f);
                assert(std::abs(linear.bounds.GetHeight()-10*scale)<.01f);
                assert(std::abs(circle.bounds.GetWidth()-size*scale)<.01f);
                assert(std::abs(circle.bounds.GetHeight()-size*scale)<.01f);
                assert(linear.id && circle.id && linear.id!=circle.id);
                InBounds(linear);InBounds(circle);
            }
        md::WavyProgressStyle style;
        style.strokeWidth=8;style.trackWidth=6;style.stopSize=10;style.gap=3;style.wavelength=28;
        InBounds(Frame([&]{md::WavyLinearProgress("custom-linear",.5f,220,style);}));
        InBounds(Frame([&]{md::WavyCircularProgress("custom-circle",.5f,96,style);}));
    }
    md3test::DestroyContext();
    std::puts("PASS: wavy linear/circular progress stay finite and inside dp bounds at small sizes, themes and scales");
}
void EndpointsAndGap() {
    md3test::ConfigureContext();md::SetTheme(false);
    md::WavyProgressStyle style;style.stopSize=0;style.animate=false;
    const ImU32 active=ImGui::ColorConvertFloat4ToU32(md::GetTheme().primary);
    const ImU32 track=ImGui::ColorConvertFloat4ToU32(md::GetTheme().secondaryContainer);
    for(bool circular:{false,true}) {
        const auto draw=[&](float value) {
            return Frame([&] {
                if(circular) md::WavyCircularProgress("endpoint",value,96,style);
                else md::WavyLinearProgress("endpoint",value,240,style);
            });
        };
        const auto zero=draw(0),full=draw(1),overflow=draw(2);
        assert(ColorCount(zero,active)==0&&ColorCount(zero,track)>0);
        assert(ColorCount(full,track)==0&&ColorCount(full,active)>0);
        assert(Same(full,overflow));
        const auto half=draw(.5f);
        assert(ColorCount(half,active)>0&&ColorCount(half,track)>0);
        if(!circular) {
            float activeRight=-1e20f,trackLeft=1e20f;
            for(const auto& v:half.vertices) {
                if(v.col==active) activeRight=std::max(activeRight,v.pos.x);
                if(v.col==track) trackLeft=std::min(trackLeft,v.pos.x);
            }
            assert(trackLeft-activeRight>=style.gap-1.f);
        }
    }
    const auto noStop=Frame([&]{md::WavyLinearProgress("stop",0,240,style);});
    style.stopSize=4;
    const auto stop=Frame([&]{md::WavyLinearProgress("stop",0,240,style);});
    assert(ColorCount(noStop,active)==0&&ColorCount(stop,active)>0);
    md3test::DestroyContext();
    std::puts("PASS: progress 0/1, completion clamp, separated active/track colors, gap and optional stop indicator");
}
void ProgressMotion() {
    md3test::ConfigureContext();md::SetTheme(false);
    for(bool circular:{false,true}) {
        md::WavyProgressStyle style;
        const auto draw=[&](float value,double time) {
            return Frame([&] {
                if(circular) md::WavyCircularProgress("motion",value,96,style);
                else md::WavyLinearProgress("motion",value,240,style);
            },time);
        };
        md::SetMotionSettings({false,1.f});
        assert(!Same(draw(-1,1.),draw(-1,1.25)));
        assert(!Same(draw(.5f,1.),draw(.5f,1.25)));
        const auto normal=draw(-1,2.);
        md::SetMotionSettings({false,2.f});
        assert(Same(normal,draw(-1,4.)));
        md::SetMotionSettings({true,1.f});
        assert(Same(draw(-1,1.),draw(-1,10.)));
        assert(Same(draw(.5f,1.),draw(.5f,10.)));
        md::SetMotionSettings({false,0.f});
        assert(Same(draw(-1,1.),draw(-1,10.)));
        md::SetMotionSettings({false,1.f});
        style.animate=false;
        assert(Same(draw(-1,1.),draw(-1,10.)));
        style.animate=true;style.waveSpeed=0;
        assert(Same(draw(.5f,1.),draw(.5f,10.)));
        const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
        assert(Same(draw(-1,2.),draw(nan,2.)));
        assert(Same(draw(-1,2.),draw(inf,2.)));
    }
    md3test::DestroyContext();
    std::puts("PASS: progress animation changes with time, obeys duration scale/reduced motion and safely displays unknown values");
}
void InvalidProgressGeometry() {
    md3test::ConfigureContext();md::SetTheme(false);
    const float nan=std::numeric_limits<float>::quiet_NaN();
    const auto invalid=Frame([&]{
        md::WavyLinearProgress(nullptr,.5f);
        md::WavyLinearProgress("invalid-width",.5f,-1);
        md::WavyLinearProgress("invalid-width",.5f,nan);
        md::WavyCircularProgress("invalid-circle",.5f,0);
        md::WavyCircularProgress("invalid-circle",.5f,nan);
        md::WavyProgressStyle style;style.strokeWidth=nan;
        md::WavyLinearProgress("invalid-style",.5f,240,style);
        md::WavyCircularProgress("invalid-style",.5f,48,style);
        style={};style.gap=-1;
        md::WavyLinearProgress("invalid-gap",.5f,240,style);
        md::WavyCircularProgress("invalid-gap",.5f,48,style);
    });
    assert(invalid.vertices.empty());
    const auto clipped=Frame([&] {
        ImGui::GetWindowDrawList()->PushClipRect({0,0},{10,10},true);
        // ImGui's item visibility uses the window clip rect, so geometry may be
        // submitted, but our nested clipping must retain the caller's clip.
        md::WavyLinearProgress("clipped",.5f);
        const auto clip=ImGui::GetWindowDrawList()->_CmdHeader.ClipRect;
        assert(clip.z<=10&&clip.w<=10);
        ImGui::GetWindowDrawList()->PopClipRect();
    });
    InBounds(clipped);
    md3test::DestroyContext();
    std::puts("PASS: invalid progress geometry is a no-op and caller clip state survives rendering");
}
} // namespace
void RunMd3ProgressTests() {
    ProgressGeometry();EndpointsAndGap();ProgressMotion();InvalidProgressGeometry();
}
