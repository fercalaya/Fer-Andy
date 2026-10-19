/*
 * Práctica de ADC · Pico principal (Pico 2, RP2350)
 * DADO:  PWM de 1 kHz en GP2, PWM del LED en GP15, comando stream, scheduler.
 * HECHO: comandos adc, fs, filtro, ps, dimmer; la función filtrar y tarea_dimmer.
 * EXTRA: comandos temp, vsys y cal (un sello por cada uno).
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

#define ADC_VREF            3.3f
#define ADC_N_PROMEDIO      64          /* muestras del comando adc    */
#define DIMMER_PERIODO_US   10000       /* el dimmer lee cada 10 ms    */
#define DIMMER_REPORTE_US   1000000     /* y reporta cada segundo      */

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

/* ================= FILTROS ================= */

/* Estado de un filtro. Se guarda aparte para que lo usen igual
 * filtrar() (una captura completa) y el dimmer (muestra por muestra). */
typedef struct {
    uint16_t ventana[16];   /* últimas 16 muestras (media móvil)          */
    uint32_t suma;          /* suma de lo que hay en la ventana           */
    uint32_t idx;           /* dónde entra la siguiente muestra           */
    uint32_t cuenta;        /* cuántas muestras hay en la ventana (<= 16) */
    uint32_t ema16;         /* EMA guardada multiplicada por 16           */
    bool     ema_lista;     /* ya se inicializó la EMA con la 1a muestra  */
} estado_filtro_t;

static void filtro_reset(estado_filtro_t *f) {
    memset(f, 0, sizeof *f);
}

/* Mete una muestra al filtro elegido y devuelve la muestra filtrada. */
static uint16_t filtro_paso(estado_filtro_t *f, uint16_t x) {
    switch (filtro) {
    case FILTRO_MEDIA:                                   /* media móvil de 16        */
        if (f->cuenta == 16) f->suma -= f->ventana[f->idx];   /* sale la más vieja   */
        else                 f->cuenta++;
        f->ventana[f->idx] = x;                          /* entra la nueva           */
        f->suma += x;
        f->idx = (f->idx + 1) & 15;
        return (uint16_t)((f->suma + f->cuenta / 2) / f->cuenta);
    case FILTRO_EMA:                                     /* alfa = 1/16              */
        if (!f->ema_lista) {                             /* arranca en la 1a muestra */
            f->ema16 = (uint32_t)x << 4;
            f->ema_lista = true;
        } else {
            /* y += (x - y) / 16, con y guardada x16 para no perder decimales */
            f->ema16 = f->ema16 + x - (f->ema16 >> 4);
        }
        return (uint16_t)((f->ema16 + 8) >> 4);
    default:
        return x;
    }
}

/* Aplica el filtro elegido a las n muestras de buf, en el mismo arreglo.
 * FILTRO_MEDIA: media móvil de 16. FILTRO_EMA: alfa = 1/16. */
static void filtrar(uint16_t *buf, uint32_t n) {
    if (filtro == FILTRO_NINGUNO) return;
    estado_filtro_t f;
    filtro_reset(&f);
    for (uint32_t i = 0; i < n; i++) buf[i] = filtro_paso(&f, buf[i]);
}

/* ================= ADC ================= */

/* Promedio de 64 muestras del canal. La primera lectura se descarta:
 * el multiplexor acaba de girar y el capacitor interno aún se está cargando. */
static float adc_promedio(uint canal) {
    adc_select_input(canal);
    (void)adc_read();
    uint32_t suma = 0;
    for (int i = 0; i < ADC_N_PROMEDIO; i++) suma += adc_read();
    return (float)suma / ADC_N_PROMEDIO;
}

static float codigo_a_volts(float codigo) {
    return codigo * ADC_VREF / 4096.0f;
}

/* ---- Calibración de dos puntos (extra) ---- */
static bool  cal_cero_ok   = false;
static bool  cal_activa    = false;
static float cal_cero      = 0.0f;   /* código medido con 0 V               */
static float cal_ref_codigo = 0.0f;  /* código medido con el voltaje conocido */
static float cal_ref_volts = 0.0f;   /* ese voltaje, comprobado con multímetro */

static float corregir_codigo(float c) {
    float codigo_ideal_ref = cal_ref_volts * 4096.0f / ADC_VREF;
    return (c - cal_cero) * codigo_ideal_ref / (cal_ref_codigo - cal_cero);
}

/* ================= DIMMER ================= */

static estado_filtro_t filtro_dimmer;
static uint32_t dimmer_t_lectura = 0;
static uint32_t dimmer_t_reporte = 0;
static uint32_t dimmer_cambios   = 0;
static int32_t  dimmer_nivel     = -1;       /* -1: aún no hay lectura */

static void dimmer_reiniciar(void) {
    filtro_reset(&filtro_dimmer);
    dimmer_cambios = 0;
    dimmer_nivel   = -1;
    dimmer_t_lectura = dimmer_t_reporte = time_us_32();
}

/* Cada 10 ms: si el dimmer está activo, leer ADC0, filtrar y ajustar el LED.
 * Además, contar cuántas veces cambia el level y reportarlo cada segundo. */
static void tarea_dimmer(void) {
    if (!dimmer_activo) return;
    uint32_t ahora = time_us_32();

    if (ahora - dimmer_t_lectura >= DIMMER_PERIODO_US) {
        dimmer_t_lectura = ahora;
        adc_select_input(0);
        uint16_t codigo = adc_read();
        codigo = filtro_paso(&filtro_dimmer, codigo);
        int32_t level = (int32_t)codigo * 1000 / 4096;     /* 0 a 999 */
        if (level != dimmer_nivel) {
            if (dimmer_nivel >= 0) dimmer_cambios++;       /* el 1o no cuenta */
            dimmer_nivel = level;
            led_set_level((uint16_t)level);
        }
    }

    if (ahora - dimmer_t_reporte >= DIMMER_REPORTE_US) {
        dimmer_t_reporte = ahora;
        consola_printf("dimmer: %lu cambios de duty en 1 s (level %ld)\n",
                       (unsigned long)dimmer_cambios, (long)dimmer_nivel);
        dimmer_cambios = 0;
    }
}

/* ================= COMANDOS ================= */

/* Si la línea empieza con el comando 'cmd' (solo o seguido de espacio),
 * devuelve un puntero a sus argumentos. Si no, NULL. */
static const char *argumento(const char *linea, const char *cmd) {
    size_t n = strlen(cmd);
    if (strncmp(linea, cmd, n) != 0) return NULL;
    if (linea[n] != '\0' && linea[n] != ' ') return NULL;
    const char *p = linea + n;
    while (*p == ' ') p++;
    return p;
}

/* "on" -> 1, "off" -> 0, otra cosa -> -1 */
static int on_off(const char *a) {
    if (strcmp(a, "on")  == 0) return 1;
    if (strcmp(a, "off") == 0) return 0;
    return -1;
}

static void cmd_adc(const char *a) {
    char *fin;
    unsigned long canal = strtoul(a, &fin, 10);
    if (*a == '\0' || *fin != '\0' || canal > 2) {
        consola_printf("uso: adc <0-2>\n");
        return;
    }
    float prom = adc_promedio((uint)canal);
    consola_printf("ADC%lu: codigo = %.1f  V = %.3f V", canal, prom, codigo_a_volts(prom));
    if (cal_activa) {
        float c = corregir_codigo(prom);
        consola_printf("   | calibrado: codigo = %.1f  V = %.3f V", c, codigo_a_volts(c));
    }
    consola_printf("\n");
}

static void cmd_fs(const char *a) {
    char *fin;
    unsigned long v = strtoul(a, &fin, 10);
    if (*a == '\0' || *fin != '\0' || v < 100 || v > 10000) {
        consola_printf("uso: fs <100-10000>\n");
        return;
    }
    fs_hz = (uint32_t)v;
    consola_printf("fs = %lu Hz\n", v);
}

static void cmd_filtro(const char *a) {
    if      (strcmp(a, "ninguno") == 0) filtro = FILTRO_NINGUNO;
    else if (strcmp(a, "media")   == 0) filtro = FILTRO_MEDIA;
    else if (strcmp(a, "ema")     == 0) filtro = FILTRO_EMA;
    else { consola_printf("uso: filtro <ninguno|media|ema>\n"); return; }
    filtro_reset(&filtro_dimmer);        /* el dimmer arranca el filtro de cero */
    consola_printf("filtro = %s\n", a);
}

static void cmd_ps(const char *a) {
    int v = on_off(a);
    if (v < 0) { consola_printf("uso: ps <on|off>\n"); return; }
    gpio_put(PIN_PS, v);                 /* 1: PWM continuo, menos rizado */
    consola_printf("ps %s (GPIO23 = %d)\n", a, v);
}

static void cmd_dimmer(const char *a) {
    int v = on_off(a);
    if (v < 0) { consola_printf("uso: dimmer <on|off>\n"); return; }
    dimmer_activo = (v == 1);
    if (dimmer_activo) dimmer_reiniciar();
    else               led_set_level(0);
    consola_printf("dimmer %s\n", a);
}

/* ---- Extras ---- */

static void cmd_temp(void) {
    adc_set_temp_sensor_enabled(true);
    float prom = adc_promedio(4);
    adc_set_temp_sensor_enabled(false);
    float v = codigo_a_volts(prom);
    float c = 27.0f - (v - 0.706f) / 0.001721f;          /* fórmula del datasheet */
    consola_printf("chip = %.1f C  (codigo %.1f, %.3f V)\n", c, prom, v);
}

static void cmd_vsys(void) {
    float prom = adc_promedio(3);                        /* ADC3 = VSYS / 3 */
    consola_printf("vsys = %.2f V  (codigo %.1f)\n", codigo_a_volts(prom) * 3.0f, prom);
}

/* cal cero | cal ref <volts> | cal reset | cal */
static void cmd_cal(const char *a) {
    if (strcmp(a, "cero") == 0) {
        cal_cero = adc_promedio(0);
        cal_cero_ok = true;
        cal_activa = false;
        consola_printf("cal: codigo con 0 V = %.1f\n", cal_cero);
    } else if (strncmp(a, "ref", 3) == 0 && (a[3] == ' ')) {
        char *fin;
        float v = strtof(a + 4, &fin);
        if (!cal_cero_ok)         { consola_printf("primero: cal cero\n"); return; }
        if (fin == a + 4 || v < 0.5f || v > ADC_VREF) {
            consola_printf("uso: cal ref <volts entre 0.5 y 3.3>\n");
            return;
        }
        float c = adc_promedio(0);
        if (c - cal_cero < 100.0f) { consola_printf("error: el voltaje de referencia es muy bajo\n"); return; }
        cal_ref_codigo = c;
        cal_ref_volts  = v;
        cal_activa = true;
        consola_printf("cal: codigo con %.3f V = %.1f. Calibracion activa\n", v, c);
    } else if (strcmp(a, "reset") == 0) {
        cal_cero_ok = cal_activa = false;
        consola_printf("cal: reiniciada\n");
    } else if (*a == '\0') {
        if (cal_activa) consola_printf("cal: activa (cero = %.1f, %.3f V = %.1f)\n",
                                       cal_cero, cal_ref_volts, cal_ref_codigo);
        else            consola_printf("cal: inactiva\n");
    } else {
        consola_printf("uso: cal cero | cal ref <volts> | cal reset\n");
    }
}

static void procesar(char *linea) {
    const char *a;
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
    else if ((a = argumento(linea, "adc"))    != NULL) cmd_adc(a);
    else if ((a = argumento(linea, "fs"))     != NULL) cmd_fs(a);
    else if ((a = argumento(linea, "filtro")) != NULL) cmd_filtro(a);
    else if ((a = argumento(linea, "ps"))     != NULL) cmd_ps(a);
    else if ((a = argumento(linea, "dimmer")) != NULL) cmd_dimmer(a);
    else if ((a = argumento(linea, "temp"))   != NULL) cmd_temp();
    else if ((a = argumento(linea, "vsys"))   != NULL) cmd_vsys();
    else if ((a = argumento(linea, "cal"))    != NULL) cmd_cal(a);
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
    adc_gpio_init(29);                    /* ADC3: VSYS / 3 (extra vsys) */

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