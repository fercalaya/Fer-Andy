#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

#define UART_ID uart0
#define BAUD_NOMINAL 115200
#define UART_TX_PIN 0
#define UART_RX_PIN 1
#define TRAZA_PIN 2  //pin fisico 4 = GP2
#define LED_PIN 25
#define BUF_SIZE 256
#define MASK (BUF_SIZE - 1)
// buffer de recepcion
static volatile uint8_t rx_buf[BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;
// buffer de transmision
static volatile uint8_t tx_buf[BUF_SIZE];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;
// metricas de error
static volatile uint32_t descartados = 0;
static volatile uint32_t overruns = 0;
static volatile uint32_t framing_errors = 0;
// variables de control
static uint32_t led_periodo_us = 500000;
static bool led_override = false;
static bool led_state = false;
// retardo para la parte 2.2
static uint32_t retardo_inyectado_us = 0;
void on_uart_rx(void) {
    // revisar errores de hardware
    uint32_t status = uart_get_hw(UART_ID)->rsr;
    if (status & UART_UARTRSR_OE_BITS) {
        overruns++;
        uart_get_hw(UART_ID)->rsr = UART_UARTRSR_OE_BITS;
    }
    if (status & UART_UARTRSR_FE_BITS) {
        framing_errors++;
        uart_get_hw(UART_ID)->rsr = UART_UARTRSR_FE_BITS;
    }
    // sacar todos los bytes de la fifo
    while (uart_is_readable(UART_ID)) {
        uint8_t c = uart_getc(UART_ID);
        uint16_t next_head = (rx_head + 1) & MASK;
        // revisar si hay espacio
        if (next_head != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next_head;
        } else {
            // buffer lleno, descartar byte nuevo
            descartados++;
        }
    }
}
// meter un caracter en la cola de transmision
void cli_putc(char c) {
    uint16_t next_head = (tx_head + 1) & MASK;
    if (next_head != tx_tail) {
        tx_buf[tx_head] = (uint8_t)c;
        tx_head = next_head;
    }
}
// meter texto en la cola
void cli_print(const char *str) {
    while (*str) {
        cli_putc(*str);
        str++;
    }
}
// mandar datos pendientes
void flush_tx(void) {
    while (tx_tail != tx_head) {
        if (!uart_is_writable(UART_ID)) {
            break;
        }
        uart_putc_raw(UART_ID, tx_buf[tx_tail]);
        tx_tail = (tx_tail + 1) & MASK;
    }
}
// procesar comandos
void procesar_comando(char *cmd) {
    char resp[100];
    if (strcmp(cmd, "get stats") == 0) {
        snprintf(resp, sizeof(resp),
                 "\r\noverruns: %lu, framing errors: %lu, descartados: %lu\r\n",
                 overruns,
                 framing_errors,
                 descartados);
        cli_print(resp);
    } else if (strcmp(cmd, "set led on") == 0) {
        led_override = true;
        gpio_put(LED_PIN, 1);
        cli_print("\r\nok\r\n");
    } else if (strcmp(cmd, "set led off") == 0) {
        led_override = true;
        gpio_put(LED_PIN, 0);
        cli_print("\r\nok\r\n");
    } else if (strncmp(cmd, "set periodo ", 12) == 0) {
        uint32_t ms = (uint32_t)strtoul(&cmd[12], NULL, 10);
        if (ms > 0) {
            led_periodo_us = ms * 1000;
            led_override = false;
            cli_print("\r\nok\r\n");
        } else {
            cli_print("\r\nerror: parametro invalido\r\n");
        }
    } else {
        cli_print("\r\nerror: comando desconocido\r\n");
    }
}
// tarea del led
void tarea_led(void) {
    if (!led_override) {
        led_state = !led_state;
        gpio_put(LED_PIN, led_state);
    }
    // retardo para la parte 2.2
    if (retardo_inyectado_us > 0) {
        sleep_us(retardo_inyectado_us);
    }
}
// tarea del log
void tarea_log(void) {
    char log_buf[80];
    uint32_t t_ms = to_ms_since_boot(get_absolute_time());
    snprintf(log_buf, sizeof(log_buf),
             "[%lu ms] Log: Tareas activas\r\n",
             t_ms);
    cli_print(log_buf);
}
// tarea del cli
void tarea_cli(void) {
    static char linea[64];
    static uint8_t n = 0;
    // revisar todos los bytes recibidos
    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & MASK;
        // eco del caracter
        cli_putc(c);
        // revisar fin de linea
        if (c == '\r' || c == '\n') {
            if (n > 0) {
                linea[n] = '\0';
                procesar_comando(linea);
                n = 0;
            }
        } else {
            // guardar caracter
            if (n < sizeof(linea) - 1) {
                linea[n] = c;
                n++;
            }
        }
    }
}
int main() {
    // configurar led
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
    // configurar pin de traza
    // pin fisico 4 = GP2
    gpio_init(TRAZA_PIN);
    gpio_set_dir(TRAZA_PIN, GPIO_OUT);
    gpio_put(TRAZA_PIN, 0);
    // configurar uart
    uint baud_real = uart_init(UART_ID, BAUD_NOMINAL);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    // configurar 8n1
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);
    // configurar interrupcion rx
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);
    // calcular error del baud
    float error_baud =
        ((float)baud_real - (float)BAUD_NOMINAL) /
        (float)BAUD_NOMINAL * 100.0f;
    // mensaje inicial
    char init_msg[100];
    snprintf(init_msg, sizeof(init_msg),
             "Baud nominal: %d | Baud real: %u | Error: %.3f%%\r\n",
             BAUD_NOMINAL,
             baud_real,
             error_baud);
    cli_print(init_msg);
    // tiempos iniciales
    uint32_t t_led = time_us_32();
    uint32_t t_log = time_us_32();
    while (true) {
        // iniciar traza
        sio_hw->gpio_set = 1u << TRAZA_PIN;
        // tarea del led
        if (time_us_32() - t_led >= led_periodo_us) {
            t_led = time_us_32();
            tarea_led();
        }
        // tarea del log
        if (time_us_32() - t_log >= 1000000) {
            t_log = time_us_32();
            tarea_log();
        }
        // atender comandos
        tarea_cli();
        // mandar respuestas
        flush_tx();
        // terminar traza
        sio_hw->gpio_clr = 1u << TRAZA_PIN;
    }
}