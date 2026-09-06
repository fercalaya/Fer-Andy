#include <stdio.h>
#include "pico/stdlib.h"
#include "hardware/gpio.h"
#include "hardware/timer.h"

#define BTN_PIN 16
#define LED_PIN 25 //led integrado en la placa
#define LED_MONITOR_PIN 17 //salida hacia D1 del analizador lógico
#define VENTANA_MS 20 //espera del debounce

static int64_t fin_debounce(alarm_id_t id, void *user_data) {
    if (gpio_get(BTN_PIN) == 0) { //verifica boton presionado
        gpio_xor_mask((1u << LED_PIN) | (1u << LED_MONITOR_PIN)); //conmuta leds
        printf("Boton confirmado -> LED conmutado\n");
    }
    gpio_set_irq_enabled(BTN_PIN, GPIO_IRQ_EDGE_FALL, true); //rehabilita interrupcion
    return 0; //one-shot
}
static void btn_isr(uint gpio, uint32_t events) {
    gpio_set_irq_enabled(BTN_PIN, GPIO_IRQ_EDGE_FALL, false); //apaga interrupcion
    add_alarm_in_ms(VENTANA_MS, fin_debounce, NULL, true); //arma temporizador
    gpio_acknowledge_irq(gpio, events); //limpia interrupcion
}
int main(void) {
    stdio_init_all();
    gpio_init(LED_PIN);
    gpio_set_dir(LED_PIN, GPIO_OUT);
    gpio_put(LED_PIN, 0);
    gpio_init(LED_MONITOR_PIN);
    gpio_set_dir(LED_MONITOR_PIN, GPIO_OUT);
    gpio_put(LED_MONITOR_PIN, 0);
    gpio_init(BTN_PIN);
    gpio_set_dir(BTN_PIN, GPIO_IN);
    gpio_pull_up(BTN_PIN); //activa pull up
    gpio_set_irq_enabled_with_callback(
        BTN_PIN,
        GPIO_IRQ_EDGE_FALL, //flanco de bajada
        true,
        &btn_isr
    );
    printf("Antirrebote listo. Presiona el pulsador.\n");
    while (true) {
        tight_loop_contents();
    }
    return 0;
}