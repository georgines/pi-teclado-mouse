#include "hid_usb.hpp"
#include "hid_state.hpp"
#include "usb_descriptors.h"
#include "uart_io.hpp"
#include "tusb.h"

namespace hid_usb {
namespace {

bool keyboard_dirty = false;
bool mouse_rel_dirty = false;
bool mouse_abs_dirty = false;

} // namespace

void mark_keyboard_dirty() { keyboard_dirty = true; }
void mark_mouse_rel_dirty() { mouse_rel_dirty = true; }
void mark_mouse_abs_dirty() { mouse_abs_dirty = true; }

void flush_pending() {
    if (keyboard_dirty && tud_hid_n_ready(HID_INSTANCE_KEYBOARD)) {
        tud_hid_n_keyboard_report(HID_INSTANCE_KEYBOARD, 0, g_keyboard_state.modifiers, g_keyboard_state.keys);
        keyboard_dirty = false;
    }

    if (g_mouse_state.mode == MouseMode::Relative) {
        if (mouse_rel_dirty && tud_hid_n_ready(HID_INSTANCE_MOUSE)) {
            mouse_rel16_report_t r{g_mouse_state.buttons, g_mouse_state.rel_dx, g_mouse_state.rel_dy,
                                    g_mouse_state.pending_wheel_v, g_mouse_state.pending_wheel_h};
            tud_hid_n_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_REL, &r, sizeof(r));
            g_mouse_state.consume_report();
            mouse_rel_dirty = false;
        }
    } else {
        if (mouse_abs_dirty && tud_hid_n_ready(HID_INSTANCE_MOUSE)) {
            tud_hid_n_abs_mouse_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_ABS, g_mouse_state.buttons,
                                        static_cast<int16_t>(g_mouse_state.abs_x),
                                        static_cast<int16_t>(g_mouse_state.abs_y),
                                        g_mouse_state.pending_wheel_v, g_mouse_state.pending_wheel_h);
            g_mouse_state.consume_report();
            mouse_abs_dirty = false;
        }
    }
}

void send_mouse_neutral_report() {
    if (!tud_hid_n_ready(HID_INSTANCE_MOUSE)) return;
    if (g_mouse_state.mode == MouseMode::Relative) {
        // Rest state for relative deltas is genuinely all-zero: no movement, no buttons.
        mouse_rel16_report_t r{0, 0, 0, 0, 0};
        tud_hid_n_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_REL, &r, sizeof(r));
    } else {
        // Absolute "neutral" keeps the cursor where it is (zeroing x/y would snap
        // it to the top-left corner) — only buttons/wheel/pan are released.
        tud_hid_n_abs_mouse_report(HID_INSTANCE_MOUSE, REPORT_ID_MOUSE_ABS, 0,
                                    static_cast<int16_t>(g_mouse_state.abs_x),
                                    static_cast<int16_t>(g_mouse_state.abs_y), 0, 0);
    }
    g_mouse_state.consume_report();
    mouse_rel_dirty = false;
    mouse_abs_dirty = false;
}

} // namespace hid_usb

extern "C" {

void tud_mount_cb(void) {
    uart_io::notify_usb_state(UsbLinkState::Mounted);
}

void tud_umount_cb(void) {
    uart_io::notify_usb_state(UsbLinkState::Disconnected);
}

void tud_suspend_cb(bool remote_wakeup_en) {
    (void) remote_wakeup_en;
    uart_io::notify_usb_state(UsbLinkState::Suspended);
}

void tud_resume_cb(void) {
    uart_io::notify_usb_state(tud_mounted() ? UsbLinkState::Mounted : UsbLinkState::Disconnected);
}

void tud_hid_report_complete_cb(uint8_t instance, uint8_t const* report, uint16_t len) {
    (void) instance;
    (void) report;
    (void) len;
    // Keyboard and mouse are separate interfaces/endpoints; hid_usb::flush_pending()
    // retries whichever report is still dirty on the next main-loop iteration —
    // no per-instance chaining needed here.
}

uint16_t tud_hid_get_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                                uint8_t* buffer, uint16_t reqlen) {
    (void) instance;
    (void) report_id;
    (void) report_type;
    (void) buffer;
    (void) reqlen;
    return 0; // no GET_REPORT support required by the spec
}

void tud_hid_set_report_cb(uint8_t instance, uint8_t report_id, hid_report_type_t report_type,
                            uint8_t const* buffer, uint16_t bufsize) {
    (void) report_id;
    (void) buffer;
    // Only the keyboard interface has an OUTPUT report (LED state); accepted
    // and otherwise ignored — nothing in the spec surfaces LED state anywhere.
    if (instance != HID_INSTANCE_KEYBOARD || report_type != HID_REPORT_TYPE_OUTPUT) return;
    if (bufsize < 1) return;
}

} // extern "C"
