/*
 * consola.h — El menú de la librería.
 * Aquí solo se dice QUÉ funciones existen. El código está en consola.c.
 */
#ifndef CONSOLA_H
#define CONSOLA_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include "hardware/uart.h"

/* Contadores para el comando get stats. */
typedef struct {
    uint32_t overruns;      /* el buzón del hardware (FIFO) se llenó */
    uint32_t framing;       /* errores de trama                      */
    uint32_t descartados;   /* la mesa redonda (buffer) se llenó     */
} consola_stats_t;

/* Configura el UART en 8N1 y sus pines. Devuelve el baud real. */
uint32_t consola_init(uart_inst_t *uart, uint tx_pin, uint rx_pin, uint32_t baud);

/* Arma una línea con eco. Devuelve true cuando llega Enter. Nunca espera. */
bool consola_leer_linea(char *linea, size_t max);

/* Como printf, pero mete el texto a la cola de TX. No bloquea. */
void consola_printf(const char *fmt, ...);

/* Envía lo que haya en la cola de TX. Llamar en cada vuelta del bucle. */
void consola_tarea(void);

/* Espera a que la cola de TX se vacíe. Solo para envíos largos (stream). */
void consola_flush(void);

/* Copia los contadores en s. */
void consola_stats(consola_stats_t *s);

#endif
