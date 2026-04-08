/*
 * main.c — ESP32OS Boot Sequence
 * ─────────────────────────────────────────────────────────────────
 * Startup order:
 *   1. NVS init
 *   2. Logging subsystem
 *   3. File system (SPIFFS)
 *   4. Kernel (process table, watchdog)
 *   5. HAL (GPIO, ADC, I2C)
 *   6. Networking (TCP/IP, WiFi)
 *   7. Shell commands registration
 *   8. Shell UART task
 *   9. Shell Telnet server (if WiFi available)
 *  10. System monitor task
 * ─────────────────────────────────────────────────────────────────
 */

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_err.h"
#include "nvs_flash.h"
#include "esp_chip_info.h"
#include "esp_flash.h"

#include "os_logging.h"
#include "os_kernel.h"
#include "os_fs.h"
#include "os_hal.h"
#include "os_networking.h"
#include "os_shell.h"

#define TAG "MAIN"

/* Forward declarations */
void shell_commands_register_all(void);
static void system_monitor_task(void *arg);
static void print_boot_info(void);

/* ─────────────────────────────────────────────── */
static void check_reset_reason(void)
{
    esp_reset_reason_t reason = esp_reset_reason();
    const char *str = "unknown";
    switch (reason) {
    case ESP_RST_POWERON:   str = "power-on";       break;
    case ESP_RST_EXT:       str = "external reset";  break;
    case ESP_RST_SW:        str = "software reset";  break;
    case ESP_RST_PANIC:     str = "panic/exception"; break;
    case ESP_RST_INT_WDT:   str = "interrupt watchdog"; break;
    case ESP_RST_TASK_WDT:  str = "task watchdog";   break;
    case ESP_RST_WDT:       str = "other watchdog";  break;
    case ESP_RST_DEEPSLEEP: str = "deep-sleep wake"; break;
    case ESP_RST_BROWNOUT:  str = "brownout";        break;
    default:                str = "unknown";         break;
    }
    OS_LOGI(TAG, "Reset reason: %s", str);

    if (reason == ESP_RST_PANIC || reason == ESP_RST_INT_WDT ||
        reason == ESP_RST_TASK_WDT) {
        OS_LOGW(TAG, "Previous crash detected — check /spiffs/logs/crash.log");
        /* Write crash marker to fs (fs may not be up yet; deferred log) */
    }
}

/* ─────────────────────────────────────────────── */
static void print_boot_info(void)
{
    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);

    OS_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
    OS_LOGI(TAG, "  ESP32OS v1.0.0  starting up");
    OS_LOGI(TAG, "  IDF: %s", esp_get_idf_version());
    OS_LOGI(TAG, "  Chip: %s  rev %d  cores: %d",
            chip.model == CHIP_ESP32   ? "ESP32"    :
            chip.model == CHIP_ESP32S2 ? "ESP32-S2" :
            chip.model == CHIP_ESP32S3 ? "ESP32-S3" :
            chip.model == CHIP_ESP32C3 ? "ESP32-C3" : "ESP32xx",
            chip.revision, chip.cores);
    OS_LOGI(TAG, "  Flash: %"PRIu32" MB  PSRAM: %s",
            flash_sz / (1024*1024),
            chip.features & CHIP_FEATURE_EMB_FLASH ? "internal" : "external");
    OS_LOGI(TAG, "  Free heap: %"PRIu32" bytes",
            esp_get_free_heap_size());
    OS_LOGI(TAG, "━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━━");
}

/* ─────────────────────────────────────────────── */
static void system_monitor_task(void *arg)
{
    (void)arg;
    /*
     * Runs every 30 s. Feeds watchdog, logs heap, writes
     * periodic stats to the log file.
     */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(30000));

        kernel_stats_t st;
        os_kernel_get_stats(&st);

        OS_LOGD(TAG, "Monitor: heap=%"PRIu32"/%"PRIu32" procs=%d up=%"PRIu32"s",
                st.free_heap_bytes, st.total_heap_bytes,
                st.process_count,
                st.uptime_ms / 1000);

        /* Warn if heap is critically low */
        if (st.free_heap_bytes < 8192) {
            OS_LOGW(TAG, "LOW HEAP WARNING: %"PRIu32" bytes free",
                    st.free_heap_bytes);
        }
    }
}

/* ─────────────────────────────────────────────── */
void app_main(void)
{
    /* ── 1. NVS ─────────────────────────────── */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES ||
        ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* ── 2. Logging ─────────────────────────── */
    os_log_init();
    os_log_set_level(OS_LOG_DEBUG);
    print_boot_info();
    check_reset_reason();

    /* ── 3. File System ─────────────────────── */
    ret = os_fs_init();
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "FS init failed: %s", esp_err_to_name(ret));
        /* Non-fatal: continue without persistent storage */
    } else {
        /* Enable log file after FS is up */
        os_log_set_file_output(true);
        OS_LOGI(TAG, "File logging active: %s", OS_LOG_FILE_PATH);
    }

    /* ── 4. Kernel ──────────────────────────── */
    ret = os_kernel_init();
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Kernel init failed: %s", esp_err_to_name(ret));
        /* Fatal */
        esp_restart();
    }

    /* ── 5. HAL ─────────────────────────────── */
    os_hal_init();

    /* ── 6. Networking ──────────────────────── */
    ret = os_net_init();
    if (ret != ESP_OK) {
        OS_LOGW(TAG, "Network init failed — WiFi/telnet unavailable");
    }

    /* ── 7. Watchdog ────────────────────────── */
    os_watchdog_enable(30000);   /* 30 second task watchdog */

    /* ── 8. Shell commands ──────────────────── */
    shell_init();
    shell_commands_register_all();

    /* ── 9. UART shell ──────────────────────── */
    ret = shell_start_uart();
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Failed to start UART shell");
    } else {
        OS_LOGI(TAG, "UART shell started (115200 baud, UART0)");
    }

    /* ── 10. Telnet server ──────────────────── */
    ret = shell_start_telnet(2222);
    if (ret != ESP_OK) {
        OS_LOGW(TAG, "Failed to start Telnet server");
    } else {
        OS_LOGI(TAG, "Telnet shell listening on port 2222");
    }

    /* ── 11. System monitor task ────────────── */
    os_process_create("sys_monitor", system_monitor_task,
                      NULL, 2048, 3, true);

    OS_LOGI(TAG, "Boot complete.  All services running.");

    /* app_main must not return — hand off to FreeRTOS scheduler */
    while (1) {
        vTaskDelay(pdMS_TO_TICKS(1000));
        os_watchdog_feed();
    }
}
