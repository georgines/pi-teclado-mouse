#include "contacts.hpp"
#include "hardware/gpio.h"

namespace contacts {
namespace {

// Nenhum destes pinos colide com UART0 (GP16/GP17), I2C1 do OLED (GP14/GP15)
// nem com o botão de modo (GP6).
constexpr uint PINS[COUNT] = {18, 19, 20, 4};

inline bool closed_level(uint8_t idx) { return ACTIVE_HIGH[idx]; }

} // namespace

void init() {
    for (uint8_t i = 0; i < COUNT; ++i) {
        // Ordem importa: escreve o nível aberto no registrador de saída ANTES
        // de habilitar a saída, para não piscar o nível fechado.
        gpio_init(PINS[i]);
        gpio_put(PINS[i], !closed_level(i));
        gpio_set_dir(PINS[i], GPIO_OUT);
    }
}

bool valid(uint8_t contact) { return contact >= 1 && contact <= COUNT; }

void set(uint8_t contact, bool closed) {
    if (!valid(contact)) return;
    uint8_t idx = static_cast<uint8_t>(contact - 1);
    gpio_put(PINS[idx], closed == closed_level(idx));
}

void open_all() {
    for (uint8_t i = 1; i <= COUNT; ++i) set(i, false);
}

uint8_t bitmap() {
    uint8_t bits = 0;
    for (uint8_t i = 0; i < COUNT; ++i) {
        if (gpio_get(PINS[i]) == closed_level(i)) bits = static_cast<uint8_t>(bits | (1u << i));
    }
    return bits;
}

} // namespace contacts
