/*
 * consola.c — Consola de texto por UART (resuelta)
 * Recepción por interrupción con buffer circular, transmisión por cola
 * y armado de líneas con eco. Lo que lleva 'static' es privado.
 */
#include "consola.h"
#include <stdarg.h>
#include <stdio.h>
#include "hardware/gpio.h"
#include "hardware/irq.h"

/* ================= Privado ================= */

static uart_inst_t *uart_id;    /* qué UART usa la consola; lo guarda consola_init */

/* La mesa redonda de recepción: la llena la ISR, la vacía el bucle */
#define BUF_SIZE 256
#define MASK     (BUF_SIZE - 1)
static volatile uint8_t  buffer[BUF_SIZE];
static volatile uint16_t head = 0, tail = 0;

/* La cola de TX: la llena consola_printf, la vacía consola_tarea */
#define TX_SIZE 1024
#define TX_MASK (TX_SIZE - 1)
static uint8_t  tx_buffer[TX_SIZE];
static uint16_t tx_head = 0, tx_tail = 0;

/* Contadores para get stats */
static volatile consola_stats_t stats;

/* ISR de recepción: guarda y se va */
static void on_uart_rx(void) {
    while (uart_is_readable(uart_id)) {                 /* while, no if            */
        uint32_t dr = uart_get_hw(uart_id)->dr;         /* el byte + sus bits de error */
        if (dr & UART_UARTDR_OE_BITS) stats.overruns++;
        if (dr & UART_UARTDR_FE_BITS) stats.framing++;
        uint8_t c = dr & 0xFF;                          /* el byte                 */

        uint16_t siguiente = (head + 1) & MASK;
        if (siguiente != tail) {                        /* ¿hay lugar?             */
            buffer[head] = c;
            head = siguiente;
        } else {
            stats.descartados++;                        /* lleno: se cuenta        */
        }
    }
}

/* Saca un byte de la mesa redonda. Devuelve el byte, o -1 si está vacía. */
static int rx_sacar(void) {
    if (head == tail) return -1;
    uint8_t c = buffer[tail];
    tail = (tail + 1) & MASK;
    return c;
}

/* Mete un carácter a la cola de TX. Si la cola está llena, se descarta. */
static void tx_meter(char c) {
    uint16_t siguiente = (tx_head + 1) & TX_MASK;
    if (siguiente != tx_tail) {
        tx_buffer[tx_head] = (uint8_t)c;
        tx_head = siguiente;
    }
}

/* ================= Público ================= */

uint32_t consola_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint32_t baud) {
    uart_id = uart;                                     /* 1. recordar qué UART se usa */
    uint32_t baud_real = uart_init(uart_id, baud);      /* 2. encender el UART         */
    gpio_set_function(tx_pin, GPIO_FUNC_UART);          /* 3. conectar los pines       */
    gpio_set_function(rx_pin, GPIO_FUNC_UART);
    uart_set_format(uart_id, 8, 1, UART_PARITY_NONE);   /* 4. formato 8N1              */

    uint irq = (uart_get_index(uart_id) == 0) ? UART0_IRQ : UART1_IRQ;
    irq_set_exclusive_handler(irq, on_uart_rx);
    irq_set_enabled(irq, true);
    uart_set_irq_enables(uart_id, true, false);         /* RX sí, TX no                */

    return baud_real;                                   /* 5. devolver el baud real    */
}

bool consola_leer_linea(char *linea, size_t max) {
    static size_t n = 0;                                /* static: no se reinicia      */
    int c;
    while ((c = rx_sacar()) >= 0) {                     /* sacar todo lo que haya      */
        if (c == '\r' || c == '\n') {                   /* Enter: la línea está completa */
            if (n == 0) continue;                       /* \n después de \r: ignorar   */
            linea[n] = '\0';
            n = 0;
            tx_meter('\r');
            tx_meter('\n');
            return true;
        } else if (c == 8 || c == 127) {                /* retroceso                   */
            if (n > 0) {
                n--;
                tx_meter('\b'); tx_meter(' '); tx_meter('\b');
            }
        } else if (c >= 32 && c < 127 && n < max - 1) {
            linea[n++] = (char)c;
            tx_meter((char)c);                          /* eco por la cola de TX       */
        }
    }
    return false;
}

void consola_printf(const char *fmt, ...) {
    char tmp[128];
    va_list args;
    va_start(args, fmt);
    vsnprintf(tmp, sizeof tmp, fmt, args);              /* arma el texto igual que printf */
    va_end(args);

    for (char *p = tmp; *p; p++) {
        if (*p == '\n') tx_meter('\r');                 /* la terminal necesita \r\n   */
        tx_meter(*p);
    }
}

void consola_tarea(void) {
    while (tx_tail != tx_head && uart_is_writable(uart_id)) {
        uart_putc_raw(uart_id, (char)tx_buffer[tx_tail]);
        tx_tail = (tx_tail + 1) & TX_MASK;
    }
}

void consola_flush(void) {                              /* espera a vaciar la cola de TX */
    while (tx_tail != tx_head) consola_tarea();
}

void consola_stats(consola_stats_t *s) {
    s->overruns    = stats.overruns;
    s->framing     = stats.framing;
    s->descartados = stats.descartados;
}
