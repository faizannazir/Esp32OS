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


#include "os_pwm.h"
#include "os_ipc.h"
#include "os_mqtt.h"
#include "os_ota.h"

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

/* Test suite entry points from component test sources */
extern void os_mqtt_test_run_all(void);
extern void os_ipc_test_run_all(void);
extern void os_ota_test_run_all(void);
extern void os_pwm_test_run_all(void);

/* ────────────────────────────────────────────────
   Utility macro
   ──────────────────────────────────────────────── */
#define SH_PRINTF(fmt, ...)  shell_printf(fd, fmt, ##__VA_ARGS__)
#define SH_WRITE(s)          shell_write(fd, s)

static int hex_nibble(char c)
{
    if (c >= '0' && c <= '9') return c - '0';
    if (c >= 'a' && c <= 'f') return 10 + (c - 'a');
    if (c >= 'A' && c <= 'F') return 10 + (c - 'A');
    return -1;
}

static size_t hex_to_bytes(const char *hex, uint8_t *out, size_t out_max)
{
    if (hex == NULL || out == NULL) {
        return 0;
    }

    size_t len = strlen(hex);
    if ((len % 2) != 0) {
        return 0;
    }

    size_t byte_len = len / 2;
    if (byte_len > out_max) {
        return 0;
    }

    for (size_t i = 0; i < byte_len; i++) {
        int hi = hex_nibble(hex[i * 2]);
        int lo = hex_nibble(hex[i * 2 + 1]);
        if (hi < 0 || lo < 0) {
            return 0;
        }
        out[i] = (uint8_t)((hi << 4) | lo);
    }

    return byte_len;
}

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
   PWM COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_pwm(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: pwm init <channel> <pin> <freq_hz>\r\n");
        SH_WRITE("       pwm duty <channel> <percent>\r\n");
        SH_WRITE("       pwm freq <channel> <freq_hz>\r\n");
        SH_WRITE("       pwm deinit <channel>\r\n");
        SH_WRITE("       pwm status\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "init") == 0) {
        if (argc < 5) {
            SH_WRITE("Usage: pwm init <channel> <pin> <freq_hz>\r\n");
            return SHELL_CMD_ERROR;
        }
        int channel = atoi(argv[2]);
        int pin = atoi(argv[3]);
        uint32_t freq = (uint32_t)atoi(argv[4]);

        esp_err_t ret = os_pwm_channel_init(channel, pin, freq);
        if (ret == ESP_OK) {
            SH_PRINTF("PWM channel %d initialized on GPIO%d @ %lu Hz\r\n", channel, pin, freq);
        } else {
            SH_PRINTF("Failed to init PWM: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "duty") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: pwm duty <channel> <percent>\r\n");
            return SHELL_CMD_ERROR;
        }
        int channel = atoi(argv[2]);
        int duty = atoi(argv[3]);

        esp_err_t ret = os_pwm_set_duty(channel, duty);
        if (ret == ESP_OK) {
            SH_PRINTF("PWM channel %d duty set to %d%%\r\n", channel, duty);
        } else {
            SH_PRINTF("Failed to set duty: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "freq") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: pwm freq <channel> <freq_hz>\r\n");
            return SHELL_CMD_ERROR;
        }
        int channel = atoi(argv[2]);
        uint32_t freq = (uint32_t)atoi(argv[3]);

        esp_err_t ret = os_pwm_set_freq(channel, freq);
        if (ret == ESP_OK) {
            SH_PRINTF("PWM channel %d frequency set to %lu Hz\r\n", channel, freq);
        } else {
            SH_PRINTF("Failed to set frequency: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "deinit") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: pwm deinit <channel>\r\n");
            return SHELL_CMD_ERROR;
        }
        int channel = atoi(argv[2]);

        esp_err_t ret = os_pwm_channel_deinit(channel);
        if (ret == ESP_OK) {
            SH_PRINTF("PWM channel %d deinitialized\r\n", channel);
        } else {
            SH_PRINTF("Failed to deinit: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        uint8_t active = os_pwm_get_active_count();
        SH_PRINTF("Active PWM channels: %d/%d\r\n", active, OS_PWM_MAX_CHANNELS);

        for (int i = 0; i < OS_PWM_MAX_CHANNELS; i++) {
            if (os_pwm_channel_is_active(i)) {
                uint8_t gpio, duty;
                uint32_t freq;
                os_pwm_get_config(i, &gpio, &freq, &duty);
                SH_PRINTF("  Channel %d: GPIO%d @ %lu Hz, duty=%d%%\r\n", i, gpio, freq, duty);
            }
        }
    }
    else {
        SH_PRINTF("pwm: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   IPC COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_msgq(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: msgq create <name> <size> <count>\r\n");
        SH_WRITE("       msgq delete <name>\r\n");
        SH_WRITE("       msgq send <name> <data>\r\n");
        SH_WRITE("       msgq recv <name> [timeout_ms]\r\n");
        SH_WRITE("       msgq list\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "create") == 0) {
        if (argc < 5) {
            SH_WRITE("Usage: msgq create <name> <size> <count>\r\n");
            return SHELL_CMD_ERROR;
        }
        size_t msg_size = (size_t)atoi(argv[3]);
        uint8_t max_msgs = (uint8_t)atoi(argv[4]);

        os_msgq_t q = os_msgq_create(argv[2], msg_size, max_msgs);
        if (q != NULL) {
            SH_PRINTF("Message queue '%s' created: size=%zu, count=%d\r\n", argv[2], msg_size, max_msgs);
        } else {
            SH_WRITE("Failed to create message queue\r\n");
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: msgq delete <name>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_msgq_t q = os_msgq_find(argv[2]);
        if (q == NULL) {
            SH_PRINTF("Message queue '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_msgq_delete(q);
        SH_PRINTF("Message queue '%s' deleted\r\n", argv[2]);
    }
    else if (strcmp(argv[1], "send") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: msgq send <name> <data>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_msgq_t q = os_msgq_find(argv[2]);
        if (q == NULL) {
            SH_PRINTF("Message queue '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        /* Send string data */
        esp_err_t ret = os_msgq_send(q, argv[3], portMAX_DELAY);
        if (ret == ESP_OK) {
            SH_PRINTF("Sent to '%s': %s\r\n", argv[2], argv[3]);
        } else {
            SH_PRINTF("Send failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "recv") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: msgq recv <name> [timeout_ms]\r\n");
            return SHELL_CMD_ERROR;
        }
        os_msgq_t q = os_msgq_find(argv[2]);
        if (q == NULL) {
            SH_PRINTF("Message queue '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        uint32_t timeout = (argc >= 4) ? (uint32_t)atoi(argv[3]) : 5000;
        char buf[OS_IPC_MAX_MSG_SIZE];
        esp_err_t ret = os_msgq_receive(q, buf, timeout);
        if (ret == ESP_OK) {
            SH_PRINTF("Received from '%s': %s\r\n", argv[2], buf);
        } else if (ret == ESP_ERR_TIMEOUT) {
            SH_WRITE("Receive timeout\r\n");
            return SHELL_CMD_ERROR;
        } else {
            SH_PRINTF("Receive failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "list") == 0) {
        os_msgq_list(fd);
    }
    else {
        SH_PRINTF("msgq: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    return SHELL_CMD_OK;
}

static int cmd_event(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: event create <name>\r\n");
        SH_WRITE("       event delete <name>\r\n");
        SH_WRITE("       event set <name> <bits>\r\n");
        SH_WRITE("       event clear <name> <bits>\r\n");
        SH_WRITE("       event get <name>\r\n");
        SH_WRITE("       event wait <name> <bits> [timeout_ms]\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "create") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: event create <name>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_create(argv[2]);
        if (ev != NULL) {
            SH_PRINTF("Event group '%s' created\r\n", argv[2]);
        } else {
            SH_WRITE("Failed to create event group\r\n");
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "delete") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: event delete <name>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_find(argv[2]);
        if (ev == NULL) {
            SH_PRINTF("Event group '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_event_delete(ev);
        SH_PRINTF("Event group '%s' deleted\r\n", argv[2]);
    }
    else if (strcmp(argv[1], "set") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: event set <name> <bits>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_find(argv[2]);
        if (ev == NULL) {
            SH_PRINTF("Event group '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_event_bits_t bits = (os_event_bits_t)strtoul(argv[3], NULL, 0);
        os_event_set(ev, bits);
        SH_PRINTF("Event bits 0x%08X set on '%s'\r\n", (unsigned)bits, argv[2]);
    }
    else if (strcmp(argv[1], "clear") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: event clear <name> <bits>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_find(argv[2]);
        if (ev == NULL) {
            SH_PRINTF("Event group '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_event_bits_t bits = (os_event_bits_t)strtoul(argv[3], NULL, 0);
        os_event_clear(ev, bits);
        SH_PRINTF("Event bits 0x%08X cleared on '%s'\r\n", (unsigned)bits, argv[2]);
    }
    else if (strcmp(argv[1], "get") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: event get <name>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_find(argv[2]);
        if (ev == NULL) {
            SH_PRINTF("Event group '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_event_bits_t bits = os_event_get(ev);
        SH_PRINTF("Event group '%s': bits=0x%08X\r\n", argv[2], (unsigned)bits);
    }
    else if (strcmp(argv[1], "wait") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: event wait <name> <bits> [timeout_ms]\r\n");
            return SHELL_CMD_ERROR;
        }
        os_event_t ev = os_event_find(argv[2]);
        if (ev == NULL) {
            SH_PRINTF("Event group '%s' not found\r\n", argv[2]);
            return SHELL_CMD_ERROR;
        }
        os_event_bits_t bits = (os_event_bits_t)strtoul(argv[3], NULL, 0);
        uint32_t timeout = (argc >= 5) ? (uint32_t)atoi(argv[4]) : portMAX_DELAY;
        os_event_bits_t result = os_event_wait(ev, bits, true, false, timeout);
        if (result != 0) {
            SH_PRINTF("Event wait returned: 0x%08X\r\n", (unsigned)result);
        } else {
            SH_WRITE("Event wait timeout\r\n");
            return SHELL_CMD_ERROR;
        }
    }
    else {
        SH_PRINTF("event: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   MQTT COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_mqtt(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: mqtt config <broker_url>\r\n");
        SH_WRITE("       mqtt connect\r\n");
        SH_WRITE("       mqtt disconnect\r\n");
        SH_WRITE("       mqtt status\r\n");
        SH_WRITE("       mqtt pub <topic> <message> [-q QoS]      (text payload)\r\n");
        SH_WRITE("       mqtt pubhex <topic> <hex> [-q QoS]       (binary payload)\r\n");
        SH_WRITE("       mqtt sub <topic> [-q QoS]\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "config") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: mqtt config <broker_url>\r\n");
            return SHELL_CMD_ERROR;
        }
        os_mqtt_config_t config = {0};
        strncpy(config.broker_url, argv[2], sizeof(config.broker_url) - 1);
        esp_err_t ret = os_mqtt_config(&config);
        if (ret == ESP_OK) {
            SH_PRINTF("MQTT configured: %s\r\n", argv[2]);
        } else {
            SH_PRINTF("Config failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "connect") == 0) {
        esp_err_t ret = os_mqtt_connect();
        if (ret == ESP_OK) {
            SH_WRITE("MQTT connecting...\r\n");
        } else {
            SH_PRINTF("Connect failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "disconnect") == 0) {
        esp_err_t ret = os_mqtt_disconnect();
        if (ret == ESP_OK) {
            SH_WRITE("MQTT disconnected\r\n");
        } else {
            SH_PRINTF("Disconnect failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        os_mqtt_state_t state = os_mqtt_get_state();
        SH_PRINTF("MQTT state: %s\r\n", os_mqtt_get_state_str(state));

        os_mqtt_stats_t stats;
        os_mqtt_get_stats(&stats);
        SH_PRINTF("Messages: sent=%lu, received=%lu, dropped=%lu\r\n",
                  stats.messages_sent, stats.messages_received, stats.messages_dropped);
    }
    else if (strcmp(argv[1], "pub") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: mqtt pub <topic> <message> [-q QoS]\r\n");
            return SHELL_CMD_ERROR;
        }
        os_mqtt_qos_t qos = OS_MQTT_QOS_0;
        if (argc >= 6 && strcmp(argv[4], "-q") == 0) {
            qos = (os_mqtt_qos_t)atoi(argv[5]);
        }
        esp_err_t ret = os_mqtt_publish(argv[2], argv[3], strlen(argv[3]), qos, false);
        if (ret == ESP_OK) {
            SH_PRINTF("Published to '%s'\r\n", argv[2]);
        } else {
            SH_PRINTF("Publish failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "pubhex") == 0) {
        if (argc < 4) {
            SH_WRITE("Usage: mqtt pubhex <topic> <hex> [-q QoS]\r\n");
            return SHELL_CMD_ERROR;
        }
        os_mqtt_qos_t qos = OS_MQTT_QOS_0;
        if (argc >= 6 && strcmp(argv[4], "-q") == 0) {
            qos = (os_mqtt_qos_t)atoi(argv[5]);
        }

        uint8_t payload[OS_MQTT_MAX_PAYLOAD_LEN];
        size_t payload_len = hex_to_bytes(argv[3], payload, sizeof(payload));
        if (payload_len == 0) {
            SH_WRITE("Invalid hex payload (must be even-length hex, max 2048 hex characters (1024 bytes binary))\r\n");
            return SHELL_CMD_ERROR;
        }

        esp_err_t ret = os_mqtt_publish(argv[2], (const char *)payload, payload_len, qos, false);
        if (ret == ESP_OK) {
            SH_PRINTF("Published %zu-byte binary payload to '%s'\r\n", payload_len, argv[2]);
        } else {
            SH_PRINTF("Publish failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "sub") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: mqtt sub <topic> [-q QoS]\r\n");
            return SHELL_CMD_ERROR;
        }
        os_mqtt_qos_t qos = OS_MQTT_QOS_0;
        if (argc >= 5 && strcmp(argv[3], "-q") == 0) {
            qos = (os_mqtt_qos_t)atoi(argv[4]);
        }
        esp_err_t ret = os_mqtt_subscribe(argv[2], qos);
        if (ret == ESP_OK) {
            SH_PRINTF("Subscribed to '%s'\r\n", argv[2]);
        } else {
            SH_PRINTF("Subscribe failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else {
        SH_PRINTF("mqtt: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   OTA COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_ota(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: ota update <url>\r\n");
        SH_WRITE("       ota status\r\n");
        SH_WRITE("       ota confirm\r\n");
        SH_WRITE("       ota rollback\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "update") == 0) {
        if (argc < 3) {
            SH_WRITE("Usage: ota update <url>\r\n");
            return SHELL_CMD_ERROR;
        }
        if (os_ota_is_in_progress()) {
            SH_WRITE("OTA already in progress\r\n");
            return SHELL_CMD_ERROR;
        }
        os_ota_config_t config = {
            .url = argv[2],
            .download_timeout_sec = 300
        };
        esp_err_t ret = os_ota_start(&config);
        if (ret == ESP_OK) {
            SH_PRINTF("OTA started from: %s\r\n", argv[2]);
            SH_WRITE("Check status with: ota status\r\n");
        } else {
            SH_PRINTF("OTA start failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "status") == 0) {
        os_ota_info_t info;
        os_ota_get_info(&info);
        SH_PRINTF("OTA State: %s (%d%%)\r\n", os_ota_get_state_str(info.state), info.progress);
        SH_PRINTF("Running partition: %s\r\n", info.current_version);
        SH_PRINTF("Can rollback: %s\r\n", info.can_rollback ? "yes" : "no");
        if (os_ota_is_in_progress()) {
            SH_PRINTF("Downloaded: %zu/%zu bytes\r\n", info.bytes_downloaded, info.total_size);
        }
        if (info.error_message[0] != '\0') {
            SH_PRINTF("Last error: %s\r\n", info.error_message);
        }
    }
    else if (strcmp(argv[1], "confirm") == 0) {
        esp_err_t ret = os_ota_confirm();
        if (ret == ESP_OK) {
            SH_WRITE("Firmware confirmed\r\n");
        } else {
            SH_PRINTF("Confirm failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else if (strcmp(argv[1], "rollback") == 0) {
        if (!os_ota_can_rollback()) {
            SH_WRITE("No rollback available\r\n");
            return SHELL_CMD_ERROR;
        }
        SH_WRITE("Initiating rollback...\r\n");
        esp_err_t ret = os_ota_rollback();
        if (ret != ESP_OK) {
            SH_PRINTF("Rollback failed: %s\r\n", esp_err_to_name(ret));
            return SHELL_CMD_ERROR;
        }
    }
    else {
        SH_PRINTF("ota: unknown subcommand '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    return SHELL_CMD_OK;
}

/* ══════════════════════════════════════════════════
   TEST COMMANDS
   ══════════════════════════════════════════════════ */

static int cmd_test(int fd, int argc, char **argv)
{
    if (argc < 2) {
        SH_WRITE("Usage: test <mqtt|ipc|ota|pwm|all>\r\n");
        return SHELL_CMD_ERROR;
    }

    if (strcmp(argv[1], "mqtt") == 0) {
        SH_WRITE("Running MQTT test suite...\r\n");
        os_mqtt_test_run_all();
    }
    else if (strcmp(argv[1], "ipc") == 0) {
        SH_WRITE("Running IPC test suite...\r\n");
        os_ipc_test_run_all();
    }
    else if (strcmp(argv[1], "ota") == 0) {
        SH_WRITE("Running OTA test suite...\r\n");
        os_ota_test_run_all();
    }
    else if (strcmp(argv[1], "pwm") == 0) {
        SH_WRITE("Running PWM test suite...\r\n");
        os_pwm_test_run_all();
    }
    else if (strcmp(argv[1], "all") == 0) {
        SH_WRITE("Running all feature test suites (MQTT, IPC, OTA, PWM)...\r\n");
        os_mqtt_test_run_all();
        os_ipc_test_run_all();
        os_ota_test_run_all();
        os_pwm_test_run_all();
    }
    else {
        SH_PRINTF("test: unknown suite '%s'\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    SH_WRITE("Test suite execution complete.\r\n");
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
        {"pwm",       "PWM control for motors/LEDs",                          "pwm <init|duty|freq|deinit|status>", cmd_pwm},
        /* IPC */
        {"msgq",      "Message queue operations",                            "msgq <create|delete|send|recv|list>", cmd_msgq},
        {"event",     "Event group operations",                              "event <create|delete|set|clear|get|wait>", cmd_event},
        /* MQTT */
        {"mqtt",      "MQTT client operations",                              "mqtt <config|connect|disconnect|status|pub|sub>", cmd_mqtt},
        /* OTA */
        {"ota",       "OTA firmware update",                                 "ota <update|status|confirm|rollback>", cmd_ota},
        /* Tests */
        {"test",      "Run feature test suites",                              "test <mqtt|ipc|ota|pwm|all>", cmd_test},
        /* NVS */
        {"nvs",       "Non-volatile storage key/value",                     "nvs <get|set|del|erase>",   cmd_nvs},
    };

    for (size_t i = 0; i < sizeof(cmds) / sizeof(cmds[0]); i++) {
        shell_register_command(&cmds[i]);
    }

    OS_LOGI(TAG, "Registered %zu built-in commands", sizeof(cmds) / sizeof(cmds[0]));
}
