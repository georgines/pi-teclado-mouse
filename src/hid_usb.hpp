#pragma once

// TinyUSB report dispatch. Full implementation in hid_usb.cpp (Task 7); the
// interface is declared here first since uart_io and mode_button both need
// to mark reports dirty / force a neutral report before that task lands.
namespace hid_usb {

void mark_keyboard_dirty();
void mark_mouse_rel_dirty();
void mark_mouse_abs_dirty();
void flush_pending();          // call every main-loop iteration
void send_mouse_neutral_report(); // immediate: zero buttons/deltas on the currently-active mouse report

} // namespace hid_usb
