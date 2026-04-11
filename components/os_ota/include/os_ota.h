/**
 * @file os_ota.h
 * @brief Over-the-Air (OTA) Firmware Update Interface for ESP32OS
 *
 * Provides secure firmware updates with automatic rollback on failure,
 * signature verification, and progress callbacks.
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

/** Maximum URL length for OTA download */
#define OS_OTA_URL_MAX_LEN 256

/** Maximum SHA256 hash string length (hex) */
#define OS_OTA_SHA256_HEX_LEN 65

/** OTA update buffer size for download */
#define OS_OTA_BUFFER_SIZE 1024

/** Maximum OTA download time in seconds (10 minutes) */
#define OS_OTA_DOWNLOAD_TIMEOUT_SEC 600

/** Default partition label for OTA */
#define OS_OTA_PARTITION_LABEL "ota"

/* ────────────────────────────────────────────────
   OTA State
   ──────────────────────────────────────────────── */

typedef enum {
    OS_OTA_STATE_IDLE = 0,          /**< No update in progress */
    OS_OTA_STATE_DOWNLOADING = 1,   /**< Downloading firmware */
    OS_OTA_STATE_VERIFYING = 2,     /**< Verifying firmware */
    OS_OTA_STATE_APPLYING = 3,      /**< Applying update */
    OS_OTA_STATE_REBOOTING = 4,     /**< Rebooting to new firmware */
    OS_OTA_STATE_ROLLBACK = 5,      /**< Rolling back to previous */
    OS_OTA_STATE_ERROR = 6          /**< Error occurred */
} os_ota_state_t;

/* ────────────────────────────────────────────────
   OTA Configuration
   ──────────────────────────────────────────────── */

typedef struct {
    const char *url;                /**< Firmware download URL (http/https) */
    const char *ca_cert;            /**< CA certificate for HTTPS (NULL = use default) */
    const char *expected_sha256;    /**< Expected SHA256 hash (hex, 64 chars, NULL = skip verify) */
    bool disable_signature_check;   /**< Set true to skip signature verification */
    uint32_t download_timeout_sec;  /**< Download timeout in seconds (0 = default) */
} os_ota_config_t;

/* ────────────────────────────────────────────────
   Progress Callback
   ──────────────────────────────────────────────── */

/**
 * @brief OTA progress callback function type
 *
 * @param state      Current OTA state
 * @param progress   Progress percentage (0-100)
 * @param bytes_recv Bytes downloaded so far
 * @param total_size Total firmware size (0 if unknown)
 * @param user_data  User data pointer
 */
typedef void (*os_ota_progress_cb_t)(os_ota_state_t state, uint8_t progress,
                                      size_t bytes_recv, size_t total_size,
                                      void *user_data);

/* ────────────────────────────────────────────────
   OTA Information
   ──────────────────────────────────────────────── */

typedef struct {
    char current_version[32];       /**< Current running firmware version */
    char new_version[32];          /**< New firmware version (during update) */
    os_ota_state_t state;          /**< Current state */
    uint8_t progress;              /**< Current progress percentage */
    size_t bytes_downloaded;       /**< Bytes downloaded */
    size_t total_size;             /**< Total expected size */
    char error_message[128];       /**< Last error message */
    bool can_rollback;             /**< True if rollback is available */
    bool rollback_confirmed;       /**< True if current boot is confirmed */
} os_ota_info_t;

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/**
 * @brief Initialize the OTA subsystem
 *
 * Must be called before using any OTA functions.
 * Checks boot status and sets up rollback if needed.
 *
 * @return ESP_OK on success
 */
esp_err_t os_ota_init(void);

/**
 * @brief Deinitialize the OTA subsystem
 */
void os_ota_deinit(void);

/**
 * @brief Start OTA update from URL
 *
 * Downloads firmware from the specified URL and applies it.
 * This function returns immediately and the update proceeds in background.
 *
 * @param config OTA configuration (URL, certificates, etc.)
 *
 * @return ESP_OK if update started, error otherwise
 */
esp_err_t os_ota_start(const os_ota_config_t *config);

/**
 * @brief Check if OTA is currently in progress
 *
 * @return true if update is running
 */
bool os_ota_is_in_progress(void);

/**
 * @brief Get current OTA state
 *
 * @return Current state enum
 */
os_ota_state_t os_ota_get_state(void);

/**
 * @brief Get OTA state as human-readable string
 *
 * @param state State to convert
 *
 * @return State string
 */
const char *os_ota_get_state_str(os_ota_state_t state);

/**
 * @brief Get current OTA progress
 *
 * @return Progress percentage (0-100)
 */
uint8_t os_ota_get_progress(void);

/**
 * @brief Get detailed OTA information
 *
 * @param info Pointer to structure to fill
 */
void os_ota_get_info(os_ota_info_t *info);

/**
 * @brief Set progress callback
 *
 * @param cb        Callback function (NULL to disable)
 * @param user_data User data passed to callback
 */
void os_ota_set_progress_callback(os_ota_progress_cb_t cb, void *user_data);

/**
 * @brief Confirm successful firmware update
 *
 * Must be called after reboot to confirm new firmware works.
 * If not called before timeout, system will rollback.
 *
 * @return ESP_OK on success
 */
esp_err_t os_ota_confirm(void);

/**
 * @brief Rollback to previous firmware
 *
 * Schedules rollback and reboots system.
 *
 * @return ESP_OK on success
 */
esp_err_t os_ota_rollback(void);

/**
 * @brief Check if rollback is available
 *
 * @return true if previous firmware exists
 */
bool os_ota_can_rollback(void);

/**
 * @brief Check if current boot needs confirmation
 *
 * @return true if confirm() should be called
 */
bool os_ota_needs_confirmation(void);

/**
 * @brief Abort current OTA operation
 *
 * Cancels download and resets state.
 */
void os_ota_abort(void);

/**
 * @brief Get current running partition label
 *
 * @return Partition label string (e.g., "ota_0", "ota_1", "factory")
 */
const char *os_ota_get_running_partition(void);

/**
 * @brief Print OTA status to file descriptor
 *
 * @param fd File descriptor (-1 for UART)
 */
void os_ota_print_status(int fd);

#ifdef __cplusplus
}
#endif
