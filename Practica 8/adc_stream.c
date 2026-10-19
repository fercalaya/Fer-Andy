/*
 * adc_stream.c — Captura de muestras a una frecuencia fija (DADO)
 */
#include "adc_stream.h"
#include "pico/stdlib.h"
#include "hardware/adc.h"
#include "consola.h"

bool adc_stream_capturar(uint canal, uint32_t n, uint32_t fs_hz, uint16_t *buf) {
    if (canal > 2 || n == 0 || n > STREAM_MAX || fs_hz < 100 || fs_hz > 10000) return false;
    uint32_t periodo_us = 1000000u / fs_hz;
    if ((uint64_t)n * periodo_us > STREAM_MAX_US) return false;

    adc_select_input(canal);
    uint32_t t = time_us_32();
    for (uint32_t i = 0; i < n; i++) {
        while (time_us_32() - t < periodo_us) { }   /* esperar a la siguiente muestra   */
        t += periodo_us;                            /* el ritmo no se atrasa            */
        buf[i] = adc_read();
    }
    return true;
}

void adc_stream_enviar(const uint16_t *buf, uint32_t n) {
    for (uint32_t i = 0; i < n; i++) {
        consola_printf("%lu %u\n", (unsigned long)i, buf[i]);
        consola_flush();                            /* sin esto, la cola de TX se desborda */
    }
}
