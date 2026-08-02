#include "u8g2pico.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"
#include "u8g2.h"
#include "u8x8.h"
#include <string.h>

u8g2pico_i2c_config_t u8g2pico_i2c_config;

void u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(u8g2pico_t *u8g2pico, i2c_inst_t *i2c, uint8_t sda_pin, uint8_t scl_pin, const u8g2_cb_t *rotation, uint8_t address)
{
    u8g2pico_i2c_config.i2c_port = i2c;
    u8g2pico_i2c_config.sda_pin = sda_pin;
    u8g2pico_i2c_config.scl_pin = scl_pin;
    u8g2pico_i2c_config.address = address;

    u8g2_Setup_ssd1306_i2c_128x64_noname_f(u8g2pico, rotation, pico_i2c_byte_cb, pico_gpio_and_delay);
}

uint8_t pico_gpio_and_delay(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    switch (msg)
    {
    case U8X8_MSG_GPIO_AND_DELAY_INIT:
        break;
    case U8X8_MSG_DELAY_NANO:
        sleep_us(arg_int ? 1 : 0);
        break;
    case U8X8_MSG_DELAY_100NANO:
        break;
    case U8X8_MSG_DELAY_10MICRO:
        sleep_us(arg_int * 10);
        break;
    case U8X8_MSG_DELAY_MILLI:
        sleep_ms(arg_int);
        break;
    case U8X8_MSG_DELAY_I2C:
        break;
    case U8X8_MSG_GPIO_D0:
    case U8X8_MSG_GPIO_D1:
    case U8X8_MSG_GPIO_D2:
    case U8X8_MSG_GPIO_D3:
    case U8X8_MSG_GPIO_D4:
    case U8X8_MSG_GPIO_D5:
    case U8X8_MSG_GPIO_D6:
    case U8X8_MSG_GPIO_D7:
        break;
    case U8X8_MSG_GPIO_CS:
    case U8X8_MSG_GPIO_DC:
    case U8X8_MSG_GPIO_RESET:
        break;
    case U8X8_MSG_GPIO_MENU_SELECT:
        u8x8_SetGPIOResult(u8x8, 0);
        break;
    case U8X8_MSG_GPIO_MENU_NEXT:
        u8x8_SetGPIOResult(u8x8, 0);
        break;
    case U8X8_MSG_GPIO_MENU_PREV:
        u8x8_SetGPIOResult(u8x8, 0);
        break;
    case U8X8_MSG_GPIO_MENU_HOME:
        u8x8_SetGPIOResult(u8x8, 0);
        break;
    default:
        u8x8_SetGPIOResult(u8x8, 1);
        break;
    }
    return 1;
}

uint8_t pico_i2c_byte_cb(u8x8_t *u8x8, uint8_t msg, uint8_t arg_int, void *arg_ptr)
{
    static uint8_t buffer[32];
    static uint8_t buf_idx;
    uint8_t *data;

    switch (msg)
    {
    case U8X8_MSG_BYTE_INIT:
        i2c_init(u8g2pico_i2c_config.i2c_port, 400 * 1000);
        gpio_set_function(u8g2pico_i2c_config.sda_pin, GPIO_FUNC_I2C);
        gpio_set_function(u8g2pico_i2c_config.scl_pin, GPIO_FUNC_I2C);
        gpio_pull_up(u8g2pico_i2c_config.sda_pin);
        gpio_pull_up(u8g2pico_i2c_config.scl_pin);
        break;
    case U8X8_MSG_BYTE_SET_DC:
        break;
    case U8X8_MSG_BYTE_START_TRANSFER:
        buf_idx = 0;
        break;
    case U8X8_MSG_BYTE_SEND:
        data = (uint8_t *)arg_ptr;
        while (arg_int > 0)
        {
            buffer[buf_idx++] = *data;
            data++;
            arg_int--;
        }
        break;
    case U8X8_MSG_BYTE_END_TRANSFER:
        i2c_write_blocking(u8g2pico_i2c_config.i2c_port, u8g2pico_i2c_config.address, buffer, buf_idx, false);
        break;
    default:
        return 0;
    }
    return 1;
}
