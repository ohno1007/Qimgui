// Window placement and the touch ownership rules. The module makes these same
// decisions inside an input filter, where a wrong answer either steals the
// whole screen from Android or lets a tap through to whatever is underneath.
#include "qk_geometry.h"

#include <cassert>
#include <cstdio>

namespace {

qk_window Panel(bool grab = true) {
    qk_window window{};
    window.panel_width = 1080;
    window.panel_height = 2400;
    window.grab = grab ? 1 : 0;
    assert(qk_window_place(&window, 220, 880, 640, 640) == 0);
    return window;
}

void Placement() {
    qk_window window = Panel();
    assert(window.x == 220 && window.y == 880 && window.width == 640);

    // Off-panel positions are pulled back instead of refused.
    assert(qk_window_place(&window, -100, -100, 640, 640) == 0);
    assert(window.x == 0 && window.y == 0);
    assert(qk_window_place(&window, 5000, 5000, 640, 640) == 0);
    assert(window.x == 1080 - 640 && window.y == 2400 - 640);
    // Full-panel windows are allowed.
    assert(qk_window_place(&window, 0, 0, 1080, 2400) == 0);
    assert(window.x == 0 && window.y == 0);

    // Sizes that cannot work are refused and leave the window alone.
    const s32 x = window.x, y = window.y, width = window.width;
    assert(qk_window_place(&window, 0, 0, 0, 100) == -22);
    assert(qk_window_place(&window, 0, 0, 100, -1) == -22);
    assert(qk_window_place(&window, 0, 0, 2000, 100) == -22);     // Wider than panel.
    assert(qk_window_place(&window, 0, 0, 100, 3000) == -22);     // Taller than panel.
    assert(qk_window_place(&window, 0, 0, QK_MAX_DIMENSION + 1, 100) == -22);
    assert(window.x == x && window.y == y && window.width == width);

    qk_window unknown{};
    assert(qk_window_place(&unknown, 0, 0, 100, 100) == -22);     // No panel size.
    assert(qk_window_place(nullptr, 0, 0, 100, 100) == -22);
}

void Mapping() {
    qk_window window = Panel();
    s32 x = 0, y = 0;

    // A digitiser reporting pixels already needs no scaling.
    qk_window_map(&window, 540, 1200, &x, &y);
    assert(x == 540 && y == 1200);
    // Out-of-range input is clamped to the panel, never wrapped.
    qk_window_map(&window, -10, 99999, &x, &y);
    assert(x == 0 && y == 2399);

    // A digitiser with its own resolution is scaled onto the panel.
    window.device_max_x = 4095;
    window.device_max_y = 4095;
    qk_window_map(&window, 0, 0, &x, &y);
    assert(x == 0 && y == 0);
    qk_window_map(&window, 4095, 4095, &x, &y);
    assert(x == 1079 && y == 2399);
    qk_window_map(&window, 2047, 2047, &x, &y);
    assert(x > 530 && x < 545 && y > 1190 && y < 1210);
    qk_window_map(&window, 9000, -5, &x, &y);
    assert(x == 1079 && y == 0);

    // Orientation flags, as a rotated or mirrored digitiser needs.
    window.invert_x = 1;
    qk_window_map(&window, 0, 0, &x, &y);
    assert(x == 1079 && y == 0);
    window.invert_x = 0;
    window.invert_y = 1;
    qk_window_map(&window, 0, 0, &x, &y);
    assert(x == 0 && y == 2399);
    window.invert_y = 0;

    // Swapped axes: the device's X becomes the panel's Y.
    window.swap_xy = 1;
    window.device_max_x = 2399;
    window.device_max_y = 1079;
    qk_window_map(&window, 1079, 2399, &x, &y);
    assert(x == 1079 && y == 2399);
}

void Containment() {
    const qk_window window = Panel();

    assert(qk_window_contains(&window, 220, 880));              // Top-left corner.
    assert(qk_window_contains(&window, 859, 1519));             // Bottom-right pixel.
    assert(!qk_window_contains(&window, 219, 880));
    assert(!qk_window_contains(&window, 860, 1519));            // Half-open rect.
    assert(!qk_window_contains(&window, 220, 1520));

    s32 x = 0, y = 0;
    qk_window_local(&window, 220, 880, &x, &y);
    assert(x == 0 && y == 0);
    qk_window_local(&window, 859, 1519, &x, &y);
    assert(x == 639 && y == 639);
    // Points outside clamp to the edge: a drag that leaves the window still
    // produces a usable pointer position.
    qk_window_local(&window, 0, 0, &x, &y);
    assert(x == 0 && y == 0);
    qk_window_local(&window, 1079, 2399, &x, &y);
    assert(x == 639 && y == 639);
}

void Ownership() {
    const qk_window window = Panel();
    qk_slots slots{};
    qk_slots_reset(&slots);
    assert(slots.primary == -1);

    // A contact outside the window belongs to Android.
    assert(qk_slots_begin(&slots, &window, 0, 100, 100) == QK_SLOT_PASSED);
    assert(slots.primary == -1);
    assert(qk_slots_move(&slots, &window, 0, 540, 1200) == QK_SLOT_PASSED);
    // Even after wandering into the window: the gesture keeps its owner.
    assert(qk_window_contains(&window, 540, 1200));
    assert(slots.primary == -1);
    assert(qk_slots_release(&slots, 0) == QK_SLOT_PASSED);

    // A contact starting inside belongs to the UI, for its whole life.
    assert(qk_slots_begin(&slots, &window, 0, 540, 1200) == QK_SLOT_CLAIMED);
    assert(slots.primary == 0);
    assert(qk_slots_move(&slots, &window, 0, 10, 10) == QK_SLOT_CLAIMED);
    s32 x = 0, y = 0;
    assert(qk_slots_position(&slots, 0, &x, &y) && x == 10 && y == 10);
    assert(qk_slots_release(&slots, 0) == QK_SLOT_CLAIMED);
    assert(slots.primary == -1);
    assert(!qk_slots_position(&slots, 0, &x, &y));

    // Second finger inside while the first is down: the first keeps the UI
    // pointer, and the second is still withheld from Android.
    assert(qk_slots_begin(&slots, &window, 2, 300, 1000) == QK_SLOT_CLAIMED);
    assert(qk_slots_begin(&slots, &window, 5, 800, 1400) == QK_SLOT_CLAIMED);
    assert(slots.primary == 2);
    assert(qk_slots_release(&slots, 2) == QK_SLOT_CLAIMED);
    assert(slots.primary == 5);          // Falls back to the other claimed slot.
    assert(qk_slots_release(&slots, 5) == QK_SLOT_CLAIMED);
    assert(slots.primary == -1);

    // Moving or releasing a slot that never started is not an event.
    assert(qk_slots_move(&slots, &window, 3, 500, 500) == QK_SLOT_FREE);
    assert(qk_slots_release(&slots, 3) == QK_SLOT_FREE);
    // Slots outside the tracked range never claim anything.
    assert(qk_slots_begin(&slots, &window, QK_MAX_SLOTS, 540, 1200) == QK_SLOT_PASSED);
    assert(qk_slots_begin(&slots, &window, -1, 540, 1200) == QK_SLOT_PASSED);
    assert(qk_slots_move(&slots, &window, QK_MAX_SLOTS, 0, 0) == QK_SLOT_FREE);
    assert(slots.primary == -1);
}

// With interception off the UI still follows the pointer for nothing: every
// contact goes to Android, which is the escape hatch when the filter misbehaves.
void GrabDisabled() {
    const qk_window window = Panel(false);
    qk_slots slots{};
    qk_slots_reset(&slots);

    assert(qk_slots_begin(&slots, &window, 0, 540, 1200) == QK_SLOT_PASSED);
    assert(slots.primary == -1);
    assert(qk_slots_release(&slots, 0) == QK_SLOT_PASSED);
}

// The window can move while a finger is down; the gesture keeps its owner.
void WindowMovesMidGesture() {
    qk_window window = Panel();
    qk_slots slots{};
    qk_slots_reset(&slots);

    assert(qk_slots_begin(&slots, &window, 0, 540, 1200) == QK_SLOT_CLAIMED);
    assert(qk_window_place(&window, 0, 0, 300, 300) == 0);
    assert(!qk_window_contains(&window, 540, 1200));
    assert(qk_slots_move(&slots, &window, 0, 540, 1200) == QK_SLOT_CLAIMED);
    assert(qk_slots_release(&slots, 0) == QK_SLOT_CLAIMED);

    // And a contact that starts where the window used to be goes to Android.
    assert(qk_slots_begin(&slots, &window, 0, 540, 1200) == QK_SLOT_PASSED);
}

} // namespace

int main() {
    Placement();
    Mapping();
    Containment();
    Ownership();
    GrabDisabled();
    WindowMovesMidGesture();
    std::printf("kernel geometry: all checks passed\n");
    return 0;
}
