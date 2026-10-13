#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define BRIDGE_UART  uart0
#define BRIDGE_BAUD  115200     /* debe coincidir con CONSOLE_BAUD de la Pico principal */
#define PIN_TX       0
#define PIN_RX       1

int main(void) {
    stdio_init_all();           /* USB CDC (el UART por defecto está desactivado en CMake) */

    uart_init(BRIDGE_UART, BRIDGE_BAUD);
    gpio_set_function(PIN_TX, GPIO_FUNC_UART);
    gpio_set_function(PIN_RX, GPIO_FUNC_UART);
    uart_set_format(BRIDGE_UART, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(BRIDGE_UART, true);

    while (true) {
        /* USB -> UART */
        int c = getchar_timeout_us(0);
        while (c != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(BRIDGE_UART, (char)c);
            c = getchar_timeout_us(0);
        }
        /* UART -> USB */
        while (uart_is_readable(BRIDGE_UART)) {
            putchar_raw(uart_getc(BRIDGE_UART));
        }
    }
}