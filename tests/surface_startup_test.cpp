#include "platform/ANativeWindowCreator.h"
#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
int opens = 0, finds = 0, closes = 0, constructorCalls = 0, surfaceCalls = 0;
bool failOpen = false, exposeDisplay = false, exposeNullSurface = false, missingState = false;
bool exposeSurfaceSupport = false, legacySurface = false, constSurface = false, unknownSurface = false;
int legacyCalls = 0, constCalls = 0, transactionCalls = 0, unknownCalls = 0;
void* metadataAddress = nullptr;
void* composerAddress = nullptr;
int fakeControl = 0;
const char* legacyName = "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEENS_3gui13LayerMetadataEPj";
const char* constName = "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPj";
// An additional bool is deliberately unsupported; it must never be guessed.
const char* unknownName = "_ZN7android21SurfaceComposerClient13createSurfaceERKNS_7String8EjjiiRKNS_2spINS_7IBinderEEERKNS_3gui13LayerMetadataEPjb";
void* Open(const char*, int) { ++opens; return failOpen ? nullptr : reinterpret_cast<void*>(1); }
int Close(void*) { ++closes; return 0; }
void Constructor(void* self) { ++constructorCalls; composerAddress = self; }
void IncStrong(void*, void*) {}
std::vector<android::detail::ui::PhysicalDisplayId> Displays() { return {{1}}; }
android::detail::StrongPointer<void> Token(android::detail::ui::PhysicalDisplayId) {
    android::detail::StrongPointer<void> result{};
    result.pointer = reinterpret_cast<void*>(1);
    return result;
}
int State(android::detail::StrongPointer<void>&, android::detail::ui::DisplayState* state) {
    state->layerStackSpaceRect.width = 1080;
    state->layerStackSpaceRect.height = 2400;
    return 0;
}
android::detail::StrongPointer<android::detail::Surface> NullSurface(void*) {
    ++surfaceCalls;
    return {};
}
void Noop(void*) {}
void RefNoop(void*, void*) {}
void StringConstructor(void*, const char* name) { assert(!std::strcmp(name, "ABI test")); }
void MetadataConstructor(void* self) { metadataAddress = self; }
void TransactionConstructor(void*) {}
void* SetLayer(void* self, android::detail::StrongPointer<void>& surface, int32_t layer) {
    assert(surface.get() == &fakeControl && layer == INT_MAX); return self;
}
void* SetTrustedOverlay(void* self, android::detail::StrongPointer<void>& surface, bool trusted) {
    assert(surface.get() == &fakeControl && trusted); return self;
}
int Apply(void*, bool synchronous, bool oneWay) {
    assert(!synchronous && oneWay); ++transactionCalls; return 0;
}
android::detail::StrongPointer<void> CheckCreateArgs(void* self, const void* name, uint32_t width,
    uint32_t height, int32_t format, int32_t flags, const void* parent,
    const void* metadata, uint32_t* transformHint) {
    assert(self == composerAddress && name);
    assert(width == 333 && height == 777 && format == 1 && flags == 0x60);
    assert(parent && *static_cast<void* const*>(parent) == nullptr);
    assert(metadata && metadata == metadataAddress && transformHint == nullptr);
    android::detail::StrongPointer<void> result{}; result.pointer = &fakeControl; return result;
}
android::detail::StrongPointer<void> LegacyCreate(void* self, void* name, uint32_t width,
    uint32_t height, int32_t format, uint32_t flags, void* parent,
    void* metadata, uint32_t* transformHint) {
    ++legacyCalls; return CheckCreateArgs(self, name, width, height, format, flags, parent, metadata, transformHint);
}
android::detail::StrongPointer<void> ConstCreate(void* self, const void* name, uint32_t width,
    uint32_t height, int32_t format, int32_t flags, const void* parent,
    const void* metadata, uint32_t* transformHint) {
    ++constCalls; return CheckCreateArgs(self, name, width, height, format, flags, parent, metadata, transformHint);
}
void UnknownCreate() { ++unknownCalls; assert(false && "Unknown createSurface ABI called"); }
void* Find(void*, const char* name) {
    ++finds;
    if (legacySurface && !std::strcmp(name, legacyName)) return reinterpret_cast<void*>(&LegacyCreate);
    if (constSurface && !std::strcmp(name, constName)) return reinterpret_cast<void*>(&ConstCreate);
    if (unknownSurface && !std::strcmp(name, unknownName)) return reinterpret_cast<void*>(&UnknownCreate);
    if (exposeSurfaceSupport) {
        if (!std::strcmp(name, "_ZN7android7String8C2EPKc")) return reinterpret_cast<void*>(&StringConstructor);
        if (!std::strcmp(name, "_ZN7android7String8D2Ev")) return reinterpret_cast<void*>(&Noop);
        if (!std::strcmp(name, "_ZN7android3gui13LayerMetadataC2Ev")) return reinterpret_cast<void*>(&MetadataConstructor);
        if (!std::strcmp(name, "_ZNK7android7RefBase9decStrongEPKv")) return reinterpret_cast<void*>(&RefNoop);
        if (!std::strcmp(name, "_ZN7android14SurfaceControl10getSurfaceEv")) return reinterpret_cast<void*>(&NullSurface);
        if (!std::strcmp(name, "_ZN7android14SurfaceControl10disconnectEv")) return reinterpret_cast<void*>(&Noop);
        if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient11TransactionC2Ev")) return reinterpret_cast<void*>(&TransactionConstructor);
        if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient11Transaction8setLayerERKNS_2spINS_14SurfaceControlEEEi")) return reinterpret_cast<void*>(&SetLayer);
        if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient11Transaction17setTrustedOverlayERKNS_2spINS_14SurfaceControlEEEb")) return reinterpret_cast<void*>(&SetTrustedOverlay);
        if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient11Transaction5applyEbb")) return reinterpret_cast<void*>(&Apply);
    }
    if (exposeNullSurface && !std::strcmp(name, "_ZN7android14SurfaceControl10getSurfaceEv"))
        return reinterpret_cast<void*>(&NullSurface);
    if (!exposeDisplay) return nullptr;
    if (!std::strcmp(name, "_ZN7android21SurfaceComposerClientC2Ev")) return reinterpret_cast<void*>(&Constructor);
    if (!std::strcmp(name, "_ZNK7android7RefBase9incStrongEPKv")) return reinterpret_cast<void*>(&IncStrong);
    if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient21getPhysicalDisplayIdsEv")) return reinterpret_cast<void*>(&Displays);
    if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient23getPhysicalDisplayTokenENS_17PhysicalDisplayIdE")) return reinterpret_cast<void*>(&Token);
    if (!std::strcmp(name, "_ZN7android21SurfaceComposerClient15getDisplayStateERKNS_2spINS_7IBinderEEEPNS_2ui12DisplayStateE")) return missingState ? nullptr : reinterpret_cast<void*>(&State);
    return nullptr;
}
}
extern "C" int __system_property_get(const char*, char* value) { std::strcpy(value, "17"); return 2; }
int main(int argc, char** argv) {
    assert(argc == 2);
    failOpen = !std::strcmp(argv[1], "missing-library");
    missingState = !std::strcmp(argv[1], "missing-display-state");
    legacySurface = !std::strcmp(argv[1], "legacy-create-surface");
    constSurface = !std::strcmp(argv[1], "const-metadata-create-surface");
    unknownSurface = !std::strcmp(argv[1], "unknown-create-surface");
    exposeSurfaceSupport = legacySurface || constSurface || unknownSurface;
    exposeDisplay = !std::strcmp(argv[1], "missing-surface-symbols") || missingState || exposeSurfaceSupport;
    exposeNullSurface = !std::strcmp(argv[1], "null-surface");
    using android::detail::Functionals;
    const auto& functions = Functionals::GetInstance({Open, Find, Close});
    assert(opens == 2 && closes == 0); // Function pointers must retain their library handles.
    if (failOpen) assert(finds == 0);
    else assert(functions.libguiHandle && functions.libutilsHandle);
    const auto display = android::ANativeWindowCreator::GetDisplayInfo();
    if (exposeDisplay && !missingState) {
        assert(display.width == 1080 && display.height == 2400);
        assert(constructorCalls == 1);
    } else {
        assert(display.width == 0 && display.height == 0);
        assert(constructorCalls == 0);
    }
    if (legacySurface || constSurface) {
        assert(functions.CanCreateSurface(true));
        assert(bool(functions.SurfaceComposerClient__CreateSurface) == legacySurface);
        assert(bool(functions.SurfaceComposerClient__CreateSurface_constMetadata) == constSurface);
        auto control = android::ANativeWindowCreator::GetComposerInstance().CreateSurface("ABI test", 333, 777, 0x20, true);
        assert(control.data == &fakeControl);
        assert(legacyCalls == int(legacySurface) && constCalls == int(constSurface));
        assert(transactionCalls == 1 && unknownCalls == 0);
    } else {
        assert(android::ANativeWindowCreator::Create("test", 1080, 2400) == nullptr);
        assert(legacyCalls == 0 && constCalls == 0 && unknownCalls == 0);
    }
    assert(closes == 0);
    if (exposeNullSurface) {
        android::detail::SurfaceControl control{reinterpret_cast<void*>(1)};
        assert(control.GetSurface() == nullptr && surfaceCalls == 1);
    }
    std::printf("PASS: surface startup %s\n", argv[1]);
}
