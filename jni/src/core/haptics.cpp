#include "haptics.h"

#include <android/log.h>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <sys/ioctl.h>
#include <unistd.h>

#define LOGI(...) __android_log_print(ANDROID_LOG_INFO, "AImGui", __VA_ARGS__)

namespace aimgui {
namespace {

constexpr const char* kLedActivate = "/sys/class/leds/vibrator/activate";
constexpr const char* kLedDuration = "/sys/class/leds/vibrator/duration";
constexpr const char* kTimedOutput = "/sys/class/timed_output/vibrator/enable";

bool BitSet(const unsigned long* bits, int bit) {
    constexpr int kPerLong = 8 * sizeof(unsigned long);
    return (bits[bit / kPerLong] >> (bit % kPerLong)) & 1ul;
}

// First /dev/input node that advertises FF_RUMBLE *and* is named like a
// vibrator.
//
// The name test is not fussiness. This walks every input node on the device,
// which includes the touchscreen, and opening those read-write with a blocking
// open is a good way to sit down next to a driver that does not expect it. So:
// O_NONBLOCK, so the open cannot be the thing that hangs; and a name check, so
// a panel that advertises feedback for its own haptics is not the one we grab
// and hold open for the life of the process.
int OpenRumbleDevice() {
    DIR* d = opendir("/dev/input");
    if (!d) return -1;
    int found = -1;
    char path[64];
    char name[128];
    while (dirent* e = readdir(d)) {
        if (std::strncmp(e->d_name, "event", 5) != 0) continue;
        std::snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        const int fd = open(path, O_RDWR | O_CLOEXEC | O_NONBLOCK);
        if (fd < 0) continue;

        name[0] = '\0';
        if (ioctl(fd, EVIOCGNAME(sizeof(name)), name) < 0) { close(fd); continue; }
        static const char* const kHints[] = { "vibra", "Vibra", "VIBRA",
                                              "haptic", "Haptic", "HAPTIC" };
        bool named = false;
        for (const char* hint : kHints) {
            if (std::strstr(name, hint)) { named = true; break; }
        }
        if (!named) { close(fd); continue; }

        unsigned long ff[(FF_MAX / (8 * sizeof(unsigned long))) + 1] = {};
        if (ioctl(fd, EVIOCGBIT(EV_FF, sizeof(ff)), ff) >= 0 &&
            BitSet(ff, FF_RUMBLE)) {
            LOGI("[haptics] using %s (%s)", path, name);
            found = fd;
            break;
        }
        close(fd);
    }
    closedir(d);
    return found;
}

void WriteInt(int fd, int v) {
    if (fd < 0) return;
    char buf[24];
    const int n = std::snprintf(buf, sizeof(buf), "%d\n", v);
    // Nothing useful to do if it fails, and this is feedback: dropping a buzz
    // must never disturb the frame it happened on.
    (void)!write(fd, buf, (size_t)n);
}

} // namespace

bool Haptics::Init() {
    if ((m_Fd = OpenRumbleDevice()) >= 0) {
        m_Mode = Mode::Evdev;
        LOGI("[haptics] evdev force feedback");
        return true;
    }

    const int act = open(kLedActivate, O_WRONLY | O_CLOEXEC);
    if (act >= 0) {
        m_FdEnable = act;
        m_FdDur    = open(kLedDuration, O_WRONLY | O_CLOEXEC);
        m_Mode     = Mode::SysfsLed;
        LOGI("[haptics] sysfs led-class");
        return true;
    }

    const int timed = open(kTimedOutput, O_WRONLY | O_CLOEXEC);
    if (timed >= 0) {
        m_Fd   = timed;
        m_Mode = Mode::SysfsTimed;
        LOGI("[haptics] sysfs timed_output");
        return true;
    }

    LOGI("[haptics] no vibrator reachable; feedback disabled");
    return false;
}

void Haptics::Shutdown() {
    if (m_Mode == Mode::Evdev && m_Fd >= 0 && m_EffectId >= 0)
        ioctl(m_Fd, EVIOCRMFF, m_EffectId);
    if (m_Fd >= 0)       close(m_Fd);
    if (m_FdEnable >= 0) close(m_FdEnable);
    if (m_FdDur >= 0)    close(m_FdDur);
    m_Fd = m_FdEnable = m_FdDur = -1;
    m_EffectId = -1;
    m_Mode = Mode::None;
}

void Haptics::Pulse(int ms, float strength) {
    if (m_Mode == Mode::None || ms <= 0) return;
    if (strength < 0.0f) strength = 0.0f;
    if (strength > 1.0f) strength = 1.0f;

    switch (m_Mode) {
    case Mode::Evdev: {
        // The effect is re-uploaded under the same id rather than a new one
        // each time: ids are a small fixed pool in the driver, and leaking one
        // per tap runs it dry within a minute of ordinary use.
        ff_effect e{};
        e.type = FF_RUMBLE;
        e.id   = (int16_t)m_EffectId;
        e.u.rumble.strong_magnitude = (uint16_t)(strength * 65535.0f);
        e.u.rumble.weak_magnitude   = (uint16_t)(strength * 30000.0f);
        e.replay.length = (uint16_t)ms;
        e.replay.delay  = 0;
        if (ioctl(m_Fd, EVIOCSFF, &e) < 0) return;
        m_EffectId = e.id;

        input_event play{};
        play.type  = EV_FF;
        play.code  = (uint16_t)e.id;
        play.value = 1;
        (void)!write(m_Fd, &play, sizeof(play));
        break;
    }
    case Mode::SysfsLed:
        WriteInt(m_FdDur, ms);
        WriteInt(m_FdEnable, 1);
        break;
    case Mode::SysfsTimed:
        WriteInt(m_Fd, ms);
        break;
    case Mode::None:
        break;
    }
}

namespace haptic {
namespace {
Haptics*    g_dev = nullptr;
const bool* g_on  = nullptr;

void Fire(int ms, float strength) {
    if (!g_dev || (g_on && !*g_on)) return;
    g_dev->Pulse(ms, strength);
}
} // namespace

void Install(Haptics* device, const bool* enabled) { g_dev = device; g_on = enabled; }

// Deliberately short and unequal. A tap the same weight as a stage change tells
// the hand nothing; what makes feedback legible is that the events feel unlike
// one another, not that any of them is strong.
void Tap()   { Fire(9,  0.42f); }
void Step()  { Fire(18, 0.70f); }
void Snap()  { Fire(12, 0.55f); }
void Heavy() { Fire(34, 1.00f); }

} // namespace haptic

} // namespace aimgui
