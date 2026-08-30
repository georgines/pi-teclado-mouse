#include "hid_state.hpp"

KeyboardState g_keyboard_state;
MouseState g_mouse_state;

static bool is_modifier(uint8_t usage) { return usage >= 0xE0 && usage <= 0xE7; }

// Soma que gruda no extremo em vez de dar a volta. Os acumuladores têm largura
// fixa (delta i16, posição virtual i32, roda i8) e estouro de inteiro com sinal
// é comportamento indefinido em C++ — não é hipótese remota numa posição
// virtual que só cresce enquanto o modo relativo estiver ativo. Saturar o host
// lê como "acabou o curso"; dar a volta seria o ponteiro saltando para o lado
// oposto da tela.
static int64_t sat_add(int64_t acc, int64_t delta, int64_t lo, int64_t hi) {
    int64_t r = acc + delta;
    return r < lo ? lo : (r > hi ? hi : r);
}

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
    // Acumula: a fila drena até 32 comandos por volta do laço principal e só um
    // relatório HID sai no fim dela. Substituir o delta jogaria fora todo
    // movimento menos o último, e a posição virtual — que sempre somou todos —
    // passaria a mentir sobre onde o ponteiro está.
    rel_dx = static_cast<int16_t>(sat_add(rel_dx, dx, INT16_MIN, INT16_MAX));
    rel_dy = static_cast<int16_t>(sat_add(rel_dy, dy, INT16_MIN, INT16_MAX));
    virtual_x = static_cast<int32_t>(sat_add(virtual_x, dx, INT32_MIN, INT32_MAX));
    virtual_y = static_cast<int32_t>(sat_add(virtual_y, dy, INT32_MIN, INT32_MAX));
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
    // Mesma acumulação do movimento, e pela mesma razão: dois MOUSE_WHEEL na
    // mesma drenagem viram um relatório só.
    pending_wheel_v = static_cast<int8_t>(sat_add(pending_wheel_v, v, INT8_MIN, INT8_MAX));
    pending_wheel_h = static_cast<int8_t>(sat_add(pending_wheel_h, h, INT8_MIN, INT8_MAX));
}

void MouseState::consume_report() {
    rel_dx = 0;
    rel_dy = 0;
    pending_wheel_v = 0;
    pending_wheel_h = 0;
}

void MouseState::release_all_buttons() {
    buttons = 0;
}

void MouseState::set_mode(MouseMode m) {
    mode = m;
    buttons = 0;
    consume_report(); // nada da carga do modo anterior atravessa a troca
    if (m == MouseMode::Relative) {
        virtual_x = 0;
        virtual_y = 0;
    }
}
