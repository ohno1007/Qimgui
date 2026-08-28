#pragma once

#include <linux/input.h>
#include <vector>
#include <functional>
#include "VectorStruct.h"

namespace Touch {
    struct touchObj {
        My_Vector2 pos{};
        int id = 0;
        bool isDown = false;
    };

    struct Device {
        int fd;
        float S2TX;
        float S2TY;
        input_absinfo absX, absY;
        touchObj Finger[10];

        // The frame being read, kept verbatim so it can be written back out
        // untouched if it turns out not to be ours, and which slots have been
        // judged. A slot is judged once, when the finger lands: a gesture that
        // starts on the overlay belongs to the overlay until it is lifted,
        // wherever it wanders.
        int pendN;
        input_event pend[192];
        bool claimed[10];
        bool mine[10];

        Device() { memset((void *) this, 0, sizeof(*this)); }
    };

    bool Init(const My_Vector2 &s, bool p_readOnly);

    void Close();

    void Down(float x, float y);

    void Move(float x, float y);

    void Up();

    void Move(touchObj *touch, float x, float y);

    void Upload();

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb);

    My_Vector2 Touch2Screen(const My_Vector2 &coord);

    My_Vector2 GetScale();

    void setOrientation(int orientation);

    void setOtherTouch(bool p_otherTouch);

    // Stop touches that land on the overlay from also reaching whatever is
    // behind it.
    //
    // Nothing in this process owns the input pipeline: the events are read from
    // /dev/input and the system has already had them. The only way to take one
    // away is EVIOCGRAB, which is exclusive — so while blocking is on the
    // touchscreen is grabbed and every event that is *not* ours is written back
    // out through a uinput device, verbatim, in the frame it arrived in.
    //
    // Which makes a wedged process able to leave a phone with a dead
    // touchscreen, so: Heartbeat() has to be called every frame and a watchdog
    // releases the grab if it stops, EmergencyRelease() is safe to call from a
    // signal handler, and Close() releases as before.
    void SetBlockRegion(float x, float y, float w, float h, bool enabled);
    void Heartbeat();
    void EmergencyRelease();
    bool Blocking();
}
