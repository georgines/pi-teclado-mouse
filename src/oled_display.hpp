#pragma once

// Core1 entry point: owns the SSD1306 exclusively. Never touches USB/UART —
// absence or failure of the display must not affect core0 in any way.
namespace oled_display {
void core1_main();
} // namespace oled_display
