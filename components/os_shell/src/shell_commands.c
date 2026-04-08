/*
 * shell_commands.c
 * All built-in shell commands for esp32os.
 * Each command follows the signature: int cmd_xxx(int fd, int argc, char **argv)
 */

#include "os_shell.h"
#include "os_kernel.h"
#include "os_fs.h"
#include "os_logging.h"
#include "os_networking.h"
#include "os_drivers.h"

#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <errno.h>
#include <inttypes.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_flash.h"
#include "esp_timer.h"
#include "esp_idf_version.h"
#include "nvs_flash.h"
#include "lwip/inet.h"
#include "lwip/ip4_addr.h"
#include "lwip/sockets.h"

#define TAG "CMDS"

/* ────────────────────────────────────────────────
   Utility macro
   ──────────────────────────────────────────────── */
#define SH_PRINTF(fmt, ...)  shell_printf(fd, fmt, ##__VA_ARGS__)
#define SH_WRITE(s)          shell_write(fd, s)

/* ══════════════════════════════════════════════════
   SYSTEM COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_help(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    shell_print_help(fd);
    return SHELL_CMD_OK;
}

static int cmd_clear(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    SH_WRITE("\033[2J\033[H");
    return SHELL_CMD_OK;
}

static int cmd_uname(int fd, int argc, char **argv)
{
    (void)argc;
    bool all = (argc >= 2 && strcmp(argv[1], "-a") == 0);

    esp_chip_info_t chip;
    esp_chip_info(&chip);

    uint32_t flash_sz = 0;
    esp_flash_get_size(NULL, &flash_sz);

    const char *model = (chip.model == CHIP_ESP32)   ? "ESP32"   :
                        (chip.model == CHIP_ESP32S2)  ? "ESP32-S2" :
                        (chip.model == CHIP_ESP32S3)  ? "ESP32-S3" :
                        (chip.model == CHIP_ESP32C3)  ? "ESP32-C3" : "ESP32xx";

    if (all) {
        SH_PRINTF("esp32os %s  %s  IDF-%s  %d core(s)  Flash:%"PRIu32"MB\r\n",
                  model,
                  chip.features & CHIP_FEATURE_WIFI_BGN ? "WiFi" : "",
                  esp_get_idf_version(),
                  chip.cores,
                  flash_sz / (1024 * 1024));
    } else {
        SH_PRINTF("esp32os\r\n");
    }
    return SHELL_CMD_OK;
}

static int cmd_uptime(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    kernel_stats_t st;
    os_kernel_get_stats(&st);
    uint32_t s = st.uptime_ms / 1000;
    uint32_t m = s / 60; s %= 60;
    uint32_t h = m / 60; m %= 60;
    uint32_t d = h / 24; h %= 24;
    SH_PRINTF("up %"PRIu32" days, %02"PRIu32":%02"PRIu32":%02"PRIu32
              "  procs: %d\r\n",
              d, h, m, s, st.process_count);
    return SHELL_CMD_OK;
}

static int cmd_free(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    kernel_stats_t st;
    os_kernel_get_stats(&st);
    SH_PRINTF("\r\n%20s  %10s  %10s  %10s\r\n",
              "", "total", "used", "free");
    SH_PRINTF("%-20s  %10"PRIu32"  %10"PRIu32"  %10"PRIu32"\r\n",
              "Heap:",
              st.total_heap_bytes,
              st.total_heap_bytes - st.free_heap_bytes,
              st.free_heap_bytes);
    SH_PRINTF("%-20s  %10s  %10s  %10"PRIu32"\r\n",
              "Min free:", "", "", st.min_free_heap_bytes);
    SH_PRINTF("%-20s  %10s  %10s  %10"PRIu32"\r\n",
              "Largest block:", "", "", st.largest_free_block);
    SH_WRITE("\r\n");
    return SHELL_CMD_OK;
}

static int cmd_reboot(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    SH_WRITE("System rebooting...\r\n");
    vTaskDelay(pdMS_TO_TICKS(300));
    esp_restart();
    return SHELL_CMD_OK; /* unreachable */
}

static int cmd_echo(int fd, int argc, char **argv)
{
    for (int i = 1; i < argc; i++) {
        SH_PRINTF("%s%s", argv[i], (i < argc - 1) ? " " : "");
    }
    SH_WRITE("\r\n");
    return SHELL_CMD_OK;
}

static int cmd_exit(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    SH_WRITE("Goodbye.\r\n");
    return SHELL_CMD_EXIT;
}

static int cmd_history(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    /* History is per-session in stack; we can't print it from here.
       Shell task would need to pass it down. Basic stub: */
    SH_WRITE("(History stored per-session in shell task)\r\n");
    return SHELL_CMD_OK;
}

static int cmd_sleep(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: sleep <seconds>\r\n"); return SHELL_CMD_ERROR; }
    int s = atoi(argv[1]);
    vTaskDelay(pdMS_TO_TICKS(s * 1000));
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   PROCESS MANAGEMENT
   ══════════════════════════════════════════════════ */

static int cmd_ps(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    os_kernel_print_ps(fd);
    return SHELL_CMD_OK;
}

static int cmd_top(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    os_kernel_print_top(fd);
    return SHELL_CMD_OK;
}

static int cmd_kill(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: kill <pid>\r\n");
        return SHELL_CMD_ERROR;
    }
    os_pid_t pid = (os_pid_t)atoi(argv[1]);
    esp_err_t ret = os_process_kill(pid);
    if (ret == ESP_ERR_NOT_FOUND) {
        SH_PRINTF("kill: no process with pid %d\r\n", pid);
        return SHELL_CMD_ERROR;
    }
    SH_PRINTF("Killed process %d\r\n", pid);
    return SHELL_CMD_OK;
}

static int cmd_suspend(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: suspend <pid>\r\n"); return SHELL_CMD_ERROR; }
    os_pid_t pid = (os_pid_t)atoi(argv[1]);
    if (os_process_suspend(pid) != ESP_OK) {
        SH_PRINTF("suspend: failed for pid %d\r\n", pid);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_resume(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: resume <pid>\r\n"); return SHELL_CMD_ERROR; }
    os_pid_t pid = (os_pid_t)atoi(argv[1]);
    if (os_process_resume(pid) != ESP_OK) {
        SH_PRINTF("resume: failed for pid %d\r\n", pid);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   FILE SYSTEM COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_ls(int fd, int argc, char **argv)
{
    const char *path = (argc >= 2) ? argv[1] : os_fs_getcwd();
    os_fs_print_ls(fd, path);
    return SHELL_CMD_OK;
}

static int cmd_pwd(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    SH_PRINTF("%s\r\n", os_fs_getcwd());
    return SHELL_CMD_OK;
}

static int cmd_cd(int fd, int argc, char **argv)
{
    if (argc < 2) {
        esp_err_t r = os_fs_chdir("/");
        (void)r;
        return SHELL_CMD_OK;
    }
    if (os_fs_chdir(argv[1]) != ESP_OK) {
        SH_PRINTF("cd: %s: No such directory\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_cat(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: cat <file>\r\n"); return SHELL_CMD_ERROR; }
    char buf[1024];
    size_t rsz = 0;
    esp_err_t ret = os_fs_read_file(argv[1], buf, sizeof(buf), &rsz);
    if (ret != ESP_OK) {
        SH_PRINTF("cat: %s: No such file\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    SH_WRITE(buf);
    if (rsz > 0 && buf[rsz-1] != '\n') SH_WRITE("\r\n");
    return SHELL_CMD_OK;
}

static int cmd_write(int fd, int argc, char **argv)
{
    /* write <file> <content...> */
    if (argc < 3) {
        SH_WRITE("Usage: write <file> <content...>\r\n");
        return SHELL_CMD_ERROR;
    }
    char content[512] = {0};
    for (int i = 2; i < argc; i++) {
        strncat(content, argv[i], sizeof(content) - strlen(content) - 2);
        if (i < argc - 1) strncat(content, " ", sizeof(content) - strlen(content) - 1);
    }
    strncat(content, "\n", sizeof(content) - strlen(content) - 1);
    if (os_fs_write_file(argv[1], content, strlen(content), false) != ESP_OK) {
        SH_PRINTF("write: cannot write to '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_append(int fd, int argc, char **argv)
{
    /* append <file> <content...> */
    if (argc < 3) {
        SH_WRITE("Usage: append <file> <content...>\r\n");
        return SHELL_CMD_ERROR;
    }
    char content[512] = {0};
    for (int i = 2; i < argc; i++) {
        strncat(content, argv[i], sizeof(content) - strlen(content) - 2);
        if (i < argc - 1) strncat(content, " ", sizeof(content) - strlen(content) - 1);
    }
    strncat(content, "\n", sizeof(content) - strlen(content) - 1);
    if (os_fs_write_file(argv[1], content, strlen(content), true) != ESP_OK) {
        SH_PRINTF("append: cannot write to '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_rm(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: rm <file>\r\n"); return SHELL_CMD_ERROR; }
    if (os_fs_remove(argv[1]) != ESP_OK) {
        SH_PRINTF("rm: cannot remove '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_mkdir(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: mkdir <dir>\r\n"); return SHELL_CMD_ERROR; }
    if (os_fs_mkdir(argv[1]) != ESP_OK) {
        SH_PRINTF("mkdir: cannot create '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_df(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    size_t total = 0, used = 0;
    os_fs_usage(&total, &used);
    size_t free_sz = total - used;
    SH_PRINTF("\r\n%-20s %10s %10s %10s  %s\r\n",
              "Filesystem", "Size", "Used", "Avail", "Mounted on");
    SH_PRINTF("%-20s %10zu %10zu %10zu  %s\r\n",
              "spiffs", total, used, free_sz, "/");
    SH_PRINTF("\nUsed: %zu%%\r\n\r\n",
              total > 0 ? (used * 100 / total) : 0);
    return SHELL_CMD_OK;
}

static int cmd_mv(int fd, int argc, char **argv)
{
    if (argc < 3) { SH_WRITE("Usage: mv <src> <dst>\r\n"); return SHELL_CMD_ERROR; }
    if (os_fs_rename(argv[1], argv[2]) != ESP_OK) {
        SH_PRINTF("mv: failed\r\n");
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   LOGGING COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_dmesg(int fd, int argc, char **argv)
{
    uint32_t lines = 0;
    if (argc >= 2) lines = (uint32_t)atoi(argv[1]);
    os_log_dump(fd, lines);
    return SHELL_CMD_OK;
}

static int cmd_loglevel(int fd, int argc, char **argv)
{
    if (argc < 2) {
        const char *levels[] = {"debug", "info", "warn", "error", "none"};
        os_log_level_t cur = os_log_get_level();
        SH_PRINTF("Current log level: %s\r\n",
                  cur < 5 ? levels[cur] : "?");
        SH_WRITE("Usage: loglevel <debug|info|warn|error|none>\r\n");
        return SHELL_CMD_OK;
    }
    os_log_level_t lv;
    if      (strcmp(argv[1], "debug") == 0) lv = OS_LOG_DEBUG;
    else if (strcmp(argv[1], "info")  == 0) lv = OS_LOG_INFO;
    else if (strcmp(argv[1], "warn")  == 0) lv = OS_LOG_WARN;
    else if (strcmp(argv[1], "error") == 0) lv = OS_LOG_ERROR;
    else if (strcmp(argv[1], "none")  == 0) lv = OS_LOG_NONE;
    else {
        SH_WRITE("Unknown level. Use: debug|info|warn|error|none\r\n");
        return SHELL_CMD_ERROR;
    }
    os_log_set_level(lv);
    SH_PRINTF("Log level set to '%s'\r\n", argv[1]);
    return SHELL_CMD_OK;
}

static int cmd_logfile(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_PRINTF("File logging: %s\r\n",
                  os_log_get_file_output() ? "enabled" : "disabled");
        SH_WRITE("Usage: logfile <on|off>\r\n");
        return SHELL_CMD_OK;
    }
    bool en = (strcmp(argv[1], "on") == 0 || strcmp(argv[1], "1") == 0);
    os_log_set_file_output(en);
    SH_PRINTF("File logging %s\r\n", en ? "enabled" : "disabled");
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   NETWORKING COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_wifi(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: wifi <scan|connect|disconnect|status>\r\n");
        SH_WRITE("       wifi connect <ssid> [password]\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "scan") == 0) {
        SH_WRITE("Scanning for networks...\r\n");
        os_wifi_scan_result_t results[16];
        int n = os_wifi_scan(results, 16);
        if (n <= 0) { SH_WRITE("No networks found.\r\n"); return SHELL_CMD_OK; }
        SH_PRINTF("\r\n%-32s %-8s %-6s %s\r\n", "SSID", "BSSID", "RSSI", "AUTH");
        SH_PRINTF("%-32s %-8s %-6s %s\r\n", "----", "-----", "----", "----");
        for (int i = 0; i < n; i++) {
            SH_PRINTF("%-32s %02X:%02X:%02X %-6d %s\r\n",
                      results[i].ssid,
                      results[i].bssid[3], results[i].bssid[4], results[i].bssid[5],
                      results[i].rssi,
                      results[i].open ? "Open" : "WPA");
        }
        SH_PRINTF("\r\n%d network(s) found\r\n", n);
    }
    else if (strcmp(argv[1], "connect") == 0) {
        if (argc < 3) { SH_WRITE("wifi connect <ssid> [password]\r\n"); return SHELL_CMD_ERROR; }
        const char *ssid = argv[2];
        const char *pass = (argc >= 4) ? argv[3] : "";
        SH_PRINTF("Connecting to '%s'...\r\n", ssid);
        esp_err_t ret = os_wifi_connect(ssid, pass);
        if (ret == ESP_OK) {
            SH_WRITE("\033[32mConnected!\033[0m\r\n");
        } else {
            SH_WRITE("\033[31mConnection failed.\033[0m\r\n");
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "disconnect") == 0) {
        os_wifi_disconnect();
        SH_WRITE("WiFi disconnected\r\n");
    }
    else if (strcmp(argv[1], "status") == 0) {
        os_net_status_t st;
        os_wifi_get_status(&st);
        SH_PRINTF("\r\nWiFi Status:  %s\r\n",
                  st.connected ? "\033[32mConnected\033[0m" : "\033[31mDisconnected\033[0m");
        if (st.connected) {
            SH_PRINTF("SSID:         %s\r\n", st.ssid);
            SH_PRINTF("IP Address:   %s\r\n", st.ip);
            SH_PRINTF("Gateway:      %s\r\n", st.gw);
            SH_PRINTF("Netmask:      %s\r\n", st.netmask);
            SH_PRINTF("RSSI:         %d dBm\r\n", st.rssi);
            SH_PRINTF("MAC:          %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                      st.mac[0],st.mac[1],st.mac[2],
                      st.mac[3],st.mac[4],st.mac[5]);
        }
        SH_WRITE("\r\n");
    }
    else {
        SH_PRINTF("wifi: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_ifconfig(int fd, int argc, char **argv)
{
    (void)argc; (void)argv;
    os_net_status_t st;
    os_wifi_get_status(&st);

    SH_WRITE("\r\nwlan0: ");
    if (st.connected) {
        SH_PRINTF("inet %s  netmask %s  bcast %s\r\n",
                  st.ip, st.netmask, st.gw);
        SH_PRINTF("        ether %02X:%02X:%02X:%02X:%02X:%02X\r\n",
                  st.mac[0],st.mac[1],st.mac[2],
                  st.mac[3],st.mac[4],st.mac[5]);
    } else {
        SH_WRITE("DOWN\r\n");
    }
    SH_WRITE("\r\n");
    return SHELL_CMD_OK;
}

static int cmd_ping(int fd, int argc, char **argv)
{
    if (argc < 2) { SH_WRITE("Usage: ping <host>\r\n"); return SHELL_CMD_ERROR; }
    int count = 4;
    if (argc >= 4 && strcmp(argv[2], "-c") == 0) count = atoi(argv[3]);

    SH_PRINTF("PING %s (%d packets)\r\n", argv[1], count);
    os_ping_result_t res;
    esp_err_t ret = os_ping(argv[1], count, &res);
    if (ret != ESP_OK) {
        SH_WRITE("ping: network unreachable or host down\r\n");
        return SHELL_CMD_ERROR;
    }
    SH_PRINTF("\r\n--- %s ping statistics ---\r\n", argv[1]);
    SH_PRINTF("%"PRIu32" packets transmitted, %"PRIu32" received, %"PRIu32"%% packet loss\r\n",
              res.sent, res.received,
              res.sent > 0 ? ((res.sent - res.received) * 100 / res.sent) : 100);
    if (res.received > 0) {
        SH_PRINTF("rtt min/avg/max = %"PRIu32"/%"PRIu32"/%"PRIu32" ms\r\n",
                  res.min_ms, res.avg_ms, res.max_ms);
    }
    return SHELL_CMD_OK;
}

static int cmd_http(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: http <url>\r\n");
        return SHELL_CMD_ERROR;
    }
    char response[2048];
    int len = os_http_get(argv[1], response, sizeof(response));
    if (len < 0) {
        SH_WRITE("http: request failed\r\n");
        return SHELL_CMD_ERROR;
    }
    SH_WRITE(response);
    SH_WRITE("\r\n");
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   GPIO / HARDWARE COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_gpio(int fd, int argc, char **argv)
{
    if (argc < 3) {
        SH_WRITE("Usage: gpio read  <pin>\r\n");
        SH_WRITE("       gpio write <pin> <0|1>\r\n");
        SH_WRITE("       gpio mode  <pin> <in|out|in_pullup|in_pulldown>\r\n");
        SH_WRITE("       gpio info\r\n");
        return SHELL_CMD_ERROR;
    }

    int pin = atoi(argv[2]);

    if (strcmp(argv[1], "read") == 0) {
        int val = gpio_driver_read(pin);
        if (val < 0) {
            SH_PRINTF("gpio: invalid pin %d\r\n", pin);
            return SHELL_CMD_ERROR;
        }
        SH_PRINTF("GPIO%d = %d\r\n", pin, val);
    }
    else if (strcmp(argv[1], "write") == 0) {
        if (argc < 4) { SH_WRITE("gpio write <pin> <0|1>\r\n"); return SHELL_CMD_ERROR; }
        int val = atoi(argv[3]);
        if (gpio_driver_write(pin, val) != ESP_OK) {
            SH_PRINTF("gpio: write failed on pin %d\r\n", pin);
            return SHELL_CMD_ERROR;
        }
        SH_PRINTF("GPIO%d <= %d\r\n", pin, val);
    }
    else if (strcmp(argv[1], "mode") == 0) {
        if (argc < 4) { SH_WRITE("gpio mode <pin> <in|out|in_pullup|in_pulldown>\r\n"); return SHELL_CMD_ERROR; }
        gpio_dir_t dir;
        if      (strcmp(argv[3], "out")         == 0) dir = GPIO_DIR_OUTPUT;
        else if (strcmp(argv[3], "in")          == 0) dir = GPIO_DIR_INPUT;
        else if (strcmp(argv[3], "in_pullup")   == 0) dir = GPIO_DIR_INPUT_PULLUP;
        else if (strcmp(argv[3], "in_pulldown") == 0) dir = GPIO_DIR_INPUT_PULLDOWN;
        else {
            SH_WRITE("gpio mode: in|out|in_pullup|in_pulldown\r\n");
            return SHELL_CMD_ERROR;
        }
        if (gpio_driver_set_dir(pin, dir) != ESP_OK) {
            SH_PRINTF("gpio: mode failed on pin %d\r\n", pin);
            return SHELL_CMD_ERROR;
        }
        SH_PRINTF("GPIO%d mode = %s\r\n", pin, argv[3]);
    }
    else if (strcmp(argv[1], "info") == 0) {
        gpio_driver_print_info(fd);
    }
    else {
        SH_PRINTF("gpio: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }
    return SHELL_CMD_OK;
}

static int cmd_adc(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: adc read  <channel>   (channel 0-9)\r\n");
        SH_WRITE("       adc readv <channel>   (returns millivolts)\r\n");
        SH_WRITE("       adc readall\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "read") == 0) {
        if (argc < 3) { SH_WRITE("adc read <channel>\r\n"); return SHELL_CMD_ERROR; }
        int ch = atoi(argv[2]);
        int raw = adc_driver_read_raw(ch);
        if (raw < 0) { SH_PRINTF("adc: invalid channel %d\r\n", ch); return SHELL_CMD_ERROR; }
        SH_PRINTF("ADC ch%d raw = %d  (%.1f%%)\r\n", ch, raw, raw * 100.0f / 4095.0f);
    }
    else if (strcmp(argv[1], "readv") == 0) {
        if (argc < 3) { SH_WRITE("adc readv <channel>\r\n"); return SHELL_CMD_ERROR; }
        int ch = atoi(argv[2]);
        int mv = adc_driver_read_mv(ch);
        if (mv < 0) { SH_PRINTF("adc: invalid channel %d\r\n", ch); return SHELL_CMD_ERROR; }
        SH_PRINTF("ADC ch%d = %d mV  (%.2f V)\r\n", ch, mv, mv / 1000.0f);
    }
    else if (strcmp(argv[1], "readall") == 0) {
        SH_WRITE("\r\nADC Channel Readings:\r\n");
        SH_WRITE("─────────────────────────────\r\n");
        for (int ch = 0; ch <= 7; ch++) {
            int raw = adc_driver_read_raw(ch);
            int mv  = adc_driver_read_mv(ch);
            if (raw >= 0) {
                SH_PRINTF("  ch%-2d  raw=%4d  %4d mV  [%.*s%.*s]\r\n",
                          ch, raw, mv,
                          raw * 20 / 4095, "████████████████████",
                          20 - raw * 20 / 4095, "░░░░░░░░░░░░░░░░░░░░");
            }
        }
        SH_WRITE("\r\n");
    }
    return SHELL_CMD_OK;
}

static int cmd_i2c(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: i2c scan  [sda_pin] [scl_pin]\r\n");
        SH_WRITE("       i2c read  <addr> <reg> <len>\r\n");
        SH_WRITE("       i2c write <addr> <reg> <byte...>\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "scan") == 0) {
        int sda = (argc >= 3) ? atoi(argv[2]) : I2C_DEFAULT_SDA;
        int scl = (argc >= 4) ? atoi(argv[3]) : I2C_DEFAULT_SCL;
        SH_PRINTF("\r\nI2C scan (SDA=%d SCL=%d):\r\n", sda, scl);
        SH_WRITE("     0  1  2  3  4  5  6  7  8  9  a  b  c  d  e  f\r\n");
        i2c_driver_scan(sda, scl, fd);
    }
    else if (strcmp(argv[1], "read") == 0) {
        if (argc < 5) { SH_WRITE("i2c read <addr> <reg> <len>\r\n"); return SHELL_CMD_ERROR; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 0);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 0);
        int     len  = atoi(argv[4]);
        if (len <= 0 || len > 64) len = 1;
        uint8_t buf[64];
        esp_err_t ret = i2c_driver_read(addr, reg, buf, len);
        if (ret != ESP_OK) {
            SH_PRINTF("i2c read failed (addr=0x%02X)\r\n", addr);
            return SHELL_CMD_ERROR;
        }
        SH_PRINTF("I2C 0x%02X reg 0x%02X:", addr, reg);
        for (int i = 0; i < len; i++) SH_PRINTF(" %02X", buf[i]);
        SH_WRITE("\r\n");
    }
    else if (strcmp(argv[1], "write") == 0) {
        if (argc < 5) { SH_WRITE("i2c write <addr> <reg> <byte...>\r\n"); return SHELL_CMD_ERROR; }
        uint8_t addr = (uint8_t)strtol(argv[2], NULL, 0);
        uint8_t reg  = (uint8_t)strtol(argv[3], NULL, 0);
        uint8_t data[16]; int len = 0;
        for (int i = 4; i < argc && len < 16; i++) {
            data[len++] = (uint8_t)strtol(argv[i], NULL, 0);
        }
        if (i2c_driver_write(addr, reg, data, len) != ESP_OK) {
            SH_PRINTF("i2c write failed (addr=0x%02X)\r\n", addr);
            return SHELL_CMD_ERROR;
        }
        SH_PRINTF("I2C 0x%02X reg 0x%02X <- %d byte(s)\r\n", addr, reg, len);
    }
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   NVS COMMANDS
   ══════════════════════════════════════════════════ */
static int cmd_nvs(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: nvs get <key>\r\n");
        SH_WRITE("       nvs set <key> <value>\r\n");
        SH_WRITE("       nvs del <key>\r\n");
        SH_WRITE("       nvs erase\r\n");
        return SHELL_CMD_ERROR;
    }
    nvs_handle_t h;
    if (strcmp(argv[1], "erase") == 0) {
        nvs_flash_erase();
        nvs_flash_init();
        SH_WRITE("NVS erased and reinitialized.\r\n");
        return SHELL_CMD_OK;
    }
    if (argc < 3) { SH_WRITE("nvs: missing key argument\r\n"); return SHELL_CMD_ERROR; }
    nvs_open("esp32os", NVS_READWRITE, &h);
    if (strcmp(argv[1], "get") == 0) {
        char val[64] = {0};
        size_t sz = sizeof(val);
        if (nvs_get_str(h, argv[2], val, &sz) == ESP_OK)
            SH_PRINTF("%s = %s\r\n", argv[2], val);
        else
            SH_PRINTF("nvs: key '%s' not found\r\n", argv[2]);
    } else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) { SH_WRITE("nvs set <key> <value>\r\n"); nvs_close(h); return SHELL_CMD_ERROR; }
        nvs_set_str(h, argv[2], argv[3]);
        nvs_commit(h);
        SH_PRINTF("nvs: %s = %s\r\n", argv[2], argv[3]);
    } else if (strcmp(argv[1], "del") == 0) {
        nvs_erase_key(h, argv[2]);
        nvs_commit(h);
        SH_PRINTF("nvs: deleted '%s'\r\n", argv[2]);
    }
    nvs_close(h);
    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   Command Registration
   ══════════════════════════════════════════════════ */
void shell_commands_register_all(void)
{
    /* System */
    static const shell_command_t cmds[] = {
        /* name          description                                            usage                        fn */
        {"help",      "Show available commands",                            "help",                      cmd_help},
        {"clear",     "Clear terminal screen",                              "clear",                     cmd_clear},
        {"uname",     "Print system information",                           "uname [-a]",                cmd_uname},
        {"uptime",    "Show system uptime",                                 "uptime",                    cmd_uptime},
        {"free",      "Display memory usage",                               "free",                      cmd_free},
        {"reboot",    "Reboot the system",                                  "reboot",                    cmd_reboot},
        {"echo",      "Print text to terminal",                             "echo [text...]",            cmd_echo},
        {"exit",      "Close current session",                              "exit",                      cmd_exit},
        {"sleep",     "Sleep for N seconds",                                "sleep <seconds>",           cmd_sleep},
        {"history",   "Show command history",                               "history",                   cmd_history},
        /* Process management */
        {"ps",        "List running processes",                             "ps",                        cmd_ps},
        {"top",       "Show CPU/memory usage",                              "top",                       cmd_top},
        {"kill",      "Terminate a process by PID",                         "kill <pid>",                cmd_kill},
        {"suspend",   "Suspend a process",                                  "suspend <pid>",             cmd_suspend},
        {"resume",    "Resume a suspended process",                         "resume <pid>",              cmd_resume},
        /* File system */
        {"ls",        "List directory contents",                            "ls [path]",                 cmd_ls},
        {"pwd",       "Print working directory",                            "pwd",                       cmd_pwd},
        {"cd",        "Change directory",                                   "cd [path]",                 cmd_cd},
        {"cat",       "Print file contents",                                "cat <file>",                cmd_cat},
        {"write",     "Write text to file (overwrite)",                     "write <file> <text...>",    cmd_write},
        {"append",    "Append text to file",                                "append <file> <text...>",   cmd_append},
        {"rm",        "Remove a file",                                      "rm <file>",                 cmd_rm},
        {"mkdir",     "Create a directory",                                 "mkdir <dir>",               cmd_mkdir},
        {"mv",        "Move/rename a file",                                 "mv <src> <dst>",            cmd_mv},
        {"df",        "Show filesystem usage",                              "df",                        cmd_df},
        /* Logging */
        {"dmesg",     "Show kernel log buffer",                             "dmesg [lines]",             cmd_dmesg},
        {"loglevel",  "Get or set log level",                               "loglevel [debug|info|warn|error|none]", cmd_loglevel},
        {"logfile",   "Enable/disable file logging",                        "logfile <on|off>",          cmd_logfile},
        /* Networking */
        {"wifi",      "WiFi management",                                    "wifi <scan|connect|disconnect|status>", cmd_wifi},
        {"ifconfig",  "Show network interface info",                        "ifconfig",                  cmd_ifconfig},
        {"ping",      "Ping a host",                                        "ping <host> [-c count]",    cmd_ping},
        {"http",      "HTTP GET request",                                   "http <url>",                cmd_http},
        /* Hardware */
        {"gpio",      "GPIO read/write/mode control",                       "gpio <read|write|mode|info>", cmd_gpio},
        {"adc",       "ADC channel read",                                   "adc <read|readv|readall> [ch]", cmd_adc},
        {"i2c",       "I2C scan and communicate",                           "i2c <scan|read|write>",     cmd_i2c},
        /* NVS */
        {"nvs",       "Non-volatile storage key/value",                     "nvs <get|set|del|erase>",   cmd_nvs},
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        shell_register_command(&cmds[i]);
    }

    OS_LOGI(TAG, "Registered %zu built-in commands", sizeof(cmds) / sizeof(cmds[0]));
}
