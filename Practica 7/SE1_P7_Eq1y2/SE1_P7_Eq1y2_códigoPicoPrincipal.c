#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdbool.h>
#include <math.h>
#include <strings.h>
#include "pico/stdlib.h"
#include "hardware/pwm.h"
#include "hardware/clocks.h"
#include "hardware/uart.h"
#include "hardware/irq.h"

// Configuración de UART
#define UART_ID          uart0
#define BAUD_RATE        115200
#define UART_TX_PIN      0
#define UART_RX_PIN      1
#define UART_IRQ_NUM     UART0_IRQ   // si cambias UART_ID a uart1, usa UART1_IRQ
#define ECHO             1   // 1 = la Pico devuelve lo que escribes; pon 0 si ves letras dobles

// Asignación de pines PWM (Slices dedicados)
#define PIN_LED          2 // Slice 1, Canal A
#define PIN_BUZZER       4 // Slice 2, Canal A
#define PIN_SERVO        6 // Slice 3, Canal A
#define PIN_MOTOR        8 // Slice 4, Canal A -> ENA del L298N (aquí va el PWM)
#define PIN_MOTOR_IN1    9 // IN1 del L298N: fijo en alto (GPIO normal, NO PWM)
#define PIN_MOTOR_IN2    10 // IN2 del L298N: fijo en bajo (GPIO normal)

#define LED_FREQ_HZ      1000.0f
#define SERVO_FREQ_HZ    50.0f
#define MOTOR_FREQ_HZ    20000.0f
#define BUZZER_DIV       10.0f

// Buffer Circular UART
#define BUFFER_SIZE      256
static volatile char rx_buffer[BUFFER_SIZE];
static volatile uint16_t head = 0;
static volatile uint16_t tail = 0;

static inline bool ring_buffer_is_empty(void) {
    return head == tail;
}

static inline void ring_buffer_push(char c) {
    uint16_t next = (head + 1) % BUFFER_SIZE;
    if (next != tail) {
        rx_buffer[head] = c;
        head = next;
    }
}

static inline char ring_buffer_pop(void) {
    char c = rx_buffer[tail];
    tail = (tail + 1) % BUFFER_SIZE;
    return c;
}

void on_uart_rx(void) {
    while (uart_is_readable(UART_ID)) {
        char c = uart_getc(UART_ID);
        ring_buffer_push(c);
    }
}

static void uart_send_str(const char *str) {
    while (*str) {
        uart_putc(UART_ID, *str++);
    }
}

// Estructuras de canales PWM
typedef struct {
    uint pin;
    uint slice;
    uint channel;
    float div;
    uint32_t top;
    uint32_t level;
    float target_freq;
    float real_freq;
} pwm_channel_t;

static pwm_channel_t pwm_led;
static pwm_channel_t pwm_buzzer;
static pwm_channel_t pwm_servo;
static pwm_channel_t pwm_motor;

static float current_duty_motor = 0.0f;
static uint32_t current_pulse_servo = 1500;
static float current_freq_buzzer = 0.0f;   // 0 = silencio

// Calibración Servo (REEMPLAZA con los extremos reales que midas en 2.3)
static float cal_us_min = 1000.0f;
static float cal_us_max = 2000.0f;

// Tabla gamma para el fade: índice de brillo percibido (0..255) -> level del LED (D = B^2.2)
static uint16_t gamma_lut[256];

// Extras concurrentes sin sleep_ms
static bool fade_active = false;
static uint64_t fade_start_time = 0;
static uint32_t fade_total_duration_us = 0;

static bool melody_active = false;
static uint melody_step = 0;
static uint64_t melody_step_time = 0;

typedef struct {
    const char *name;
    float freq;
} note_entry_t;

static const note_entry_t note_table[] = {
    {"do4",      261.63f},
    {"re4",      293.66f},
    {"mi4",      329.63f},
    {"fa4",      349.23f},
    {"sol4",     392.00f},
    {"la4",      440.00f},
    {"si4",      493.88f},
    {"do5",      523.25f},
    {"silencio", 0.0f}
};
#define TOTAL_NOTES (sizeof(note_table) / sizeof(note_table[0]))

typedef struct {
    float freq;
    uint32_t duration_ms;
} melody_note_t;

// Duraciones distintas y ninguna nota repetida seguida (así no hay cortes ni silencios entre ellas)
static const melody_note_t melody_sequence[] = {
    {261.63f, 400}, {329.63f, 400}, {392.00f, 400}, {523.25f, 800},
    {392.00f, 400}, {329.63f, 400}, {293.66f, 400}, {349.23f, 400},
    {440.00f, 400}, {392.00f, 800}, {329.63f, 200}, {392.00f, 200},
    {523.25f, 800}
};
#define MELODY_NOTES_COUNT (sizeof(melody_sequence) / sizeof(melody_sequence[0]))

// Parser numérico estricto: rechaza "hola", "12abc", "nan", etc. (atof los convertía en 0)
static bool parse_float(const char *s, float *out) {
    char *end;
    float v = strtof(s, &end);
    if (end == s || *end != '\0' || !isfinite(v)) return false;
    *out = v;
    return true;
}

// Nombre de la nota más cercana a una frecuencia (La4 = 440 Hz). cents = desviación (100 = un semitono)
static void note_name(float hz, char *out, size_t n, int *cents) {
    static const char *names[12] = {"Do", "Do#", "Re", "Re#", "Mi", "Fa",
                                    "Fa#", "Sol", "Sol#", "La", "La#", "Si"};
    float midi = 69.0f + 12.0f * log2f(hz / 440.0f);
    int m = (int)lroundf(midi);
    *cents = (int)lroundf((midi - (float)m) * 100.0f);
    snprintf(out, n, "%s%d", names[((m % 12) + 12) % 12], m / 12 - 1);
}

void config_pwm_slice(pwm_channel_t *p, uint pin, float freq, float fixed_div) {
    p->pin = pin;
    p->slice = pwm_gpio_to_slice_num(pin);
    p->channel = pwm_gpio_to_channel(pin);
    p->target_freq = freq;

    gpio_set_function(pin, GPIO_FUNC_PWM);
    float f_sys = (float)clock_get_hz(clk_sys);

    if (fixed_div > 0.0f) {
        p->div = fixed_div;
        p->top = (uint32_t)roundf(f_sys / (p->div * freq)) - 1;
    } else {
        p->div = 1.0f;
        p->top = (uint32_t)roundf(f_sys / (p->div * freq)) - 1;
        while (p->top > 65535 && p->div < 256.0f) {
            p->div += 1.0f;
            p->top = (uint32_t)roundf(f_sys / (p->div * freq)) - 1;
        }
    }

    p->real_freq = f_sys / (p->div * (p->top + 1));
    p->level = 0;

    pwm_set_clkdiv(p->slice, p->div);
    pwm_set_wrap(p->slice, p->top);
    pwm_set_chan_level(p->slice, p->channel, 0);
    pwm_set_enabled(p->slice, true);
}

// Escribe el level del LED y lo recuerda (así "estado" muestra el duty real, también durante el fade)
static void led_set_level(uint32_t lvl) {
    if (lvl > pwm_led.top + 1) lvl = pwm_led.top + 1;
    pwm_led.level = lvl;
    pwm_set_chan_level(pwm_led.slice, pwm_led.channel, lvl);
}

void set_buzzer_freq(float freq) {
    if (freq <= 0.0f) {
        pwm_buzzer.level = 0;
        pwm_set_chan_level(pwm_buzzer.slice, pwm_buzzer.channel, 0);
        current_freq_buzzer = 0.0f;
        pwm_buzzer.real_freq = 0.0f;
        return;
    }

    float f_sys = (float)clock_get_hz(clk_sys);
    pwm_buzzer.target_freq = freq;
    pwm_buzzer.div = BUZZER_DIV;
    pwm_buzzer.top = (uint32_t)roundf(f_sys / (pwm_buzzer.div * freq)) - 1;
    if (pwm_buzzer.top > 65535) pwm_buzzer.top = 65535;

    pwm_buzzer.level = (pwm_buzzer.top + 1) / 2;
    pwm_buzzer.real_freq = f_sys / (pwm_buzzer.div * (pwm_buzzer.top + 1));

    // TOP y level cambian juntos: level = 0 primero para que nunca quede level > TOP
    // (si no, la salida se queda en alto y el buzzer se calla), y se reinicia el contador.
    pwm_set_chan_level(pwm_buzzer.slice, pwm_buzzer.channel, 0);
    pwm_set_wrap(pwm_buzzer.slice, pwm_buzzer.top);
    pwm_set_counter(pwm_buzzer.slice, 0);
    pwm_set_chan_level(pwm_buzzer.slice, pwm_buzzer.channel, pwm_buzzer.level);
    current_freq_buzzer = freq;
}

void task_fade(void) {
    if (!fade_active) return;

    uint64_t elapsed = time_us_64() - fade_start_time;
    if (elapsed >= fade_total_duration_us) {
        fade_active = false;
        led_set_level(0);
        return;
    }

    float half_period = (float)fade_total_duration_us / 2.0f;
    float norm_bright;
    if ((float)elapsed <= half_period) {
        norm_bright = (float)elapsed / half_period;
    } else {
        norm_bright = 1.0f - (((float)elapsed - half_period) / half_period);
    }

    int idx = (int)(norm_bright * 255.0f + 0.5f);
    if (idx < 0) idx = 0;
    if (idx > 255) idx = 255;
    led_set_level(gamma_lut[idx]);   // tabla gamma precalculada
}

void task_melody(void) {
    if (!melody_active) return;

    uint64_t now = time_us_64();
    if (now >= melody_step_time) {
        if (melody_step < MELODY_NOTES_COUNT) {
            set_buzzer_freq(melody_sequence[melody_step].freq);
            // se acumula sobre el tiempo previsto (no sobre "now") para que el ritmo no se desfase
            melody_step_time += (uint64_t)melody_sequence[melody_step].duration_ms * 1000;
            melody_step++;
        } else {
            set_buzzer_freq(0.0f);
            melody_active = false;
        }
    }
}

void process_command(char *cmd) {
    char name[32] = "";
    char arg[32] = "";
    char extra[32] = "";
    char out_buf[128];
    // Anchos máximos en el formato: nunca se desborda aunque la línea sea larga
    int parsed = sscanf(cmd, "%31s %31s %31s", name, arg, extra);
    if (parsed <= 0) return;   // línea vacía o solo espacios

    float val;

    if (strcasecmp(name, "estado") == 0 && parsed == 1) {
        float duty_led = (float)pwm_led.level * 100.0f / (float)(pwm_led.top + 1);
        snprintf(out_buf, sizeof(out_buf), "LED: Duty=%.1f%%, FreqTarget=%.1fHz, FreqReal=%.1fHz\r\n",
                 duty_led, pwm_led.target_freq, pwm_led.real_freq);
        uart_send_str(out_buf);

        if (current_freq_buzzer <= 0.0f) {
            uart_send_str("BUZZER: silencio\r\n");
        } else {
            char nota[8];
            int cents;
            note_name(pwm_buzzer.real_freq, nota, sizeof(nota), &cents);   // nota según la f REAL
            snprintf(out_buf, sizeof(out_buf), "BUZZER: %s (%+d cents)%s\r\n",
                     nota, cents, melody_active ? " [melodia]" : "");
            uart_send_str(out_buf);
        }

        snprintf(out_buf, sizeof(out_buf), "SERVO: Pulso=%uus, FreqTarget=%.1fHz, FreqReal=%.1fHz\r\n",
                 (unsigned)current_pulse_servo, pwm_servo.target_freq, pwm_servo.real_freq);
        uart_send_str(out_buf);

        snprintf(out_buf, sizeof(out_buf), "MOTOR: Duty=%.1f%%, FreqTarget=%.1fHz, FreqReal=%.1fHz\r\n",
                 current_duty_motor, pwm_motor.target_freq, pwm_motor.real_freq);
        uart_send_str(out_buf);
        return;
    }

    if (strcasecmp(name, "led") == 0 && parsed == 2) {
        if (parse_float(arg, &val) && val >= 0.0f && val <= 100.0f) {
            fade_active = false;
            led_set_level((uint32_t)roundf((val / 100.0f) * (pwm_led.top + 1)));
            uart_send_str("ok\r\n");
        } else {
            uart_send_str("Error: Duty de LED invalido o fuera de rango (0 a 100)\r\n");
        }
    } else if (strcasecmp(name, "tono") == 0 && parsed == 2) {
        // 1) Interpretar el argumento (nombre de nota o número) SIN tocar el estado todavía
        float f_target = 0.0f;
        bool es_nota = false;
        for (size_t i = 0; i < TOTAL_NOTES; i++) {
            if (strcasecmp(arg, note_table[i].name) == 0) {
                f_target = note_table[i].freq;
                es_nota = true;
                break;
            }
        }
        if (!es_nota && !parse_float(arg, &f_target)) {
            uart_send_str("Error: Tono invalido (usa Hz, 0 o una nota como la4)\r\n");
            return;
        }

        // 2) Validar rango y 3) solo entonces aplicar
        float f_sys = (float)clock_get_hz(clk_sys);
        float min_hz = f_sys / (BUZZER_DIV * 65536.0f);

        if (f_target == 0.0f || (f_target >= min_hz && f_target <= 5000.0f)) {
            melody_active = false;
            set_buzzer_freq(f_target);
            uart_send_str("ok\r\n");
        } else {
            snprintf(out_buf, sizeof(out_buf), "Error: Tono fuera de rango (%.1f a 5000 Hz o nota valida)\r\n", min_hz);
            uart_send_str(out_buf);
        }
    } else if (strcasecmp(name, "servo") == 0 && parsed == 2) {
        if (parse_float(arg, &val) && val >= 500.0f && val <= 2500.0f) {
            current_pulse_servo = (uint32_t)roundf(val);
            pwm_set_chan_level(pwm_servo.slice, pwm_servo.channel, current_pulse_servo);
            uart_send_str("ok\r\n");
        } else {
            uart_send_str("Error: Pulso de servo invalido o fuera de rango (500 a 2500 us)\r\n");
        }
    } else if (strcasecmp(name, "motor") == 0 && parsed == 2) {
        if (parse_float(arg, &val) && val >= 0.0f && val <= 100.0f) {
            current_duty_motor = val;
            uint32_t lvl = (uint32_t)roundf((val / 100.0f) * (pwm_motor.top + 1));
            if (lvl > (pwm_motor.top + 1)) lvl = pwm_motor.top + 1;
            pwm_motor.level = lvl;
            pwm_set_chan_level(pwm_motor.slice, pwm_motor.channel, lvl);
            uart_send_str("ok\r\n");
        } else {
            uart_send_str("Error: Duty de motor invalido o fuera de rango (0 a 100)\r\n");
        }
    } else if (strcasecmp(name, "angulo") == 0 && parsed == 2) {
        if (parse_float(arg, &val) && val >= 0.0f && val <= 180.0f) {
            float pulse = cal_us_min + (val * (cal_us_max - cal_us_min) / 180.0f);
            current_pulse_servo = (uint32_t)roundf(pulse);
            pwm_set_chan_level(pwm_servo.slice, pwm_servo.channel, current_pulse_servo);
            uart_send_str("ok\r\n");
        } else {
            uart_send_str("Error: Angulo invalido o fuera de rango (0 a 180)\r\n");
        }
    } else if (strcasecmp(name, "fade") == 0 && parsed == 2) {
        if (parse_float(arg, &val) && val >= 100.0f && val <= 60000.0f) {
            fade_total_duration_us = (uint32_t)(val * 1000.0f);
            fade_start_time = time_us_64();
            fade_active = true;
            uart_send_str("ok\r\n");
        } else {
            uart_send_str("Error: Duracion de fade invalida (100 a 60000 ms)\r\n");
        }
    } else if (strcasecmp(name, "melodia") == 0 && parsed == 1) {
        melody_step = 0;
        melody_step_time = time_us_64();
        melody_active = true;
        uart_send_str("ok\r\n");
    } else {
        uart_send_str("Error: Comando desconocido o formato invalido\r\n");
    }
}

int main() {
    uart_init(UART_ID, BAUD_RATE);
    gpio_set_function(UART_TX_PIN, GPIO_FUNC_UART);
    gpio_set_function(UART_RX_PIN, GPIO_FUNC_UART);
    uart_set_hw_flow(UART_ID, false, false);
    uart_set_format(UART_ID, 8, 1, UART_PARITY_NONE);
    uart_set_fifo_enabled(UART_ID, true);

    irq_set_exclusive_handler(UART_IRQ_NUM, on_uart_rx);
    irq_set_enabled(UART_IRQ_NUM, true);
    uart_set_irq_enables(UART_ID, true, false);

    float f_sys = (float)clock_get_hz(clk_sys);
    float div_led = f_sys / (LED_FREQ_HZ * 1000.0f);
    float div_servo = f_sys / 1000000.0f; // 1 tick = 1 us

    config_pwm_slice(&pwm_led, PIN_LED, LED_FREQ_HZ, div_led);
    config_pwm_slice(&pwm_buzzer, PIN_BUZZER, 440.0f, BUZZER_DIV);
    pwm_set_chan_level(pwm_buzzer.slice, pwm_buzzer.channel, 0);
    config_pwm_slice(&pwm_servo, PIN_SERVO, SERVO_FREQ_HZ, div_servo);
    pwm_set_chan_level(pwm_servo.slice, pwm_servo.channel, 1500);
    // Puente H L298N: dirección fija (IN1 = 1, IN2 = 0) y la velocidad la da el PWM en ENA.
    // Se fijan ANTES de arrancar el PWM; ENA arranca con level = 0, así que el motor queda parado.
    gpio_init(PIN_MOTOR_IN1);
    gpio_set_dir(PIN_MOTOR_IN1, GPIO_OUT);
    gpio_put(PIN_MOTOR_IN1, 1);
    gpio_init(PIN_MOTOR_IN2);
    gpio_set_dir(PIN_MOTOR_IN2, GPIO_OUT);
    gpio_put(PIN_MOTOR_IN2, 0);
    config_pwm_slice(&pwm_motor, PIN_MOTOR, MOTOR_FREQ_HZ, 1.0f);

    // Tabla gamma (D = B^2.2), calculada una sola vez
    for (int i = 0; i < 256; i++) {
        gamma_lut[i] = (uint16_t)roundf(powf((float)i / 255.0f, 2.2f) * (float)(pwm_led.top + 1));
    }

    char line[64];
    uint idx = 0;

    while (true) {
        task_fade();
        task_melody();

        while (!ring_buffer_is_empty()) {
            char c = ring_buffer_pop();
            if (c == '\r' || c == '\n') {
                if (idx > 0) {
                    line[idx] = '\0';
                    idx = 0;
                    if (ECHO) uart_send_str("\r\n");
                    process_command(line);
                }
            } else if (c == '\b' || c == 127) {
                if (idx > 0) {
                    idx--;
                    if (ECHO) uart_send_str("\b \b");
                }
            } else if (c >= 32 && c < 127) {
                if (idx < sizeof(line) - 1) {
                    line[idx++] = c;
                    if (ECHO) uart_putc(UART_ID, c);
                }
            }
        }
    }

    return 0;
}