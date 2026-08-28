#include <dirent.h>
#include <fcntl.h>
#include <unistd.h>
#include <cmath>
#include <linux/input.h>
#include <linux/uinput.h>
#include <vector>
#include <thread>
#include <unordered_map>
#include <atomic>
#include <ctime>
#include "spinlock.h"

#include "imgui.h"
#include "TouchHelperA.h"
#include "Utils.h"

#define maxE 5
#define maxF 10
#define UNGRAB 0
#define GRAB 1

//TODO 触摸穿透

namespace Touch {
    static struct {
        input_event downEvent[2]{{{}, EV_KEY, BTN_TOUCH,       1}, {{}, EV_KEY, BTN_TOOL_FINGER, 1}};
        input_event event[512]{0};
    } input;

    static My_Vector2 touch_scale;

    static My_Vector2 screenSize;

    static std::vector<Device> devices;

    static int nowfd;

    static int orientation = 0;

    static bool initialized = false;

    static bool readOnly = false;

    static bool otherTouch = false;

    static std::function<void(std::vector<Device> *)> callback;

    static spinlock lock;

    // ── Blocking pass-through ───────────────────────────────────────────
    static bool blockEnabled = false;          // asked for
    static std::atomic<bool> blockGrabbed{false};  // actually holding them
    static float blockX = 0, blockY = 0, blockW = 0, blockH = 0;
    static std::atomic<long long> heartbeat{0};
    static std::atomic<bool>      watchdogRun{false};

    static long long NowMs() {
        struct timespec ts{};
        clock_gettime(CLOCK_MONOTONIC, &ts);
        return (long long)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
    }

    static bool AnyFingerDown() {
        for (auto &d : devices)
            for (int s = 0; s < maxF; ++s)
                if (d.Finger[s].isDown) return true;
        return false;
    }

    // Only ever between gestures. Taking the grab mid-touch strands the down
    // the system already has — the rest of that gesture would come back out of
    // a *different* device and the original would never be lifted — and giving
    // it back mid-touch strands one the other way.
    static void SetGrab(bool on) {
        if (on == blockGrabbed.load()) return;
        if (AnyFingerDown()) return;
        for (auto &d : devices)
            if (d.fd > 0) ioctl(d.fd, EVIOCGRAB, on ? GRAB : UNGRAB);
        blockGrabbed.store(on);
    }

    // Async-signal reachable: no allocation, no locks, just the two ioctls that
    // give the touchscreen back. Worth calling from a crash handler, because
    // the alternative is a phone that needs a reboot to be touched again.
    void EmergencyRelease() {
        for (auto &d : devices)
            if (d.fd > 0) ioctl(d.fd, EVIOCGRAB, UNGRAB);
        blockGrabbed.store(false);
        blockEnabled = false;
    }

    bool Blocking() { return blockGrabbed; }

    void Heartbeat() { heartbeat.store(NowMs()); }

    // The grab is exclusive, so a process that stops drawing while holding it
    // takes the touchscreen with it. Nothing else can notice that from inside,
    // so this does: no frame for two seconds and the devices go back.
    static void *Watchdog(void *) {
        while (watchdogRun.load()) {
            struct timespec ts{0, 250 * 1000 * 1000};
            nanosleep(&ts, nullptr);
            if (!blockGrabbed.load()) continue;
            const long long last = heartbeat.load();
            if (last != 0 && NowMs() - last > 2000) {
                // Not SetGrab: that waits for the finger to come up, and a
                // process that has stopped drawing may never see it do so.
                for (auto &d : devices)
                    if (d.fd > 0) ioctl(d.fd, EVIOCGRAB, UNGRAB);
                blockGrabbed.store(false);
            }
        }
        return nullptr;
    }

    static bool InsideBlock(const My_Vector2 &p) {
        return p.x >= blockX && p.y >= blockY &&
               p.x <= blockX + blockW && p.y <= blockY + blockH;
    }

    void Upload() {
        static bool isFirstDown = true;
        int tmpCnt = 0, tmpCnt2 = 0;
        for (auto &device: devices) {
            for (auto &finger: device.Finger) {
                if (finger.isDown) {
                    if (tmpCnt2++ > 20) {
                        goto finish;
                    }
                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_X;
                    input.event[tmpCnt].value = (int) finger.pos.x;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_Y;
                    input.event[tmpCnt].value = (int) finger.pos.y;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_POSITION_X;
                    input.event[tmpCnt].value = (int) finger.pos.x;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_POSITION_Y;
                    input.event[tmpCnt].value = (int) finger.pos.y;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_ABS;
                    input.event[tmpCnt].code = ABS_MT_TRACKING_ID;
                    input.event[tmpCnt].value = finger.id;
                    tmpCnt++;

                    input.event[tmpCnt].type = EV_SYN;
                    input.event[tmpCnt].code = SYN_MT_REPORT;
                    input.event[tmpCnt].value = 0;
                    tmpCnt++;
                }
            }
        }
        finish:
        bool is = false;
        if (tmpCnt == 0) {
            input.event[tmpCnt].type = EV_SYN;
            input.event[tmpCnt].code = SYN_MT_REPORT;
            input.event[tmpCnt].value = 0;
            tmpCnt++;
            if (!isFirstDown) {
                isFirstDown = true;
                input.event[tmpCnt].type = EV_KEY;
                input.event[tmpCnt].code = BTN_TOUCH;
                input.event[tmpCnt].value = 0;
                tmpCnt++;
                input.event[tmpCnt].type = EV_KEY;
                input.event[tmpCnt].code = BTN_TOOL_FINGER;
                input.event[tmpCnt].value = 0;
                tmpCnt++;
            }
        } else {
            is = true;
        }
        input.event[tmpCnt].type = EV_SYN;
        input.event[tmpCnt].code = SYN_REPORT;
        input.event[tmpCnt].value = 0;
        tmpCnt++;

        if (is && isFirstDown) {
            isFirstDown = false;
            write(nowfd, &input, sizeof(struct input_event) * (tmpCnt + 2));
        } else {
            write(nowfd, input.event, sizeof(struct input_event) * tmpCnt);
        }
    }


    /*void *TypeB(void *arg) {
        int i = (int) (long) arg;
        Device &device = devices[i];
        int latest = 0;
        input_event inputEvent[64]{0};

        while (Touch_initialized) {
            auto readSize = (int32_t) read(origfd[i], inputEvent, sizeof(inputEvent));
            if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
                continue;
            }
            size_t count = size_t(readSize) / sizeof(input_event);
            for (size_t j = 0; j < count; j++) {
                input_event &ie = inputEvent[j];
                if (latest < 0)
                    latest = 0;
                if (latest >= 10)
                    continue;
                if (ie.code == ABS_MT_TRACKING_ID) {
                    if (ie.value < 0) {
                        Finger[i][latest].isDown = false;
                    } else {
                        Finger[i][latest].isDown = true;
                    }
                    Finger[i][latest].id = (i * 2 + 1) * maxF + ie.value;
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_X) {
                    Finger[i][latest].isDown = true;
                    Finger[i][latest].x = (int) (ie.value * S2TX);
                    continue;
                }
                if (ie.code == ABS_MT_POSITION_Y) {
                    Finger[i][latest].isDown = true;
                    Finger[i][latest].y = (int) (ie.value * S2TY);
                    continue;
                }
                if (ie.code == SYN_MT_REPORT) {
                    latest += 1;
                    continue;
                }
                if (ie.code == SYN_REPORT) {
                    Upload();
                    memset(&Finger[i][0], 0, sizeof(Finger) * 10);
                    latest = -1;
                    continue;
                }
            }
        }
        return nullptr;
    }*/

    static void *TypeA(void *arg) {
        int i = (int) (long) arg;
        Device &device = devices[i];

        int latest = 0;
        input_event inputEvent[64]{0};

        while (initialized) {
            auto readSize = (int32_t) read(device.fd, inputEvent, sizeof(inputEvent));
            if (readSize <= 0 || (readSize % sizeof(input_event)) != 0) {
                continue;
            }
            size_t count = size_t(readSize) / sizeof(input_event);

            lock.lock();
            for (size_t j = 0; j < count; j++) {
                input_event &ie = inputEvent[j];
                if (blockGrabbed.load() && device.pendN < (int)(sizeof(device.pend) / sizeof(device.pend[0])))
                    device.pend[device.pendN++] = ie;
                if (ie.type == EV_ABS) {
                    if (ie.code == ABS_MT_SLOT) {
                        latest = ie.value;
                        continue;
                    }
                    if (ie.code == ABS_MT_TRACKING_ID) {
                        if (ie.value == -1) {
                            device.Finger[latest].isDown = false;
                        } else {
                            device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                            device.Finger[latest].isDown = true;
                        }
                        continue;
                    }
                    if (ie.code == ABS_MT_POSITION_X) {
                        device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                        device.Finger[latest].pos.x = (float) ie.value * device.S2TX;
                        continue;
                    }
                    if (ie.code == ABS_MT_POSITION_Y) {
                        device.Finger[latest].id = (i * 2 + 1) * maxF + latest;
                        device.Finger[latest].pos.y = (float) ie.value * device.S2TY;
                        continue;
                    }
                }
                if (ie.code == SYN_REPORT) {
                    // Judge each finger once, as it lands, then hand the frame
                    // back to the system unless one of ours is down in it.
                    if (blockGrabbed.load()) {
                        bool ours = false;
                        for (int s = 0; s < maxF; ++s) {
                            if (device.Finger[s].isDown) {
                                if (!device.claimed[s]) {
                                    device.claimed[s] = true;
                                    device.mine[s] = InsideBlock(Touch2Screen(device.Finger[s].pos));
                                }
                                if (device.mine[s]) ours = true;
                            } else {
                                device.claimed[s] = false;
                                device.mine[s]    = false;
                            }
                        }
                        if (!ours && nowfd > 0 && device.pendN > 0)
                            write(nowfd, device.pend,
                                  sizeof(input_event) * (size_t)device.pendN);
                        device.pendN = 0;
                    }
                    if (ImGui::GetCurrentContext() != nullptr) {
                        ImGuiIO &io = ImGui::GetIO();
                        if (device.Finger[latest].isDown) {
                            auto pos = Touch2Screen(device.Finger[latest].pos);
                            io.MousePos = ImVec2(pos.x, pos.y);
                            io.MouseDown[0] = true;
                        } else {
                            io.MouseDown[0] = false;
                        }
                    }

                    // AImGui patch: never re-inject events. We only observe.
                    if (false /* was: !readOnly */) {
                        if (callback) {
                            callback(&devices);
                        } else {
                            Upload();
                        }
                    }
                    continue;
                }
            }
            lock.unlock();

        }
        return nullptr;
    }

    static bool checkDeviceIsTouch(int fd) {
        uint8_t *bits = NULL;
        ssize_t bits_size = 0;
        int res, j, k;
        bool itmp = false, itmp2 = false, itmp3 = false;
        struct input_absinfo abs{};
        while (true) {
            res = ioctl(fd, EVIOCGBIT(EV_ABS, bits_size), bits);
            if (res < bits_size)
                break;
            bits_size = res + 16;
            bits = (uint8_t *) realloc(bits, bits_size * 2);
        }
        for (j = 0; j < res; j++) {
            for (k = 0; k < 8; k++)
                if (bits[j] & 1 << k && ioctl(fd, EVIOCGABS(j * 8 + k), &abs) == 0) {
                    if (j * 8 + k == ABS_MT_SLOT) {
                        itmp = true;
                        continue;
                    }
                    if (j * 8 + k == ABS_MT_POSITION_X) {
                        itmp2 = true;
                        continue;
                    }
                    if (j * 8 + k == ABS_MT_POSITION_Y) {
                        itmp3 = true;
                        continue;
                    }
                }
        }
        free(bits);
        return itmp && itmp2 && itmp3;
    }

    bool Init(const My_Vector2 &s, bool p_readOnly) {
        Close();
        devices.clear();
        My_Vector2 size = s;
        readOnly = p_readOnly;
        if (size.x > size.y) {
            screenSize = size;
        } else {
            screenSize = {size.y, size.x};
        }
        DIR *dir = opendir("/dev/input/");
        if (!dir) {
            return false;
        }

        dirent *ptr = NULL;
        int eventCount = 0;
        while ((ptr = readdir(dir)) != NULL) {
            if (strstr(ptr->d_name, "event"))
                eventCount++;
        }

        char temp[128];
        for (int i = 0; i <= eventCount; i++) {
            sprintf(temp, "/dev/input/event%d", i);
            int fd = open(temp, O_RDWR);
            if (fd < 0) {
                continue;
            }
            if (checkDeviceIsTouch(fd)) {
                Device device{};
                if (ioctl(fd, EVIOCGABS(ABS_MT_POSITION_X), &device.absX) == 0
                    && ioctl(fd, EVIOCGABS(ABS_MT_POSITION_Y), &device.absY) == 0) {
                    device.fd = fd;
                    // AImGui patch: never EVIOCGRAB so the system still gets
                    // touch events (no exclusive grab → phone stays usable).
                    if (false /* was: !readOnly */) {
                        ioctl(fd, EVIOCGRAB, GRAB);
                    }
                    devices.push_back(device);
                }
            } else {
                close(fd);
            }
        }

        if (devices.empty()) {
            return false;
        }

        int screenX = devices[0].absX.maximum;
        int screenY = devices[0].absY.maximum;

        // The device the blocked-through events are written back out of. Made
        // up front so that turning blocking on is only an ioctl and cannot fail
        // half way, and inert until something is written to it. Optional: no
        // /dev/uinput means no blocking, not no touch.
        {
            struct uinput_user_dev ui_dev;
            nowfd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
            if (nowfd <= 0) {
                nowfd = 0;
                goto uinput_done;
            }

            int string_len = rand() % 10 + 5;
            char string[string_len];
            memset(&ui_dev, 0, sizeof(ui_dev));

            genRandomString(string, string_len);
            strncpy(ui_dev.name, string, UINPUT_MAX_NAME_SIZE);

            ui_dev.id.bustype = 0;
            ui_dev.id.vendor = rand() % 10 + 5;
            ui_dev.id.product = rand() % 10 + 5;
            ui_dev.id.version = rand() % 10 + 5;

            ioctl(nowfd, UI_SET_PROPBIT, INPUT_PROP_DIRECT);

            ioctl(nowfd, UI_SET_EVBIT, EV_ABS);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_X);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_Y);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_X);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_POSITION_Y);
            ioctl(nowfd, UI_SET_ABSBIT, ABS_MT_TRACKING_ID);
            ioctl(nowfd, UI_SET_EVBIT, EV_SYN);
            ioctl(nowfd, UI_SET_EVBIT, EV_KEY);
            ioctl(nowfd, UI_SET_KEYBIT, BTN_TOOL_FINGER);
            ioctl(nowfd, UI_SET_KEYBIT, BTN_TOUCH);

            genRandomString(string, string_len);
            ioctl(nowfd, UI_SET_PHYS, string);

            int fd = devices[0].fd;
            {
                struct input_id id{};
                if (ioctl(fd, EVIOCGID, &id) == 0) {
                    ui_dev.id.bustype = id.bustype;
                    ui_dev.id.vendor = id.vendor;
                    ui_dev.id.product = id.product;
                    ui_dev.id.version = id.version;
                }
                uint8_t *bits = NULL;
                ssize_t bits_size = 0;
                int res, j, k;
                while (1) {
                    res = ioctl(fd, EVIOCGBIT(EV_KEY, bits_size), bits);
                    if (res < bits_size)
                        break;
                    bits_size = res + 16;
                    bits = (uint8_t *) realloc(bits, bits_size * 2);
                }
                for (j = 0; j < res; j++) {
                    for (k = 0; k < 8; k++)
                        if (bits[j] & 1 << k) {
                            if (j * 8 + k == BTN_TOUCH || j * 8 + k == BTN_TOOL_FINGER)
                                continue;
                            ioctl(nowfd, UI_SET_KEYBIT, j * 8 + k);
                        }
                }
                free(bits);
            }
            ui_dev.absmin[ABS_MT_POSITION_X] = 0;
            ui_dev.absmax[ABS_MT_POSITION_X] = screenX;
            ui_dev.absmin[ABS_MT_POSITION_Y] = 0;
            ui_dev.absmax[ABS_MT_POSITION_Y] = screenY;
            ui_dev.absmin[ABS_X] = 0;
            ui_dev.absmax[ABS_X] = screenX;
            ui_dev.absmin[ABS_Y] = 0;
            ui_dev.absmax[ABS_Y] = screenY;
            ui_dev.absmin[ABS_MT_TRACKING_ID] = 0;
            ui_dev.absmax[ABS_MT_TRACKING_ID] = 65535;
            write(nowfd, &ui_dev, sizeof(ui_dev));

            if (ioctl(nowfd, UI_DEV_CREATE)) {
                close(nowfd);
                nowfd = 0;
            }
        }
    uinput_done:
        initialized = true;

        watchdogRun.store(true);
        heartbeat.store(NowMs());
        { pthread_t wd; pthread_create(&wd, nullptr, Watchdog, nullptr); }

        pthread_t t;
        for (int i = 0; i < devices.size(); i++) {
            devices[i].S2TX = (float) screenX / (float) devices[i].absX.maximum;
            devices[i].S2TY = (float) screenY / (float) devices[i].absY.maximum;
            pthread_create(&t, nullptr, TypeA, (void *) (long) i);
        }
        if (size.x > size.y) {
            std::swap(size.x, size.y);
        }
        if (otherTouch) {
            std::swap(size.x, size.y);
        }
        touch_scale.x = (float) screenX / size.x;
        touch_scale.y = (float) screenY / size.y;

        //system("chmod 000 -R /proc/bus/input/*");
        return true;
    }

    void Close() {
        if (initialized) {
            watchdogRun.store(false);
            blockEnabled = false;
            blockGrabbed.store(false);
            for (auto &device: devices) {
                ioctl(device.fd, EVIOCGRAB, UNGRAB);
                close(device.fd);
                device.fd = 0;
            }
            if (nowfd > 0) {
                ioctl(nowfd, UI_DEV_DESTROY);
                close(nowfd);
                nowfd = 0;
            }
            memset(input.event, 0, sizeof(input.event));
            initialized = false;
            devices.clear();
        }
    }

    void Down(float x, float y) {
        lock.lock();
        touchObj &touch = devices[0].Finger[9];
        touch.id = 19;
        touch.pos = My_Vector2(x, y) * touch_scale;
        touch.isDown = true;
        Upload();
        lock.unlock();
    }

    void Move(touchObj *touch, float x, float y) {
        lock.lock();
        touch->pos = My_Vector2(x, y) * touch_scale;
        Upload();
        lock.unlock();
    }

    void Move(float x, float y) {
        Down(x, y);
    }

    void Up() {
        lock.lock();
        touchObj &touch = devices[0].Finger[9];
        touch.isDown = false;
        Upload();
        lock.unlock();
    }

    void SetBlockRegion(float x, float y, float w, float h, bool enabled) {
        heartbeat.store(NowMs());
        lock.lock();
        blockX = x; blockY = y; blockW = w; blockH = h;
        blockEnabled = enabled && nowfd > 0;
        SetGrab(blockEnabled);
        lock.unlock();
    }

    void SetCallBack(const std::function<void(std::vector<Device> *)> &cb) {
        callback = cb;
    }

    My_Vector2 Touch2Screen(const My_Vector2 &coord) {
        float x = coord.x, y = coord.y;
        float xt = x / touch_scale.x;
        float yt = y / touch_scale.y;

        if (otherTouch) {
            switch (orientation) {
                case 1:
                    x = xt;
                    y = yt;
                    break;
                case 2:
                    y = yt;
                    x = screenSize.y - xt;
                    break;
                case 3:
                    x = screenSize.y - xt;
                    y = screenSize.x - yt;
                    break;
                default:
                    y = xt;
                    x = screenSize.y - yt;
                    break;
            }
        } else {
            switch (orientation) {
                case 1:
                    x = yt;
                    y = screenSize.y - xt;
                    break;
                case 2:
                    x = screenSize.y - xt;
                    y = screenSize.x - yt;
                    break;
                case 3:
                    y = xt;
                    x = screenSize.x - yt;
                    break;
                default:
                    x = xt;
                    y = yt;
                    break;
            }
        }
        return {x, y};
    }

    My_Vector2 GetScale() {
        return touch_scale;
    }

    void setOrientation(int o) {
        orientation = o;
    }

    void setOtherTouch(bool p_otherTouch) {
        otherTouch = p_otherTouch;
    }
}