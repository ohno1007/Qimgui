#include "platform/touch_slots.h"
#include <cassert>
#include <climits>
#include <cstdio>
#include <cstring>

int main() {
    Touch::Device device;
    device.S2TX = 2.f; device.S2TY = .5f;
    int slot = 0;
    using Touch::detail::ApplySlotEvent;
    using Touch::detail::CurrentFinger;
    for (int valid : {0, 9}) {
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_SLOT, valid));
        assert(CurrentFinger(device, slot) == &device.Finger[valid]);
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_TRACKING_ID, 123));
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_POSITION_X, 42));
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_POSITION_Y, 80));
        assert(device.Finger[valid].isDown && device.Finger[valid].id == 50 + valid);
        assert(device.Finger[valid].pos.x == 84.f && device.Finger[valid].pos.y == 40.f);
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_TRACKING_ID, -1));
        assert(!device.Finger[valid].isDown);
    }
    const Touch::Device before = device;
    for (int invalid : {-1, 10, INT_MIN, INT_MAX}) {
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_SLOT, invalid));
        assert(slot == -1 && CurrentFinger(device, slot) == nullptr);
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_TRACKING_ID, 123));
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_POSITION_X, 999));
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_POSITION_Y, 999));
        assert(ApplySlotEvent(device, 2, slot, ABS_MT_TRACKING_ID, -1));
        assert(std::memcmp(&device, &before, sizeof(device)) == 0);
    }
    // A valid slot after an ignored contact must work normally.
    assert(ApplySlotEvent(device, 2, slot, ABS_MT_SLOT, 3));
    assert(ApplySlotEvent(device, 2, slot, ABS_MT_TRACKING_ID, 456));
    assert(device.Finger[3].isDown && device.Finger[3].id == 53);
    assert(!ApplySlotEvent(device, 2, slot, ABS_PRESSURE, 5));
    std::puts("PASS: touch slot bounds, ignored contacts, coordinate scaling and recovery");
}
