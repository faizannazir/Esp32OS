#include "os_drivers.h"
#include "os_logging.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "driver/gpio.h"
#include "driver/i2c.h"
#include "driver/spi_master.h"
#include "driver/uart.h"
#include "esp_adc/adc_cali.h"
#include "esp_adc/adc_cali_scheme.h"
#include "esp_adc/adc_oneshot.h"
#include "lwip/sockets.h"

#define TAG "HAL"

/* ────────────────────────────────────────────────
   FD printf helper
   ──────────────────────────────────────────────── */
static void fd_printf(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));
static void fd_printf(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    size_t len = strlen(buf);
    if (fd < 0) uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, buf, len);
    else         send(fd, buf, len, 0);
}

/* ════════════════════════════════════════════════
   GPIO DRIVER
   ════════════════════════════════════════════════ */
static struct {
    uint8_t  dir[GPIO_MAX_PIN + 1];   /* 0=in,1=out */
    bool     initialised;
} s_gpio = { .initialised = false };

esp_err_t gpio_driver_init(void)
{
    memset(s_gpio.dir, 0, sizeof(s_gpio.dir));
    s_gpio.initialised = true;
    OS_LOGD(TAG, "GPIO driver ready");
    return ESP_OK;
}

static bool gpio_pin_valid(int pin)
{
    return (pin >= 0 && pin <= GPIO_MAX_PIN);
}

esp_err_t gpio_driver_set_dir(int pin, gpio_dir_t dir)
{
    if (!gpio_pin_valid(pin)) return ESP_ERR_INVALID_ARG;

    gpio_config_t cfg = {
        .pin_bit_mask = (1ULL << pin),
        .intr_type    = GPIO_INTR_DISABLE,
    };

    switch (dir) {
    case GPIO_DIR_OUTPUT:
        cfg.mode      = GPIO_MODE_OUTPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case GPIO_DIR_OUTPUT_OD:
        cfg.mode      = GPIO_MODE_OUTPUT_OD;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case GPIO_DIR_INPUT:
        cfg.mode      = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case GPIO_DIR_INPUT_PULLUP:
        cfg.mode      = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_ENABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_DISABLE;
        break;
    case GPIO_DIR_INPUT_PULLDOWN:
        cfg.mode      = GPIO_MODE_INPUT;
        cfg.pull_up_en   = GPIO_PULLUP_DISABLE;
        cfg.pull_down_en = GPIO_PULLDOWN_ENABLE;
        break;
    default:
        return ESP_ERR_INVALID_ARG;
    }

    esp_err_t ret = gpio_config(&cfg);
    if (ret == ESP_OK) {
        s_gpio.dir[pin] = (dir == GPIO_DIR_OUTPUT || dir == GPIO_DIR_OUTPUT_OD) ? 1 : 0;
    }
    return ret;
}

int gpio_driver_read(int pin)
{
    if (!gpio_pin_valid(pin)) return -1;
    return gpio_get_level((gpio_num_t)pin);
}

esp_err_t gpio_driver_write(int pin, int val)
{
    if (!gpio_pin_valid(pin)) return ESP_ERR_INVALID_ARG;
    /* Auto-configure as output if not already */
    if (!s_gpio.dir[pin]) {
        esp_err_t r = gpio_driver_set_dir(pin, GPIO_DIR_OUTPUT);
        if (r != ESP_OK) return r;
    }
    return gpio_set_level((gpio_num_t)pin, val ? 1 : 0);
}

void gpio_driver_print_info(int fd)
{
    fd_printf(fd, "\r\n%-6s %-8s %-6s\r\n", "PIN", "DIR", "LEVEL");
    fd_printf(fd, "%-6s %-8s %-6s\r\n",    "---", "---", "-----");
    for (int i = 0; i <= GPIO_MAX_PIN; i++) {
        /* Skip input-only / special pins */
        if (i == 6 || i == 7 || i == 8 || i == 9 || i == 10 || i == 11) continue;
        int level = gpio_get_level((gpio_num_t)i);
        fd_printf(fd, "GPIO%-2d %-8s %-6d\r\n",
                  i,
                  s_gpio.dir[i] ? "OUTPUT" : "INPUT",
                  level);
    }
    fd_printf(fd, "\r\n");
}

/* ════════════════════════════════════════════════
   ADC DRIVER
   ════════════════════════════════════════════════ */
static adc_oneshot_unit_handle_t s_adc_unit = NULL;
static adc_cali_handle_t s_adc_cali = NULL;
static bool s_adc_init = false;

/* Mapping: logical channel → ADC1 hardware channel */
static const adc_channel_t ADC_CH_MAP[] = {
    ADC_CHANNEL_0,   /* ch0 → GPIO36 */
    ADC_CHANNEL_1,   /* ch1 → GPIO37 */
    ADC_CHANNEL_2,   /* ch2 → GPIO38 */
    ADC_CHANNEL_3,   /* ch3 → GPIO39 */
    ADC_CHANNEL_4,   /* ch4 → GPIO32 */
    ADC_CHANNEL_5,   /* ch5 → GPIO33 */
    ADC_CHANNEL_6,   /* ch6 → GPIO34 */
    ADC_CHANNEL_7,   /* ch7 → GPIO35 */
};
#define ADC_CH_COUNT  (sizeof(ADC_CH_MAP) / sizeof(ADC_CH_MAP[0]))

esp_err_t adc_driver_init(void)
{
    if (!s_adc_unit) {
        adc_oneshot_unit_init_cfg_t unit_cfg = {
            .unit_id = ADC_UNIT_1,
            .clk_src = ADC_DIGI_CLK_SRC_DEFAULT,
            .ulp_mode = ADC_ULP_MODE_DISABLE,
        };
        adc_oneshot_new_unit(&unit_cfg, &s_adc_unit);
    }

    for (size_t i = 0; i < ADC_CH_COUNT; i++) {
        adc_oneshot_chan_cfg_t chan_cfg = {
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
        };
        adc_oneshot_config_channel(s_adc_unit, ADC_CH_MAP[i], &chan_cfg);
    }

    if (!s_adc_cali) {
#if ADC_CALI_SCHEME_LINE_FITTING_SUPPORTED
        adc_cali_line_fitting_config_t cali_config = {
            .unit_id = ADC_UNIT_1,
            .atten = ADC_ATTEN_DB_12,
            .bitwidth = ADC_BITWIDTH_12,
#if CONFIG_IDF_TARGET_ESP32
            .default_vref = ADC_VREF_MV,
#endif
        };
        adc_cali_create_scheme_line_fitting(&cali_config, &s_adc_cali);
#endif
    }
    s_adc_init = true;
    OS_LOGD(TAG, "ADC driver ready (%zu channels)", ADC_CH_COUNT);
    return ESP_OK;
}

int adc_driver_read_raw(int channel)
{
    if (channel < 0 || channel >= (int)ADC_CH_COUNT) return -1;
    if (!s_adc_init) adc_driver_init();

    int raw = 0;
    if (adc_oneshot_read(s_adc_unit, ADC_CH_MAP[channel], &raw) != ESP_OK) {
        return -1;
    }
    return raw;
}

int adc_driver_read_mv(int channel)
{
    int raw = adc_driver_read_raw(channel);
    if (raw < 0) return -1;
    if (s_adc_cali) {
        int mv = 0;
        if (adc_cali_raw_to_voltage(s_adc_cali, raw, &mv) == ESP_OK) {
            return mv;
        }
    }
    return raw;
}

/* ════════════════════════════════════════════════
   I2C DRIVER
   ════════════════════════════════════════════════ */
static bool s_i2c_init = false;

esp_err_t i2c_driver_init(int sda_pin, int scl_pin, uint32_t freq_hz)
{
    if (s_i2c_init) {
        i2c_driver_deinit();
    }
    i2c_config_t cfg = {
        .mode             = I2C_MODE_MASTER,
        .sda_io_num       = sda_pin,
        .scl_io_num       = scl_pin,
        .sda_pullup_en    = GPIO_PULLUP_ENABLE,
        .scl_pullup_en    = GPIO_PULLUP_ENABLE,
        .master.clk_speed = freq_hz ? freq_hz : I2C_DEFAULT_FREQ_HZ,
    };
    esp_err_t ret = i2c_param_config(I2C_PORT_NUM, &cfg);
    if (ret != ESP_OK) return ret;
    ret = i2c_driver_install(I2C_PORT_NUM, I2C_MODE_MASTER, 0, 0, 0);
    if (ret == ESP_OK) {
        s_i2c_init = true;
        OS_LOGD(TAG, "I2C driver ready (SDA=%d SCL=%d freq=%"PRIu32" Hz)",
                sda_pin, scl_pin, freq_hz);
    }
    return ret;
}

void i2c_driver_deinit(void)
{
    if (s_i2c_init) {
        i2c_driver_delete(I2C_PORT_NUM);
        s_i2c_init = false;
    }
}

void i2c_driver_scan(int sda, int scl, int fd)
{
    /* Re-init with requested pins */
    i2c_driver_init(sda, scl, I2C_DEFAULT_FREQ_HZ);

    int found = 0;
    for (int row = 0; row < 8; row++) {
        fd_printf(fd, "%02X:", row * 0x10);
        for (int col = 0; col < 16; col++) {
            uint8_t addr = (uint8_t)(row * 16 + col);
            if (addr < 0x08 || addr > 0x77) {
                fd_printf(fd, "   ");
                continue;
            }
            i2c_cmd_handle_t cmd = i2c_cmd_link_create();
            i2c_master_start(cmd);
            i2c_master_write_byte(cmd, (addr << 1) | I2C_MASTER_WRITE, true);
            i2c_master_stop(cmd);
            esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd,
                                                   pdMS_TO_TICKS(I2C_TIMEOUT_MS));
            i2c_cmd_link_delete(cmd);
            if (ret == ESP_OK) {
                fd_printf(fd, " %02X", addr);
                found++;
            } else {
                fd_printf(fd, " --");
            }
        }
        fd_printf(fd, "\r\n");
    }
    fd_printf(fd, "\r\n%d device(s) found\r\n", found);
}

esp_err_t i2c_driver_read(uint8_t dev_addr, uint8_t reg,
                           uint8_t *buf, size_t len)
{
    if (!s_i2c_init) return ESP_ERR_INVALID_STATE;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_READ, true);
    if (len > 1) i2c_master_read(cmd, buf, len - 1, I2C_MASTER_ACK);
    i2c_master_read_byte(cmd, buf + len - 1, I2C_MASTER_NACK);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

esp_err_t i2c_driver_write(uint8_t dev_addr, uint8_t reg,
                            const uint8_t *data, size_t len)
{
    if (!s_i2c_init) return ESP_ERR_INVALID_STATE;

    i2c_cmd_handle_t cmd = i2c_cmd_link_create();
    i2c_master_start(cmd);
    i2c_master_write_byte(cmd, (dev_addr << 1) | I2C_MASTER_WRITE, true);
    i2c_master_write_byte(cmd, reg, true);
    i2c_master_write(cmd, (uint8_t *)data, len, true);
    i2c_master_stop(cmd);
    esp_err_t ret = i2c_master_cmd_begin(I2C_PORT_NUM, cmd,
                                          pdMS_TO_TICKS(I2C_TIMEOUT_MS));
    i2c_cmd_link_delete(cmd);
    return ret;
}

/* ════════════════════════════════════════════════
   SPI DRIVER
   ════════════════════════════════════════════════ */
static spi_device_handle_t s_spi_dev = NULL;
static bool s_spi_init = false;

esp_err_t spi_driver_init(int mosi, int miso, int clk, int cs, uint32_t freq_hz)
{
    spi_bus_config_t buscfg = {
        .mosi_io_num   = mosi,
        .miso_io_num   = miso,
        .sclk_io_num   = clk,
        .quadwp_io_num = -1,
        .quadhd_io_num = -1,
        .max_transfer_sz = 4096,
    };
    spi_device_interface_config_t devcfg = {
        .clock_speed_hz = (int)(freq_hz ? freq_hz : SPI_DEFAULT_FREQ),
        .mode           = 0,
        .spics_io_num   = cs,
        .queue_size     = 1,
    };
    esp_err_t ret = spi_bus_initialize(SPI2_HOST, &buscfg, SPI_DMA_CH_AUTO);
    if (ret != ESP_OK && ret != ESP_ERR_INVALID_STATE) return ret;
    ret = spi_bus_add_device(SPI2_HOST, &devcfg, &s_spi_dev);
    if (ret == ESP_OK) {
        s_spi_init = true;
        OS_LOGD(TAG, "SPI driver ready (MOSI=%d MISO=%d CLK=%d CS=%d)",
                mosi, miso, clk, cs);
    }
    return ret;
}

void spi_driver_deinit(void)
{
    if (s_spi_init) {
        spi_bus_remove_device(s_spi_dev);
        spi_bus_free(SPI2_HOST);
        s_spi_init = false;
    }
}

esp_err_t spi_driver_transfer(const uint8_t *tx, uint8_t *rx, size_t len)
{
    if (!s_spi_init || len == 0) return ESP_ERR_INVALID_STATE;
    spi_transaction_t t = {
        .length    = len * 8,
        .tx_buffer = tx,
        .rx_buffer = rx,
    };
    return spi_device_transmit(s_spi_dev, &t);
}

/* ════════════════════════════════════════════════
   Additional UART Port
   ════════════════════════════════════════════════ */
esp_err_t uart_driver_init_port(int port, int tx, int rx,
                                 int baud, int buf_sz)
{
    uart_config_t cfg = {
        .baud_rate  = baud,
        .data_bits  = UART_DATA_8_BITS,
        .parity     = UART_PARITY_DISABLE,
        .stop_bits  = UART_STOP_BITS_1,
        .flow_ctrl  = UART_HW_FLOWCTRL_DISABLE,
        .source_clk = UART_SCLK_DEFAULT,
    };
    uart_driver_install(port, buf_sz, buf_sz, 0, NULL, 0);
    uart_param_config(port, &cfg);
    uart_set_pin(port, tx, rx, UART_PIN_NO_CHANGE, UART_PIN_NO_CHANGE);
    OS_LOGD(TAG, "UART%d ready (TX=%d RX=%d baud=%d)", port, tx, rx, baud);
    return ESP_OK;
}

/* ════════════════════════════════════════════════
   HAL Init
   ════════════════════════════════════════════════ */
esp_err_t os_hal_init(void)
{
    gpio_driver_init();
    adc_driver_init();
    i2c_driver_init(I2C_DEFAULT_SDA, I2C_DEFAULT_SCL, I2C_DEFAULT_FREQ_HZ);
    OS_LOGI(TAG, "HAL initialised (GPIO/ADC/I2C/SPI ready)");
    return ESP_OK;
}
