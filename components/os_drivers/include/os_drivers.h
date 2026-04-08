#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   GPIO Driver
   ──────────────────────────────────────────────── */
#define GPIO_MAX_PIN    39      /* ESP32 highest GPIO */
#define GPIO_SAFE_MASK  0x0000000EFF3FF3FULL  /* valid output-capable pins */

typedef enum {
    GPIO_DIR_INPUT          = 0,
    GPIO_DIR_OUTPUT         = 1,
    GPIO_DIR_INPUT_PULLUP   = 2,
    GPIO_DIR_INPUT_PULLDOWN = 3,
    GPIO_DIR_OUTPUT_OD      = 4,
} gpio_dir_t;

esp_err_t gpio_driver_init(void);
esp_err_t gpio_driver_set_dir(int pin, gpio_dir_t dir);
int       gpio_driver_read(int pin);          /* returns 0/1 or -1 on error */
esp_err_t gpio_driver_write(int pin, int val);
void      gpio_driver_print_info(int fd);

/* ────────────────────────────────────────────────
   ADC Driver
   ──────────────────────────────────────────────── */
#define ADC_MAX_CHANNEL     9   /* ADC1 ch 0-7, plus internal channels */
#define ADC_RESOLUTION_BITS 12
#define ADC_MAX_RAW         4095
#define ADC_VREF_MV         3300

esp_err_t adc_driver_init(void);
int       adc_driver_read_raw(int channel);   /* returns 0-4095 or -1 */
int       adc_driver_read_mv(int channel);    /* returns millivolts or -1 */

/* ────────────────────────────────────────────────
   I2C Driver
   ──────────────────────────────────────────────── */
#define I2C_DEFAULT_SDA      21
#define I2C_DEFAULT_SCL      22
#define I2C_DEFAULT_FREQ_HZ  100000
#define I2C_PORT_NUM         I2C_NUM_0
#define I2C_TIMEOUT_MS       50

esp_err_t i2c_driver_init(int sda_pin, int scl_pin, uint32_t freq_hz);
void      i2c_driver_deinit(void);
void      i2c_driver_scan(int sda, int scl, int fd);
esp_err_t i2c_driver_read(uint8_t dev_addr, uint8_t reg, uint8_t *buf, size_t len);
esp_err_t i2c_driver_write(uint8_t dev_addr, uint8_t reg, const uint8_t *data, size_t len);

/* ────────────────────────────────────────────────
   SPI Driver (basic master)
   ──────────────────────────────────────────────── */
#define SPI_DEFAULT_MOSI  23
#define SPI_DEFAULT_MISO  19
#define SPI_DEFAULT_CLK   18
#define SPI_DEFAULT_CS    5
#define SPI_DEFAULT_FREQ  1000000   /* 1 MHz */

esp_err_t spi_driver_init(int mosi, int miso, int clk, int cs, uint32_t freq_hz);
void      spi_driver_deinit(void);
esp_err_t spi_driver_transfer(const uint8_t *tx, uint8_t *rx, size_t len);

/* ────────────────────────────────────────────────
   UART Driver (additional ports)
   ──────────────────────────────────────────────── */
esp_err_t uart_driver_init_port(int port, int tx, int rx,
                                 int baud, int buf_sz);

/* ────────────────────────────────────────────────
   HAL Init (initialises all drivers)
   ──────────────────────────────────────────────── */
esp_err_t os_hal_init(void);

#ifdef __cplusplus
}
#endif
