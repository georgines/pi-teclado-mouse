#pragma once
#include <cstdint>
#include "hid_state.hpp"

// GP6 mode button: pull-up, active low, 30ms debounce. mode_set() is the
// single place that performs a mouse-mode switch — both the button and the
// UART SET_MOUSE_MODE command route through it (root-cause fix, not two
// copies of the neutral-report/release/zero-position dance).
namespace mode_button {

void init();
void poll(uint32_t now_ms);
void mode_set(MouseMode m);

} // namespace mode_button
