#pragma once
#include <cstdint>
#include "hid_state.hpp"

// Cross-core snapshot: core0 (USB/UART/input) publishes, core1 (OLED) consumes.
// Also reused verbatim as the UART GET_STATUS/STATUS payload — one struct,
// two consumers, no separate serialization format to keep in sync.
enum class UsbLinkState : uint8_t { Disconnected = 0, Mounted = 1, Suspended = 2 };

struct __attribute__((packed)) DeviceSnapshot {
    UsbLinkState usb_state = UsbLinkState::Disconnected;
    MouseMode mouse_mode = MouseMode::Relative;
    uint8_t modifiers = 0;
    uint8_t keys[6] = {0, 0, 0, 0, 0, 0};
    uint8_t mouse_buttons = 0;
    uint16_t abs_x = 0, abs_y = 0;
    int32_t virtual_x = 0, virtual_y = 0;
    int8_t last_wheel_v = 0, last_wheel_h = 0;
    uint16_t uart_error_count = 0;
    bool oled_ok = true;
};

void shared_state_init();
void shared_state_publish(const DeviceSnapshot& s);
DeviceSnapshot shared_state_consume();

// Single-bool flag, set by core1 (oled_display) after probing the SSD1306,
// read by core0 (uart_io) to fold into DeviceSnapshot and to detect edges for
// the DisplayFailure/DisplayRecovered UART event. A lone aligned bool needs
// no critical section to stay coherent between cores.
void shared_state_set_oled_ok(bool ok);
bool shared_state_get_oled_ok();
