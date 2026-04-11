/**
 * @file os_pwm.h
 * @brief PWM Driver Interface for ESP32OS
 *
 * Provides LEDC-based PWM output control for motor control, LED dimming,
 * and servo control applications.
 *
 * Copyright (c) 2026 ESP32OS Contributors
 * SPDX-License-Identifier: MIT
 */

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Configuration Constants
   ──────────────────────────────────────────────── */

/** Maximum number of PWM channels available */
#define OS_PWM_MAX_CHANNELS 8

/** Default PWM frequency in Hz */
#define OS_PWM_DEFAULT_FREQ 5000

/** Maximum PWM duty cycle percentage */
#define OS_PWM_MAX_DUTY_PERCENT 100

/** Maximum PWM duty cycle in microseconds (for servo mode) */
#define OS_PWM_MAX_DUTY_US 20000

/* ────────────────────────────────────────────────
   PWM Channel Handle
   ──────────────────────────────────────────────── */

typedef int8_t os_pwm_channel_t;

/** Invalid channel constant */
#define OS_PWM_CHANNEL_INVALID -1

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/**
 * @brief Initialize the PWM subsystem
 *
 * Must be called before using any other PWM functions.
 *
 * @return ESP_OK on success, ESP_ERR_NO_MEM if resources unavailable
 */
esp_err_t os_pwm_init(void);

/**
 * @brief Deinitialize the PWM subsystem
 *
 * Releases all PWM channels and disables LEDC peripheral.
 */
void os_pwm_deinit(void);

/**
 * @brief Configure and initialize a PWM channel
 *
 * Sets up a GPIO pin for PWM output with specified frequency.
 * The duty cycle is initially set to 0%.
 *
 * @param channel   PWM channel number (0 to OS_PWM_MAX_CHANNELS-1)
 * @param gpio_pin  GPIO pin number to output PWM on
 * @param freq_hz   PWM frequency in Hertz (typically 50-40000 Hz)
 *
 * @return ESP_OK on success
 *         ESP_ERR_INVALID_ARG if channel or pin invalid
 *         ESP_ERR_INVALID_STATE if channel already in use
 *         ESP_ERR_NO_MEM if LEDC resources exhausted
 */
esp_err_t os_pwm_channel_init(os_pwm_channel_t channel, uint8_t gpio_pin, uint32_t freq_hz);

/**
 * @brief Deinitialize a PWM channel
 *
 * Releases the channel and resets the GPIO to input mode.
 *
 * @param channel PWM channel to release
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel invalid
 */
esp_err_t os_pwm_channel_deinit(os_pwm_channel_t channel);

/**
 * @brief Set PWM duty cycle as percentage
 *
 * @param channel      PWM channel number
 * @param duty_percent Duty cycle percentage (0-100)
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel or duty invalid
 */
esp_err_t os_pwm_set_duty(os_pwm_channel_t channel, uint8_t duty_percent);

/**
 * @brief Set PWM duty cycle in microseconds
 *
 * Useful for servo control where pulse width matters.
 * For standard servos: 1000-2000us at 50Hz.
 *
 * @param channel PWM channel number
 * @param duty_us Duty cycle in microseconds
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel or duty invalid
 */
esp_err_t os_pwm_set_duty_us(os_pwm_channel_t channel, uint32_t duty_us);

/**
 * @brief Set PWM frequency
 *
 * Changes the frequency of an active PWM channel.
 * Duty cycle is preserved.
 *
 * @param channel PWM channel number
 * @param freq_hz New frequency in Hertz
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel or freq invalid
 */
esp_err_t os_pwm_set_freq(os_pwm_channel_t channel, uint32_t freq_hz);

/**
 * @brief Get current PWM configuration
 *
 * @param channel  PWM channel number
 * @param gpio_pin Output: GPIO pin number (can be NULL)
 * @param freq_hz  Output: Current frequency (can be NULL)
 * @param duty_pct Output: Current duty percentage (can be NULL)
 *
 * @return ESP_OK on success, ESP_ERR_INVALID_ARG if channel invalid
 */
esp_err_t os_pwm_get_config(os_pwm_channel_t channel, uint8_t *gpio_pin,
                             uint32_t *freq_hz, uint8_t *duty_pct);

/**
 * @brief Check if PWM channel is initialized
 *
 * @param channel PWM channel number
 *
 * @return true if channel is active, false otherwise
 */
bool os_pwm_channel_is_active(os_pwm_channel_t channel);

/**
 * @brief Get number of active PWM channels
 *
 * @return Count of initialized channels
 */
uint8_t os_pwm_get_active_count(void);

/**
 * @brief Print PWM status to file descriptor
 *
 * Outputs a formatted table of all PWM channels.
 *
 * @param fd File descriptor (-1 for UART, socket fd for Telnet)
 */
void os_pwm_print_status(int fd);

#ifdef __cplusplus
}
#endif
