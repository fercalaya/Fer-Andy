#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"
#include "hardware/pwm.h"
#include "hardware/structs/sio.h"

#define GEN_PIN 16 //pin generador pwm
#define SIG_PIN 15 //pin medidor de senal
#define TRAZA_PIN 14 //pin de traza isr

//cambiar según la prueba: 100, 1000 o 10000
#define FREQ_TEST_HZ 10000

static volatile uint32_t t_subida_prev = 0;
static volatile uint32_t t_bajada = 0;
static volatile uint32_t T_ticks = 0;
static volatile uint32_t alto_ticks = 0;
static volatile bool listo = false;

static void sig_isr(uint gpio, uint32_t events) {
    uint32_t t = timer_hw->timerawl; //lee tiempo actual
    sio_hw->gpio_set = (1u << TRAZA_PIN); //sube traza
    if (events & GPIO_IRQ_EDGE_RISE) {
        if (t_subida_prev != 0) {
            T_ticks = t - t_subida_prev; //calcula periodo
            alto_ticks = t_bajada - t_subida_prev; //calcula tiempo en alto
            listo = true; //avisa dato listo
        }
        t_subida_prev = t;
    } else if (events & GPIO_IRQ_EDGE_FALL) {
        t_bajada = t; //guarda flanco de bajada
    }
    sio_hw->gpio_clr = (1u << TRAZA_PIN); //baja traza
    gpio_acknowledge_irq(gpio, events); //limpia interrupcion
}
static void iniciar_generador_pwm(uint pin, uint32_t freq_hz) {
    gpio_set_function(pin, GPIO_FUNC_PWM);
    uint slice = pwm_gpio_to_slice_num(pin);
    pwm_set_clkdiv(slice, 125.0f); //1 tick = 1 µs a 125 MHz
    uint32_t wrap = 1000000 / freq_hz;
    pwm_set_wrap(slice, (uint16_t)(wrap - 1));
    pwm_set_chan_level(slice, pwm_gpio_to_channel(pin), (uint16_t)(wrap / 2));
    pwm_set_enabled(slice, true);
}
int main(void) {
    stdio_init_all();
    gpio_init(TRAZA_PIN);
    gpio_set_dir(TRAZA_PIN, GPIO_OUT);
    gpio_put(TRAZA_PIN, 0);
    gpio_init(SIG_PIN);
    gpio_set_dir(SIG_PIN, GPIO_IN);
    gpio_disable_pulls(SIG_PIN);
    gpio_set_irq_enabled_with_callback(
        SIG_PIN,
        GPIO_IRQ_EDGE_RISE | GPIO_IRQ_EDGE_FALL, //escucha ambos flancos
        true,
        &sig_isr
    );
    iniciar_generador_pwm(GEN_PIN, FREQ_TEST_HZ);
    while (true) {
        if (listo) {
            uint32_t T = T_ticks; //copia local
            uint32_t alto = alto_ticks;
            listo = false;
            if (T > 0) {
                float f_hz = 1e6f / (float)T; //calcula frecuencia
                float duty = 100.0f * (float)alto / (float)T; //calcula duty
                printf("T = %u us | f = %.2f Hz | duty = %.1f %%\n", T, f_hz, duty);
            }
            sleep_ms(200);
        }
    }
    return 0;
}
