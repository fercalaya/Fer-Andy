#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <ctype.h>
#include "pico/stdlib.h"
#include "pico/rand.h"
#include "hardware/adc.h"
#include "hardware/pwm.h"
#include "hardware/gpio.h"
#include "hardware/clocks.h"
#include "consola.h"

/* ================= CONFIGURACIÓN ================= */

#define DEBUG_RUIDO     1       /* 1 = habilita el comando extra "ruido" */
#define DEBUG_BOTON     0       /* 0 = desactiva la línea de diagnóstico del botón en get stats */
#define MENSAJES_JUEGO  1       /* 1 = imprime "acierto/error" al presionar el botón */

/* UART de la consola */
#define UART_ID         uart0
#define BAUD_RATE       115200
#define TX_PIN          0
#define RX_PIN          1

/* Pines */
#define PIN_POT         26      /* ADC0 */
#define ADC_CANAL       0
#define PIN_PISTA       2       /* PWM slice 1, canal A (LED con resistencia a GND) */
#define PIN_SERVO       6       /* PWM slice 3, canal A */
#define PIN_BOTON       14      /* Conectar a GND, con pull-up interna activa */
#define PIN_PS          23      /* 1 = PWM continuo en fuente (menos rizado en ADC) */

/* ADC / perilla */
#define ADC_PERIODO_US  10000   /* Muestreo cada 10 ms (sin sleep_ms) */
#define EMA_SHIFT       3       /* EMA con alfa = 1/2^3 = 1/8 */
#define HISTERESIS      8       /* Códigos de margen para evitar oscilaciones */

/* Juego */
#define TOLERANCIA      5       /* Acepta si |numero - secreto| <= 5 */
#define RADIO_PISTA     20      /* A 20 o más posiciones: LED apagado */
#define N_NUMEROS       3
#define N_INTENTOS      3

/* Antirrebote reforzado: 150 ms para eliminar rebote al presionar y al soltar */
#define DEBOUNCE_US     150000  

/* Pista con LED: PWM a 1 kHz (wrap = 999 con 1 us por tick) */
#define LED_TOP         999

/* Servo: 50 Hz (wrap = 19999 con 1 us por tick)*/
#define SERVO_TOP       19999
#define SERVO_CERRADO   1000    /* 1000 us */
#define SERVO_ABIERTO   2000    /* 2000 us */

/* ================= ESTADO ================= */

typedef enum { DETENIDO, BUSCANDO, ABIERTA, BLOQUEADA } estado_t;
typedef enum { RES_NINGUNO, RES_ABIERTA, RES_BLOQUEADA } resultado_t;

static uint32_t f_sys_hz;

/* Variables compartidas ISR <-> bucle */
static volatile bool     boton_evento = false;
static volatile uint32_t boton_t_us   = 0;

#if DEBUG_BOTON
static volatile uint32_t dbg_flancos = 0;
static volatile uint32_t dbg_pulsos  = 0;
#endif

/* Perilla */
static uint32_t adc_proximo_us;
static uint32_t ema_acc;
static bool     ema_lista = false;
static uint16_t codigo_filtrado = 0;
static int      numero = 0;

/* Juego */
static estado_t    estado = DETENIDO;
static int         secreto[N_NUMEROS];
static int         k = 0;                       /* Índice del número buscado (0..2) */
static int         intentos = N_INTENTOS;
static uint64_t    t_inicio_us = 0;
static float       tiempo_total_s = 0.0f;
static uint32_t    reaccion_us = 0;
static resultado_t ultimo_res = RES_NINGUNO;
static float       ultimo_tiempo_s = 0.0f;
static float       record_s = -1.0f;

/* Pista */
static int pista_d = -1;

/* ================= FILTRO + CÓDIGO A NÚMERO ================= */

static uint16_t filtro_ema(uint16_t x) {
    if (!ema_lista) {
        ema_acc = (uint32_t)x << EMA_SHIFT;
        ema_lista = true;
    } else {
        ema_acc = ema_acc + x - (ema_acc >> EMA_SHIFT);
    }
    return (uint16_t)((ema_acc + (1u << (EMA_SHIFT - 1))) >> EMA_SHIFT);
}

static int numero_con_histeresis(uint16_t codigo, int actual) {
    int cand = (int)codigo * 100 / 4096;
    int lo = actual * 4096 / 100;
    int hi = (actual + 1) * 4096 / 100;
    if ((int)codigo + HISTERESIS < lo || (int)codigo >= hi + HISTERESIS) return cand;
    return actual;
}

/* ================= PWM ================= */

static void servo_set_us(uint32_t us) {
    pwm_set_gpio_level(PIN_SERVO, us);
}

static void servo_init(void) {
    gpio_set_function(PIN_SERVO, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_SERVO);
    pwm_set_clkdiv(slice, (float)f_sys_hz / 1000000.0f);
    pwm_set_wrap(slice, SERVO_TOP);
    pwm_set_gpio_level(PIN_SERVO, SERVO_CERRADO);
    pwm_set_enabled(slice, true);
}

static void pista_init(void) {
    gpio_set_function(PIN_PISTA, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(PIN_PISTA);
    pwm_set_clkdiv(slice, (float)f_sys_hz / 1000000.0f);
    pwm_set_wrap(slice, LED_TOP);
    pwm_set_gpio_level(PIN_PISTA, 0);
    pwm_set_enabled(slice, true);
}

static void pista_apagar(void) {
    pwm_set_gpio_level(PIN_PISTA, 0);
}

static void pista_aplicar(int d) {
    if (d >= RADIO_PISTA) {
        pwm_set_gpio_level(PIN_PISTA, 0);
    } else {
        float cercania = (float)(RADIO_PISTA - d) / (float)RADIO_PISTA;
        uint16_t nivel = (uint16_t)(cercania * (LED_TOP + 1) + 0.5f);
        pwm_set_gpio_level(PIN_PISTA, nivel);
    }
}

/* ================= RUIDO (EVIDENCIA) ================= */

#if DEBUG_RUIDO
#define RUIDO_N 200
static bool     ruido_capturando = false;
static bool     ruido_listo = false;
static uint16_t ruido_i = 0;
static uint16_t raw_buf[RUIDO_N], filt_buf[RUIDO_N];
static uint32_t cambios_raw, cambios_ema, cambios_final;
static int      prev_raw, prev_ema, prev_final;

static bool     csv_activo = false;
static uint16_t csv_i = 0;
static uint32_t csv_proximo_us = 0;

static void ruido_iniciar(void) {
    ruido_i = 0;
    cambios_raw = cambios_ema = cambios_final = 0;
    ruido_listo = false;
    csv_activo = false;
    ruido_capturando = true;
}

static void ruido_muestra(uint16_t raw) {
    int n_raw = (int)raw * 100 / 4096;
    int n_ema = (int)codigo_filtrado * 100 / 4096;
    if (ruido_i > 0) {
        if (n_raw != prev_raw)     cambios_raw++;
        if (n_ema != prev_ema)     cambios_ema++;
        if (numero != prev_final)  cambios_final++;
    }
    prev_raw = n_raw; prev_ema = n_ema; prev_final = numero;
    raw_buf[ruido_i]  = raw;
    filt_buf[ruido_i] = codigo_filtrado;
    if (++ruido_i >= RUIDO_N) { ruido_capturando = false; ruido_listo = true; }
}

static void ruido_reporte(void) {
    uint16_t rmin = 0xFFFF, rmax = 0, fmin = 0xFFFF, fmax = 0;
    for (int i = 0; i < RUIDO_N; i++) {
        if (raw_buf[i]  < rmin) rmin = raw_buf[i];
        if (raw_buf[i]  > rmax) rmax = raw_buf[i];
        if (filt_buf[i] < fmin) fmin = filt_buf[i];
        if (filt_buf[i] > fmax) fmax = filt_buf[i];
    }
    float ancho = 4096.0f / 100.0f;
    consola_printf("Ruido con la perilla quieta (%d lecturas, 1 cada %d ms)\n", RUIDO_N, ADC_PERIODO_US / 1000);
    consola_printf("  Sin filtro : min %u  max %u  rango %u codigos (%.2f numeros)\n",
                   rmin, rmax, rmax - rmin, (float)(rmax - rmin) / ancho);
    consola_printf("  Con filtro : min %u  max %u  rango %u codigos (%.2f numeros)\n",
                   fmin, fmax, fmax - fmin, (float)(fmax - fmin) / ancho);
    consola_printf("  Cada numero ocupa %.2f codigos\n", ancho);
    consola_printf("  Saltos de numero: sin filtro %lu | EMA %lu | EMA+histeresis %lu\n",
                   (unsigned long)cambios_raw, (unsigned long)cambios_ema, (unsigned long)cambios_final);
    consola_printf("  (usa 'ruido csv' para volcar las 200 lecturas)\n");
    ruido_listo = false;
}

static void ruido_csv_tarea(void) {
    if (!csv_activo) return;
    uint32_t ahora = time_us_32();
    if ((int32_t)(ahora - csv_proximo_us) < 0) return;
    csv_proximo_us = ahora + 2500;
    if (csv_i == 0) consola_printf("n,sin_filtro,con_filtro\n");
    if (csv_i < RUIDO_N) {
        consola_printf("%u,%u,%u\n", csv_i, raw_buf[csv_i], filt_buf[csv_i]);
        csv_i++;
    } else {
        csv_activo = false;
        consola_printf("> ");
    }
}
#endif

/* ================= ISR DEL BOTÓN CON DOBLE FILTRO ================= */

static void boton_isr(uint gpio, uint32_t events) {
    if (gpio != PIN_BOTON) return;
    if (!(events & GPIO_IRQ_EDGE_FALL)) return;

    uint32_t ahora = time_us_32();
#if DEBUG_BOTON
    dbg_flancos++;
#endif

    /* 1. Comprueba ventana temporal (150 ms) */
    if (ahora - boton_t_us >= DEBOUNCE_US) {
        /* 2. Comprueba que el pin siga efectivamente en nivel bajo (0) */
        if (gpio_get(PIN_BOTON) == 0) {
            boton_t_us = ahora;
            boton_evento = true;
#if DEBUG_BOTON
            dbg_pulsos++;
#endif
        }
    }
}

/* ================= LÓGICA DEL JUEGO ================= */

static void partida_iniciar(const int s[N_NUMEROS]) {
    for (int i = 0; i < N_NUMEROS; i++) secreto[i] = s[i];
    k = 0;
    intentos = N_INTENTOS;
    boton_evento = false;
    servo_set_us(SERVO_CERRADO);
    pista_d = -1;
    t_inicio_us = time_us_64();
    estado = BUSCANDO;
}

static void tarea_boton(void) {
    if (!boton_evento) return;
    boton_evento = false;
    uint32_t t_pulso = boton_t_us;

    if (estado != BUSCANDO) return;

    int d = abs(numero - secreto[k]);
    if (d <= TOLERANCIA) {
        k++;
        if (k == N_NUMEROS) {
            tiempo_total_s = (float)(time_us_64() - t_inicio_us) / 1e6f;
            servo_set_us(SERVO_ABIERTO);
            reaccion_us = time_us_32() - t_pulso;
            estado = ABIERTA;
            ultimo_res = RES_ABIERTA;
            ultimo_tiempo_s = tiempo_total_s;
            if (record_s < 0.0f || tiempo_total_s < record_s) record_s = tiempo_total_s;
#if MENSAJES_JUEGO
            consola_printf("Caja abierta en %.1f s\n> ", tiempo_total_s);
#endif
        } else {
#if MENSAJES_JUEGO
            consola_printf("Acierto: numero %d de %d. Busca el siguiente\n> ", k, N_NUMEROS);
#endif
        }
    } else {
        /* Error: se descuenta intento y se mantiene el número actual k */
        intentos--;
        if (intentos <= 0) {
            estado = BLOQUEADA;
            ultimo_res = RES_BLOQUEADA;
#if MENSAJES_JUEGO
            consola_printf("Error. Caja bloqueada\n> ");
#endif
        } else {
#if MENSAJES_JUEGO
            consola_printf("Error. Sigues en numero %d de %d. Intentos restantes: %d\n> ", k + 1, N_NUMEROS, intentos);
#endif
        }
    }
}

static void tarea_pista(void) {
    if (estado != BUSCANDO) {
        if (pista_d != -1) { pista_apagar(); pista_d = -1; }
        return;
    }
    int d = abs(numero - secreto[k]);
    if (d > RADIO_PISTA) d = RADIO_PISTA;
    if (d != pista_d) {
        pista_aplicar(d);
        pista_d = d;
    }
}

/* ================= ADC ================= */

static void tarea_adc(void) {
    uint32_t ahora = time_us_32();
    if ((int32_t)(ahora - adc_proximo_us) < 0) return;
    adc_proximo_us += ADC_PERIODO_US;
    if ((int32_t)(ahora - adc_proximo_us) >= 0)
        adc_proximo_us = ahora + ADC_PERIODO_US;

    uint16_t raw = adc_read();
    codigo_filtrado = filtro_ema(raw);
    numero = numero_con_histeresis(codigo_filtrado, numero);
#if DEBUG_RUIDO
    if (ruido_capturando) ruido_muestra(raw);
#endif
}

/* ================= CONSOLA SERIAL ================= */

static bool leer_0_99(const char **p, int *out) {
    while (**p == ' ') (*p)++;
    if (!isdigit((unsigned char)**p)) return false;
    char *fin;
    long v = strtol(*p, &fin, 10);
    if (v < 0 || v > 99 || (*fin != '\0' && *fin != ' ')) return false;
    *out = (int)v;
    *p = fin;
    return true;
}

static void cmd_nuevo(void) {
    int s[N_NUMEROS];
    for (int i = 0; i < N_NUMEROS; i++) s[i] = (int)(get_rand_32() % 100);
    partida_iniciar(s);
    consola_printf("ok\n");
}

static void cmd_secreto(const char *args) {
    int s[N_NUMEROS];
    const char *p = args;
    for (int i = 0; i < N_NUMEROS; i++) {
        if (!leer_0_99(&p, &s[i])) { consola_printf("error: uso: secreto <a> <b> <c> (0 a 99)\n"); return; }
    }
    while (*p == ' ') p++;
    if (*p != '\0') { consola_printf("error: uso: secreto <a> <b> <c> (0 a 99)\n"); return; }
    partida_iniciar(s);
    consola_printf("ok\n");
}

static void cmd_stats(void) {
    switch (estado) {
    case DETENIDO:  consola_printf("Estado: Sin partidas\n"); break;
    case BUSCANDO:  consola_printf("Estado: Buscando numero %d de %d\n", k + 1, N_NUMEROS); break;
    case ABIERTA:   consola_printf("Estado: Abierta, tiempo total %.1f s\n", tiempo_total_s); break;
    case BLOQUEADA: consola_printf("Estado: Bloqueada\n"); break;
    }
    consola_printf("Perilla: %d\n", numero);
    if (estado == DETENIDO) consola_printf("Intentos restantes: -\n");
    else                    consola_printf("Intentos restantes: %d\n", intentos);
    switch (ultimo_res) {
    case RES_NINGUNO:    consola_printf("Ultima partida: Sin partidas\n"); break;
    case RES_ABIERTA:    consola_printf("Ultima partida: Abierta, %.1f s\n", ultimo_tiempo_s); break;
    case RES_BLOQUEADA:  consola_printf("Ultima partida: Bloqueada\n"); break;
    }
    if (estado == ABIERTA)
        consola_printf("Reaccion boton->servo: %lu us\n", (unsigned long)reaccion_us);
#if DEBUG_BOTON
    consola_printf("Boton: flancos %lu, pulsaciones %lu, pin GP%d = %d\n",
                   (unsigned long)dbg_flancos, (unsigned long)dbg_pulsos, PIN_BOTON, (int)gpio_get(PIN_BOTON));
#endif
    if (record_s < 0.0f) consola_printf("Récord: --\n");
    else                 consola_printf("Récord: %.1f s\n", record_s);
}

static void procesar(char *linea) {
    while (*linea == ' ') linea++;
    size_t n = strlen(linea);
    while (n > 0 && linea[n - 1] == ' ') linea[--n] = '\0';
    if (n == 0) return;

    if (strcmp(linea, "nuevo") == 0)                cmd_nuevo();
    else if (strncmp(linea, "secreto", 7) == 0 && (linea[7] == ' ' || linea[7] == '\0'))
                                                    cmd_secreto(linea + 7);
    else if (strcmp(linea, "get stats") == 0)       cmd_stats();
    else if (strcmp(linea, "reset") == 0)           { record_s = -1.0f; consola_printf("ok\n"); }
#if DEBUG_RUIDO
    else if (strcmp(linea, "ruido") == 0)           { ruido_iniciar(); consola_printf("capturando 200 lecturas (2 s), no muevas la perilla...\n"); }
    else if (strcmp(linea, "ruido csv") == 0)       { csv_i = 0; csv_activo = true; csv_proximo_us = time_us_32(); }
#endif
    else                                            consola_printf("error: comando desconocido\n");
}

static void tarea_uart(void) {
    static char linea[64];
    if (consola_leer_linea(linea, sizeof linea)) {
        procesar(linea);
#if DEBUG_RUIDO
        if (!csv_activo)
#endif
            consola_printf("> ");
    }
#if DEBUG_RUIDO
    if (ruido_listo) { ruido_reporte(); consola_printf("> "); }
    ruido_csv_tarea();
#endif
}

/* ================= MAIN ================= */

int main(void) {
    consola_init(UART_ID, TX_PIN, RX_PIN, BAUD_RATE);
    f_sys_hz = clock_get_hz(clk_sys);

    gpio_init(PIN_PS);
    gpio_set_dir(PIN_PS, GPIO_OUT);
    gpio_put(PIN_PS, 1);

    adc_init();
    adc_gpio_init(PIN_POT);
    adc_select_input(ADC_CANAL);

    servo_init();
    pista_init();

    gpio_init(PIN_BOTON);
    gpio_set_dir(PIN_BOTON, GPIO_IN);
    gpio_pull_up(PIN_BOTON);
    gpio_set_irq_enabled_with_callback(PIN_BOTON, GPIO_IRQ_EDGE_FALL, true, &boton_isr);

    uint16_t c0 = adc_read();
    codigo_filtrado = filtro_ema(c0);
    numero = (int)codigo_filtrado * 100 / 4096;
    adc_proximo_us = time_us_32() + ADC_PERIODO_US;

    consola_printf("Caja fuerte lista. Comandos: nuevo | secreto a b c | get stats | reset\n> ");

    while (true) {
        tarea_adc();        /* Perilla -> EMA -> Histéresis */
        tarea_boton();      /* Evalúa aciertos o errores sin reiniciar k */
        tarea_pista();      /* Control proporcional de brillo en LED */
        tarea_uart();       /* Procesamiento serial */
        consola_tarea();    /* Flush de buffers de consola */
    }
}