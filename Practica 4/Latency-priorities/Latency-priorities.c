#include "pico/stdlib.h"
#include "hardware/gpio.h"

#define LED_PIN 15    // Pin del led externo
#define BTN_PIN 16    // Pin del boton
#define TRAZA_PIN 14  // Pin de traza para osciloscopio

// funcion de interrupcion del boton
static void button_isr(uint gpio, uint32_t events) {
    // inicia la señal de prueba
    gpio_put(TRAZA_PIN, 1);

    // conmuta el estado del led directamente en cada flanco detectado
    gpio_xor_mask(1u << LED_PIN);

    // reconoce y limpia la interrupcion que ya fue atendida
    gpio_acknowledge_irq(gpio, events);

    // termina la señal de prueba
    gpio_put(TRAZA_PIN, 0);
}

int main(void) {
    // inicia la pico
    stdio_init_all();

    // configura el led externo
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);

    // configura el pin de prueba
    gpio_init(TRAZA_PIN);
    gpio_set_dir(TRAZA_PIN, GPIO_OUT);
    gpio_put(TRAZA_PIN, 0);

    // configura el boton
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_disable_pulls(BTN_PIN);

    // detecta tanto flanco de bajada como de subida para responder sin importar como cierre el boton en el circuito
    gpio_set_irq_enabled_with_callback(
        BTN_PIN,
        GPIO_IRQ_EDGE_FALL | GPIO_IRQ_EDGE_RISE,
        true,
        &button_isr
    );

    // espera las interrupciones
    while (true) {
        tight_loop_contents();
    }
}