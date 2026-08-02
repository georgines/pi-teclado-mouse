#pragma once
#include <cstdint>
#include "shared_state.hpp"

// UART0 framing/dispatch: IRQ-fed 512-byte circular RX/TX buffers, a 32-entry
// command queue drained each poll(), and the ACK/NACK/EVENT/STATUS replies.
namespace uart_io {

void init();          // uart0 921600-8N1 on GP16(TX)/GP17(RX), enables IRQs
void poll(uint32_t now_ms);

// Called by hid_usb's tud_*_cb on every USB link transition.
void notify_usb_state(UsbLinkState state);

// Emits an EVENT_STATUS frame (seq=0). Used directly by mode_button for the
// button-triggered mode-change event.
void emit_event(const uint8_t* payload, uint16_t len);

// Rebuilds a DeviceSnapshot from the current keyboard/mouse/usb/uart state
// and publishes it for core1. Called internally, and by mode_button after a
// button-triggered mode switch.
void republish_snapshot();

} // namespace uart_io
