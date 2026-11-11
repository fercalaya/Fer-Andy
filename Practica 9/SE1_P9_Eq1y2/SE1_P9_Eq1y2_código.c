#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "pico/stdlib.h"
#include "hardware/i2c.h"

/* ---------------- Configuración ---------------- */
#define I2C_PORT          i2c0
#define SDA_PIN           4
#define SCL_PIN           5
#define I2C_BAUD_INICIAL  100000
#define I2C_TIMEOUT_US    1000          // Máx. 1 ms por transferencia

#define REG_PWR_MGMT_1    0x6B
#define REG_GYRO_CONFIG   0x1B
#define REG_WHO_AM_I      0x75
#define REG_GYRO_XOUT_H   0x43          // 0x43 a 0x48 (X, Y, Z)

#define GYRO_SENS         131.0f        // cuentas por °/s en ±250 °/s (diapositiva 22)
#define N_CAL             200           // lecturas para calibrar
#define DT_SEC            0.010f        // 10 ms exactos (diapositiva 23)

/* ---------------- Estado global ---------------- */
static uint8_t  mpu_addr  = 0x68;
static uint32_t baud_real = 0;
static uint32_t n_nack    = 0;
static uint32_t n_timeout = 0;

static bool     tarea_activa   = true;
static bool     calibrando     = false;

// Variables para los 3 ejes (X, Y, Z)
static int32_t  cal_suma_x     = 0, cal_suma_y     = 0, cal_suma_z     = 0;
static int      cal_n          = 0;
static float    offset_x       = 0.0f, offset_y    = 0.0f, offset_z    = 0.0f;
static float    angulo_x       = 0.0f, angulo_y    = 0.0f, angulo_z    = 0.0f;

static uint32_t t_tarea        = 0;

/* ---------------- Utilidades de error ---------------- */
static void contar_error(int r) {
    if (r == PICO_ERROR_TIMEOUT) n_timeout++;
    else                         n_nack++;
}

static const char *nombre_error(int r) {
    if (r == PICO_ERROR_TIMEOUT) return "TIMEOUT: bus atorado o sin pull-up";
    if (r == PICO_ERROR_GENERIC) return "NACK: nadie respondio";
    return "transferencia incompleta";
}

/* ---------------- Acceso al MPU6050 ---------------- */
static int mpu_escribir(uint8_t reg, uint8_t valor) {
    uint8_t d[2] = { reg, valor };
    int r = i2c_write_timeout_us(I2C_PORT, mpu_addr, d, 2, false, I2C_TIMEOUT_US);
    if (r < 0) contar_error(r);
    return r;
}

static int mpu_leer(uint8_t reg, uint8_t *buf, size_t n) {
    int r = i2c_write_timeout_us(I2C_PORT, mpu_addr, &reg, 1, true, I2C_TIMEOUT_US);
    if (r < 0) { contar_error(r); return r; }
    r = i2c_read_timeout_us(I2C_PORT, mpu_addr, buf, n, false, I2C_TIMEOUT_US);
    if (r < 0) contar_error(r);
    return r;
}

/* Lee los 3 Ejes (X, Y, Z) del Giroscopio (diapositiva 50) */
static int leer_gyro_xyz(int16_t *gx, int16_t *gy, int16_t *gz) {
    uint8_t d[6];
    int r = mpu_leer(REG_GYRO_XOUT_H, d, 6);
    if (r != 6) return (r < 0) ? r : PICO_ERROR_GENERIC;

    *gx = (int16_t)(((uint16_t)d[0] << 8) | d[1]); // Eje X (0x43, 0x44)
    *gy = (int16_t)(((uint16_t)d[2] << 8) | d[3]); // Eje Y (0x45, 0x46)
    *gz = (int16_t)(((uint16_t)d[4] << 8) | d[5]); // Eje Z (0x47, 0x48)
    return 0;
}

/* ---------------- Tarea continua de integración (cada 10 ms) ---------------- */
static void tarea_giro(void) {
    uint32_t ahora = time_us_32();
    if (ahora - t_tarea < 10000) return;  // 10 ms exactos
    t_tarea = ahora;

    if (!tarea_activa) return;

    int16_t gx, gy, gz;
    if (leer_gyro_xyz(&gx, &gy, &gz) != 0) return;

    if (calibrando) {
        cal_suma_x += gx;
        cal_suma_y += gy;
        cal_suma_z += gz;
        if (++cal_n >= N_CAL) {
            offset_x = (float)cal_suma_x / N_CAL;
            offset_y = (float)cal_suma_y / N_CAL;
            offset_z = (float)cal_suma_z / N_CAL;
            calibrando = false;
            angulo_x = 0.0f; angulo_y = 0.0f; angulo_z = 0.0f;
            printf("\ncal lista de los 3 ejes:\n  Offset X: %.1f | Y: %.1f | Z: %.1f cuentas\n> ",
                   offset_x, offset_y, offset_z);
            fflush(stdout);
        }
        return;
    }

    // Integración independiente para X, Y y Z
    float giro_x = (gx - offset_x) / GYRO_SENS;
    float giro_y = (gy - offset_y) / GYRO_SENS;
    float giro_z = (gz - offset_z) / GYRO_SENS;

    angulo_x += giro_x * DT_SEC;
    angulo_y += giro_y * DT_SEC;
    angulo_z += giro_z * DT_SEC;
}

/* ---------------- Comandos ---------------- */
static void cmd_scan(void) {
    printf("Buscando de 0x08 a 0x77...\n");
    int n = 0;
    for (uint8_t dir = 0x08; dir <= 0x77; dir++) {
        uint8_t dato;
        if (i2c_read_timeout_us(I2C_PORT, dir, &dato, 1, false, I2C_TIMEOUT_US) >= 0) {
            printf("Encontrado: 0x%02X\n", dir);
            n++;
        }
    }
    printf("Listo: %d dispositivo(s)\n", n);
}

static void cmd_whoami(void) {
    uint8_t id;
    int r = mpu_leer(REG_WHO_AM_I, &id, 1);
    if (r == 1) printf("WHO_AM_I = 0x%02X (direccion usada 0x%02X)\n", id, mpu_addr);
    else        printf("error: %d (%s)\n", r, nombre_error(r));
}

static void cmd_wake(void) {
    int r1 = mpu_escribir(REG_PWR_MGMT_1, 0x00);
    sleep_ms(10);
    int r2 = mpu_escribir(REG_GYRO_CONFIG, 0x00);

    if (r1 == 2 && r2 == 2) {
        tarea_activa = true;
        t_tarea      = time_us_32();
        printf("ok\n");
    } else {
        printf("error: %d (%s)\n", r1 < 0 ? r1 : r2, nombre_error(r1 < 0 ? r1 : r2));
    }
}

static void cmd_gyro(void) {
    int16_t gx, gy, gz;
    if (leer_gyro_xyz(&gx, &gy, &gz) != 0) { printf("error al leer gyro\n"); return; }
    printf("Giro X: %6.2f °/s | Giro Y: %6.2f °/s | Giro Z: %6.2f °/s\n",
           (gx - offset_x) / GYRO_SENS,
           (gy - offset_y) / GYRO_SENS,
           (gz - offset_z) / GYRO_SENS);
}

static void cmd_cal(void) {
    cal_suma_x = 0; cal_suma_y = 0; cal_suma_z = 0;
    cal_n = 0; calibrando = true;
    printf("Calibrando 3 ejes (%d lecturas ~2 s): no muevas el sensor...\n", N_CAL);
}

static void cmd_vel(const char *arg) {
    int khz = arg ? atoi(arg) : 0;
    if (khz != 100 && khz != 400) { printf("uso: vel <100|400>\n"); return; }
    baud_real = i2c_set_baudrate(I2C_PORT, (uint)khz * 1000);
    printf("pedido: %d kHz, real: %lu Hz\n", khz, (unsigned long)baud_real);
}

static void cmd_addr(const char *arg) {
    if (!arg) { printf("direccion actual: 0x%02X\n", mpu_addr); return; }
    mpu_addr = (uint8_t)strtol(arg, NULL, 16);
    printf("direccion = 0x%02X\n", mpu_addr);
}

static void procesar_comando(char *linea) {
    char *cmd = strtok(linea, " \t");
    char *arg = strtok(NULL, " \t");
    if (!cmd) return;

    if      (!strcmp(cmd, "scan"))    cmd_scan();
    else if (!strcmp(cmd, "whoami"))  cmd_whoami();
    else if (!strcmp(cmd, "wake"))    cmd_wake();
    else if (!strcmp(cmd, "gyro"))    cmd_gyro();
    else if (!strcmp(cmd, "cal"))     cmd_cal();
    else if (!strcmp(cmd, "angulo"))  printf("Angulo X: %6.2f° | Angulo Y: %6.2f° | Angulo Z: %6.2f°\n", 
                                             angulo_x, angulo_y, angulo_z);
    else if (!strcmp(cmd, "cero"))    { angulo_x = 0.0f; angulo_y = 0.0f; angulo_z = 0.0f; printf("angulos reset a 0\n"); }
    else if (!strcmp(cmd, "vel"))     cmd_vel(arg);
    else if (!strcmp(cmd, "errores")) printf("NACK: %lu  timeouts: %lu\n",
                                             (unsigned long)n_nack, (unsigned long)n_timeout);
    else if (!strcmp(cmd, "addr"))    cmd_addr(arg);
    else printf("error: comando desconocido\n");
}

/* ---------------- Lectura de Consola USB ---------------- */
static char buf_linea[48];
static int  len_linea = 0;

static void leer_comandos_usb(void) {
    int c = getchar_timeout_us(0);
    while (c != PICO_ERROR_TIMEOUT) {
        if (c == '\r' || c == '\n') {
            if (len_linea > 0) {
                buf_linea[len_linea] = '\0';
                printf("\n");
                procesar_comando(buf_linea);
                len_linea = 0;
                printf("> ");
                fflush(stdout);
            }
        } else if ((c == 8 || c == 127) && len_linea > 0) {
            len_linea--;
            printf("\b \b");
            fflush(stdout);
        } else if (c >= 32 && c < 127 && len_linea < (int)sizeof(buf_linea) - 1) {
            buf_linea[len_linea++] = c;
            putchar(c);
            fflush(stdout);
        }
        c = getchar_timeout_us(0);
    }
}

/* ---------------- Main ---------------- */
int main(void) {
    stdio_init_all();

    baud_real = i2c_init(I2C_PORT, I2C_BAUD_INICIAL);
    gpio_set_function(SDA_PIN, GPIO_FUNC_I2C);
    gpio_set_function(SCL_PIN, GPIO_FUNC_I2C);

    sleep_ms(2000);

    mpu_escribir(REG_PWR_MGMT_1, 0x00);
    sleep_ms(10);
    mpu_escribir(REG_GYRO_CONFIG, 0x00);

    printf("\n--- Practica I2C MPU6050 (Lectura en 3 Ejes) ---\n> ");
    fflush(stdout);

    t_tarea = time_us_32();
    while (true) {
        tarea_giro();
        leer_comandos_usb();
    }
}