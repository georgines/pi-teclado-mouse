#pragma once
#include "u8g2.h"
#include "pico/stdlib.h"
#include "hardware/i2c.h"

#ifdef __cplusplus
extern "C"
{
#endif
    typedef struct
    {
        i2c_inst_t *i2c_port;
        uint8_t sda_pin;
        uint8_t scl_pin;
        uint8_t address;

    } u8g2pico_i2c_config_t;

    typedef u8g2_t u8g2pico_t;
    typedef u8x8_t u8x8pico_t;

    extern uint8_t pico_gpio_and_delay(u8x8pico_t *u8x8pico, uint8_t msg, uint8_t arg_int, void *arg_ptr);
    extern uint8_t pico_i2c_byte_cb(u8x8pico_t *u8x8pico, uint8_t msg, uint8_t arg_int, void *arg_ptr);

    extern void u8g2_Setup_ssd1306_i2c_128x64_noname_f_pico(u8g2pico_t *u8g2pico, i2c_inst_t *i2c, uint8_t sda_pin, uint8_t scl_pin, const u8g2_cb_t *rotation, uint8_t address);
    
#ifdef __cplusplus
}
#endif