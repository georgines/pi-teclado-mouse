#include "mode_button.hpp"
#include "hid_usb.hpp"
#include "uart_io.hpp"
#include "uart_protocol.hpp"
#include "hardware/gpio.h"

namespace mode_button {
namespace {

constexpr uint BUTTON_PIN = 6;
constexpr uint32_t DEBOUNCE_MS = 30;

bool raw_state = true;       // pull-up idle level: true = released
bool confirmed_state = true;
uint32_t last_change_ms = 0;

} // namespace

void init() {
    gpio_init(BUTTON_PIN);
    gpio_set_dir(BUTTON_PIN, GPIO_IN);
    gpio_pull_up(BUTTON_PIN);
    raw_state = confirmed_state = gpio_get(BUTTON_PIN);
}

void poll(uint32_t now_ms) {
    bool level = gpio_get(BUTTON_PIN);
    if (level != raw_state) {
        raw_state = level;
        last_change_ms = now_ms;
    }
    if (raw_state != confirmed_state && (now_ms - last_change_ms) >= DEBOUNCE_MS) {
        confirmed_state = raw_state;
        if (!confirmed_state) { // active low: false == pressed
            MouseMode next = (g_mouse_state.mode == MouseMode::Relative) ? MouseMode::Absolute
                                                                          : MouseMode::Relative;
            mode_set(next);
        }
    }
}

void mode_set(MouseMode m) {
    hid_usb::send_mouse_neutral_report(); // neutral on whichever report is active *before* the switch
    g_mouse_state.set_mode(m);            // flips mode, clears buttons/deltas, zeroes virtual pos if Relative

    uint8_t payload[2] = {static_cast<uint8_t>(EventCode::MouseModeChanged), static_cast<uint8_t>(m)};
    uart_io::emit_event(payload, 2);
    uart_io::republish_snapshot();
}

} // namespace mode_button
