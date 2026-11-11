#include "core/sensor_tilt.h"
#include <android/sensor.h>
#include <cassert>
#include <cmath>
#include <cstdio>
#include <cstring>

struct ASensorManager {};
struct ASensorEventQueue {};
struct ASensor {};
struct ALooper {};
namespace {
ASensorManager manager;
ASensorEventQueue queue;
ASensor sensor;
ALooper looper;
int reads=0, destroys=0, closes=0;
bool sensorAvailable=true, enableSucceeds=true;
constexpr float gravity=9.80665f;
ASensorManager* GetInstance() { return &manager; }
const ASensor* GetDefault(ASensorManager* m,int type) {
    assert(m==&manager&&type==ASENSOR_TYPE_ACCELEROMETER);
    return sensorAvailable?&sensor:nullptr;
}
ALooper* Prepare(int options) { assert(options==ALOOPER_PREPARE_ALLOW_NON_CALLBACKS);return &looper; }
ASensorEventQueue* CreateQueue(ASensorManager* m,ALooper* l,int ident,ALooper_callbackFunc callback,void* data) {
    assert(m==&manager&&l==&looper&&ident>=0&&!callback&&!data);return &queue;
}
int DestroyQueue(ASensorManager* m,ASensorEventQueue* q) { assert(m==&manager&&q==&queue);++destroys;return 0; }
int Enable(ASensorEventQueue* q,const ASensor* s) { assert(q==&queue&&s==&sensor);return enableSucceeds?0:-1; }
int SetRate(ASensorEventQueue* q,const ASensor* s,int32_t rate) { assert(q==&queue&&s==&sensor&&rate==20000);return 0; }
ssize_t GetEvents(ASensorEventQueue* q,ASensorEvent* events,size_t count) {
    assert(q==&queue&&count==16);
    if(reads++!=0) return 0;
    // The platform writes full ASensorEvent objects, including flags/reserved
    // fields. A full batch detects an undersized destination; different event
    // values detect a wrong stride even when only the first value looks valid.
    ASensorEvent batch[16]{};
    for(int i=0;i<16;++i) {
        batch[i].version=sizeof(ASensorEvent);
        batch[i].type=ASENSOR_TYPE_ACCELEROMETER;
        batch[i].timestamp=1000000+i;
        batch[i].acceleration.x=gravity*.1f;
        batch[i].acceleration.y=gravity*.2f;
        batch[i].flags=0x12345678;
        batch[i].reserved1[0]=0x11111111;
        batch[i].reserved1[1]=0x22222222;
        batch[i].reserved1[2]=0x33333333;
    }
    batch[15].acceleration.x=-gravity*.5f;
    batch[15].acceleration.y=gravity*.75f;
    std::memcpy(events,batch,sizeof(batch));
    return 16;
}
}
extern "C" int __android_log_print(int,const char*,const char*,...) { return 0; }
extern "C" void* __wrap_dlopen(const char* name,int) { assert(!std::strcmp(name,"libandroid.so"));return &manager; }
extern "C" int __wrap_dlclose(void* handle) { assert(handle==&manager);++closes;return 0; }
extern "C" void* __wrap_dlsym(void* handle,const char* name) {
    assert(handle==&manager);
    if(!std::strcmp(name,"ASensorManager_getInstance")) return reinterpret_cast<void*>(&GetInstance);
    if(!std::strcmp(name,"ASensorManager_getDefaultSensor")) return reinterpret_cast<void*>(&GetDefault);
    if(!std::strcmp(name,"ASensorManager_createEventQueue")) return reinterpret_cast<void*>(&CreateQueue);
    if(!std::strcmp(name,"ASensorManager_destroyEventQueue")) return reinterpret_cast<void*>(&DestroyQueue);
    if(!std::strcmp(name,"ASensorEventQueue_enableSensor")) return reinterpret_cast<void*>(&Enable);
    if(!std::strcmp(name,"ASensorEventQueue_setEventRate")) return reinterpret_cast<void*>(&SetRate);
    if(!std::strcmp(name,"ASensorEventQueue_getEvents")) return reinterpret_cast<void*>(&GetEvents);
    if(!std::strcmp(name,"ALooper_prepare")) return reinterpret_cast<void*>(&Prepare);
    return nullptr;
}
int main() {
    static_assert(sizeof(ASensorEvent)==104,"Unexpected official sensor ABI");
    aimgui::SensorTilt tilt;
    assert(tilt.Init()&&tilt.Available());
    tilt.Update(.25f);
    const float smoothing=1.f-std::exp(-1.f);
    assert(reads==2);
    assert(std::abs(tilt.x()-.5f*smoothing)<.00001f);
    assert(std::abs(tilt.y()-.75f*smoothing)<.00001f);
    const float x=tilt.x(),y=tilt.y();
    tilt.Update(.25f);assert(tilt.x()==x&&tilt.y()==y);
    tilt.Shutdown();assert(!tilt.Available()&&destroys==1&&closes==1);
    const int priorReads=reads;tilt.Update(.25f);assert(reads==priorReads);
    tilt.Shutdown();assert(destroys==1&&closes==1);
    std::puts("PASS: official 104-byte sensor ABI, full-batch writes, event stride and queue draining");

    sensorAvailable=false;
    assert(!tilt.Init()&&!tilt.Available());tilt.Update(.25f);assert(reads==priorReads&&closes==2);
    tilt.Shutdown();assert(destroys==1&&closes==2);
    sensorAvailable=true;enableSucceeds=false;
    assert(!tilt.Init()&&!tilt.Available());assert(destroys==2&&closes==3);
    tilt.Shutdown();assert(destroys==2&&closes==3);
    std::puts("PASS: missing sensor and failed enable clean up without polling or double destruction");
}
