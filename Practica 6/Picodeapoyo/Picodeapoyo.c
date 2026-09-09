#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define BAUD_RATE 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1

int main() {
    stdio_init_all();
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    while (true) {
        // USB -> Hardware UART
        int c_usb = getchar_timeout_us(0);
        if (c_usb != PICO_ERROR_TIMEOUT) {
            uart_putc(UART_ID, (char)c_usb);
        }
        // Hardware UART -> USB
        if (uart_is_readable(UART_ID)) {
            putchar(uart_getc(UART_ID));
        }
    }
}