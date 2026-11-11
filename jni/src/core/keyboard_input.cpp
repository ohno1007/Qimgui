#include "keyboard_input.h"

#include "imgui.h"

#include <atomic>
#include <cerrno>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <mutex>
#include <poll.h>
#include <thread>
#include <unistd.h>
#include <vector>

namespace aimgui::kbd_input {

namespace {

struct KeyMapping {
    int      linux_code;
    ImGuiKey imgui_key;
    char     ascii;
    char     ascii_shift;
};

constexpr KeyMapping kMap[] = {
    {KEY_A, ImGuiKey_A, 'a', 'A'}, {KEY_B, ImGuiKey_B, 'b', 'B'},
    {KEY_C, ImGuiKey_C, 'c', 'C'}, {KEY_D, ImGuiKey_D, 'd', 'D'},
    {KEY_E, ImGuiKey_E, 'e', 'E'}, {KEY_F, ImGuiKey_F, 'f', 'F'},
    {KEY_G, ImGuiKey_G, 'g', 'G'}, {KEY_H, ImGuiKey_H, 'h', 'H'},
    {KEY_I, ImGuiKey_I, 'i', 'I'}, {KEY_J, ImGuiKey_J, 'j', 'J'},
    {KEY_K, ImGuiKey_K, 'k', 'K'}, {KEY_L, ImGuiKey_L, 'l', 'L'},
    {KEY_M, ImGuiKey_M, 'm', 'M'}, {KEY_N, ImGuiKey_N, 'n', 'N'},
    {KEY_O, ImGuiKey_O, 'o', 'O'}, {KEY_P, ImGuiKey_P, 'p', 'P'},
    {KEY_Q, ImGuiKey_Q, 'q', 'Q'}, {KEY_R, ImGuiKey_R, 'r', 'R'},
    {KEY_S, ImGuiKey_S, 's', 'S'}, {KEY_T, ImGuiKey_T, 't', 'T'},
    {KEY_U, ImGuiKey_U, 'u', 'U'}, {KEY_V, ImGuiKey_V, 'v', 'V'},
    {KEY_W, ImGuiKey_W, 'w', 'W'}, {KEY_X, ImGuiKey_X, 'x', 'X'},
    {KEY_Y, ImGuiKey_Y, 'y', 'Y'}, {KEY_Z, ImGuiKey_Z, 'z', 'Z'},

    {KEY_0, ImGuiKey_0, '0', ')'}, {KEY_1, ImGuiKey_1, '1', '!'},
    {KEY_2, ImGuiKey_2, '2', '@'}, {KEY_3, ImGuiKey_3, '3', '#'},
    {KEY_4, ImGuiKey_4, '4', '$'}, {KEY_5, ImGuiKey_5, '5', '%'},
    {KEY_6, ImGuiKey_6, '6', '^'}, {KEY_7, ImGuiKey_7, '7', '&'},
    {KEY_8, ImGuiKey_8, '8', '*'}, {KEY_9, ImGuiKey_9, '9', '('},

    {KEY_SPACE,     ImGuiKey_Space,        ' ',  ' '},
    {KEY_ENTER,     ImGuiKey_Enter,        0,    0  },
    {KEY_BACKSPACE, ImGuiKey_Backspace,    0,    0  },
    {KEY_TAB,       ImGuiKey_Tab,          '\t', '\t'},
    {KEY_ESC,       ImGuiKey_Escape,       0,    0  },
    {KEY_LEFT,      ImGuiKey_LeftArrow,    0,    0  },
    {KEY_RIGHT,     ImGuiKey_RightArrow,   0,    0  },
    {KEY_UP,        ImGuiKey_UpArrow,      0,    0  },
    {KEY_DOWN,      ImGuiKey_DownArrow,    0,    0  },
    {KEY_HOME,      ImGuiKey_Home,         0,    0  },
    {KEY_END,       ImGuiKey_End,          0,    0  },
    {KEY_DELETE,    ImGuiKey_Delete,       0,    0  },
    {KEY_INSERT,    ImGuiKey_Insert,       0,    0  },

    {KEY_MINUS,      ImGuiKey_Minus,         '-',  '_'},
    {KEY_EQUAL,      ImGuiKey_Equal,         '=',  '+'},
    {KEY_LEFTBRACE,  ImGuiKey_LeftBracket,   '[',  '{'},
    {KEY_RIGHTBRACE, ImGuiKey_RightBracket,  ']',  '}'},
    {KEY_BACKSLASH,  ImGuiKey_Backslash,     '\\', '|'},
    {KEY_SEMICOLON,  ImGuiKey_Semicolon,     ';',  ':'},
    {KEY_APOSTROPHE, ImGuiKey_Apostrophe,    '\'', '"'},
    {KEY_GRAVE,      ImGuiKey_GraveAccent,   '`',  '~'},
    {KEY_COMMA,      ImGuiKey_Comma,         ',',  '<'},
    {KEY_DOT,        ImGuiKey_Period,        '.',  '>'},
    {KEY_SLASH,      ImGuiKey_Slash,         '/',  '?'},
};

const KeyMapping* Lookup(int linux_code) {
    for (const auto& m : kMap)
        if (m.linux_code == linux_code) return &m;
    return nullptr;
}

struct PendingEvent {
    enum Kind { Char, Key } kind;
    union {
        unsigned int ch;
        struct { ImGuiKey key; bool down; } k;
    };
};

std::mutex                g_mtx;
std::vector<PendingEvent> g_queue;

std::atomic<bool>         g_running{false};
std::vector<int>          g_fds;
std::vector<std::thread>  g_threads;
std::atomic<int>          g_volume_presses{0};

bool LooksLikeKeyboardKeys(const uint8_t* bits) {
    auto has = [&](int k) -> bool {
        return (bits[k / 8] & (1u << (k % 8))) != 0;
    };
    // Linux KEY_* values follow scan-code order, not alphabetical order.
    int letters = 0;
    for (const auto& mapping : kMap)
        if (mapping.ascii >= 'a' && mapping.ascii <= 'z' && has(mapping.linux_code))
            ++letters;
    return letters >= 20 || has(KEY_VOLUMEUP) || has(KEY_VOLUMEDOWN);
}

bool LooksLikeKeyboard(int fd) {
    uint8_t bits[(KEY_MAX / 8) + 1] = {};
    return ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(bits)), bits) >= 0 &&
           LooksLikeKeyboardKeys(bits);
}

struct Modifiers {
    bool left_shift = false, right_shift = false;
    bool left_ctrl = false, right_ctrl = false;
    bool left_alt = false, right_alt = false;
    bool left_super = false, right_super = false;
};

void QueueKey(ImGuiKey key, bool down) {
    PendingEvent event{};
    event.kind = PendingEvent::Key;
    event.k.key = key;
    event.k.down = down;
    g_queue.push_back(event);
}

// Called while holding g_mtx; ImGui receives these events only in Flush().
void HandleKeyEvent(unsigned code, int value, Modifiers& mods) {
    const bool down = value != 0;
    const bool press = value == 1;
    if (press && (code == KEY_VOLUMEUP || code == KEY_VOLUMEDOWN)) {
        g_volume_presses.fetch_add(1, std::memory_order_relaxed);
        return;
    }

    bool* side = nullptr;
    ImGuiKey physical = ImGuiKey_None;
    switch (code) {
        case KEY_LEFTSHIFT:  side = &mods.left_shift;  physical = ImGuiKey_LeftShift;  break;
        case KEY_RIGHTSHIFT: side = &mods.right_shift; physical = ImGuiKey_RightShift; break;
        case KEY_LEFTCTRL:   side = &mods.left_ctrl;   physical = ImGuiKey_LeftCtrl;   break;
        case KEY_RIGHTCTRL:  side = &mods.right_ctrl;  physical = ImGuiKey_RightCtrl;  break;
        case KEY_LEFTALT:    side = &mods.left_alt;    physical = ImGuiKey_LeftAlt;    break;
        case KEY_RIGHTALT:   side = &mods.right_alt;   physical = ImGuiKey_RightAlt;   break;
        case KEY_LEFTMETA:   side = &mods.left_super;  physical = ImGuiKey_LeftSuper;  break;
        case KEY_RIGHTMETA:  side = &mods.right_super; physical = ImGuiKey_RightSuper; break;
    }
    if (side) {
        *side = down;
        QueueKey(physical, down);
        QueueKey(ImGuiMod_Shift, mods.left_shift || mods.right_shift);
        QueueKey(ImGuiMod_Ctrl, mods.left_ctrl || mods.right_ctrl);
        QueueKey(ImGuiMod_Alt, mods.left_alt || mods.right_alt);
        QueueKey(ImGuiMod_Super, mods.left_super || mods.right_super);
        return;
    }

    const KeyMapping* mapping = Lookup(code);
    if (!mapping) return;
    QueueKey(mapping->imgui_key, down);
    const bool shift = mods.left_shift || mods.right_shift;
    const char ch = shift ? mapping->ascii_shift : mapping->ascii;
    const bool shortcut = mods.left_ctrl || mods.right_ctrl || mods.left_alt ||
                          mods.right_alt || mods.left_super || mods.right_super;
    if ((press || value == 2) && ch && !shortcut) {
        PendingEvent event{};
        event.kind = PendingEvent::Char;
        event.ch = static_cast<unsigned char>(ch);
        g_queue.push_back(event);
    }
}

void ReaderLoop(int fd) {
    Modifiers mods;
    input_event ev[16];
    while (g_running.load(std::memory_order_relaxed)) {

        struct pollfd pfd{};
        pfd.fd     = fd;
        pfd.events = POLLIN;
        const int pr = ::poll(&pfd, 1, 200);
        if (pr <= 0) {
            if (pr < 0 && errno != EINTR) break;
            continue;
        }

        ssize_t n = ::read(fd, ev, sizeof(ev));
        if (n <= 0) {
            if (n < 0 && (errno == EAGAIN || errno == EINTR)) continue;
            break;
        }
        size_t count = (size_t)n / sizeof(input_event);

        std::lock_guard<std::mutex> lk(g_mtx);
        for (size_t i = 0; i < count; ++i) {
            if (ev[i].type != EV_KEY) continue;
            HandleKeyEvent(ev[i].code, ev[i].value, mods);
        }
    }
}

}

void Init() {
    if (g_running.exchange(true)) return;

    DIR* dir = opendir("/dev/input");
    if (!dir) { g_running.store(false); return; }
    while (dirent* e = readdir(dir)) {
        if (std::strncmp(e->d_name, "event", 5) != 0) continue;
        char path[64];
        std::snprintf(path, sizeof(path), "/dev/input/%s", e->d_name);
        int fd = ::open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0) continue;
        if (!LooksLikeKeyboard(fd)) { ::close(fd); continue; }
        g_fds.push_back(fd);
        g_threads.emplace_back(ReaderLoop, fd);
    }
    closedir(dir);
}

void Shutdown() {
    g_running.store(false);
    // poll() has a 200 ms timeout. Join before closing so readers never race
    // with a reused file descriptor or outlive the input state.
    for (auto& t : g_threads) if (t.joinable()) t.join();
    for (int fd : g_fds) ::close(fd);
    g_threads.clear();
    g_fds.clear();
    std::lock_guard<std::mutex> lk(g_mtx);
    g_queue.clear();
}

void Flush() {
    ImGuiIO& io = ImGui::GetIO();
    std::lock_guard<std::mutex> lk(g_mtx);
    for (const auto& ev : g_queue) {
        if (ev.kind == PendingEvent::Key) io.AddKeyEvent(ev.k.key, ev.k.down);
        else                              io.AddInputCharacter(ev.ch);
    }
    g_queue.clear();
}

int ConsumeVolumePresses() {
    return g_volume_presses.exchange(0, std::memory_order_relaxed);
}

}
