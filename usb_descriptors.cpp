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

#include <cstring>
#include "bsp/board_api.h"
#include "tusb.h"
#include "usb_descriptors.h"

#define USB_VID   0xCafe
#define USB_PID   0x4004
#define USB_BCD   0x0200

//--------------------------------------------------------------------+
// Device Descriptor
//--------------------------------------------------------------------+
tusb_desc_device_t const desc_device = {
    .bLength = sizeof(tusb_desc_device_t),
    .bDescriptorType = TUSB_DESC_DEVICE,
    .bcdUSB = USB_BCD,
    .bDeviceClass = 0x00,
    .bDeviceSubClass = 0x00,
    .bDeviceProtocol = 0x00,
    .bMaxPacketSize0 = CFG_TUD_ENDPOINT0_SIZE,

    .idVendor = USB_VID,
    .idProduct = USB_PID,
    .bcdDevice = 0x0100,

    .iManufacturer = 0x01,
    .iProduct = 0x02,
    .iSerialNumber = 0x03,

    .bNumConfigurations = 0x01,
};

uint8_t const* tud_descriptor_device_cb(void) {
    return reinterpret_cast<uint8_t const*>(&desc_device);
}

//--------------------------------------------------------------------+
// HID Report Descriptors
//--------------------------------------------------------------------+

// Keyboard: boot protocol, no report ID (BIOS boot drivers expect the raw
// 8-byte modifier+reserved+6-key layout with no ID byte prefix).
uint8_t const desc_hid_report_keyboard[] = {
    TUD_HID_REPORT_DESC_KEYBOARD(),
};

// Custom relative-mouse template: same shape as TinyUSB's stock
// TUD_HID_REPORT_DESC_MOUSE (5 buttons, i8 wheel, i8 AC-pan) but with 16-bit
// signed X/Y deltas, per spec (stock macro is 8-bit, -127..127).
#define HID_REPORT_DESC_MOUSE_REL16(...) \
  HID_USAGE_PAGE ( HID_USAGE_PAGE_DESKTOP      )                   ,\
  HID_USAGE      ( HID_USAGE_DESKTOP_MOUSE     )                   ,\
  HID_COLLECTION ( HID_COLLECTION_APPLICATION  )                   ,\
    __VA_ARGS__ \
    HID_USAGE      ( HID_USAGE_DESKTOP_POINTER )                   ,\
    HID_COLLECTION ( HID_COLLECTION_PHYSICAL   )                   ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_BUTTON  )                   ,\
        HID_USAGE_MIN   ( 1                                      ) ,\
        HID_USAGE_MAX   ( 5                                      ) ,\
        HID_LOGICAL_MIN ( 0                                      ) ,\
        HID_LOGICAL_MAX ( 1                                      ) ,\
        HID_REPORT_COUNT( 5                                      ) ,\
        HID_REPORT_SIZE ( 1                                      ) ,\
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_ABSOLUTE ) ,\
        HID_REPORT_COUNT( 1                                      ) ,\
        HID_REPORT_SIZE ( 3                                      ) ,\
        HID_INPUT       ( HID_CONSTANT                           ) ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_DESKTOP )                   ,\
        HID_USAGE         ( HID_USAGE_DESKTOP_X                  ) ,\
        HID_USAGE         ( HID_USAGE_DESKTOP_Y                  ) ,\
        HID_LOGICAL_MIN_N ( 0x8000, 2                            ) ,\
        HID_LOGICAL_MAX_N ( 0x7FFF, 2                            ) ,\
        HID_REPORT_SIZE   ( 16                                   ) ,\
        HID_REPORT_COUNT  ( 2                                    ) ,\
        HID_INPUT         ( HID_DATA | HID_VARIABLE | HID_RELATIVE ) ,\
        HID_USAGE       ( HID_USAGE_DESKTOP_WHEEL                )  ,\
        HID_LOGICAL_MIN ( 0x81                                   )  ,\
        HID_LOGICAL_MAX ( 0x7f                                   )  ,\
        HID_REPORT_COUNT( 1                                      )  ,\
        HID_REPORT_SIZE ( 8                                      )  ,\
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE )  ,\
      HID_USAGE_PAGE  ( HID_USAGE_PAGE_CONSUMER ), \
        HID_USAGE_N     ( HID_USAGE_CONSUMER_AC_PAN, 2           ), \
        HID_LOGICAL_MIN ( 0x81                                   ), \
        HID_LOGICAL_MAX ( 0x7f                                   ), \
        HID_REPORT_COUNT( 1                                      ), \
        HID_REPORT_SIZE ( 8                                      ), \
        HID_INPUT       ( HID_DATA | HID_VARIABLE | HID_RELATIVE ), \
    HID_COLLECTION_END                                            , \
  HID_COLLECTION_END \

// Mouse interface: two report IDs multiplexed — relative (16-bit deltas) and
// absolute (stock TUD_HID_REPORT_DESC_ABSMOUSE already matches the spec
// exactly: u16 0..32767 X/Y, 5 buttons, i8 wheel, i8 pan).
uint8_t const desc_hid_report_mouse[] = {
    HID_REPORT_DESC_MOUSE_REL16(HID_REPORT_ID(REPORT_ID_MOUSE_REL)),
    TUD_HID_REPORT_DESC_ABSMOUSE(HID_REPORT_ID(REPORT_ID_MOUSE_ABS)),
};

uint8_t const* tud_hid_descriptor_report_cb(uint8_t instance) {
    return (instance == HID_INSTANCE_KEYBOARD) ? desc_hid_report_keyboard : desc_hid_report_mouse;
}

//--------------------------------------------------------------------+
// Configuration Descriptor
//--------------------------------------------------------------------+

#define CONFIG_TOTAL_LEN (TUD_CONFIG_DESC_LEN + 2 * TUD_HID_DESC_LEN)

#define EPNUM_HID_KEYBOARD 0x81
#define EPNUM_HID_MOUSE    0x82

uint8_t const desc_configuration[] = {
    TUD_CONFIG_DESCRIPTOR(1, ITF_NUM_TOTAL, 0, CONFIG_TOTAL_LEN, TUSB_DESC_CONFIG_ATT_REMOTE_WAKEUP, 100),

    // Keyboard: boot protocol, 1ms polling.
    TUD_HID_DESCRIPTOR(ITF_NUM_KEYBOARD, 0, HID_ITF_PROTOCOL_KEYBOARD,
                        sizeof(desc_hid_report_keyboard), EPNUM_HID_KEYBOARD, CFG_TUD_HID_EP_BUFSIZE, 1),

    // Mouse: no boot protocol (custom 16-bit relative shape), 1ms polling.
    TUD_HID_DESCRIPTOR(ITF_NUM_MOUSE, 0, HID_ITF_PROTOCOL_NONE,
                        sizeof(desc_hid_report_mouse), EPNUM_HID_MOUSE, CFG_TUD_HID_EP_BUFSIZE, 1),
};

uint8_t const* tud_descriptor_configuration_cb(uint8_t index) {
    (void) index;
    return desc_configuration;
}

//--------------------------------------------------------------------+
// String Descriptors
//--------------------------------------------------------------------+

enum { STRID_LANGID = 0, STRID_MANUFACTURER, STRID_PRODUCT, STRID_SERIAL };

char const* string_desc_arr[] = {
    nullptr,                                  // 0: langid, built directly below
    "Pico HID-UART Bridge",                   // 1: Manufacturer
    "Composite Keyboard/Mouse",               // 2: Product
    nullptr,                                  // 3: Serial uses unique ID
};

static uint16_t _desc_str[32 + 1];

uint16_t const* tud_descriptor_string_cb(uint8_t index, uint16_t langid) {
    (void) langid;
    size_t chr_count;

    switch (index) {
        case STRID_LANGID:
            _desc_str[1] = 0x0409;
            chr_count = 1;
            break;

        case STRID_SERIAL:
            chr_count = board_usb_get_serial(_desc_str + 1, 32);
            break;

        default:
            if (!(index < sizeof(string_desc_arr) / sizeof(string_desc_arr[0]))) return nullptr;

            const char* str = string_desc_arr[index];
            chr_count = strlen(str);
            size_t const max_count = sizeof(_desc_str) / sizeof(_desc_str[0]) - 1;
            if (chr_count > max_count) chr_count = max_count;

            for (size_t i = 0; i < chr_count; i++) {
                _desc_str[1 + i] = static_cast<uint16_t>(str[i]);
            }
            break;
    }

    _desc_str[0] = static_cast<uint16_t>((TUSB_DESC_STRING << 8) | (2 * chr_count + 2));
    return _desc_str;
}
