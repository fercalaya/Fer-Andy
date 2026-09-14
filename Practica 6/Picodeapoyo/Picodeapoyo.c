#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

#define UART_ID uart0
#define BAUD_RATE 115200 // cambiar a 57600 para la prueba 2.4
#define UART_TX_PIN 0
#define UART_RX_PIN 1

int main() {
    // iniciar comunicacion usb
    stdio_init_all();
    // esperar un poco para que la consola usb se conecte
    sleep_ms(1000);
    // configurar uart
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // configurar uart en 8n1
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);
    while (true) {
        // usb -> uart
        int c_usb = getchar_timeout_us(0);
        if (c_usb != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID, (char)c_usb);
        }
        // uart -> usb
        while (uart_is_readable(UART_ID)) {
            int c_uart = uart_getc(UART_ID);
            putchar(c_uart);
            fflush(stdout);
        }
        // dejar que usb siga trabajando
        tight_loop_contents();
    }
}