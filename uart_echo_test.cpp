// Minimal UART bring-up diagnostic, decoupled from the full protocol stack.
// Every ~1s, sends "PICOALIVE\n" unprompted (proves Pico TX -> adapter RX
// works with zero input needed). Also echoes back any byte it receives
// (proves adapter TX -> Pico RX works). Same pins/baud as the real firmware:
// UART0, TX=GP16, RX=GP17, 921600 8N1.
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/gpio.h"

int main() {
    uart_init(uart0, 921600);
    gpio_set_function(16, GPIO_FUNC_UART); // TX
    gpio_set_function(17, GPIO_FUNC_UART); // RX
    uart_set_hw_flow(uart0, false, false);
    uart_set_format(uart0, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(uart0, true);

    absolute_time_t next_heartbeat = make_timeout_time_ms(1000);

    while (true) {
        while (uart_is_readable(uart0)) {
            uart_putc_raw(uart0, uart_getc(uart0)); // pure echo, byte by byte
        }
        if (time_reached(next_heartbeat)) {
            uart_puts(uart0, "PICOALIVE\n");
            next_heartbeat = make_timeout_time_ms(1000);
        }
    }
}
