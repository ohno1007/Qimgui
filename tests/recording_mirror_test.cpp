#include "platform/ANativeWindowCreator.h"

#include <array>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {
using android::detail::StrongPointer;
using Creator = android::ANativeWindowCreator;
std::unordered_map<std::string, void*> exports;
std::unordered_map<const void*, std::string> names;
std::unordered_map<void*, int> disconnected, released;
std::array<int, 64> controls{};
alignas(std::max_align_t) std::array<std::array<unsigned char, 64>, 64> surfaces{};
std::array<int, 64> kinds{}; // 1 = window, 2 = mirror, 3 = mirror root.
int nextControl = 0, mirrorCalls = 0, rootCalls = 0, applyStatus = 0;
int matrixCalls = 0, positionCalls = 0, flagsCalls = 0;
void* composerAddress = nullptr;
void* mainControl = nullptr;
void* lastMirror = nullptr;
void* lastRoot = nullptr;
uint32_t lastFlags = 0;
bool missingMirror = false;
bool unknownMirror = false;
int mirrorArity = 2, calledMirrorArity = 0;

template<typename Fn> void Raw(const char* name, Fn fn) { exports.emplace(name, reinterpret_cast<void*>(fn)); }
void* Open(const char*, int) { return reinterpret_cast<void*>(1); }
void* Find(void*, const char* name) { auto it=exports.find(name); return it==exports.end()?nullptr:it->second; }
int Close(void*) { assert(false && "Resolved Android libraries must remain loaded"); return 0; }
void Constructor(void* self) { composerAddress=self; }
void Noop(void*) {}
void IncStrong(void*, void*) {}
void DecStrong(void* self, void*) { ++released[self]; }
void Disconnect(void* self) { ++disconnected[self]; }
void StringConstructor(void* self, const char* value) { names[self]=value; }
void StringDestructor(void* self) { names.erase(self); }
std::vector<android::detail::ui::PhysicalDisplayId> Displays() { return {{1}}; }
StrongPointer<void> Pointer(void* value) { StrongPointer<void> p{}; p.pointer=value; return p; }
StrongPointer<void> DisplayToken(android::detail::ui::PhysicalDisplayId id) {
    assert(id.value==1); return Pointer(reinterpret_cast<void*>(1));
}
int DisplayState(StrongPointer<void>& token, android::detail::ui::DisplayState* state) {
    assert(token.get()==reinterpret_cast<void*>(1));
    state->layerStack.id=0; state->layerStackSpaceRect={1080,2400}; return 0;
}
void* NewControl(int kind) {
    assert(nextControl < static_cast<int>(controls.size()));
    const int i=nextControl++; kinds[i]=kind; return &controls[i];
}
int ControlIndex(void* control) {
    for(int i=0;i<nextControl;++i) if(control==&controls[i]) return i;
    assert(false && "Unexpected SurfaceControl pointer"); return -1;
}
StrongPointer<void> CreateSurface(void* self, const void* name, uint32_t width, uint32_t height,
    int32_t format, int32_t flags, const void* parent, const void*, uint32_t* hint) {
    assert(self==composerAddress && width>0 && height>0 && format==1 && parent && !hint);
    assert(*static_cast<void* const*>(parent)==nullptr);
    if(names.at(name)=="recording-test") {
        assert(flags==0); mainControl=NewControl(1); return Pointer(mainControl);
    }
    assert(names.at(name).find("MirrorRoot@") == 0);
    assert(flags==0x4000); ++rootCalls; lastRoot=NewControl(3); return Pointer(lastRoot);
}
StrongPointer<android::detail::Surface> GetSurface(void* control) {
    const int i=ControlIndex(control); assert(kinds[i]==1);
    StrongPointer<android::detail::Surface> result{};
    result.pointer=reinterpret_cast<android::detail::Surface*>(surfaces[i].data()); return result;
}
StrongPointer<void> MirrorSource(void* self, void* source, int arity) {
    // mirrorSurface is a non-static member; the source must be the window,
    // never the composer object or an old control from before a rebuild.
    assert(self==composerAddress && source==mainControl && arity==mirrorArity);
    calledMirrorArity=arity;
    ++mirrorCalls; lastMirror=NewControl(2); return Pointer(lastMirror);
}
StrongPointer<void> MirrorSurface1(void* self, void* source) {
    return MirrorSource(self,source,1);
}
StrongPointer<void> MirrorSurface2(void* self, void* source, void* stopAt) {
    assert(!stopAt); return MirrorSource(self,source,2);
}
StrongPointer<void> MirrorSurface3(void* self, void* source, void* stopAt, void* cropBy) {
    // Android 17 adds cropBy. Both optional controls must be explicitly null;
    // forwarding only stopAt would leave the new argument indeterminate.
    assert(!stopAt && !cropBy); return MirrorSource(self,source,3);
}
void UnsupportedMirror() { assert(false && "Unknown four-parameter mirrorSurface ABI invoked"); }
void* SetLayer(void* self, StrongPointer<void>& control, int32_t layer) {
    ControlIndex(control.get()); assert(layer==INT_MAX); return self;
}
void* SetTrustedOverlay(void* self, StrongPointer<void>& control, bool trusted) {
    ControlIndex(control.get()); assert(trusted); return self;
}
void* SetLayerStack(void* self, StrongPointer<void>& control, uint32_t stack) {
    ControlIndex(control.get()); assert(stack==7); return self;
}
void* ShowOrHide(void* self, StrongPointer<void>& control) { ControlIndex(control.get()); return self; }
void* Reparent(void* self, StrongPointer<void>& control, StrongPointer<void>& parent) {
    assert(kinds[ControlIndex(control.get())]==2 && kinds[ControlIndex(parent.get())]==3); return self;
}
void* Matrix(void* self, StrongPointer<void>& control, float a, float b, float c, float d) {
    ControlIndex(control.get()); assert(std::isfinite(a)&&std::isfinite(b)&&std::isfinite(c)&&std::isfinite(d));
    ++matrixCalls; return self;
}
void* Position(void* self, StrongPointer<void>& control, float x, float y) {
    ControlIndex(control.get()); assert(std::isfinite(x)&&std::isfinite(y)); ++positionCalls; return self;
}
void* Flags(void* self, StrongPointer<void>& control, uint32_t flags, uint32_t mask) {
    assert(control.get()==mainControl && mask==0x40 && (flags==0 || flags==0x40));
    ++flagsCalls; lastFlags=flags; return self;
}
int Apply(void*, bool, bool) { return applyStatus; }
void CommonExports() {
    Raw("_ZN7android21SurfaceComposerClientC2Ev",Constructor);
    Raw("_ZNK7android7RefBase9incStrongEPKv",IncStrong);
    Raw("_ZNK7android7RefBase9decStrongEPKv",DecStrong);
    Raw("_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv",Displays);
    Raw("_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE",DisplayToken);
    Raw("_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE",DisplayState);
    Raw("_ZN7android7String8C2EPKc",StringConstructor);
    Raw("_ZN7android7String8D2Ev",StringDestructor);
    Raw("_ZN7android3gui13LayerMetadataC2Ev",Noop);
    Raw("_ZN7android14SurfaceControl10getSurfaceEv",GetSurface);
    Raw("_ZN7android14SurfaceControl10disconnectEv",Disconnect);
    Raw("_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj",CreateSurface);
    if(unknownMirror)
        Raw("_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_S2_S2_",UnsupportedMirror);
    else if(!missingMirror) {
        if(mirrorArity==1)
            Raw("_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlE",MirrorSurface1);
        else if(mirrorArity==2)
            Raw("_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_",MirrorSurface2);
        else
            Raw("_ZN7android21SurfaceComposerClient13mirrorSurfaceEPNS_14SurfaceControlES2_S2_",MirrorSurface3);
    }
    Raw("_ZN7android21SurfaceComposerClient11TransactionC2Ev",Noop);
    Raw("_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi",SetLayer);
    Raw("_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb",SetTrustedOverlay);
    Raw("_ZN7android21SurfaceComposerClient11Transaction13setLayerStackERKNS_2spINS_14SurfaceControlEEENS_2ui10LayerStackE",SetLayerStack);
    Raw("_ZN7android21SurfaceComposerClient11Transaction4showERKNS_2spINS_14SurfaceControlEEE",ShowOrHide);
    Raw("_ZN7android21SurfaceComposerClient11Transaction4hideERKNS_2spINS_14SurfaceControlEEE",ShowOrHide);
    Raw("_ZN7android21SurfaceComposerClient11Transaction8reparentERKNS_2spINS_14SurfaceControlEEES6_",Reparent);
    Raw("_ZN7android21SurfaceComposerClient11Transaction9setMatrixERKNS_2spINS_14SurfaceControlEEEffff",Matrix);
    Raw("_ZN7android21SurfaceComposerClient11Transaction11setPositionERKNS_2spINS_14SurfaceControlEEEff",Position);
    Raw("_ZN7android21SurfaceComposerClient11Transaction8setFlagsERKNS_2spINS_14SurfaceControlEEEjj",Flags);
    Raw("_ZN7android21SurfaceComposerClient11Transaction5applyEbb",Apply);
}
std::string Device(const char* id, uint32_t stack, int width=1080, int height=2400) {
    return std::string("DisplayDeviceInfo{\"test\", uniqueId=\"")+id+"\", type "+(stack?"VIRTUAL":"INTERNAL")+", state ON}\n"
        "  mUniqueId="+id+"\n  mCurrentLayerStack="+std::to_string(stack)+
        "\n  mCurrentOrientation=0\n  mCurrentLayerStackRect=Rect(0, 0 - "+std::to_string(width)+", "+std::to_string(height)+")\n";
}
void PairReleased(void* mirror, void* root) {
    assert(disconnected[mirror]==1 && released[mirror]==1);
    assert(disconnected[root]==1 && released[root]==1);
}
} // namespace

extern "C" int __system_property_get(const char*, char* value) { std::strcpy(value,"17"); return 2; }

int main(int argc,char** argv) {
    assert(argc==2); missingMirror=std::strcmp(argv[1],"missing-mirror")==0;
    unknownMirror=std::strcmp(argv[1],"unknown-mirror")==0;
    mirrorArity=std::strcmp(argv[1],"mirror-1")==0?1:std::strcmp(argv[1],"mirror-3")==0?3:2;
    CommonExports(); const auto& functions=android::detail::Functionals::GetInstance({Open,Find,Close});
    const bool knownMirror=!missingMirror && !unknownMirror;
    assert(bool(functions.SurfaceComposerClient__MirrorSurface)==(knownMirror && mirrorArity==1));
    assert(bool(functions.SurfaceComposerClient__MirrorSurface2)==(knownMirror && mirrorArity==2));
    assert(bool(functions.SurfaceComposerClient__MirrorSurface3)==(knownMirror && mirrorArity==3));
    ANativeWindow* window=Creator::Create("recording-test",1080,2400,false); assert(window);
    assert(Creator::GetCachedSurfaceCount()==1);
    const auto builtin=Device("local:0",0);
    const auto recording=builtin+Device("virtual:recorder",7);
    Creator::ProcessMirrorDisplayDump(recording);
    if(!knownMirror) {
        assert(mirrorCalls==0 && rootCalls==0 && calledMirrorArity==0 && Creator::GetMirrorSurfaceCount()==0);
        Creator::Destroy(window);
        std::printf("PASS: recording mirror %s leaves the window alive without invoking an unknown ABI\n",argv[1]); return 0;
    }
    assert(mirrorCalls==1 && rootCalls==1 && calledMirrorArity==mirrorArity && Creator::GetMirrorSurfaceCount()==1);
    assert(Creator::HasMirrorForLayerStack("7"));
    const auto firstMirror=lastMirror,firstRoot=lastRoot;
    const int initialMatrixCalls=matrixCalls;
    Creator::ProcessMirrorDisplayDump(recording);
    assert(mirrorCalls==1 && rootCalls==1 && matrixCalls==initialMatrixCalls);

    // Recorder resolution can change while its stack and rotation remain the
    // same. The mirror must update its transform, not remain zero/stale.
    Creator::ProcessMirrorDisplayDump(builtin+Device("virtual:recorder",7,720,1600));
    assert(mirrorCalls==1 && matrixCalls>initialMatrixCalls);
    Creator::ClearAllMirrorSurfaces();
    assert(Creator::GetMirrorSurfaceCount()==0 && !Creator::HasMirrorForLayerStack("7"));
    PairReleased(firstMirror,firstRoot);
    Creator::ProcessMirrorDisplayDump(recording);
    assert(mirrorCalls==2 && rootCalls==2 && Creator::GetMirrorSurfaceCount()==1);

    auto deadMirror=lastMirror,deadRoot=lastRoot;
    Creator::ProcessMirrorDisplayDump(builtin); // Recorder disconnected.
    assert(Creator::GetMirrorSurfaceCount()==0); PairReleased(deadMirror,deadRoot);
    Creator::ProcessMirrorDisplayDump(recording);
    assert(mirrorCalls==3 && Creator::GetMirrorSurfaceCount()==1);
    deadMirror=lastMirror; deadRoot=lastRoot;
    Creator::ProcessMirrorDisplayDump(builtin+Device("virtual:new-recorder",7));
    assert(mirrorCalls==4 && Creator::GetMirrorSurfaceCount()==1); PairReleased(deadMirror,deadRoot);

    Creator::ProcessMirrorDisplayDump(builtin+Device("virtual:new-recorder",7)+Device("AImGuiMirror",0x41493344));
    assert(mirrorCalls==4 && Creator::GetMirrorSurfaceCount()==1);
    assert(!Creator::HasMirrorForLayerStack(std::to_string(0x41493344)));

    assert(Creator::SetSkipScreenshot(window,false) && lastFlags==0);
    applyStatus=-22;
    assert(!Creator::SetSkipScreenshot(window,true) && lastFlags==0x40);
    applyStatus=0;
    assert(Creator::SetSkipScreenshot(window,false) && lastFlags==0 && flagsCalls==3);

    deadMirror=lastMirror; deadRoot=lastRoot;
    Creator::Destroy(window);
    assert(Creator::GetCachedSurfaceCount()==0 && Creator::GetMirrorSurfaceCount()==0);
    PairReleased(deadMirror,deadRoot);
    window=Creator::Create("recording-test",1080,2400,false); assert(window);
    Creator::ProcessMirrorDisplayDump(recording);
    assert(mirrorCalls==5 && Creator::GetMirrorSurfaceCount()==1);
    Creator::Destroy(window);
    assert(Creator::GetCachedSurfaceCount()==0 && Creator::GetMirrorSurfaceCount()==0);

    // Some display dumps contain only the virtual device. The compositor's
    // primary display query still supplies its geometry in that case.
    window=Creator::Create("recording-test",1080,2400,false); assert(window);
    Creator::ProcessMirrorDisplayDump(Device("virtual:recorder",7));
    assert(mirrorCalls==6 && Creator::GetMirrorSurfaceCount()==1);
    deadMirror=lastMirror; deadRoot=lastRoot;
    Creator::ProcessMirrorDisplayDump("");
    Creator::ProcessMirrorDisplayDump("dumpsys display output unavailable\n");
    assert(mirrorCalls==6 && Creator::GetMirrorSurfaceCount()==1);
    assert(lastMirror==deadMirror && released[deadMirror]==0);

    const int beforeClearMatrix=matrixCalls,beforeClearPosition=positionCalls;
    Creator::ClearMirrorSurfaceForLayerStack("7");
    assert(Creator::GetMirrorSurfaceCount()==0 && !Creator::HasMirrorForLayerStack("7"));
    PairReleased(deadMirror,deadRoot);
    Creator::ProcessMirrorDisplayDump(recording);
    assert(mirrorCalls==7 && Creator::GetMirrorSurfaceCount()==1);
    assert(matrixCalls>beforeClearMatrix && positionCalls>beforeClearPosition);
    Creator::Destroy(window);
    assert(Creator::GetCachedSurfaceCount()==0 && Creator::GetMirrorSurfaceCount()==0);
    std::printf("PASS: recording mirror %d-argument ABI, ownership, reconnect/rebuild/resize, single-stack cleanup, incomplete dumps, private stack exclusion and capture flag transaction failures\n",mirrorArity);
}
