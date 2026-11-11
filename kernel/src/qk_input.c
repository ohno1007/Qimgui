/* SPDX-License-Identifier: MIT */
#include <linux/device.h>
#include <linux/input.h>
#include <linux/module.h>
#include <linux/seq_file.h>
#include <linux/slab.h>
#include <linux/spinlock.h>
#include <linux/workqueue.h>

#include "qk_input.h"
#include "qk_utils.h"

#define QK_RING_SIZE   256
#define QK_FRAME_MAX   64
#define QK_REPLAY_MAX  256

/* One multitouch frame as it arrived, so the parts that belong to Android can
 * be replayed once the frame's positions are known.
 */
struct qk_recorded {
    unsigned int type, code;
    int value;
    int slot;      /* -1 for events that belong to no slot. */
};

struct qk_device {
    struct input_handle handle;
    bool touchscreen;
    bool replaying;
    int slot;
    /* Events bound for Android, handed back from a worker: see qk_replay. */
    struct work_struct replay_work;
    spinlock_t replay_lock;
    struct qk_recorded replay[QK_REPLAY_MAX];
    unsigned int replay_head, replay_tail;
    u64 replay_dropped;
    struct qk_recorded frame[QK_FRAME_MAX];
    unsigned int recorded;
    bool overflow;
    struct {
        bool touched;      /* Changed in this frame. */
        bool begin, end;
        int x, y;
        bool have_x, have_y;
    } pending[QK_MAX_SLOTS];
    struct qk_slots slots;
};

static struct qk_window *window;
static DEFINE_SPINLOCK(ring_lock);
static struct qk_ui_event ring[QK_RING_SIZE];
static u32 ring_head, ring_tail;
static u64 dropped_events, claimed_gestures, passed_gestures, replayed_frames;

static void qk_ring_push(const struct qk_ui_event *event)
{
    unsigned long flags;

    spin_lock_irqsave(&ring_lock, flags);
    if ((ring_head + 1) % QK_RING_SIZE == ring_tail) {
        /* The worker is behind: losing a move is better than losing a press,
         * so the oldest event goes.
         */
        ring_tail = (ring_tail + 1) % QK_RING_SIZE;
        dropped_events++;
    }
    ring[ring_head] = *event;
    ring_head = (ring_head + 1) % QK_RING_SIZE;
    spin_unlock_irqrestore(&ring_lock, flags);
}

u32 qk_input_drain(struct qk_ui_event *events, u32 max)
{
    unsigned long flags;
    u32 count = 0;

    spin_lock_irqsave(&ring_lock, flags);
    while (count < max && ring_tail != ring_head) {
        events[count++] = ring[ring_tail];
        ring_tail = (ring_tail + 1) % QK_RING_SIZE;
    }
    spin_unlock_irqrestore(&ring_lock, flags);
    return count;
}

static void qk_push_pointer(u32 type, int panel_x, int panel_y)
{
    struct qk_ui_event event = { 0 };
    s32 local_x = 0, local_y = 0;

    qk_window_local(window, panel_x, panel_y, &local_x, &local_y);
    event.type = type;
    event.x = local_x;
    event.y = local_y;
    qk_ring_push(&event);
}

static void qk_record(struct qk_device *device, unsigned int type, unsigned int code,
                      int value, int slot)
{
    if (device->recorded >= QK_FRAME_MAX) {
        device->overflow = true;
        return;
    }
    device->frame[device->recorded].type = type;
    device->frame[device->recorded].code = code;
    device->frame[device->recorded].value = value;
    device->frame[device->recorded].slot = slot;
    device->recorded++;
}

/* Hands a frame's untaken events back to the input core.
 *
 * This cannot happen inside the filter: input_event() takes the same
 * dev->event_lock the filter is already holding, and that spinlock is not
 * recursive. So the events are queued here and replayed from a worker, which
 * costs them a scheduling delay and nothing else. The replay flag lets them
 * back past this filter; it is read in the worker's own call stack.
 */
static void qk_replay_worker(struct work_struct *work)
{
    struct qk_device *device = container_of(work, struct qk_device, replay_work);
    struct input_dev *dev = device->handle.dev;
    unsigned long flags;
    bool wrote = false;

    device->replaying = true;
    for (;;) {
        struct qk_recorded event;

        spin_lock_irqsave(&device->replay_lock, flags);
        if (device->replay_tail == device->replay_head) {
            spin_unlock_irqrestore(&device->replay_lock, flags);
            break;
        }
        event = device->replay[device->replay_tail];
        device->replay_tail = (device->replay_tail + 1) % QK_REPLAY_MAX;
        spin_unlock_irqrestore(&device->replay_lock, flags);

        input_event(dev, event.type, event.code, event.value);
        wrote = event.type != EV_SYN || event.code != SYN_REPORT;
        if (!wrote)
            replayed_frames++;
    }
    device->replaying = false;
}

static void qk_replay_queue(struct qk_device *device, unsigned int type,
                            unsigned int code, int value)
{
    const unsigned int next = (device->replay_head + 1) % QK_REPLAY_MAX;

    if (next == device->replay_tail) {
        device->replay_dropped++;
        return;
    }
    device->replay[device->replay_head].type = type;
    device->replay[device->replay_head].code = code;
    device->replay[device->replay_head].value = value;
    device->replay_head = next;
}

static void qk_replay(struct qk_device *device, const bool *claimed)
{
    unsigned long flags;
    unsigned int i;
    int last_slot = -2;
    bool wrote = false;

    spin_lock_irqsave(&device->replay_lock, flags);
    for (i = 0; i < device->recorded; i++) {
        const struct qk_recorded *event = &device->frame[i];

        if (event->slot >= 0 && event->slot < QK_MAX_SLOTS && claimed[event->slot])
            continue;
        if (event->slot >= 0 && event->slot != last_slot) {
            qk_replay_queue(device, EV_ABS, ABS_MT_SLOT, event->slot);
            last_slot = event->slot;
        }
        qk_replay_queue(device, event->type, event->code, event->value);
        wrote = true;
    }
    if (wrote)
        qk_replay_queue(device, EV_SYN, SYN_REPORT, 0);
    spin_unlock_irqrestore(&device->replay_lock, flags);
    if (wrote)
        schedule_work(&device->replay_work);
}

static void qk_reset_frame(struct qk_device *device)
{
    unsigned int slot;

    device->recorded = 0;
    device->overflow = false;
    for (slot = 0; slot < QK_MAX_SLOTS; slot++) {
        device->pending[slot].touched = false;
        device->pending[slot].begin = false;
        device->pending[slot].end = false;
        device->pending[slot].have_x = false;
        device->pending[slot].have_y = false;
    }
}

/* Resolves one multitouch frame: decides who owns each contact, feeds the UI
 * the ones it owns, and replays the rest.
 */
static void qk_flush_frame(struct qk_device *device)
{
    bool claimed[QK_MAX_SLOTS] = { false };
    bool any_claimed = false;
    unsigned int slot;

    for (slot = 0; slot < QK_MAX_SLOTS; slot++) {
        const bool primary = device->slots.primary == (int)slot;
        s32 panel_x = 0, panel_y = 0;
        int decision;

        if (!device->pending[slot].touched)
            continue;
        if (device->pending[slot].end) {
            /* Report the lift at the contact's last known place. */
            qk_slots_position(&device->slots, slot, &panel_x, &panel_y);
            decision = qk_slots_release(&device->slots, slot);
            if (decision == QK_SLOT_CLAIMED && primary)
                qk_push_pointer(QK_EVENT_POINTER_UP, panel_x, panel_y);
        } else if (device->pending[slot].begin) {
            decision = qk_slots_begin(&device->slots, window, slot,
                                      device->pending[slot].x, device->pending[slot].y);
            if (decision == QK_SLOT_CLAIMED) {
                claimed_gestures++;
                if (device->slots.primary == (int)slot &&
                    qk_slots_position(&device->slots, slot, &panel_x, &panel_y))
                    qk_push_pointer(QK_EVENT_POINTER_DOWN, panel_x, panel_y);
            } else {
                passed_gestures++;
            }
        } else {
            decision = qk_slots_move(&device->slots, window, slot,
                                     device->pending[slot].x, device->pending[slot].y);
            if (decision == QK_SLOT_CLAIMED && primary &&
                qk_slots_position(&device->slots, slot, &panel_x, &panel_y))
                qk_push_pointer(QK_EVENT_POINTER_MOVE, panel_x, panel_y);
        }
        if (decision == QK_SLOT_CLAIMED) {
            claimed[slot] = true;
            any_claimed = true;
        }
    }
    /* Everything the UI did not claim goes back to the input core, one frame
     * late. A frame recorded past this buffer is replayed as far as it got:
     * dropping it silently would leave Android with a stuck contact.
     */
    if (device->overflow)
        LOGW("Input frame longer than %d events; replay is partial", QK_FRAME_MAX);
    qk_replay(device, claimed);
    (void)any_claimed;
    qk_reset_frame(device);
}

static void qk_touch_position(struct qk_device *device, int slot, bool x_axis, int value)
{
    if (slot < 0 || slot >= QK_MAX_SLOTS)
        return;
    device->pending[slot].touched = true;
    if (x_axis) {
        device->pending[slot].x = value;
        device->pending[slot].have_x = true;
    } else {
        device->pending[slot].y = value;
        device->pending[slot].have_y = true;
    }
    if (!device->pending[slot].have_x || !device->pending[slot].have_y) {
        /* Fill the missing axis from the contact's last known position. */
        s32 known_x = 0, known_y = 0;

        if (qk_slots_position(&device->slots, slot, &known_x, &known_y)) {
            if (!device->pending[slot].have_x)
                device->pending[slot].x = known_x;
            if (!device->pending[slot].have_y)
                device->pending[slot].y = known_y;
        }
    }
}

static bool qk_filter(struct input_handle *handle, unsigned int type,
                      unsigned int code, int value)
{
    struct qk_device *device = handle->private;

    if (!device || device->replaying || !window)
        return false;

    if (!device->touchscreen) {
        /* Keyboards are observed: the UI sees the key, and so does Android. */
        if (type == EV_KEY && code < BTN_MISC) {
            struct qk_ui_event event = { 0 };

            event.type = QK_EVENT_KEY;
            event.code = code;
            event.down = value != 0;
            if (value != 2) /* Autorepeat is left to ImGui's own timing. */
                qk_ring_push(&event);
        }
        return false;
    }

    /* Until the window is on screen there is nothing to intercept for, and
     * the window rect is still a placeholder: leave the device alone.
     */
    if (!READ_ONCE(window->grab) || !READ_ONCE(window->live))
        return false;

    switch (type) {
    case EV_ABS:
        switch (code) {
        case ABS_MT_SLOT:
            device->slot = value;
            qk_record(device, type, code, value, -1);
            return true;
        case ABS_MT_TRACKING_ID:
            if (device->slot >= 0 && device->slot < QK_MAX_SLOTS) {
                device->pending[device->slot].touched = true;
                if (value >= 0)
                    device->pending[device->slot].begin = true;
                else
                    device->pending[device->slot].end = true;
            }
            qk_record(device, type, code, value, device->slot);
            return true;
        case ABS_MT_POSITION_X:
        case ABS_X:
            qk_touch_position(device, device->slot, true, value);
            qk_record(device, type, code, value, device->slot);
            return true;
        case ABS_MT_POSITION_Y:
        case ABS_Y:
            qk_touch_position(device, device->slot, false, value);
            qk_record(device, type, code, value, device->slot);
            return true;
        default:
            qk_record(device, type, code, value, device->slot);
            return true;
        }
    case EV_KEY:
        if (code == BTN_TOUCH && device->slot >= 0 && device->slot < QK_MAX_SLOTS) {
            /* Single-touch devices announce contact here instead. */
            device->pending[device->slot].touched = true;
            if (value)
                device->pending[device->slot].begin = true;
            else
                device->pending[device->slot].end = true;
        }
        qk_record(device, type, code, value, device->slot);
        return true;
    case EV_SYN:
        if (code != SYN_REPORT) {
            qk_record(device, type, code, value, device->slot);
            return true;
        }
        qk_flush_frame(device);
        device->slot = 0;
        return true;
    default:
        return false;
    }
}

static bool qk_is_touchscreen(struct input_dev *dev)
{
    return test_bit(EV_ABS, dev->evbit) &&
           (test_bit(ABS_MT_POSITION_X, dev->absbit) || test_bit(ABS_X, dev->absbit)) &&
           test_bit(EV_KEY, dev->evbit) && test_bit(BTN_TOUCH, dev->keybit);
}

static int qk_connect(struct input_handler *handler, struct input_dev *dev,
                      const struct input_device_id *id)
{
    struct qk_device *device;
    int ret;

    device = kzalloc(sizeof(*device), GFP_KERNEL);
    if (!device)
        return -ENOMEM;
    device->touchscreen = qk_is_touchscreen(dev);
    spin_lock_init(&device->replay_lock);
    INIT_WORK(&device->replay_work, qk_replay_worker);
    device->handle.dev = dev;
    device->handle.handler = handler;
    device->handle.name = QK_TAG;
    device->handle.private = device;
    qk_slots_reset(&device->slots);
    qk_reset_frame(device);

    ret = input_register_handle(&device->handle);
    if (ret)
        goto free;
    ret = input_open_device(&device->handle);
    if (ret)
        goto unregister;
    LOGI("Input device attached: %s (%s)", dev_name(&dev->dev),
         device->touchscreen ? "touchscreen" : "keys");
    return 0;

unregister:
    input_unregister_handle(&device->handle);
free:
    kfree(device);
    return ret;
}

static void qk_disconnect(struct input_handle *handle)
{
    struct qk_device *device = handle->private;

    input_close_device(handle);
    input_unregister_handle(handle);
    /* No event can reach the filter any more; drain the pending replay. */
    cancel_work_sync(&device->replay_work);
    kfree(device);
}

static const struct input_device_id qk_ids[] = {
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT_MASK(EV_ABS) },
    },
    {
        .flags = INPUT_DEVICE_ID_MATCH_EVBIT,
        .evbit = { BIT_MASK(EV_KEY) },
    },
    { },
};
MODULE_DEVICE_TABLE(input, qk_ids);

static struct input_handler qk_handler = {
    .filter = qk_filter,
    .connect = qk_connect,
    .disconnect = qk_disconnect,
    .name = QK_TAG,
    .id_table = qk_ids,
};

int qk_input_init(struct qk_window *state)
{
    int ret;

    if (!state)
        return -EINVAL;
    window = state;
    ring_head = ring_tail = 0;
    ret = input_register_handler(&qk_handler);
    if (ret)
        window = NULL;
    return ret;
}

void qk_input_exit(void)
{
    if (!window)
        return;
    input_unregister_handler(&qk_handler);
    window = NULL;
}

void qk_input_status(struct seq_file *seq)
{
    unsigned long flags;
    u32 queued;

    spin_lock_irqsave(&ring_lock, flags);
    queued = (ring_head + QK_RING_SIZE - ring_tail) % QK_RING_SIZE;
    spin_unlock_irqrestore(&ring_lock, flags);
    seq_printf(seq, "input_queued=%u\ninput_dropped=%llu\nwindow_live=%u\n", queued,
               dropped_events, window ? READ_ONCE(window->live) : 0);
    seq_printf(seq, "gestures_claimed=%llu\ngestures_passed=%llu\nframes_replayed=%llu\n",
               claimed_gestures, passed_gestures, replayed_frames);
}
