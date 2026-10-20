#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID     uart0
#define BAUD_RATE   115200
#define TX_PIN      0
#define RX_PIN      1

int main(void) {
    stdio_init_all();                                   /* USB CDC (la terminal del PC) */

    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);   /* 8N1 */
    uart_set_fifo_enabled(UART_ID, true);

    while (true) {
        /* UART -> USB */
        while (uart_is_readable(UART_ID)) {
            putchar_raw(uart_getc(UART_ID));
        }
        /* USB -> UART */
        int c;
        while ((c = getchar_timeout_us(0)) != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID, (char)c);
        }
    }
}