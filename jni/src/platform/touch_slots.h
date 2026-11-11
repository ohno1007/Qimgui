#pragma once

#include <cstring>
#include "TouchHelperA.h"

namespace Touch::detail {

// The evdev slot number is a device-provided index, not an array bound.
inline touchObj* CurrentFinger(Device& device, int slot) {
    constexpr int count = sizeof(device.Finger) / sizeof(device.Finger[0]);
    return slot >= 0 && slot < count ? &device.Finger[slot] : nullptr;
}

// Return true when this is a handled multitouch ABS event, including ignored
// events for unsupported slots. Do not alias an invalid slot to a valid finger.
inline bool ApplySlotEvent(Device& device, int deviceIndex, int& slot,
                           unsigned short code, int value) {
    if (code == ABS_MT_SLOT) {
        slot = CurrentFinger(device, value) ? value : -1;
        return true;
    }
    if (code != ABS_MT_TRACKING_ID && code != ABS_MT_POSITION_X && code != ABS_MT_POSITION_Y)
        return false;
    touchObj* finger = CurrentFinger(device, slot);
    if (!finger) return true;
    constexpr int count = sizeof(device.Finger) / sizeof(device.Finger[0]);
    if (code == ABS_MT_TRACKING_ID) {
        finger->isDown = value != -1;
        if (finger->isDown) finger->id = (deviceIndex * 2 + 1) * count + slot;
    } else {
        finger->id = (deviceIndex * 2 + 1) * count + slot;
        if (code == ABS_MT_POSITION_X) finger->pos.x = static_cast<float>(value) * device.S2TX;
        else finger->pos.y = static_cast<float>(value) * device.S2TY;
    }
    return true;
}

} // namespace Touch::detail
