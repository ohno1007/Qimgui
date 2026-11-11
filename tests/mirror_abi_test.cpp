#include "platform/ANativeWindowCreator.h"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <string>
#include <unordered_map>

namespace {
using android::detail::StrongPointer;
using android::detail::ui::LayerStack;
using android::detail::ui::Rect;
using android::detail::ui::Rotation;
std::unordered_map<std::string, void*> exports;
int tokenObject, producerObject, surfaceObject;
void* transactionObject = nullptr;
void* stringObject = nullptr;
int created = 0, destroyed = 0, attached = 0, projected = 0, stacked = 0, closed = 0;
int attachmentStatus = 0;
const char* composerPrefix = "_ZN7android21SurfaceComposerClient";
const char* createString = "20createVirtualDisplayERKNSt3__112basic_stringIcNS1_11char_traitsIcEENS1_9allocatorIcEEEE";
const char* producerSuffix = "11Transaction17setDisplaySurfaceERKNS_2spINS_7IBinderEEERKNS2_INS_22IGraphicBufferProducerEEE";
const char* surfaceSuffix = "11Transaction17setDisplaySurfaceERKNS_2spINS_7IBinderEEERKNS2_INS_7SurfaceEEE";
const char* modernStack = "11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEENS_2ui10LayerStackE";
const char* legacyStack = "11Transaction20setDisplayLayerStackERKNS_2spINS_7IBinderEEEj";
const char* modernProjection = "11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEENS_2ui8RotationERKNS_4RectESB_";
const char* legacyProjection = "11Transaction20setDisplayProjectionERKNS_2spINS_7IBinderEEEjRKNS_4RectES9_";

template<typename Fn> void Raw(const std::string& name, Fn fn) { exports.emplace(name, reinterpret_cast<void*>(fn)); }
template<typename Fn> void Export(const std::string& suffix, Fn fn) { Raw(std::string(composerPrefix) + suffix, fn); }
void* Open(const char*, int) { return reinterpret_cast<void*>(1); }
void* Find(void*, const char* name) { auto it = exports.find(name); return it == exports.end() ? nullptr : it->second; }
int Close(void*) { ++closed; return 0; }
void Constructor(void*) {}
void Ref(void*, void*) {}
void StringConstructor(void* self, const char* name) { assert(!std::strcmp(name, "mirror-test")); stringObject = self; }
void StringDestructor(void* self) { assert(self == stringObject); }
void TransactionConstructor(void* self) { transactionObject = self; }
int Apply(void* self, bool synchronous, bool oneWay) { assert(self == transactionObject && !synchronous && !oneWay); return 0; }
StrongPointer<void> Token() { StrongPointer<void> result{}; result.pointer = &tokenObject; return result; }
std::vector<android::detail::ui::PhysicalDisplayId> Displays() { return {{123}}; }
StrongPointer<void> DisplayToken(android::detail::ui::PhysicalDisplayId id) { assert(id.value == 123); return Token(); }
int DisplayState(StrongPointer<void>& token, android::detail::ui::DisplayState* state) {
    assert(token.get() == &tokenObject); state->layerStackSpaceRect = {1216, 2640}; return 0;
}
StrongPointer<void> CheckCreate(const std::string* name, bool secure, const std::string* uniqueId, float rate, int arity) {
    assert(name && *name == "mirror-test" && secure);
    assert(uniqueId && *uniqueId == "mirror-test" && rate == 0.f);
    assert(created == 0); created = arity; return Token();
}
StrongPointer<void> Create4(const std::string* n, bool secure, const std::string* id, float rate) {
    return CheckCreate(n, secure, id, rate, 4);
}
StrongPointer<void> Create5(const std::string* n, bool secure, bool power, const std::string* id, float rate) {
    assert(!power); return CheckCreate(n, secure, id, rate, 5);
}
StrongPointer<void> Create6(const std::string* n, bool secure, bool power, const std::string* id, uint32_t uid, float rate) {
    assert(!power && uid == UINT32_MAX); return CheckCreate(n, secure, id, rate, 6);
}
StrongPointer<void> CreateLegacy(void* name, bool secure) {
    assert(name == stringObject && secure && created == 0); created = 2; return Token();
}
void Unsupported() { assert(false && "Unsupported ABI invoked"); }
int DestroyModern(StrongPointer<void>& token) { assert(token.get() == &tokenObject); ++destroyed; return 0; }
void DestroyLegacy(StrongPointer<void>& token) { assert(token.get() == &tokenObject); ++destroyed; }
StrongPointer<void> GetProducer(void* self) {
    assert(self == &surfaceObject); StrongPointer<void> result{}; result.pointer = &producerObject; return result;
}
void CheckTransaction(void* self, StrongPointer<void>& token) { assert(self == transactionObject && token.get() == &tokenObject); }
int Attach(void* self, StrongPointer<void>& token, StrongPointer<void>& producer) {
    CheckTransaction(self, token); assert(producer.get() == &producerObject); ++attached; return attachmentStatus;
}
void StackModern(void* self, StrongPointer<void>& token, LayerStack stack) {
    CheckTransaction(self, token); assert(stack.id == 0x12345678); ++stacked;
}
void StackLegacy(void* self, StrongPointer<void>& token, uint32_t stack) {
    CheckTransaction(self, token); assert(stack == 0x12345678); ++stacked;
}
void CheckProjection(void* self, StrongPointer<void>& token, uint32_t rotation, const Rect* src, const Rect* dst) {
    CheckTransaction(self, token); assert(rotation == 2 && src && dst);
    assert(src->left == 0 && src->top == 0 && src->right == 1216 && src->bottom == 2640);
    assert(dst->left == 0 && dst->top == 0 && dst->right == 608 && dst->bottom == 1320);
    ++projected;
}
void ProjectionModern(void* self, StrongPointer<void>& token, Rotation rotation, const Rect* src, const Rect* dst) {
    CheckProjection(self, token, static_cast<uint32_t>(rotation), src, dst);
}
void ProjectionLegacy(void* self, StrongPointer<void>& token, uint32_t rotation, const Rect* src, const Rect* dst) {
    CheckProjection(self, token, rotation, src, dst);
}
void CommonExports() {
    Export("C2Ev", Constructor);
    Raw("_ZNK7android7RefBase9incStrongEPKv", Ref);
    Export("21getPhysicalDisplayIdsEv", Displays);
    Export("23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE", DisplayToken);
    Export("15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE", DisplayState);
    Raw("_ZN7android7String8C2EPKc", StringConstructor);
    Raw("_ZN7android7String8D2Ev", StringDestructor);
    Export("11TransactionC2Ev", TransactionConstructor);
    Export("11Transaction5applyEbb", Apply);
    Raw("_ZNK7android7Surface25getIGraphicBufferProducerEv", GetProducer);
}
}
extern "C" int __system_property_get(const char*, char* value) { std::strcpy(value, "17"); return 2; }
int main(int argc, char** argv) {
    assert(argc == 2); const std::string scenario = argv[1]; CommonExports();
    const bool legacyLayout = scenario == "legacy-layout";
    const bool surfaceOnly = scenario == "surface-only";
    const bool unknownCreate = scenario == "unknown-create";
    const bool unknownLayout = scenario == "unknown-layout";
    const int arity = scenario == "create-4" ? 4 : scenario == "create-6" ? 6 : scenario == "create-legacy" ? 2 : 5;
    if (unknownCreate) Export(std::string(createString) + "bbS9_jfb", Unsupported);
    else if (arity == 4) Export(std::string(createString) + "bS9_f", Create4);
    else if (arity == 6) Export(std::string(createString) + "bbS9_jf", Create6);
    else if (arity == 2) Export("13createDisplayERKNS_7String8Eb", CreateLegacy);
    else Export(std::string(createString) + "bbS9_f", Create5);
    if (arity == 2) Export("14destroyDisplayERKNS_2spINS_7IBinderEEE", DestroyLegacy);
    else Export("21destroyVirtualDisplayERKNS_2spINS_7IBinderEEE", DestroyModern);
    if (!surfaceOnly) Export(producerSuffix, Attach);
    if (surfaceOnly || scenario == "both-surface-overloads") Export(surfaceSuffix, Unsupported);
    if (legacyLayout) { Export(legacyStack, StackLegacy); Export(legacyProjection, ProjectionLegacy); }
    else if (unknownLayout) {
        Export(std::string(modernStack) + "b", Unsupported);
        Export(std::string(modernProjection) + "b", Unsupported);
    } else { Export(modernStack, StackModern); Export(modernProjection, ProjectionModern); }
    attachmentStatus = scenario == "surface-error" ? -22 : 0;

    const auto& f = android::detail::Functionals::GetInstance({Open, Find, Close});
    assert(closed == 0);
    std::string missing;
    const bool supported = android::ANativeWindowCreator::ScreenCaptureSupported(&missing);
    if (supported != !(unknownCreate || surfaceOnly || unknownLayout))
        std::fprintf(stderr, "%s unexpected support result; missing: %s\n", scenario.c_str(), missing.c_str());
    assert(supported == !(unknownCreate || surfaceOnly || unknownLayout));
    if (unknownCreate) assert(missing.find("createDisplay") != std::string::npos);
    if (surfaceOnly) assert(missing.find("setDisplaySurface") != std::string::npos);
    if (unknownLayout) assert(missing.find("setDisplayLayerStack") != std::string::npos && missing.find("setDisplayProjection") != std::string::npos);

    auto& composer = android::ANativeWindowCreator::GetComposerInstance();
    auto token = composer.CreateVirtualDisplay("mirror-test", true);
    if (unknownCreate) { assert(!token && created == 0); token = Token(); }
    else { assert(token.get() == &tokenObject && created == arity); composer.DestroyVirtualDisplay(token); assert(destroyed == 1); }
    auto producer = f.Surface__GetIGraphicBufferProducer(&surfaceObject);
    android::detail::SurfaceComposerClientTransaction transaction;
    assert(transaction.SetDisplaySurface(token, producer) == (!surfaceOnly && attachmentStatus == 0));
    assert(attached == (surfaceOnly ? 0 : 1));
    const Rect src{0, 0, 1216, 2640}, dst{0, 0, 608, 1320};
    assert(transaction.SetDisplayLayerStack(token, 0x12345678) == !unknownLayout);
    assert(transaction.SetDisplayProjection(token, 2, src, dst) == !unknownLayout);
    assert(stacked == (unknownLayout ? 0 : 1) && projected == (unknownLayout ? 0 : 1));
    assert(transaction.Apply(false, false) == 0);
    assert(closed == 0);
    std::printf("PASS: mirror ABI %s\n", argv[1]);
}
