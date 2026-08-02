#include "hid_state.hpp"

KeyboardState g_keyboard_state;
MouseState g_mouse_state;

static bool is_modifier(uint8_t usage) { return usage >= 0xE0 && usage <= 0xE7; }

bool KeyboardState::key_down(uint8_t usage) {
    if (is_modifier(usage)) {
        modifiers = static_cast<uint8_t>(modifiers | (1u << (usage - 0xE0)));
        return true;
    }
    int free_slot = -1;
    for (int i = 0; i < 6; ++i) {
        if (keys[i] == usage) return true; // already down, idempotent
        if (free_slot < 0 && keys[i] == 0) free_slot = i;
    }
    if (free_slot < 0) return false; // 7th distinct key: reject, state unchanged
    keys[free_slot] = usage;
    return true;
}

void KeyboardState::key_up(uint8_t usage) {
    if (is_modifier(usage)) {
        modifiers = static_cast<uint8_t>(modifiers & ~(1u << (usage - 0xE0)));
        return;
    }
    for (auto& k : keys) {
        if (k == usage) { k = 0; return; }
    }
}

void KeyboardState::release_all() {
    modifiers = 0;
    for (auto& k : keys) k = 0;
}

bool MouseState::move_relative(int16_t dx, int16_t dy) {
    rel_dx = dx;
    rel_dy = dy;
    virtual_x += dx;
    virtual_y += dy;
    return true;
}

bool MouseState::move_absolute(uint16_t x, uint16_t y) {
    if (x > 32767 || y > 32767) return false;
    abs_x = x;
    abs_y = y;
    return true;
}

bool MouseState::set_buttons(uint8_t bitmap) {
    if (bitmap & 0xE0) return false; // only 5 buttons exist
    buttons = bitmap;
    return true;
}

void MouseState::set_wheel(int8_t v, int8_t h) {
    last_wheel_v = v;
    last_wheel_h = h;
}

void MouseState::release_all_buttons() {
    buttons = 0;
}

void MouseState::set_mode(MouseMode m) {
    mode = m;
    buttons = 0;
    rel_dx = 0;
    rel_dy = 0;
    if (m == MouseMode::Relative) {
        virtual_x = 0;
        virtual_y = 0;
    }
}
