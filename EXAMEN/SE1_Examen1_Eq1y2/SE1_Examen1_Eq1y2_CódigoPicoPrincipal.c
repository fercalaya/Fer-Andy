#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/uart.h"
#include "hardware/irq.h"
#include "hardware/gpio.h"

// Pines UART (comunicación hacia la Pico de Apoyo)
#define UART_ID          uart0
#define BAUD_NOMINAL     115200
#define UART_TX_PIN      0 // GP0 -> RX de Pico Apoyo
#define UART_RX_PIN      1 // GP1 -> TX de Pico Apoyo

// Periféricos del juego en la Pico 2020
#define LED_ROJO_PIN     14
#define LED_VERDE_PIN    15
#define BTN_J1_PIN       16
#define BTN_J2_PIN       17

// Buffer circular UART (Recepción y Transmisión no bloqueantes)
#define BUF_SIZE         256
#define MASK             (BUF_SIZE - 1)

static volatile uint8_t rx_buf[BUF_SIZE];
static volatile uint16_t rx_head = 0;
static volatile uint16_t rx_tail = 0;

static volatile uint8_t tx_buf[BUF_SIZE];
static volatile uint16_t tx_head = 0;
static volatile uint16_t tx_tail = 0;

// Máquinas de estado
typedef enum {
    ESTADO_DETENIDO,
    ESTADO_PREPARADOS,
    ESTADO_YA
} estado_juego_t;

typedef enum {
    RES_SIN_PARTIDAS,
    RES_GANA,
    RES_FALSO,
    RES_SIN_GANADOR
} tipo_resultado_t;

// Variables compartidas con ISR / Callbacks (volatile obligatorias)
static volatile estado_juego_t estado_actual = ESTADO_DETENIDO;
static volatile tipo_resultado_t ult_resultado = RES_SIN_PARTIDAS;
static volatile int ult_ganador = 0;            // 1: J1, 2: J2
static volatile int ult_infractor = 0;          // Jugador que causó salida en falso
static volatile uint32_t ult_tiempo_reaccion_ms = 0;

// Scoreboard y configuración
static volatile uint32_t score_j1 = 0;
static volatile uint32_t score_j2 = 0;
static int32_t periodo_parpadeo_ms = 200;       // 50 ms a 1000 ms

// Control del Timer
static struct repeating_timer timer_juego;
static volatile bool timer_activo = false;
static volatile uint32_t ticks_restantes = 0;
static volatile bool led_rojo_estado = false;
static volatile uint64_t t_inicio_verde_us = 0;

// Antirrebote por software (200 ms)
static volatile uint64_t ult_pulsacion_j1_us = 0;
static volatile uint64_t ult_pulsacion_j2_us = 0;
#define DEBOUNCE_TIME_US 200000

// ISR Recepción UART
void on_uart_rx(void) {
    while (uart_is_readable(UART_ID)) {
        uint8_t c = uart_getc(UART_ID);
        uint16_t next_head = (rx_head + 1) & MASK;
        if (next_head != rx_tail) {
            rx_buf[rx_head] = c;
            rx_head = next_head;
        }
    }
}

// Envío a la cola de salida TX
void cli_putc(char c) {
    uint16_t next_head = (tx_head + 1) & MASK;
    if (next_head != tx_tail) {
        tx_buf[tx_head] = (uint8_t)c;
        tx_head = next_head;
    }
}

void cli_print(const char *str) {
    while (*str) {
        cli_putc(*str);
        str++;
    }
}

void flush_tx(void) {
    while (tx_tail != tx_head) {
        if (!uart_is_writable(UART_ID)) {
            break;
        }
        uart_putc_raw(UART_ID, tx_buf[tx_tail]);
        tx_tail = (tx_tail + 1) & MASK;
    }
}

// Callback de Timer Repetitivo
bool timer_callback(struct repeating_timer *t) {
    if (estado_actual == ESTADO_PREPARADOS) {
        led_rojo_estado = !led_rojo_estado;
        gpio_put(LED_ROJO_PIN, led_rojo_estado);

        if (ticks_restantes > 0) {
            ticks_restantes--;
        }

        if (ticks_restantes == 0) {
            gpio_put(LED_ROJO_PIN, false);
            led_rojo_estado = false;

            // Encender verde y tomar timestamp exacto
            gpio_put(LED_VERDE_PIN, true);
            t_inicio_verde_us = time_us_64();
            estado_actual = ESTADO_YA;

            // Ventana máxima de respuesta: 5 segundos
            ticks_restantes = 5000 / periodo_parpadeo_ms;
            if (ticks_restantes == 0) ticks_restantes = 1;
        }
        return true;
    } 
    else if (estado_actual == ESTADO_YA) {
        if (ticks_restantes > 0) {
            ticks_restantes--;
        }

        if (ticks_restantes == 0) {
            gpio_put(LED_VERDE_PIN, false);
            ult_resultado = RES_SIN_GANADOR;
            ult_ganador = 0;
            estado_actual = ESTADO_DETENIDO;
            timer_activo = false;
            return false; // Cancela el timer
        }
        return true;
    }

    return false;
}

// ISR Botones J1 y J2
void gpio_callback(uint gpio, uint32_t events) {
    uint64_t t_ahora = time_us_64();

    if (gpio == BTN_J1_PIN) {
        if (t_ahora - ult_pulsacion_j1_us < DEBOUNCE_TIME_US) return;
        ult_pulsacion_j1_us = t_ahora;
    } else if (gpio == BTN_J2_PIN) {
        if (t_ahora - ult_pulsacion_j2_us < DEBOUNCE_TIME_US) return;
        ult_pulsacion_j2_us = t_ahora;
    } else {
        return;
    }

    if (estado_actual == ESTADO_DETENIDO) {
        return;
    }

    // Salida en falso durante PREPARADOS
    if (estado_actual == ESTADO_PREPARADOS) {
        if (timer_activo) {
            cancel_repeating_timer(&timer_juego);
            timer_activo = false;
        }
        gpio_put(LED_ROJO_PIN, false);
        led_rojo_estado = false;

        ult_resultado = RES_FALSO;
        if (gpio == BTN_J1_PIN) {
            ult_infractor = 1;
            ult_ganador = 2;
            score_j2++;
        } else {
            ult_infractor = 2;
            ult_ganador = 1;
            score_j1++;
        }
        estado_actual = ESTADO_DETENIDO;
        return;
    }

    // Pulsación válida durante ¡YA!
    if (estado_actual == ESTADO_YA) {
        if (timer_activo) {
            cancel_repeating_timer(&timer_juego);
            timer_activo = false;
        }
        gpio_put(LED_VERDE_PIN, false);

        uint64_t delta_us = t_ahora - t_inicio_verde_us;
        ult_tiempo_reaccion_ms = (uint32_t)((delta_us + 500) / 1000);

        ult_resultado = RES_GANA;
        if (gpio == BTN_J1_PIN) {
            ult_ganador = 1;
            score_j1++;
        } else {
            ult_ganador = 2;
            score_j2++;
        }
        estado_actual = ESTADO_DETENIDO;
    }
}

// Comandos de consola
void cmd_start(void) {
    static bool semilla_inicializada = false;

    if (estado_actual != ESTADO_DETENIDO) {
        cli_print("error: partida en curso\r\n");
        return;
    }

    if (!semilla_inicializada) {
        srand(time_us_32());
        semilla_inicializada = true;
    }

    // Tiempo aleatorio entre 2000 y 5000 ms
    int espera_ms = 2000 + (rand() % (5000 - 2000 + 1));
    ticks_restantes = espera_ms / periodo_parpadeo_ms;
    if (ticks_restantes == 0) ticks_restantes = 1;

    led_rojo_estado = true;
    gpio_put(LED_ROJO_PIN, true);
    gpio_put(LED_VERDE_PIN, false);

    estado_actual = ESTADO_PREPARADOS;
    
    // Timer repetitivo con valor negativo para no acumular retraso
    add_repeating_timer_ms(-periodo_parpadeo_ms, timer_callback, NULL, &timer_juego);
    timer_activo = true;

    cli_print("ok\r\n");
}

void cmd_set_periodo(int ms) {
    if (ms >= 50 && ms <= 1000) {
        periodo_parpadeo_ms = ms;
        cli_print("ok\r\n");
    } else {
        cli_print("error: comando desconocido\r\n");
    }
}

void cmd_get_stats(void) {
    char buf[128];
    cli_print("Rango: 2000 - 5000 ms\r\n");

    switch (ult_resultado) {
        case RES_SIN_PARTIDAS:
            cli_print("Ganador: Sin partidas\r\n");
            cli_print("Tiempo: N/A\r\n");
            break;
        case RES_GANA:
            snprintf(buf, sizeof(buf), "Ganador: J%d\r\n", ult_ganador);
            cli_print(buf);
            snprintf(buf, sizeof(buf), "Tiempo: %lu ms\r\n", (unsigned long)ult_tiempo_reaccion_ms);
            cli_print(buf);
            break;
        case RES_FALSO:
            snprintf(buf, sizeof(buf), "Ganador: J%d (salida en falso J%d)\r\n", ult_ganador, ult_infractor);
            cli_print(buf);
            cli_print("Tiempo: N/A\r\n");
            break;
        case RES_SIN_GANADOR:
            cli_print("Ganador: Sin ganador\r\n");
            cli_print("Tiempo: N/A\r\n");
            break;
    }

    snprintf(buf, sizeof(buf), "Score: J1 %lu, J2 %lu\r\n", (unsigned long)score_j1, (unsigned long)score_j2);
    cli_print(buf);
}

void cmd_reset(void) {
    score_j1 = 0;
    score_j2 = 0;
    cli_print("ok; score a cero\r\n");
}

void procesar_comando(char *cmd) {
    while (*cmd == ' ') cmd++;
    int len = strlen(cmd);
    while (len > 0 && (cmd[len - 1] == ' ' || cmd[len - 1] == '\r' || cmd[len - 1] == '\n')) {
        cmd[--len] = '\0';
    }

    int ms_val;
    if (strcmp(cmd, "start") == 0) {
        cmd_start();
    } else if (sscanf(cmd, "set periodo %d", &ms_val) == 1) {
        cmd_set_periodo(ms_val);
    } else if (strcmp(cmd, "get stats") == 0) {
        cmd_get_stats();
    } else if (strcmp(cmd, "reset") == 0) {
        cmd_reset();
    } else if (strlen(cmd) > 0) {
        cli_print("error: comando desconocido\r\n");
    }
}

void tarea_cli(void) {
    static char linea[64];
    static uint8_t n = 0;

    while (rx_tail != rx_head) {
        char c = rx_buf[rx_tail];
        rx_tail = (rx_tail + 1) & MASK;

        // Eco en la consola
        cli_putc(c);

        if (c == '\r' || c == '\n') {
            if (n > 0) {
                linea[n] = '\0';
                cli_print("\r\n");
                procesar_comando(linea);
                n = 0;
            }
        } else if (n < sizeof(linea) - 1) {
            linea[n++] = c;
        }
    }
}

int main() {
    // Configurar LEDs
    gpio_init(LED_ROJO_PIN);
    gpio_set_dir(LED_ROJO_PIN, GPIO_OUT);
    gpio_put(LED_ROJO_PIN, 0);

    gpio_init(LED_VERDE_PIN);
    gpio_set_dir(LED_VERDE_PIN, GPIO_OUT);
    gpio_put(LED_VERDE_PIN, 0);

    // Configurar Botones
    gpio_init(BTN_J1_PIN);
    gpio_set_dir(BTN_J1_PIN, GPIO_IN);
    gpio_pull_up(BTN_J1_PIN);

    gpio_init(BTN_J2_PIN);
    gpio_set_dir(BTN_J2_PIN, GPIO_IN);
    gpio_pull_up(BTN_J2_PIN);

    gpio_set_irq_enabled_with_callback(BTN_J1_PIN, GPIO_IRQ_EDGE_FALL, true, &gpio_callback);
    gpio_set_irq_enabled(BTN_J2_PIN, GPIO_IRQ_EDGE_FALL, true);

    // Configurar UART0
    uart_init(UART_ID, BAUD_NOMINAL);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    // IRQ de UART0 RX
    irq_set_exclusive_handler(UART0_IRQ, on_uart_rx);
    irq_set_enabled(UART0_IRQ, true);
    uart_set_irq_enables(UART_ID, true, false);

    // Bucle cooperativo
    while (true) {
        tarea_cli();
        flush_tx();
        tight_loop_contents();
    }
}