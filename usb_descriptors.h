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

#ifndef USB_DESCRIPTORS_H_
#define USB_DESCRIPTORS_H_

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

// Two HID interfaces: keyboard (boot protocol, single unnumbered report) and
// mouse (two report IDs multiplexed: relative and absolute).
enum { ITF_NUM_KEYBOARD = 0, ITF_NUM_MOUSE, ITF_NUM_TOTAL };
enum { HID_INSTANCE_KEYBOARD = 0, HID_INSTANCE_MOUSE = 1 };
enum { REPORT_ID_MOUSE_REL = 1, REPORT_ID_MOUSE_ABS = 2 };

// Relative mouse report: 16-bit signed X/Y deltas (spec requires 16-bit,
// TinyUSB's stock TUD_HID_REPORT_DESC_MOUSE is 8-bit) + 5 buttons + wheel + pan.
typedef struct __attribute__((packed)) {
    uint8_t buttons;
    int16_t x;
    int16_t y;
    int8_t wheel;
    int8_t pan;
} mouse_rel16_report_t;

#ifdef __cplusplus
}
#endif

#endif /* USB_DESCRIPTORS_H_ */
