// Material 3 Expressive progress geometry, implemented for Dear ImGui.
// Token values and amplitude thresholds follow AndroidX (Apache-2.0), commit
// 587b8da057e3dfa7fb908e4b62b1bb84271eb20d. See third_party/material_expressive/progress.
#include "ui/md3_progress.h"
#include "ui/md3_motion.h"
#include "md3_internal.h"

#include <array>
#include <vector>

namespace aimgui::md3 {
namespace {
constexpr double kTau = 6.28318530717958647692;
constexpr float kMaxPixels = 1000000.f;
struct Span { float start, end; };

bool ValidStyle(const WavyProgressStyle& s) {
    for (float v : {s.amplitude,s.wavelength,s.strokeWidth,s.trackWidth,s.gap,s.stopSize,s.waveSpeed})
        if (!std::isfinite(v)) return false;
    return s.wavelength >= 0 && s.strokeWidth >= 0 && s.trackWidth >= 0 && s.gap >= 0 && s.stopSize >= 0;
}
bool ValidPixels(float value) { return std::isfinite(value) && value >= 0.f && value <= kMaxPixels; }
bool Reserve(const char* id, ImVec2 size, ImRect& rect) {
    if (!id || !GImGui || !GImGui->WithinFrameScope || size.x<=0 || size.y<=0 ||
        !ValidPixels(size.x) || !ValidPixels(size.y)) return false;
    ImGuiWindow* window=ImGui::GetCurrentWindow();
    if (window->SkipItems) return false;
    rect=ImRect(window->DC.CursorPos,detail::Add(window->DC.CursorPos,size));
    ImGui::ItemSize(rect);
    return ImGui::ItemAdd(rect,window->GetID(id));
}
double MotionTime(const WavyProgressStyle& style) {
    const auto& motion=GetMotionSettings();
    if (!style.animate || motion.reduced || !std::isfinite(motion.durationScale) || motion.durationScale<=0)
        return 0.;
    return ImGui::GetTime()/double(motion.durationScale);
}
float Amplitude(float value,const WavyProgressStyle& style) {
    const bool indeterminate=!std::isfinite(value)||value<0;
    return std::clamp(style.amplitude,0.f,1.f)*
           (indeterminate || (value>.1f && value<.95f) ? 1.f : 0.f);
}
void RoundedPath(ImDrawList* draw,const std::vector<ImVec2>& points,ImU32 color,float thickness,bool closed=false) {
    if (points.size()<2 || thickness<=0 || !(color&IM_COL32_A_MASK)) return;
    draw->AddPolyline(points.data(),static_cast<int>(points.size()),color,thickness,
                      ImDrawFlags_StrokeCenter|(closed?ImDrawFlags_Closed:0));
    if (!closed) {
        draw->AddCircleFilled(points.front(),thickness*.5f,color);
        draw->AddCircleFilled(points.back(),thickness*.5f,color);
    }
}
void RoundedLine(ImDrawList* draw,float x0,float x1,float y,float thickness,ImU32 color) {
    if (x1<=x0 || thickness<=0) return;
    draw->AddLine({x0,y},{x1,y},color,thickness);
    draw->AddCircleFilled({x0,y},thickness*.5f,color);
    draw->AddCircleFilled({x1,y},thickness*.5f,color);
}
void Arc(ImDrawList* draw,ImVec2 center,float radius,double begin,double span,float amplitude,
         int waves,double phase,float thickness,ImU32 color,bool closed=false) {
    if (span<=0 || radius<=0 || thickness<=0) return;
    // At most 2048 segments per arc: independent of duration and input size.
    const int segments=std::clamp(static_cast<int>(std::ceil(std::min(2048.,
        std::max(double(radius)*span*.6,span/kTau*waves*16.)))),8,2048);
    std::vector<ImVec2> points;
    points.reserve(segments+1);
    for (int i=0;i<segments+(closed?0:1);++i) {
        const double angle=begin+span*double(i)/segments;
        const float r=radius+amplitude*static_cast<float>(std::sin(angle*waves-phase));
        points.emplace_back(center.x+r*static_cast<float>(std::cos(angle)),
                            center.y+r*static_cast<float>(std::sin(angle)));
    }
    RoundedPath(draw,points,color,thickness,closed);
}
} // namespace

void WavyLinearProgress(const char* id,float value,float width,const WavyProgressStyle& style) {
    if (!GImGui || !GImGui->WithinFrameScope || !ValidStyle(style) || !std::isfinite(width) || width<0) return;
    const bool indeterminate=!std::isfinite(value)||value<0;
    const float scale=GetTheme().scale;
    const float w=width==0 ? ImGui::GetContentRegionAvail().x : detail::Dp(width);
    const float rawStroke=detail::Dp(style.strokeWidth),rawTrack=detail::Dp(style.trackWidth);
    const float rawStop=detail::Dp(style.stopSize),peak=detail::Dp(3.f);
    const float gap=detail::Dp(style.gap);
    const float wave=detail::Dp(style.wavelength>0 ? style.wavelength : indeterminate?20.f:40.f);
    for (float v : {rawStroke,rawTrack,rawStop,gap,wave}) if (!ValidPixels(v)) return;
    const float height=std::max({detail::Dp(10.f),rawStroke+2*peak,rawTrack,rawStop});
    ImRect rect;
    if (!Reserve(id,{w,height},rect)) return;
    const float stroke=std::min(rawStroke,w),track=std::min(rawTrack,w),stop=std::min(rawStop,w);
    const float inset=std::max({stroke,track,stop})*.5f;
    const float x0=rect.Min.x+inset,x1=rect.Max.x-inset,length=std::max(0.f,x1-x0);
    const float middle=rect.GetCenter().y;
    const auto& theme=GetTheme();
    const float alpha=detail::Disabled() ? .38f : 1.f;
    const ImU32 indicator=detail::Color(theme.primary,alpha),trackColor=detail::Color(theme.secondaryContainer,alpha);
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(rect.Min,rect.Max,true);
    if (length<=0) {
        const bool active=indeterminate || value>0;
        if (w>0 && (active?stroke:track)>0)
            draw->AddCircleFilled(rect.GetCenter(),std::min(w,height)*.5f,active?indicator:trackColor);
        draw->PopClipRect();return;
    }
    const double time=MotionTime(style);
    const double phase=std::fmod(time*double(style.waveSpeed),1.)*kTau;
    std::array<Span,2> spans{};int count=0;
    if (indeterminate) {
        const double cycle=std::fmod(time/1.8,1.);
        for (int i=0;i<2;++i) {
            const double p=std::fmod(cycle+i*.5,1.);
            const float start=static_cast<float>(p*1.6-.6);
            const float end=start+static_cast<float>(.35+.15*std::sin(p*kTau*.5));
            const Span span{std::clamp(start,0.f,1.f),std::clamp(end,0.f,1.f)};
            if (span.end>span.start) spans[count++]=span;
        }
        if (count==2 && spans[1].start<spans[0].start) std::swap(spans[0],spans[1]);
    } else if (value>0) spans[count++]={0.f,std::clamp(value,0.f,1.f)};
    if (stroke<=0) count=0;

    // Subtract the rounded active spans from the straight remaining track.
    const float clearance=gap+(stroke+track)*.5f;
    float cursor=x0;
    for (int i=0;i<count;++i) {
        const float begin=x0+length*spans[i].start,end=x0+length*spans[i].end;
        RoundedLine(draw,cursor,std::min(x1,begin-clearance),middle,track,trackColor);
        cursor=std::max(cursor,end+clearance);
    }
    RoundedLine(draw,cursor,x1,middle,track,trackColor);
    const float amplitude=peak*Amplitude(value,style);
    std::vector<ImVec2> points;
    for (int i=0;i<count;++i) {
        const float begin=x0+length*spans[i].start,end=x0+length*spans[i].end;
        const double samples=std::max((end-begin)/std::max(.5f*scale,1.f),
                                     (end-begin)/std::max(wave,.01f)*16.f);
        const int segments=std::clamp(static_cast<int>(std::ceil(std::min(samples,2048.))),1,2048);
        points.clear();points.reserve(segments+1);
        for (int sample=0;sample<=segments;++sample) {
            const float x=begin+(end-begin)*float(sample)/segments;
            const float y=middle+amplitude*static_cast<float>(std::sin((x-x0)/std::max(wave,.01f)*kTau-phase));
            points.emplace_back(x,y);
        }
        RoundedPath(draw,points,indicator,stroke);
    }
    if (!indeterminate && value<1.f && stop>0) {
        const float activeEnd=x0+length*std::clamp(value,0.f,1.f);
        if (value<=0 || activeEnd+stroke*.5f+gap < x1-stop*.5f)
            draw->AddCircleFilled({x1,middle},stop*.5f,indicator);
    }
    draw->PopClipRect();
}

void WavyCircularProgress(const char* id,float value,float diameter,const WavyProgressStyle& style) {
    if (!GImGui || !GImGui->WithinFrameScope || !ValidStyle(style) || !std::isfinite(diameter) || diameter<=0) return;
    const bool indeterminate=!std::isfinite(value)||value<0;
    const float d=detail::Dp(diameter),rawStroke=detail::Dp(style.strokeWidth),rawTrack=detail::Dp(style.trackWidth);
    const float gap=detail::Dp(style.gap),wave=detail::Dp(style.wavelength>0?style.wavelength:15.f);
    for (float v : {rawStroke,rawTrack,gap,wave}) if (!ValidPixels(v)) return;
    ImRect rect;
    if (!Reserve(id,{d,d},rect)) return;
    const float thicknessLimit=d*.25f;
    const float stroke=std::min(rawStroke,thicknessLimit),track=std::min(rawTrack,thicknessLimit);
    const float peak=std::min(detail::Dp(1.6f),d*.1f);
    const float radius=d*.5f-std::max(stroke,track)*.5f-peak;
    if (radius<=0) return;
    const float alpha=detail::Disabled() ? .38f : 1.f;
    const auto& theme=GetTheme();
    const ImU32 indicator=detail::Color(theme.primary,alpha),trackColor=detail::Color(theme.secondaryContainer,alpha);
    const double time=MotionTime(style),phase=std::fmod(time*double(style.waveSpeed),1.)*kTau;
    const double start=indeterminate ? -kTau*.25+std::fmod(time*.5,1.)*kTau : -kTau*.25;
    const double fraction=indeterminate ? .35+.2*(1.-std::cos(std::fmod(time/1.8,1.)*kTau)) : std::clamp(value,0.f,1.f);
    const double span=fraction*kTau;
    const double clearance=(gap+(stroke+track)*.5)/radius;
    const int waves=std::clamp(static_cast<int>(std::min(128.,std::round(kTau*radius/std::max(wave,.01f)))),3,128);
    auto* draw=ImGui::GetWindowDrawList();
    draw->PushClipRect(rect.Min,rect.Max,true);
    if (span<=0 || stroke<=0) Arc(draw,rect.GetCenter(),radius,0,kTau,0,0,0,track,trackColor,true);
    else if (span<kTau) Arc(draw,rect.GetCenter(),radius,start+span+clearance,
                           std::max(0.,kTau-span-2*clearance),0,0,0,track,trackColor);
    Arc(draw,rect.GetCenter(),radius,start,span,peak*Amplitude(value,style),waves,phase,stroke,indicator,span>=kTau);
    draw->PopClipRect();
}
} // namespace aimgui::md3
