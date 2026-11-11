#include "core/screen_mirror.h"
#include "platform/ANativeWindowCreator.h"

#include <cassert>
#include <cstdio>
#include <cstring>

namespace {
using android::detail::Functionals;
using android::detail::StrongPointer;
namespace ui = android::detail::ui;

enum class Failure { None, Reader, Window, Producer, Token, DisplayState, DisplaySurface, Apply };
Failure failure = Failure::None;
int readersCreated = 0, readersDeleted = 0, displaysCreated = 0, displaysDeleted = 0;
int surfaceCalls = 0, stackCalls = 0, projectionCalls = 0, applyCalls = 0, powerCalls = 0;
int producerObject = 0, tokenObject = 0;
alignas(std::max_align_t) char surfaceStorage[64]{};

void* Open(const char*, int) { return reinterpret_cast<void*>(1); }
void* Find(void*, const char*) { return nullptr; }
int Close(void*) { return 0; }
void Noop(void*) {}
void IncStrong(void*, void*) {}
void StringConstructor(void*, const char*) {}
std::vector<ui::PhysicalDisplayId> Displays() { return {{1}}; }
StrongPointer<void> Token(ui::PhysicalDisplayId) { return {&tokenObject}; }
int State(StrongPointer<void>&, ui::DisplayState* state) {
    if (failure == Failure::DisplayState) return -1;
    state->layerStack.id = 37;
    return 0;
}
StrongPointer<void> Producer(void* self) {
    assert(self == surfaceStorage);
    return {failure == Failure::Producer ? nullptr : &producerObject};
}
StrongPointer<void> CreateDisplay(void*, bool secure) {
    assert(!secure);
    if (failure == Failure::Token) return {};
    ++displaysCreated;
    return {&tokenObject};
}
void DestroyDisplay(StrongPointer<void>& token) {
    assert(token.get() == &tokenObject);
    ++displaysDeleted;
}
int SetSurface(void*, StrongPointer<void>& token, StrongPointer<void>& producer) {
    assert(token.get() == &tokenObject && producer.get() == &producerObject);
    ++surfaceCalls;
    return failure == Failure::DisplaySurface ? -22 : 0;
}
void SetStack(void*, StrongPointer<void>& token, ui::LayerStack stack) {
    assert(token.get() == &tokenObject && stack.id == 37);
    ++stackCalls;
}
void SetProjection(void*, StrongPointer<void>& token, ui::Rotation orientation,
                    const ui::Rect* src, const ui::Rect* dst) {
    assert(token.get() == &tokenObject && static_cast<int32_t>(orientation) == 0);
    assert(src->left == 0 && src->top == 0 && src->right == 1080 && src->bottom == 2400);
    assert(dst->left == 0 && dst->top == 0 && dst->right == 540 && dst->bottom == 1200);
    ++projectionCalls;
}
int Apply(void*, bool sync, bool oneWay) {
    assert(!sync && !oneWay);
    ++applyCalls;
    return failure == Failure::Apply ? -1 : 0;
}
void Power(StrongPointer<void>& token, int32_t mode) {
    assert(token.get() == &tokenObject && mode == 2);
    ++powerCalls;
}
int ReaderNew(int32_t w, int32_t h, int32_t format, uint64_t usage, int32_t maxImages, void** out) {
    assert(w == 540 && h == 1200 && format == 1 && usage == 0x300 && maxImages == 5);
    if (failure == Failure::Reader) return -1;
    *out = new int(0);
    ++readersCreated;
    return 0;
}
int ReaderWindow(void* reader, ANativeWindow** out) {
    assert(reader);
    if (failure == Failure::Window) return -1;
    *out = reinterpret_cast<ANativeWindow*>(surfaceStorage + sizeof(std::max_align_t) / 2);
    return 0;
}
void ReaderDelete(void* reader) { delete static_cast<int*>(reader); ++readersDeleted; }
int Acquire(void*, void**) { return -1; }
int Buffer(void*, AHardwareBuffer**) { return -1; }
}

extern "C" int __system_property_get(const char*, char* value) { std::strcpy(value, "17"); return 2; }
extern "C" void* __wrap_dlopen(const char* name, int) {
    assert(!std::strcmp(name, "libmediandk.so"));
    return reinterpret_cast<void*>(2);
}
extern "C" void* __wrap_dlsym(void* handle, const char* name) {
    assert(handle == reinterpret_cast<void*>(2));
    if (!std::strcmp(name, "AImageReader_newWithUsage")) return reinterpret_cast<void*>(&ReaderNew);
    if (!std::strcmp(name, "AImageReader_getWindow")) return reinterpret_cast<void*>(&ReaderWindow);
    if (!std::strcmp(name, "AImageReader_delete")) return reinterpret_cast<void*>(&ReaderDelete);
    if (!std::strcmp(name, "AImageReader_acquireLatestImage")) return reinterpret_cast<void*>(&Acquire);
    if (!std::strcmp(name, "AImage_delete")) return reinterpret_cast<void*>(&Noop);
    if (!std::strcmp(name, "AImage_getHardwareBuffer")) return reinterpret_cast<void*>(&Buffer);
    return nullptr;
}

int main() {
    // Inject calls after symbol loading; the ABI resolver has separate tests.
    auto& f = const_cast<Functionals&>(Functionals::GetInstance({Open, Find, Close}));
    f.SurfaceComposerClient__Constructor = Noop;
    f.RefBase__IncStrong = IncStrong;
    f.SurfaceComposerClient__GetPhysicalDisplayIds = Displays;
    f.SurfaceComposerClient__GetPhysicalDisplayToken = Token;
    f.SurfaceComposerClient__GetDisplayState = State;
    f.String8__Constructor = StringConstructor;
    f.String8__Destructor = Noop;
    f.Surface__GetIGraphicBufferProducer = Producer;
    f.SurfaceComposerClient__CreateDisplay = CreateDisplay;
    f.SurfaceComposerClient__DestroyDisplay = DestroyDisplay;
    f.SurfaceComposerClient__Transaction__Constructor = Noop;
    f.SurfaceComposerClient__Transaction__SetDisplaySurface = SetSurface;
    f.SurfaceComposerClient__Transaction__SetDisplayLayerStack = SetStack;
    f.SurfaceComposerClient__Transaction__SetDisplayProjection = SetProjection;
    f.SurfaceComposerClient__Transaction__Apply = Apply;
    f.SurfaceComposerClient__SetDisplayPowerMode = Power;

    for (const auto scenario : {Failure::Reader, Failure::Window, Failure::Producer,
                               Failure::Token, Failure::DisplayState,
                               Failure::DisplaySurface, Failure::Apply, Failure::None}) {
        failure = scenario;
        readersCreated = readersDeleted = displaysCreated = displaysDeleted = 0;
        surfaceCalls = stackCalls = projectionCalls = applyCalls = powerCalls = 0;
        aimgui::ScreenMirror mirror;
        assert(!mirror.Start(0, 1200, 1080, 2400));
        assert(readersCreated == 0);
        const bool success = mirror.Start(540, 1200, 1080, 2400);
        assert(success == (scenario == Failure::None));
        assert(mirror.running() == success);
        if (success) {
            assert(readersDeleted == 0 && displaysDeleted == 0 && powerCalls == 1);
            assert(surfaceCalls == 1 && stackCalls == 1 && projectionCalls == 1 && applyCalls == 1);
            assert(mirror.Start(540, 1200, 1080, 2400));
            assert(readersCreated == 1 && displaysCreated == 1);
        } else {
            assert(powerCalls == 0);
            if (scenario == Failure::DisplayState) assert(surfaceCalls == 0);
            if (scenario == Failure::DisplaySurface) assert(stackCalls == 0 && applyCalls == 0);
        }
        mirror.Stop();
        mirror.Stop();
        assert(!mirror.running());
        assert(readersCreated == readersDeleted && displaysCreated == displaysDeleted);
    }
    std::puts("PASS: screen mirror startup failure cleanup, status checks and idempotent stop");
}
