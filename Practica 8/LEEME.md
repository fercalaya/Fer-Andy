# Práctica de ADC · Plantilla

## La librería consola

`lib/consola/` trae la librería completa (`consola.h` y `consola.c`), con la función nueva
`consola_flush()`, que espera a que la cola de TX se vacíe. Si prefieren usar su propia
versión, pueden reemplazar `consola.c` con la suya y agregarle `consola_flush()`.

## Qué viene resuelto

| Archivo | Contenido |
| --- | --- |
| `adc_stream.c` / `.h` | Captura de n muestras a una Fs fija y envío por la consola |
| `main.c` | PWM de 1 kHz en GP2, PWM del LED en GP15, comando `stream`, scheduler |
| `graficar.py` | Envía `stream`, guarda un CSV y grafica las muestras con su histograma |
| `lib/consola/` | La librería de consola completa, con `consola_flush()` |

## Qué se programa (marcado con TODO en `main.c`)

- Comandos `adc`, `fs`, `filtro`, `ps` y `dimmer` dentro de `procesar`.
- La función `filtrar`: media móvil de 16 y EMA con alfa = 1/16.
- La tarea `tarea_dimmer`.

## Uso de graficar.py

```
pip install pyserial matplotlib
python graficar.py COM5 "stream 0 1000" --nombre ruido_ps_off --fs 1000
```

El puerto es el del Pico puente. `--fs` pone el eje horizontal en milisegundos.
