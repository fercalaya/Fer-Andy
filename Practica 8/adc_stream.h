/*
 * adc_stream.h — Captura de muestras a una frecuencia fija (DADO)
 */
#ifndef ADC_STREAM_H
#define ADC_STREAM_H

#include <stdbool.h>
#include <stdint.h>
#include "pico/types.h"

#define STREAM_MAX      2000      /* muestras máximas por captura          */
#define STREAM_MAX_US   5000000   /* duración máxima de una captura: 5 s   */

/* Toma n muestras del canal (0 a 2) a fs_hz y las deja en buf.
 * Mientras captura, la Pico solo muestrea: así el tiempo entre muestras es regular.
 * Devuelve false si n, fs_hz o la duración (n / fs_hz) están fuera de rango. */
bool adc_stream_capturar(uint canal, uint32_t n, uint32_t fs_hz, uint16_t *buf);

/* Envía buf por la consola, una muestra por línea: "indice codigo".
 * Espera a que la cola de TX se vacíe después de cada línea (consola_flush). */
void adc_stream_enviar(const uint16_t *buf, uint32_t n);

#endif
