/*
 * Práctica de ADC · Plantilla
 * DADO: PWM de 1 kHz en GP2, PWM del LED en GP15, comando stream, scheduler.
 * TODO: comandos adc, fs, filtro, ps, dimmer; la función filtrar y la tarea del dimmer.
 */
#include <string.h>
#include <stdlib.h>
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "consola.h"
#include "adc_stream.h"

#define UART_ID     uart0
#define BAUD_RATE   115200
#define TX_PIN      0
#define RX_PIN      1

#define PIN_PWM     2       /* señal de prueba: 1 kHz, 50 %            */
#define PIN_LED     15      /* LED del dimmer                          */
#define PIN_PS      23      /* modo de la fuente de alimentación       */
#define LED_TOP     999     /* LED a 1 kHz, 1000 niveles (como en PWM) */

typedef enum { FILTRO_NINGUNO, FILTRO_MEDIA, FILTRO_EMA } filtro_t;

static uint32_t fs_hz  = 1000;               /* para stream            */
static filtro_t filtro = FILTRO_NINGUNO;
static bool     dimmer_activo = false;
static uint16_t muestras[STREAM_MAX];

/* ================= DADO ================= */

/* PWM de prueba: 1 kHz, 50 %, en GP2 */
static void pwm_prueba_init(void) {
    gpio_set_function(PIN_PWM, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_PWM);
    pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);   /* 1 tick = 1 us */
    pwm_set_wrap(slice, 999);                                     /* 1 kHz         */
    pwm_set_gpio_level(PIN_PWM, 500);                             /* 50 %          */
    pwm_set_enabled(slice, true);
}

/* PWM del LED: 1 kHz, level de 0 a 1000 */
static void led_init(void) {
    gpio_set_function(PIN_LED, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_LED);
    pwm_set_clkdiv(slice, clock_get_hz(clk_sys) / 1000000.0f);
    pwm_set_wrap(slice, LED_TOP);
    pwm_set_gpio_level(PIN_LED, 0);
    pwm_set_enabled(slice, true);
}

static void led_set_level(uint16_t level) {
    pwm_set_gpio_level(PIN_LED, level);
}

/* ================= TODO ================= */

/* Aplica el filtro elegido a las n muestras de buf, en el mismo arreglo.
 * FILTRO_MEDIA: media móvil de 16. FILTRO_EMA: alfa = 1/16. */
static void filtrar(uint16_t *buf, uint32_t n) {
    /* TODO */
    (void)buf;
    (void)n;
}

/* Cada 10 ms: si el dimmer está activo, leer ADC0, filtrar y ajustar el LED.
 * Además, contar cuántas veces cambia el level y reportarlo cada segundo. */
static void tarea_dimmer(void) {
    /* TODO */
}

static void procesar(char *linea) {
    if (strncmp(linea, "stream ", 7) == 0) {                  /* DADO */
        char *fin;
        uint32_t canal = strtoul(&linea[7], &fin, 10);
        uint32_t n     = strtoul(fin, NULL, 10);
        if (!adc_stream_capturar(canal, n, fs_hz, muestras)) {
            consola_printf("error: canal 0-2, n hasta %u, Fs 100-10000, n/Fs hasta 5 s\n", STREAM_MAX);
            return;
        }
        filtrar(muestras, n);
        adc_stream_enviar(muestras, n);
    }
    /* TODO: adc <canal>, fs <Hz>, filtro <ninguno|media|ema>, ps <on|off>, dimmer <on|off> */
    else {
        consola_printf("error: comando desconocido\n");
    }
}

/* ================= DADO ================= */

static void tarea_uart(void) {
    static char linea[64];
    if (consola_leer_linea(linea, sizeof linea)) {
        procesar(linea);
        consola_printf("> ");
    }
}

int main(void) {
    consola_init(UART_ID, TX_PIN, RX_PIN, BAUD_RATE);

    adc_init();
    adc_gpio_init(26);                    /* ADC0: potenciómetro    */
    adc_gpio_init(27);                    /* ADC1: PWM directo      */
    adc_gpio_init(28);                    /* ADC2: PWM con filtro RC */

    gpio_init(PIN_PS);
    gpio_set_dir(PIN_PS, GPIO_OUT);
    gpio_put(PIN_PS, 0);

    pwm_prueba_init();
    led_init();
    led_set_level(0);

    consola_printf("Practica ADC lista\n> ");

    while (true) {
        tarea_uart();
        tarea_dimmer();
        consola_tarea();
    }
}
