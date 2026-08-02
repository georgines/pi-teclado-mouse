/*
 * The MIT License (MIT)
 *
 * Copyright (c) 2019 Ha Thach (tinyusb.org)
 *
 * Permission is hereby granted, free of charge, to any person obtaining a copy
 * of this software and associated documentation files (the "Software"), to deal
 * in the Software without restriction, including without limitation the rights
 * to use, copy, modify, merge, publish, distribute, sublicense, and/or sell
 * copies of the Software, and to permit persons to whom the Software is
 * furnished to do so, subject to the following conditions:
 *
 * The above copyright notice and this permission notice shall be included in
 * all copies or substantial portions of the Software.
 *
 * THE SOFTWARE IS PROVIDED "AS IS", WITHOUT WARRANTY OF ANY KIND, EXPRESS OR
 * IMPLIED, INCLUDING BUT NOT LIMITED TO THE WARRANTIES OF MERCHANTABILITY,
 * FITNESS FOR A PARTICULAR PURPOSE AND NONINFRINGEMENT. IN NO EVENT SHALL THE
 * AUTHORS OR COPYRIGHT HOLDERS BE LIABLE FOR ANY CLAIM, DAMAGES OR OTHER
 * LIABILITY, WHETHER IN AN ACTION OF CONTRACT, TORT OR OTHERWISE, ARISING FROM,
 * OUT OF OR IN CONNECTION WITH THE SOFTWARE OR THE USE OR OTHER DEALINGS IN
 * THE SOFTWARE.
 *
 */

#include "bsp/board_api.h"
#include "tusb.h"
#include "pico/multicore.h"

#include "src/shared_state.hpp"
#include "src/uart_io.hpp"
#include "src/mode_button.hpp"
#include "src/hid_usb.hpp"
#include "src/oled_display.hpp"

int main() {
    board_init();
    shared_state_init();

    const tusb_rhport_init_t rh_init = {
        .role = TUSB_ROLE_DEVICE,
        .speed = TUSB_SPEED_FULL, // RP2040/RP2350 USB PHY is full-speed only
    };
    TU_ASSERT(tud_rhport_init(BOARD_TUD_RHPORT, &rh_init), -1);
    board_init_after_tusb();

    uart_io::init();
    mode_button::init();
    multicore_launch_core1(oled_display::core1_main);

    while (true) {
        tud_task();
        uint32_t now = board_millis();
        uart_io::poll(now);
        mode_button::poll(now);
        hid_usb::flush_pending();
    }
}
