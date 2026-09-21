#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"

//LA PICO DE APOYO (puente)

#define UART_ID uart0
#define BAUD_RATE 115200 //cambiar a 57600 solo para la prueba 2.4

#define UART_TX_PIN 0 //pin fisico 1
#define UART_RX_PIN 1 //pin fisico 2

int main() {

    //iniciar comunicacion USB
    stdio_init_all();

    //dar tiempo a que la consola USB se conecte
    sleep_ms(2000);

    //configurar UART
    uart_init(UART_ID, BAUD_RATE);

    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);

    //formato 8N1
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);

    uart_set_fifo_enabled(UART_ID, true);

    while (true) {

        //USB -> UART
        int c_usb = getchar_timeout_us(1000);

        if (c_usb != PICO_ERROR_TIMEOUT) {
            uart_putc_raw(UART_ID, (char)c_usb);
        }

        //UART -> USB
        while (uart_is_readable(UART_ID)) {

            int c_uart = uart_getc(UART_ID);

            putchar(c_uart);
        }

        //mandar inmediatamente a la consola
        fflush(stdout);

        tight_loop_contents();
    }
}