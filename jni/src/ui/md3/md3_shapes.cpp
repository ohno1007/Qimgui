#include "ui/md3_shapes.h"
#include "md3_internal.h"

#include <algorithm>
#include <cmath>
#include <limits>
#include <vector>

namespace aimgui::md3 {
namespace {
// Immutable normalized upstream curves; provenance and reproduction tools are
// in third_party/material_shapes/ and scripts/material_shapes/.
#include "../../../../third_party/material_shapes/material_shapes_data.inc"
static_assert(sizeof(kShapeGeometries) / sizeof(kShapeGeometries[0]) == MaterialShapeCount);
constexpr double kPi = 3.14159265358979323846;
constexpr double kCurveErrorPx = .25;
constexpr int kMaxContourPoints = 4096;
constexpr int kMaxCurveDepth = 12;
struct Point { double x, y; };
Point Mid(Point a, Point b) { return {(a.x+b.x)*.5,(a.y+b.y)*.5}; }
double DistanceSquared(Point a, Point b) {
    const double x=a.x-b.x, y=a.y-b.y;
    return x*x+y*y;
}
bool Finite(ImVec2 p) { return std::isfinite(p.x) && std::isfinite(p.y); }
struct Bounds {
    double min_x=std::numeric_limits<double>::infinity(), min_y=min_x;
    double max_x=-min_x, max_y=-min_x;
    void Add(Point p) {
        min_x=std::min(min_x,p.x); max_x=std::max(max_x,p.x);
        min_y=std::min(min_y,p.y); max_y=std::max(max_y,p.y);
    }
};
Point CurveAt(const Point (&p)[4], double t) {
    const double u=1.-t;
    return {u*u*u*p[0].x+3.*u*u*t*p[1].x+3.*u*t*t*p[2].x+t*t*t*p[3].x,
            u*u*u*p[0].y+3.*u*u*t*p[1].y+3.*u*t*t*p[2].y+t*t*t*p[3].y};
}
void AddExtrema(Bounds& bounds, const Point (&p)[4], double p0,double p1,double p2,double p3) {
    // Derivative divided by three: a*t^2+b*t+c.
    const double a=-p0+3.*p1-3.*p2+p3, b=2.*(p0-2.*p1+p2), c=p1-p0;
    auto include=[&](double t) { if(t>0. && t<1.) bounds.Add(CurveAt(p,t)); };
    if(std::abs(a)<1.e-14) {
        if(std::abs(b)>1.e-14) include(-c/b);
    } else {
        const double discriminant=b*b-4.*a*c;
        if(discriminant>=0.) {
            const double root=std::sqrt(discriminant);
            include((-b+root)/(2.*a)); include((-b-root)/(2.*a));
        }
    }
}
void AddCurveBounds(Bounds& bounds,const Point (&p)[4]) {
    bounds.Add(p[0]); bounds.Add(p[3]);
    AddExtrema(bounds,p,p[0].x,p[1].x,p[2].x,p[3].x);
    AddExtrema(bounds,p,p[0].y,p[1].y,p[2].y,p[3].y);
}
void RotateCurve(const ShapeCubic& cubic,ImVec2 center,double cosine,double sine,Point (&out)[4]) {
    const ImVec2 points[]={cubic.start,cubic.control1,cubic.control2,cubic.end};
    for(int i=0;i<4;++i) {
        const double x=points[i].x-double(center.x), y=points[i].y-double(center.y);
        out[i]={cosine*x-sine*y,sine*x+cosine*y};
    }
}
bool Append(std::vector<ImVec2>& points,Point p) {
    if(!std::isfinite(p.x)||!std::isfinite(p.y)||
       std::abs(p.x)>std::numeric_limits<float>::max()||
       std::abs(p.y)>std::numeric_limits<float>::max()) return false;
    const ImVec2 point(static_cast<float>(p.x),static_cast<float>(p.y));
    if(!points.empty() && DistanceSquared({points.back().x,points.back().y},{point.x,point.y})<1.e-8)
        return true;
    if(points.size()>=kMaxContourPoints) return false;
    points.push_back(point); return true;
}
bool Flatten(std::vector<ImVec2>& points,const Point (&p)[4],int depth) {
    // Control-point distance from a linear cubic, times .75, bounds chord
    // error even for coincident endpoints and collinear backtracking.
    const Point line1{(2.*p[0].x+p[3].x)/3.,(2.*p[0].y+p[3].y)/3.};
    const Point line2{(p[0].x+2.*p[3].x)/3.,(p[0].y+2.*p[3].y)/3.};
    const double error2=std::max(DistanceSquared(p[1],line1),DistanceSquared(p[2],line2));
    if(error2*.75*.75<=kCurveErrorPx*kCurveErrorPx) return Append(points,p[3]);
    // Bound both stack and triangulation cost. Never emit a partial contour.
    if(depth>=kMaxCurveDepth) return false;
    const Point a=Mid(p[0],p[1]),b=Mid(p[1],p[2]),c=Mid(p[2],p[3]);
    const Point d=Mid(a,b),e=Mid(b,c),f=Mid(d,e);
    const Point left[]={p[0],a,d,f},right[]={f,e,c,p[3]};
    return Flatten(points,left,depth+1)&&Flatten(points,right,depth+1);
}
void CleanContour(std::vector<ImVec2>& points) {
    if(points.size()>1 && DistanceSquared({points.front().x,points.front().y},
                                         {points.back().x,points.back().y})<1.e-8) points.pop_back();
    // Straight edges may span several upstream cubics. Remove their interior
    // points to reduce ear-clipping work without changing pixel-step corners.
    bool changed=true;
    while(changed && points.size()>=3) {
        changed=false;
        for(std::size_t i=0;i<points.size();++i) {
            const ImVec2 a=points[(i+points.size()-1)%points.size()],b=points[i],c=points[(i+1)%points.size()];
            const double ux=double(b.x)-a.x,uy=double(b.y)-a.y,vx=double(c.x)-b.x,vy=double(c.y)-b.y;
            const double cross=ux*vy-uy*vx,dot=ux*vx+uy*vy;
            if(dot>=0. && cross*cross<=1.e-12*(ux*ux+uy*uy)*(vx*vx+vy*vy)) {
                points.erase(points.begin()+i); changed=true; break;
            }
        }
    }
}
} // namespace

const ShapeGeometry& GetShapeGeometry(MaterialShape shape) {
    const int index=static_cast<int>(shape);
    static const ShapeGeometry empty{"",nullptr,0,ImVec2(.5f,.5f)};
    return index>=0 && index<MaterialShapeCount ? kShapeGeometries[index] : empty;
}
const char* ShapeName(MaterialShape shape) { return GetShapeGeometry(shape).name; }

void DrawShape(ImDrawList* draw,MaterialShape shape,ImVec2 origin,ImVec2 size,
               ImU32 fill,float rotation,ImU32 stroke,float strokeWidth,ShapeFit fit) {
    const ShapeGeometry& geometry=GetShapeGeometry(shape);
    if(!draw||!geometry.cubics||!Finite(origin)||!Finite(size)||!std::isfinite(rotation)||
       !std::isfinite(strokeWidth)||size.x<=0.f||size.y<=0.f||
       (fit!=ShapeFit::Contain && fit!=ShapeFit::Stretch)) return;
    if(strokeWidth<=0.f) stroke=0;
    if(!((fill|stroke)&IM_COL32_A_MASK)) return;
    const double inset=(stroke&IM_COL32_A_MASK) ? double(strokeWidth)*.5 : 0.;
    const double availableX=double(size.x)-2.*inset,availableY=double(size.y)-2.*inset;
    if(availableX<=0. || availableY<=0.) return;
    const double radians=std::remainder(double(rotation),360.)*kPi/180.;
    const double cosine=std::cos(radians),sine=std::sin(radians);
    Bounds bounds;
    for(int i=0;i<geometry.cubic_count;++i) {
        Point p[4]; RotateCurve(geometry.cubics[i],geometry.center,cosine,sine,p); AddCurveBounds(bounds,p);
    }
    const double width=bounds.max_x-bounds.min_x,height=bounds.max_y-bounds.min_y;
    if(!(width>0.)||!(height>0.)) return;
    double scaleX=availableX,scaleY=availableY;
    if(fit==ShapeFit::Contain)
        scaleX=scaleY=std::min({availableX,availableY,availableX/width,availableY/height});
    const Point targetCenter{double(origin.x)+double(size.x)*.5,double(origin.y)+double(size.y)*.5};
    const Point pathCenter{(bounds.min_x+bounds.max_x)*.5,(bounds.min_y+bounds.max_y)*.5};
    const double halfWidth=width*scaleX*.5+inset+draw->_FringeScale;
    const double halfHeight=height*scaleY*.5+inset+draw->_FringeScale;
    const ImVec4 clip=draw->_CmdHeader.ClipRect;
    if(targetCenter.x+halfWidth<clip.x||targetCenter.x-halfWidth>clip.z||
       targetCenter.y+halfHeight<clip.y||targetCenter.y-halfHeight>clip.w) return;
    std::vector<ImVec2> points;
    points.reserve(std::min(kMaxContourPoints,geometry.cubic_count*4));
    for(int i=0;i<geometry.cubic_count;++i) {
        Point p[4]; RotateCurve(geometry.cubics[i],geometry.center,cosine,sine,p);
        for(Point& point:p) point={(point.x-pathCenter.x)*scaleX+targetCenter.x,
                                  (point.y-pathCenter.y)*scaleY+targetCenter.y};
        if(i==0 && !Append(points,p[0])) return;
        if(!Flatten(points,p,0)) return;
    }
    CleanContour(points);
    if(points.size()<3) return;
    // Dear ImGui AA requires clockwise screen-space winding.
    double area2=0.; const ImVec2 base=points.front();
    for(std::size_t i=1;i+1<points.size();++i)
        area2+=(double(points[i].x)-base.x)*(double(points[i+1].y)-base.y)-
               (double(points[i].y)-base.y)*(double(points[i+1].x)-base.x);
    if(!std::isfinite(area2)||std::abs(area2)<1.e-8) return;
    if(area2<0.) std::reverse(points.begin(),points.end());
    if(fill&IM_COL32_A_MASK)
        draw->AddConcavePolyFilled(points.data(),static_cast<int>(points.size()),fill);
    if(stroke&IM_COL32_A_MASK)
        draw->AddPolyline(points.data(),static_cast<int>(points.size()),stroke,strokeWidth,
                          ImDrawFlags_Closed|ImDrawFlags_StrokeCenter);
}

void Shape(const char* id,MaterialShape shape,ImVec2 sizeDp,float rotation,bool outlined,ShapeFit fit) {
    if(!ImGui::GetCurrentContext()||!id||!Finite(sizeDp)||sizeDp.x<=0.f||sizeDp.y<=0.f) return;
    ImGuiWindow* window=ImGui::GetCurrentWindow();
    if(window->SkipItems) return;
    const ImVec2 size(detail::Dp(sizeDp.x),detail::Dp(sizeDp.y));
    if(!Finite(size)) return;
    const ImRect rect(window->DC.CursorPos,detail::Add(window->DC.CursorPos,size));
    ImGui::ItemSize(rect);
    if(!ImGui::ItemAdd(rect,window->GetID(id))) return;
    const ImU32 color=detail::Color(GetTheme().primary,detail::Disabled() ? .38f : 1.f);
    DrawShape(window->DrawList,shape,rect.Min,size,outlined ? 0 : color,rotation,
              outlined ? color : 0,detail::Dp(2.f),fit);
}
} // namespace aimgui::md3
