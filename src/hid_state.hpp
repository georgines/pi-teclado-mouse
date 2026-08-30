#pragma once
#include <cstdint>
#include <cstddef>

// Pure keyboard/mouse state machine — no Pico SDK, no TinyUSB. Hardware glue
// (uart_io, hid_usb, mode_button) reads/writes this and turns it into HID
// reports or UART payloads.

struct KeyboardState {
    uint8_t modifiers = 0;          // bit i (0..7) = usage (0xE0+i)
    uint8_t keys[6] = {0, 0, 0, 0, 0, 0}; // 6KRO non-modifier usages, 0 = empty slot

    // Modifier usages (0xE0-0xE7) go straight into `modifiers`; everything
    // else competes for the 6 key slots. Idempotent: pressing an already-down
    // key is a no-op success. Returns false (state unchanged) only when a 7th
    // distinct non-modifier key would be needed.
    bool key_down(uint8_t usage);
    void key_up(uint8_t usage);
    void release_all();
};

enum class MouseMode : uint8_t { Relative = 0, Absolute = 1 };

struct MouseState {
    MouseMode mode = MouseMode::Relative;
    uint8_t buttons = 0;            // bits 0..4 = 5 buttons
    uint16_t abs_x = 0, abs_y = 0;  // 0..32767, valid in Absolute mode
    // Carga de UM relatório HID: movimento e roda são eventos, não níveis. Se
    // acumulam entre comandos e são zerados por consume_report() assim que o
    // relatório sai — senão o relatório seguinte (o de um clique, digamos)
    // repetiria o movimento e a rolagem anteriores.
    int16_t rel_dx = 0, rel_dy = 0;
    int8_t pending_wheel_v = 0, pending_wheel_h = 0;

    // Registro de estado, não carga de relatório: sobrevive ao envio porque é o
    // que o STATUS e a tela mostram.
    int32_t virtual_x = 0, virtual_y = 0; // accumulated position while in Relative mode
    int8_t last_wheel_v = 0, last_wheel_h = 0;

    bool move_relative(int16_t dx, int16_t dy);   // only meaningful when mode==Relative
    bool move_absolute(uint16_t x, uint16_t y);   // false (rejected) if x or y > 32767
    bool set_buttons(uint8_t bitmap);             // false if any bit above bit 4 is set
    void set_wheel(int8_t v, int8_t h);
    void release_all_buttons();
    void set_mode(MouseMode m);                   // neutral: releases buttons, zeroes deltas,
                                                    // zeroes virtual pos when (re-)entering Relative

    // Chamada por quem enviou o relatório HID, e só por ele.
    void consume_report();
};

// Single physical device: one keyboard, one mouse. uart_io writes to these
// from validated commands, hid_usb reads them to build reports, mode_button
// calls MouseState::set_mode() through g_mouse_state.
extern KeyboardState g_keyboard_state;
extern MouseState g_mouse_state;
