#include "sensor_tilt.h"

#include <android/log.h>
#include <dlfcn.h>
#include <cmath>
#include <cstring>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AImGui", __VA_ARGS__)

namespace aimgui {
namespace {

// ASensorEvent's layout is stable ABI, but pulling in <android/sensor.h> would
// also pull the link-time symbols we are deliberately resolving by hand. The
// only fields we need are the three floats at offset 16 (the acceleration
// vector inside the union) and the type tag.
struct SensorEvent {
    int   version;
    int   sensor;
    int   type;
    int   reserved0;
    int64_t timestamp;
    union {
        float data[16];
        struct { float x, y, z; } vec;
    };
};
static_assert(sizeof(SensorEvent) >= 24, "unexpected ASensorEvent layout");

constexpr int kTypeAccelerometer = 1;
constexpr float kGravity = 9.80665f;

using PFN_getInstance   = void* (*)();
using PFN_getDefault    = const void* (*)(void*, int);
using PFN_createQueue   = void* (*)(void*, void*, int, void*, void*);
using PFN_destroyQueue  = int   (*)(void*, void*);
using PFN_enableSensor  = int   (*)(void*, const void*);
using PFN_setEventRate  = int   (*)(void*, const void*, int32_t);
using PFN_getEvents     = ssize_t(*)(void*, SensorEvent*, size_t);
using PFN_looperPrepare = void* (*)(int);

PFN_destroyQueue g_DestroyQueue = nullptr;
PFN_getEvents    g_GetEvents    = nullptr;
void*            g_Manager      = nullptr;

} // namespace

bool SensorTilt::Init() {
    m_Lib = dlopen("libandroid.so", RTLD_NOW | RTLD_LOCAL);
    if (!m_Lib) return false;

    auto sym = [&](const char* n) { return dlsym(m_Lib, n); };
    auto getInstance  = (PFN_getInstance)  sym("ASensorManager_getInstance");
    auto getDefault   = (PFN_getDefault)   sym("ASensorManager_getDefaultSensor");
    auto createQueue  = (PFN_createQueue)  sym("ASensorManager_createEventQueue");
    auto enableSensor = (PFN_enableSensor) sym("ASensorEventQueue_enableSensor");
    auto setRate      = (PFN_setEventRate) sym("ASensorEventQueue_setEventRate");
    auto looperPrep   = (PFN_looperPrepare)sym("ALooper_prepare");
    g_DestroyQueue    = (PFN_destroyQueue) sym("ASensorManager_destroyEventQueue");
    g_GetEvents       = (PFN_getEvents)    sym("ASensorEventQueue_getEvents");

    if (!getInstance || !getDefault || !createQueue || !enableSensor ||
        !g_GetEvents || !looperPrep) {
        dlclose(m_Lib); m_Lib = nullptr;
        return false;
    }

    // The queue needs a looper on this thread; a bare ELF's main thread has
    // none until asked. ALOOPER_PREPARE_ALLOW_NON_CALLBACKS = 1, since we poll
    // the queue directly instead of registering a callback.
    m_Looper = looperPrep(1);
    if (!m_Looper) { dlclose(m_Lib); m_Lib = nullptr; return false; }

    g_Manager = getInstance();
    if (!g_Manager) { dlclose(m_Lib); m_Lib = nullptr; return false; }

    const void* accel = getDefault(g_Manager, kTypeAccelerometer);
    if (!accel) { dlclose(m_Lib); m_Lib = nullptr; return false; }

    m_Queue = createQueue(g_Manager, m_Looper, 0 /*ident*/, nullptr, nullptr);
    if (!m_Queue) { dlclose(m_Lib); m_Lib = nullptr; return false; }

    if (enableSensor(m_Queue, accel) < 0) {
        if (g_DestroyQueue) g_DestroyQueue(g_Manager, m_Queue);
        m_Queue = nullptr;
        dlclose(m_Lib); m_Lib = nullptr;
        return false;
    }
    // 20 ms. The highlight is smoothed heavily on top of this, so a faster
    // stream would only burn power for motion we then filter back out.
    if (setRate) setRate(m_Queue, accel, 20000);

    m_Ok = true;
    LOGI("[tilt] accelerometer ready");
    return true;
}

void SensorTilt::Shutdown() {
    if (m_Queue && g_DestroyQueue && g_Manager) g_DestroyQueue(g_Manager, m_Queue);
    if (m_Lib) dlclose(m_Lib);
    m_Queue = m_Lib = nullptr;
    m_Ok = false;
}

void SensorTilt::Update(float dt) {
    if (!m_Ok) return;

    // Drain to the newest sample; intermediate ones are of no interest since
    // the value below is a low-pass filter anyway.
    SensorEvent ev[16];
    float ax = 0.0f, ay = 0.0f;
    bool  got = false;
    for (;;) {
        ssize_t n = g_GetEvents(m_Queue, ev, 16);
        if (n <= 0) break;
        for (ssize_t i = 0; i < n; ++i) {
            if (ev[i].type != kTypeAccelerometer) continue;
            ax = ev[i].vec.x; ay = ev[i].vec.y;
            got = true;
        }
        if (n < 16) break;
    }
    if (!got) return;

    // Device axes point right and up; screen y points down, so y flips. Divide
    // by g to land in -1..1 and clamp: past about 45 degrees the lean stops
    // meaning anything useful for a light direction.
    float tx = -ax / kGravity;
    float ty =  ay / kGravity;
    tx = tx < -1.0f ? -1.0f : (tx > 1.0f ? 1.0f : tx);
    ty = ty < -1.0f ? -1.0f : (ty > 1.0f ? 1.0f : ty);

    // Heavy smoothing: raw accelerometer is noisy enough that feeding it
    // straight to the highlight makes the rim shimmer while the phone sits
    // still on a table. Frame-rate independent so 60 and 120 Hz agree.
    const float k = 1.0f - std::exp(-4.0f * dt);
    m_X += (tx - m_X) * k;
    m_Y += (ty - m_Y) * k;
}

} // namespace aimgui
