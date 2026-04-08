/*
 * examples/sensor_logger/main.c
 * ─────────────────────────────────────────────────────────────────
 * Example ESP32OS Application: Periodic Sensor Logger
 *
 * Demonstrates:
 *   - Creating a background service task via os_process_create()
 *   - Reading ADC channels periodically
 *   - Writing sensor data to SPIFFS log file
 *   - Registering a custom shell command
 *
 * To integrate: include this file's init call from main.c
 * ─────────────────────────────────────────────────────────────────
 */

#include "os_kernel.h"
#include "os_fs.h"
#include "os_logging.h"
#include "os_drivers.h"
#include "os_shell.h"

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_timer.h"

#include <stdio.h>
#include <string.h>
#include <inttypes.h>

#define TAG         "SENSOR_LOG"
#define LOG_FILE    "/logs/sensor.csv"
#define INTERVAL_MS 5000

/* ─────────────────────────────────────────────── */
static void sensor_logger_task(void *arg)
{
    OS_LOGI(TAG, "Sensor logger started (interval=%d ms)", INTERVAL_MS);

    /* Write CSV header if file doesn't exist */
    if (!os_fs_exists(LOG_FILE)) {
        os_fs_write_file(LOG_FILE,
                         "timestamp_ms,ch0_mv,ch1_mv,ch2_mv\n",
                         35, false);
    }

    while (1) {
        uint64_t ts  = (uint64_t)esp_timer_get_time() / 1000ULL;
        int ch0 = adc_driver_read_mv(0);
        int ch1 = adc_driver_read_mv(1);
        int ch2 = adc_driver_read_mv(2);

        char line[80];
        int n = snprintf(line, sizeof(line),
                         "%"PRIu64",%d,%d,%d\n",
                         ts, ch0, ch1, ch2);

        /* Append to CSV */
        os_fs_write_file(LOG_FILE, line, (size_t)n, true);

        OS_LOGD(TAG, "ch0=%dmV ch1=%dmV ch2=%dmV", ch0, ch1, ch2);

        vTaskDelay(pdMS_TO_TICKS(INTERVAL_MS));
    }
}

/* ─────────────────────────────────────────────── */
static int cmd_sensorlog(int fd, int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "show") == 0) {
        /* Print last N lines of the log file */
        char buf[2048];
        size_t rsz = 0;
        if (os_fs_read_file(LOG_FILE, buf, sizeof(buf), &rsz) != ESP_OK) {
            shell_write(fd, "sensor log: file not found\r\n");
            return SHELL_CMD_ERROR;
        }
        shell_write(fd, "\r\ntimestamp_ms, ch0_mv, ch1_mv, ch2_mv\r\n");
        shell_write(fd, "─────────────────────────────────────\r\n");
        shell_write(fd, buf);
        shell_printf(fd, "\r\n(%zu bytes)\r\n", rsz);
        return SHELL_CMD_OK;
    }

    if (argc >= 2 && strcmp(argv[1], "clear") == 0) {
        os_fs_remove(LOG_FILE);
        shell_write(fd, "Sensor log cleared.\r\n");
        return SHELL_CMD_OK;
    }

    shell_write(fd, "Usage: sensorlog <show|clear>\r\n");
    return SHELL_CMD_ERROR;
}

/* ─────────────────────────────────────────────── */
void sensor_logger_init(void)
{
    /* Register CLI command */
    static const shell_command_t cmd =
        SHELL_CMD_ENTRY("sensorlog",
                        "View/clear sensor log file",
                        "sensorlog <show|clear>",
                        cmd_sensorlog);
    shell_register_command(&cmd);

    /* Start background logger task */
    os_process_create("sensor_log", sensor_logger_task,
                      NULL, 3072, 3, false);

    OS_LOGI(TAG, "Sensor logger initialised. Log: %s", LOG_FILE);
}
