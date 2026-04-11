/**
 * @file os_pwm.c
 * @brief PWM Driver Implementation using ESP32 LEDC
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#include "os_pwm.h"
#include "os_logging.h"

#include "driver/ledc.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"

#include <string.h>

/* ────────────────────────────────────────────────
   Module Configuration
   ──────────────────────────────────────────────── */

#define TAG "OS_PWM"

/** LEDC speed mode - using low speed for all channels */
#define PWM_SPEED_MODE LEDC_LOW_SPEED_MODE

/** LEDC timer resolution (bits) - 13 bits gives 0-8191 duty range */
#define PWM_TIMER_RES LEDC_TIMER_13_BIT

/** Maximum duty value at 13-bit resolution */
#define PWM_MAX_DUTY_VALUE 8191

/* ────────────────────────────────────────────────
   Channel State Structure
   ──────────────────────────────────────────────── */

typedef struct {
    bool active;           /** Channel is initialized */
    uint8_t gpio_pin;      /** Output GPIO pin number */
    uint32_t freq_hz;      /** Current frequency */
    uint32_t duty_percent; /** Current duty percentage (0-100) */
    ledc_channel_t ledc_ch; /** Associated LEDC channel */
    ledc_timer_t ledc_timer; /** Associated LEDC timer */
} pwm_channel_state_t;

/* ────────────────────────────────────────────────
   Module State
   ──────────────────────────────────────────────── */

/** Channel state table - statically allocated */
static pwm_channel_state_t s_channels[OS_PWM_MAX_CHANNELS];

/** Mutex protecting channel table */
static SemaphoreHandle_t s_mutex;

/** Module initialized flag */
static bool s_initialized = false;

/** LEDC timer allocation bitmap */
static uint8_t s_timer_usage = 0;

/** LEDC timer reference counts per timer index */
static uint8_t s_timer_refcount[LEDC_TIMER_MAX];

/* ────────────────────────────────────────────────
   Private Helper Functions
   ──────────────────────────────────────────────── */

/**
 * @brief Allocate a LEDC timer
 *
 * Finds and marks a free timer slot.
 *
 * @return Timer index (0-3) or -1 if none available
 */
static int pwm_alloc_timer(void)
{
    for (int i = 0; i < LEDC_TIMER_MAX; i++) {
        if (!(s_timer_usage & (1 << i))) {
            s_timer_usage |= (1 << i);
            s_timer_refcount[i] = 1;
            return i;
        }
    }
    return -1;
}

/**
 * @brief Free a LEDC timer
 *
 * @param timer Timer index to release
 */
static void pwm_free_timer(int timer)
{
    if (timer >= 0 && timer < LEDC_TIMER_MAX) {
        if (s_timer_refcount[timer] > 0) {
            s_timer_refcount[timer]--;
        }
        if (s_timer_refcount[timer] == 0) {
            s_timer_usage &= ~(1 << timer);
        }
    }
}

/**
 * @brief Convert percentage to duty register value
 *
 * @param percent Duty cycle percentage (0-100)
 *
 * @return Duty register value (0-PWM_MAX_DUTY_VALUE)
 */
static uint32_t duty_percent_to_reg(uint8_t percent)
{
    if (percent >= 100) {
        return PWM_MAX_DUTY_VALUE;
    }
    return (percent * PWM_MAX_DUTY_VALUE) / 100;
}

/**
 * @brief Convert microseconds to duty register value
 *
 * @param us      Pulse width in microseconds
 * @param freq_hz PWM frequency
 *
 * @return Duty register value
 */
static uint32_t duty_us_to_reg(uint32_t us, uint32_t freq_hz)
{
    /* Period in microseconds = 1,000,000 / frequency */
    uint32_t period_us = 1000000 / freq_hz;

    if (us >= period_us) {
        return PWM_MAX_DUTY_VALUE;
    }

    /* Scale us to duty register value */
    return (uint32_t)(((uint64_t)us * PWM_MAX_DUTY_VALUE) / period_us);
}

/**
 * @brief Validate channel number
 *
 * @param channel Channel to validate
 *
 * @return true if valid, false otherwise
 */
static inline bool is_valid_channel(os_pwm_channel_t channel)
{
    return (channel >= 0 && channel < OS_PWM_MAX_CHANNELS);
}

/* ────────────────────────────────────────────────
   Public API Implementation
   ──────────────────────────────────────────────── */

esp_err_t os_pwm_init(void)
{
    if (s_initialized) {
        return ESP_OK;
    }

    /* Create mutex for thread safety */
    s_mutex = xSemaphoreCreateMutex();
    if (s_mutex == NULL) {
        OS_LOGE(TAG, "Failed to create mutex");
        return ESP_ERR_NO_MEM;
    }

    /* Clear channel table */
    memset(s_channels, 0, sizeof(s_channels));
    for (int i = 0; i < OS_PWM_MAX_CHANNELS; i++) {
        s_channels[i].ledc_ch = LEDC_CHANNEL_0 + i;
        s_channels[i].ledc_timer = LEDC_TIMER_0;
    }

    s_timer_usage = 0;
    memset(s_timer_refcount, 0, sizeof(s_timer_refcount));
    s_initialized = true;

    OS_LOGI(TAG, "PWM initialized, %d channels available", OS_PWM_MAX_CHANNELS);
    return ESP_OK;
}

void os_pwm_deinit(void)
{
    if (!s_initialized) {
        return;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Deinitialize all active channels */
    for (int i = 0; i < OS_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            ledc_stop(PWM_SPEED_MODE, s_channels[i].ledc_ch, 0);
            pwm_free_timer(s_channels[i].ledc_timer);
            s_channels[i].active = false;
        }
    }

    /* Reset timer usage */
    s_timer_usage = 0;

    xSemaphoreGive(s_mutex);

    /* Delete mutex */
    vSemaphoreDelete(s_mutex);
    s_mutex = NULL;
    s_initialized = false;

    OS_LOGI(TAG, "PWM deinitialized");
}

esp_err_t os_pwm_channel_init(os_pwm_channel_t channel, uint8_t gpio_pin, uint32_t freq_hz)
{
    if (!s_initialized) {
        OS_LOGE(TAG, "PWM not initialized");
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_channel(channel)) {
        OS_LOGE(TAG, "Invalid channel: %d", channel);
        return ESP_ERR_INVALID_ARG;
    }

    if (freq_hz == 0 || freq_hz > 40000000) {
        OS_LOGE(TAG, "Invalid frequency: %lu Hz", freq_hz);
        return ESP_ERR_INVALID_ARG;
    }

    /* Validate GPIO pin range (0-48 for ESP32-S3, 0-39 for ESP32) */
    gpio_num_t gpio = (gpio_num_t)gpio_pin;
    if (!GPIO_IS_VALID_OUTPUT_GPIO(gpio)) {
        OS_LOGE(TAG, "Invalid GPIO pin: %d", gpio_pin);
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    /* Check if channel already in use */
    if (s_channels[channel].active) {
        OS_LOGW(TAG, "Channel %d already active, deinitializing first", channel);
        ledc_stop(PWM_SPEED_MODE, s_channels[channel].ledc_ch, 0);
        pwm_free_timer(s_channels[channel].ledc_timer);
        s_channels[channel].active = false;
    }

    /* Allocate a timer */
    int timer_idx = pwm_alloc_timer();
    if (timer_idx < 0) {
        OS_LOGE(TAG, "No LEDC timers available");
        xSemaphoreGive(s_mutex);
        return ESP_ERR_NO_MEM;
    }

    ledc_timer_t timer = LEDC_TIMER_0 + timer_idx;
    s_channels[channel].ledc_timer = timer;

    /* Configure timer */
    ledc_timer_config_t timer_cfg = {
        .speed_mode = PWM_SPEED_MODE,
        .duty_resolution = PWM_TIMER_RES,
        .timer_num = timer,
        .freq_hz = freq_hz,
        .clk_cfg = LEDC_AUTO_CLK
    };

    esp_err_t ret = ledc_timer_config(&timer_cfg);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "LEDC timer config failed: %s", esp_err_to_name(ret));
        pwm_free_timer(timer_idx);
        xSemaphoreGive(s_mutex);
        return ret;
    }

    /* Configure channel */
    ledc_channel_config_t ch_cfg = {
        .gpio_num = gpio_pin,
        .speed_mode = PWM_SPEED_MODE,
        .channel = s_channels[channel].ledc_ch,
        .intr_type = LEDC_INTR_DISABLE,
        .timer_sel = timer,
        .duty = 0,  /* Start with 0% duty */
        .hpoint = 0
    };

    ret = ledc_channel_config(&ch_cfg);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "LEDC channel config failed: %s", esp_err_to_name(ret));
        pwm_free_timer(timer_idx);
        xSemaphoreGive(s_mutex);
        return ret;
    }

    /* Update state */
    s_channels[channel].active = true;
    s_channels[channel].gpio_pin = gpio_pin;
    s_channels[channel].freq_hz = freq_hz;
    s_channels[channel].duty_percent = 0;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Channel %d initialized: GPIO%d @ %lu Hz", channel, gpio_pin, freq_hz);
    return ESP_OK;
}

esp_err_t os_pwm_channel_deinit(os_pwm_channel_t channel)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_channel(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_channels[channel].active) {
        xSemaphoreGive(s_mutex);
        return ESP_OK;  /* Already inactive */
    }

    /* Stop PWM output and set GPIO low */
    ledc_stop(PWM_SPEED_MODE, s_channels[channel].ledc_ch, 0);

    /* Free timer only when no channel references it anymore */
    pwm_free_timer(s_channels[channel].ledc_timer);

    s_channels[channel].active = false;

    xSemaphoreGive(s_mutex);

    OS_LOGI(TAG, "Channel %d deinitialized", channel);
    return ESP_OK;
}

esp_err_t os_pwm_set_duty(os_pwm_channel_t channel, uint8_t duty_percent)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_channel(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    if (duty_percent > 100) {
        duty_percent = 100;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_channels[channel].active) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t duty_val = duty_percent_to_reg(duty_percent);

    esp_err_t ret = ledc_set_duty(PWM_SPEED_MODE, s_channels[channel].ledc_ch, duty_val);
    if (ret == ESP_OK) {
        ledc_update_duty(PWM_SPEED_MODE, s_channels[channel].ledc_ch);
        s_channels[channel].duty_percent = duty_percent;
    }

    xSemaphoreGive(s_mutex);

    OS_LOGD(TAG, "Channel %d duty set to %d%% (reg=%lu)", channel, duty_percent, duty_val);
    return ret;
}

esp_err_t os_pwm_set_duty_us(os_pwm_channel_t channel, uint32_t duty_us)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_channel(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_channels[channel].active) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    uint32_t duty_val = duty_us_to_reg(duty_us, s_channels[channel].freq_hz);

    esp_err_t ret = ledc_set_duty(PWM_SPEED_MODE, s_channels[channel].ledc_ch, duty_val);
    if (ret == ESP_OK) {
        ledc_update_duty(PWM_SPEED_MODE, s_channels[channel].ledc_ch);
        /* Recalculate percentage for tracking */
        s_channels[channel].duty_percent = (uint32_t)(((uint64_t)duty_us *
            s_channels[channel].freq_hz) / 10000ULL);
        if (s_channels[channel].duty_percent > 100) {
            s_channels[channel].duty_percent = 100;
        }
    }

    xSemaphoreGive(s_mutex);

    OS_LOGD(TAG, "Channel %d duty set to %lu us", channel, duty_us);
    return ret;
}

esp_err_t os_pwm_set_freq(os_pwm_channel_t channel, uint32_t freq_hz)
{
    if (!s_initialized) {
        return ESP_ERR_INVALID_STATE;
    }

    if (!is_valid_channel(channel) || freq_hz == 0 || freq_hz > 40000000) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_channels[channel].active) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    esp_err_t ret = ledc_set_freq(PWM_SPEED_MODE, s_channels[channel].ledc_timer, freq_hz);
    if (ret == ESP_OK) {
        s_channels[channel].freq_hz = freq_hz;
    }

    xSemaphoreGive(s_mutex);

    if (ret == ESP_OK) {
        OS_LOGI(TAG, "Channel %d frequency set to %lu Hz", channel, freq_hz);
    } else {
        OS_LOGE(TAG, "Failed to set frequency: %s", esp_err_to_name(ret));
    }

    return ret;
}

esp_err_t os_pwm_get_config(os_pwm_channel_t channel, uint8_t *gpio_pin,
                             uint32_t *freq_hz, uint8_t *duty_pct)
{
    if (!is_valid_channel(channel)) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    if (!s_channels[channel].active) {
        xSemaphoreGive(s_mutex);
        return ESP_ERR_INVALID_STATE;
    }

    if (gpio_pin != NULL) {
        *gpio_pin = s_channels[channel].gpio_pin;
    }
    if (freq_hz != NULL) {
        *freq_hz = s_channels[channel].freq_hz;
    }
    if (duty_pct != NULL) {
        *duty_pct = (uint8_t)s_channels[channel].duty_percent;
    }

    xSemaphoreGive(s_mutex);
    return ESP_OK;
}

bool os_pwm_channel_is_active(os_pwm_channel_t channel)
{
    if (!is_valid_channel(channel)) {
        return false;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);
    bool active = s_channels[channel].active;
    xSemaphoreGive(s_mutex);

    return active;
}

uint8_t os_pwm_get_active_count(void)
{
    if (!s_initialized) {
        return 0;
    }

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t count = 0;
    for (int i = 0; i < OS_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            count++;
        }
    }

    xSemaphoreGive(s_mutex);
    return count;
}

void os_pwm_print_status(int fd)
{
    if (!s_initialized) {
        return;
    }

    /* Use shell_printf via fd - this function is called from shell context
     * The actual output is handled by the shell's output functions */
    (void)fd;

    xSemaphoreTake(s_mutex, portMAX_DELAY);

    uint8_t active_count = 0;
    for (int i = 0; i < OS_PWM_MAX_CHANNELS; i++) {
        if (s_channels[i].active) {
            active_count++;
        }
    }

    xSemaphoreGive(s_mutex);

    /* Note: Actual output would require shell integration
     * This function is a placeholder for future shell command integration */
    OS_LOGI(TAG, "Active PWM channels: %d/%d", active_count, OS_PWM_MAX_CHANNELS);
}
