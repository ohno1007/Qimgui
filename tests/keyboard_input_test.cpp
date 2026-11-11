// Native evdev translation without opening input devices or creating a window.
#include "../jni/src/core/keyboard_input.cpp"
#include <cassert>

int main() {
    using namespace aimgui::kbd_input;
    uint8_t bits[(KEY_MAX / 8) + 1] = {};
    auto set = [&](int code) { bits[code / 8] |= 1u << (code % 8); };
    assert(!LooksLikeKeyboardKeys(bits));
    set(KEY_VOLUMEUP);
    assert(LooksLikeKeyboardKeys(bits));
    std::memset(bits, 0, sizeof(bits));
    for (const auto& mapping : kMap)
        if (mapping.ascii >= 'a' && mapping.ascii <= 'z') set(mapping.linux_code);
    assert(LooksLikeKeyboardKeys(bits));

    Modifiers mods;
    HandleKeyEvent(KEY_LEFTSHIFT, 1, mods);
    HandleKeyEvent(KEY_RIGHTSHIFT, 1, mods);
    HandleKeyEvent(KEY_LEFTSHIFT, 0, mods);
    assert(g_queue[g_queue.size() - 4].k.key == ImGuiMod_Shift);
    assert(g_queue[g_queue.size() - 4].k.down);
    HandleKeyEvent(KEY_A, 1, mods);
    assert(g_queue.back().kind == PendingEvent::Char && g_queue.back().ch == 'A');
    HandleKeyEvent(KEY_RIGHTSHIFT, 0, mods);
    assert(!g_queue[g_queue.size() - 4].k.down);
    HandleKeyEvent(KEY_A, 2, mods);
    assert(g_queue.back().kind == PendingEvent::Char && g_queue.back().ch == 'a');
    HandleKeyEvent(KEY_LEFTCTRL, 1, mods);
    const size_t before = g_queue.size();
    HandleKeyEvent(KEY_A, 1, mods);
    assert(g_queue.size() == before + 1); // Ctrl+A must not insert a literal 'a'.
    assert(g_queue.back().kind == PendingEvent::Key && g_queue.back().k.key == ImGuiKey_A);
    HandleKeyEvent(KEY_LEFTCTRL, 0, mods);
    HandleKeyEvent(KEY_VOLUMEDOWN, 1, mods);
    assert(ConsumeVolumePresses() == 1);
    assert(ConsumeVolumePresses() == 0);
    std::puts("PASS: keyboard discovery, independent modifiers, text/repeat, shortcuts and volume events");
}
