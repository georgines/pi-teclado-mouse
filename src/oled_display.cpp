#include "oled_display.hpp"
#include "shared_state.hpp"
#include "u8g2pico.h"
#include "hardware/i2c.h"
#include "hardware/gpio.h"
#include "pico/time.h"
#include <cstdio>
#include <cstring>

namespace oled_display {
namespace {

constexpr uint I2C_SDA_PIN = 14;
constexpr uint I2C_SCL_PIN = 15;
constexpr uint32_t I2C_BAUD = 400000;
constexpr uint32_t MIN_REDRAW_MS = 50;

u8g2pico_t u8g2;

bool probe_address(uint8_t addr) {
    uint8_t probe = 0;
    return i2c_write_blocking(i2c1, addr, &probe, 1, false) >= 0;
}

void render(const DeviceSnapshot& s) {
    char line[24];
    u8g2_ClearBuffer(&u8g2);
    u8g2_SetFont(&u8g2, u8g2_font_6x10_tf);

    const char* usb_str = (s.usb_state == UsbLinkState::Mounted)     ? "MOUNTED"
                           : (s.usb_state == UsbLinkState::Suspended) ? "SUSPEND"
                                                                       : "DISCONN";
    const char* mode_str = (s.mouse_mode == MouseMode::Absolute) ? "ABS" : "REL";

    snprintf(line, sizeof(line), "USB:%s %s", usb_str, mode_str);
    u8g2_DrawStr(&u8g2, 0, 8, line);

    snprintf(line, sizeof(line), "UART err:%u", s.uart_error_count);
    u8g2_DrawStr(&u8g2, 0, 18, line);

    snprintf(line, sizeof(line), "MOD:%02X K:%02X%02X%02X%02X%02X%02X", s.modifiers, s.keys[0], s.keys[1],
             s.keys[2], s.keys[3], s.keys[4], s.keys[5]);
    u8g2_DrawStr(&u8g2, 0, 28, line);

    if (s.mouse_mode == MouseMode::Absolute) {
        snprintf(line, sizeof(line), "X:%u Y:%u", s.abs_x, s.abs_y);
    } else {
        snprintf(line, sizeof(line), "vX:%ld vY:%ld", static_cast<long>(s.virtual_x),
                 static_cast<long>(s.virtual_y));
    }
    u8g2_DrawStr(&u8g2, 0, 38, line);

    snprintf(line, sizeof(line), "BTN:%c%c%c%c%c", (s.mouse_buttons & 0x01) ? 'L' : '-',
             (s.mouse_buttons & 0x02) ? 'R' : '-', (s.mouse_buttons & 0x04) ? 'M' : '-',
             (s.mouse_buttons & 0x08) ? '4' : '-', (s.mouse_buttons & 0x10) ? '5' : '-');
    u8g2_DrawStr(&u8g2, 0, 48, line);

    snprintf(line, sizeof(line), "Wv:%d Wh:%d", s.last_wheel_v, s.last_wheel_h);
    u8g2_DrawStr(&u8g2, 0, 58, line);

    u8g2_SendBuffer(&u8g2);
}

} // namespace

void core1_main() {
    i2c_init(i2c1, I2C_BAUD);
    gpio_set_function(I2C_SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(I2C_SCL_PIN, GPIO_FUNC_I2C);
    gpio_pull_up(I2C_SDA_PIN);
    gpio_pull_up(I2C_SCL_PIN);

    uint8_t addr = 0;
    if (probe_address(0x3C)) addr = 0x3C;
    else if (probe_address(0x3D)) addr = 0x3D;

    shared_state_set_oled_ok(addr != 0);
    if (addr == 0) {
        // No display found: never touch u8g2 again, never block core0/USB/UART.
        while (true) sleep_ms(200);
    }

    u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(&u8g2, i2c1, I2C_SDA_PIN, I2C_SCL_PIN, U8G2_R0, addr);
    u8g2_InitDisplay(&u8g2);
    u8g2_SetPowerSave(&u8g2, 0);

    DeviceSnapshot last{};
    bool have_last = false;
    uint32_t last_draw_ms = 0;

    while (true) {
        DeviceSnapshot cur = shared_state_consume();
        uint32_t now = to_ms_since_boot(get_absolute_time());
        bool changed = !have_last || memcmp(&cur, &last, sizeof(cur)) != 0;
        if (changed && (now - last_draw_ms) >= MIN_REDRAW_MS) {
            render(cur);
            last = cur;
            have_last = true;
            last_draw_ms = now;
        }
        sleep_ms(5);
    }
}

} // namespace oled_display
