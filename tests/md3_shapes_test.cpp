#include "ui/md3_shapes.h"
#include "ui/md3.h"
#include "imgui_internal.h"
#include "md3_backend.h"
#include <algorithm>
#include <array>
#include <cassert>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <limits>
#include <string>
namespace md = aimgui::md3;
namespace {
constexpr const char* names[] = {
    "Circle", "Square", "Slanted", "Arch", "Fan", "Arrow", "SemiCircle", "Oval", "Pill",
    "Triangle", "Diamond", "ClamShell", "Pentagon", "Gem", "Sunny", "VerySunny",
    "Cookie4Sided", "Cookie6Sided", "Cookie7Sided", "Cookie9Sided", "Cookie12Sided",
    "Ghostish", "Clover4Leaf", "Clover8Leaf", "Burst", "SoftBurst", "Boom", "SoftBoom",
    "Flower", "Puffy", "PuffyDiamond", "PixelCircle", "PixelTriangle", "Bun", "Heart"
};
constexpr ImU32 ink = IM_COL32(98,58,160,255);
const char* body_font = "tests/md3_web/fonts/Roboto-400.ttf";
const char* label_font = "tests/md3_web/fonts/Roboto-500.ttf";
bool Near(double a,double b,double tolerance=1e-5) { return std::abs(a-b)<=tolerance; }
void HashByte(std::uint64_t& h,std::uint8_t b) { h=(h^b)*1099511628211ull; }
void HashWord(std::uint64_t& h,std::uint32_t word) {
    for(int i=0;i<4;++i) HashByte(h,static_cast<std::uint8_t>(word>>(8*i)));
}
void HashFloat(std::uint64_t& h,float value) {
    std::uint32_t bits;std::memcpy(&bits,&value,sizeof(bits));HashWord(h,bits);
}
// Green's theorem integrated exactly in the cubic polynomial basis, independent
// of the renderer's flattening and ear-clipping triangulation.
double CurveArea(const md::ShapeGeometry& g) {
    double integral=0;
    for(int c=0;c<g.cubic_count;++c) {
        const auto& p=g.cubics[c];
        const auto coefficients=[](double a,double b,double c,double d) {
            return std::array<double,4>{a,3*(b-a),3*(a-2*b+c),d-a+3*(b-c)};
        };
        const auto x=coefficients(p.start.x,p.control1.x,p.control2.x,p.end.x);
        const auto y=coefficients(p.start.y,p.control1.y,p.control2.y,p.end.y);
        for(int i=0;i<4;++i) for(int j=1;j<4;++j)
            integral+=(x[i]*j*y[j]-y[i]*j*x[j])/(i+j);
    }
    return integral*.5;
}
struct Bounds {
    double left=1e30,top=1e30,right=-1e30,bottom=-1e30;
    void Add(double x,double y) {
        left=std::min(left,x);top=std::min(top,y);right=std::max(right,x);bottom=std::max(bottom,y);
    }
    double Width() const { return right-left; }
    double Height() const { return bottom-top; }
};
// Dense sampling is only a bounds oracle; it is never used to draw the shapes.
Bounds ReferenceBounds(const md::ShapeGeometry& g,double degrees) {
    Bounds result;const double r=degrees*3.14159265358979323846/180.;
    const double cosine=std::cos(r),sine=std::sin(r);
    for(int c=0;c<g.cubic_count;++c) {
        const auto& p=g.cubics[c];
        for(int step=0;step<=256;++step) {
            const double t=step/256.,u=1-t;
            const double x=u*u*u*p.start.x+3*u*u*t*p.control1.x+3*u*t*t*p.control2.x+t*t*t*p.end.x-g.center.x;
            const double y=u*u*u*p.start.y+3*u*u*t*p.control1.y+3*u*t*t*p.control2.y+t*t*t*p.end.y-g.center.y;
            result.Add(x*cosine-y*sine+g.center.x,x*sine+y*cosine+g.center.y);
        }
    }
    return result;
}
void Catalog() {
    static_assert(md::MaterialShapeCount==35 && std::size(names)==35);
    std::uint64_t hash=14695981039346656037ull;int cubic_count=0;
    for(int i=0;i<md::MaterialShapeCount;++i) {
        const auto shape=static_cast<md::MaterialShape>(i);const auto& g=md::GetShapeGeometry(shape);
        assert(std::strcmp(g.name,names[i])==0 && std::strcmp(md::ShapeName(shape),names[i])==0);
        assert(&g==&md::GetShapeGeometry(shape));assert(g.cubics && g.cubic_count>0);cubic_count+=g.cubic_count;
        for(const char* p=g.name;*p;++p) HashByte(hash,static_cast<std::uint8_t>(*p));
        HashByte(hash,0);HashWord(hash,static_cast<std::uint32_t>(g.cubic_count));
        HashFloat(hash,g.center.x);HashFloat(hash,g.center.y);
        assert(std::isfinite(g.center.x)&&std::isfinite(g.center.y));
        for(int c=0;c<g.cubic_count;++c) {
            const auto& p=g.cubics[c];const auto& next=g.cubics[(c+1)%g.cubic_count];
            assert(Near(p.end.x,next.start.x)&&Near(p.end.y,next.start.y));
            for(ImVec2 v:{p.start,p.control1,p.control2,p.end}) {
                assert(std::isfinite(v.x)&&std::isfinite(v.y));
                assert(v.x>=-1e-5f&&v.x<=1.00001f&&v.y>=-1e-5f&&v.y<=1.00001f);
                HashFloat(hash,v.x);HashFloat(hash,v.y);
            }
        }
        assert(CurveArea(g)>.05&&CurveArea(g)<=1.00001);
    }
    // Independently calculated from the official JVM JSON export: name+NUL,
    // LE32 count, LE float32 center, then every cubic control point.
    assert(cubic_count==943);assert(hash==0x29ce5e64dbb4557cull);
    for(int invalid:{-1,md::MaterialShapeCount,1000}) {
        const auto& g=md::GetShapeGeometry(static_cast<md::MaterialShape>(invalid));
        assert(g.cubic_count==0&&g.cubics==nullptr);
        assert(md::ShapeName(static_cast<md::MaterialShape>(invalid))[0]=='\0');
    }
    std::puts("PASS: 35 official names, 943 closed normalized cubics and exact JVM-export float32 fingerprint");
}
ImDrawList* StartFrame(bool antialias=false) {
    ImGui::NewFrame();auto* list=ImGui::GetForegroundDrawList();
    if(!antialias) list->Flags&=~(ImDrawFlags_AAFill|ImDrawFlags_AALines);
    return list;
}
void FinishFrame() { ImGui::Render();md3test::ValidateDrawData();md3test::UploadTextures(); }
Bounds MeshBounds(const ImDrawList& list) {
    Bounds result;
    for(const auto& v:list.VtxBuffer) {
        assert(std::isfinite(v.pos.x)&&std::isfinite(v.pos.y));result.Add(v.pos.x,v.pos.y);
    }
    return result;
}
double MeshArea(const ImDrawList& list) {
    double area=0;
    for(const auto& cmd:list.CmdBuffer) {
        assert(cmd.ElemCount%3==0);
        for(unsigned offset=0;offset<cmd.ElemCount;offset+=3) {
            ImVec2 p[3];
            for(unsigned vertex=0;vertex<3;++vertex) {
                const unsigned index=cmd.VtxOffset+list.IdxBuffer[cmd.IdxOffset+offset+vertex];
                assert(index<static_cast<unsigned>(list.VtxBuffer.Size));p[vertex]=list.VtxBuffer[index].pos;
            }
            const double cross=(static_cast<double>(p[1].x)-p[0].x)*(p[2].y-p[0].y)
                              -(static_cast<double>(p[1].y)-p[0].y)*(p[2].x-p[0].x);
            assert(cross>-.02);area+=std::abs(cross)*.5;
        }
    }
    return area;
}
void FilledGeometry() {
    md3test::ConfigureContext();const ImVec2 origin(128,96);
    for(int i=0;i<md::MaterialShapeCount;++i) {
        const auto shape=static_cast<md::MaterialShape>(i);const auto& g=md::GetShapeGeometry(shape);
        for(auto fit:{md::ShapeFit::Contain,md::ShapeFit::Stretch}) for(float angle:{0.f,37.f,90.f,-145.f}) {
            const ImVec2 size(512,384);const Bounds reference=ReferenceBounds(g,angle);
            double sx=size.x,sy=size.y;
            if(fit==md::ShapeFit::Contain)
                sx=sy=std::min({static_cast<double>(std::min(size.x,size.y)),size.x/reference.Width(),size.y/reference.Height()});
            auto* list=StartFrame();md::DrawShape(list,shape,origin,size,ink,angle,0,1,fit);
            assert(!list->VtxBuffer.empty()&&!list->IdxBuffer.empty());
            const Bounds actual=MeshBounds(*list);
            const double expected_area=CurveArea(g)*sx*sy,actual_area=MeshArea(*list);
            const double relative_error=std::abs(actual_area-expected_area)/expected_area;
            if(relative_error>=.008)
                std::fprintf(stderr,"%s %.0fdeg fit%d area expected %.4f actual %.4f error %.3f%%\n",
                             g.name,angle,static_cast<int>(fit),expected_area,actual_area,relative_error*100);
            assert(relative_error<.008);
            assert(Near((actual.left+actual.right)*.5,origin.x+size.x*.5,.5));
            assert(Near((actual.top+actual.bottom)*.5,origin.y+size.y*.5,.5));
            assert(Near(actual.Width(),reference.Width()*sx,.8));
            assert(Near(actual.Height(),reference.Height()*sy,.8));
            if(fit==md::ShapeFit::Contain) {
                assert(actual.left>=origin.x-.01&&actual.right<=origin.x+size.x+.01);
                assert(actual.top>=origin.y-.01&&actual.bottom<=origin.y+size.y+.01);
            }
            FinishFrame();
        }
    }
    md3test::DestroyContext();
    std::puts("PASS: concave/convex fill matches analytic cubic area; contain/stretch rotation, bounds and winding");
}
void StrokesAndSmallSizes() {
    md3test::ConfigureContext();
    for(int i=0;i<md::MaterialShapeCount;++i)
        for(ImVec2 size:{ImVec2(1,1),ImVec2(12,8),ImVec2(96,96),ImVec2(310,150)})
            for(bool filled:{false,true}) {
                auto* list=StartFrame(true);
                md::DrawShape(list,static_cast<md::MaterialShape>(i),{50,50},size,filled?ink:0,31,ink,2);
                if(!list->VtxBuffer.empty()) {
                    const Bounds b=MeshBounds(*list);
                    assert(b.left>=47.5&&b.top>=47.5);
                    assert(b.right<=50+size.x+2.5&&b.bottom<=50+size.y+2.5);
                }
                assert(list->VtxBuffer.Size<20000);FinishFrame();
            }
    md3test::DestroyContext();
    std::puts("PASS: anti-aliased outlines and fill+stroke at tiny/normal/non-square sizes for all presets");
}
void NoopsAndState() {
    md3test::ConfigureContext();auto* list=StartFrame();
    const float nan=std::numeric_limits<float>::quiet_NaN(),inf=std::numeric_limits<float>::infinity();
    const auto circle=md::MaterialShape::Circle;
    md::DrawShape(nullptr,circle,{0,0},{100,100},ink);
    md::DrawShape(list,static_cast<md::MaterialShape>(-1),{0,0},{100,100},ink);
    md::DrawShape(list,circle,{0,0},{100,100},0);
    md::DrawShape(list,circle,{0,0},{0,100},ink);
    md::DrawShape(list,circle,{0,0},{-1,100},ink);
    md::DrawShape(list,circle,{0,0},{nan,100},ink);
    md::DrawShape(list,circle,{inf,0},{100,100},ink);
    md::DrawShape(list,circle,{0,0},{100,100},ink,nan);
    md::DrawShape(list,circle,{0,0},{100,100},ink,0,ink,inf);
    assert(list->VtxBuffer.empty()&&list->IdxBuffer.empty());
    list->PushClipRect({0,0},{10,10},false);
    md::DrawShape(list,circle,{40,40},{100,100},ink);list->PopClipRect();
    assert(list->VtxBuffer.empty());
    list->PathLineTo({11,13});list->PathLineTo({17,19});
    const int flags=list->Flags,clips=list->_ClipRectStack.Size;
    md::DrawShape(list,md::MaterialShape::Clover4Leaf,{20,20},{100,100},ink);
    assert(!list->VtxBuffer.empty());
    assert(list->_Path.Size==2&&list->_Path[0].x==11&&list->_Path[0].y==13);
    assert(list->_Path[1].x==17&&list->_Path[1].y==19);
    assert(list->Flags==flags&&list->_ClipRectStack.Size==clips);
    list->PathClear();FinishFrame();md3test::DestroyContext();
    std::puts("PASS: invalid/transparent/clipped draws are no-ops; caller path and drawing state are preserved");
}
void Layout() {
    for(float scale:{.75f,1.f,2.f}) {
        md3test::ConfigureContext();md::SetTheme(false,scale);
        for(int frame=0;frame<3;++frame) {
            ImGui::NewFrame();md::PushTheme();
            ImGui::SetNextWindowPos({0,0});ImGui::SetNextWindowSize({800,800});
            ImGui::Begin("shape-layout",nullptr,ImGuiWindowFlags_NoSavedSettings);
            md::Shape("filled",md::MaterialShape::Heart,{120,80});
            assert(Near(ImGui::GetItemRectSize().x,120*scale));assert(Near(ImGui::GetItemRectSize().y,80*scale));
            const ImGuiID first=ImGui::GetItemID();const float bottom=ImGui::GetItemRectMax().y;
            md::Shape("outlined",md::MaterialShape::Puffy,{120,80},20,true);
            assert(first!=0&&ImGui::GetItemID()!=0&&first!=ImGui::GetItemID());
            assert(ImGui::GetItemRectMin().y>=bottom);assert(Near(ImGui::GetItemRectSize().x,120*scale));
            ImGui::End();md::PopTheme();FinishFrame();
        }
        md3test::DestroyContext();
    }
    std::puts("PASS: layout helper uses logical dp, distinct item IDs and independent context lifetimes");
}
void Capture(const std::filesystem::path& directory) {
    std::filesystem::create_directories(directory);
    for(bool dark:{false,true}) {
        md3test::ConfigureContext();auto& io=ImGui::GetIO();io.DisplaySize={1280,1280};
        ImFont* body=md3test::LoadComparisonFont(body_font);
        ImFont* label=md3test::LoadComparisonFont(label_font);
        io.FontDefault=body;md::SetFonts(body,label);md::SetTheme(dark);
        for(int frame=0;frame<3;++frame) {
            ImGui::NewFrame();const auto& t=md::GetTheme();
            const auto color=[](ImVec4 c) { return ImGui::ColorConvertFloat4ToU32(c); };
            auto* draw=ImGui::GetBackgroundDrawList();
            draw->AddRectFilled({0,0},io.DisplaySize,color(t.surface));
            draw->AddText(label,36,{40,32},color(t.onSurface),"Material 3 Shapes");
            draw->AddText(body,17,{40,82},color(t.onSurfaceVariant),
                          "35 AndroidX presets / native cubic paths / filled and outlined");
            for(int i=0;i<md::MaterialShapeCount;++i) {
                const ImVec2 p(40.f+(i%7)*172.f,132.f+(i/7)*178.f);
                draw->AddRectFilled(p,{p.x+164,p.y+166},color(t.surfaceContainer),20);
                md::DrawShape(draw,static_cast<md::MaterialShape>(i),{p.x+30,p.y+15},{104,104},color(t.primary));
                const char* name=md::ShapeName(static_cast<md::MaterialShape>(i));
                const float width=body->CalcTextSizeA(15,FLT_MAX,0,name).x;
                draw->AddText(body,15,{p.x+(164-width)*.5f,p.y+137},color(t.onSurface),name);
            }
            draw->AddText(label,20,{40,1046},color(t.onSurface),"Outline / rotation / non-square sizing");
            const md::MaterialShape examples[]={md::MaterialShape::Flower,md::MaterialShape::Clover4Leaf,
                md::MaterialShape::Heart,md::MaterialShape::PixelCircle,md::MaterialShape::Cookie9Sided};
            for(int i=0;i<5;++i)
                md::DrawShape(draw,examples[i],{60.f+i*244,1100},{180,120},i==2?color(t.primaryContainer):0,
                              i==1?45.f:0.f,color(t.primary),3.f,i==2?md::ShapeFit::Stretch:md::ShapeFit::Contain);
            FinishFrame();
        }
        const std::string name=dark?"md3-shapes-dark.draw":"md3-shapes-light.draw";
        md3test::DumpDrawData((directory/name).string());md3test::DestroyContext();
    }
    std::printf("Captured all 35 native MaterialShapes in %s\n",directory.string().c_str());
}
} // namespace
int main(int argc,char** argv) {
    if(argc>1) {
        assert((argc==3||argc==5)&&std::strcmp(argv[1],"--capture")==0);
        if(argc==5) { body_font=argv[3];label_font=argv[4]; }
    }
    Catalog();FilledGeometry();StrokesAndSmallSizes();NoopsAndState();Layout();
    if(argc>1) Capture(argv[2]);
}
