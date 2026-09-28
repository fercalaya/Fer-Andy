#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

// Pico de Apoyo: Puente USB-UART transparente
#define UART_ID          uart0
#define BAUD_RATE        115200

#define UART_TX_PIN      0 // GP0 -> RX de Pico Principal (GP1)
#define UART_RX_PIN      1 // GP1 -> TX de Pico Principal (GP0)

int main() {
    // Inicializar USB stdio
    stdio_init_all();

    // Configurar hardware UART0 a 115200 8N1
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    while (true) {
        // USB -> UART (Enviar a Pico Principal)
        int c_usb = getchar_timeout_us(1000);
        if (c_usb != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID, (char)c_usb);
        }

        // UART -> USB (Recibir de Pico Principal y mostrar en la PC)
        while (uart_is_readable(UART_ID)) {
            int c_uart = uart_getc(UART_ID);
            putchar(c_uart);
        }

        fflush(stdout);
        tight_loop_contents();
    }
}