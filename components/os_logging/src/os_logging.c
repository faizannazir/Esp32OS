#include "os_logging.h"

#include <stdio.h>
#include <string.h>
#include <stdarg.h>
#include <time.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_timer.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

/* ────────────────────────────────────────────────
   ANSI colour codes for terminal output
   ──────────────────────────────────────────────── */
#define ANSI_RESET   "\033[0m"
#define ANSI_RED     "\033[31m"
#define ANSI_YELLOW  "\033[33m"
#define ANSI_CYAN    "\033[36m"
#define ANSI_WHITE   "\033[37m"
#define ANSI_BOLD    "\033[1m"

/* ────────────────────────────────────────────────
   Internal state
   ──────────────────────────────────────────────── */
typedef struct {
    os_log_entry_t  ring[OS_LOG_RING_SIZE];
    uint32_t        head;           /* next write index   */
    uint32_t        count;          /* total entries ever */
    os_log_level_t  level;
    bool            file_output;
    bool            initialised;
    SemaphoreHandle_t lock;
    FILE            *logfile;
} log_state_t;

static log_state_t s_log = {
    .head        = 0,
    .count       = 0,
    .level       = OS_LOG_DEBUG,
    .file_output = false,
    .initialised = false,
    .lock        = NULL,
    .logfile     = NULL
};

/* ────────────────────────────────────────────────
   Helpers
   ──────────────────────────────────────────────── */
static const char *level_str(os_log_level_t l)
{
    switch (l) {
    case OS_LOG_DEBUG: return "DBG";
    case OS_LOG_INFO:  return "INF";
    case OS_LOG_WARN:  return "WRN";
    case OS_LOG_ERROR: return "ERR";
    default:           return "???";
    }
}

static const char *level_colour(os_log_level_t l)
{
    switch (l) {
    case OS_LOG_DEBUG: return ANSI_CYAN;
    case OS_LOG_INFO:  return ANSI_WHITE;
    case OS_LOG_WARN:  return ANSI_YELLOW;
    case OS_LOG_ERROR: return ANSI_RED ANSI_BOLD;
    default:           return ANSI_RESET;
    }
}

static void uart_write_str(const char *s)
{
    uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, s, strlen(s));
}

static void write_to_sinks(const os_log_entry_t *e)
{
    /* Format: [   1234.567] INF  TAG: message\n */
    char line[OS_LOG_MAX_MSG_LEN + 80];
    uint32_t ms  = e->timestamp_ms;
    uint32_t sec = ms / 1000;
    uint32_t frac = ms % 1000;

    /* Coloured UART output */
    char uart_line[sizeof(line) + 32];
    snprintf(uart_line, sizeof(uart_line),
             "%s[%7" PRIu32 ".%03" PRIu32 "] %s  %-16s%s %s\r\n",
             level_colour(e->level),
             sec, frac,
             level_str(e->level),
             e->tag,
             ANSI_RESET,
             e->msg);
    uart_write_str(uart_line);

    /* Plain-text file output */
    if (s_log.file_output && s_log.logfile) {
        snprintf(line, sizeof(line),
                 "[%7" PRIu32 ".%03" PRIu32 "] %s  %-16s %s\n",
                 sec, frac,
                 level_str(e->level),
                 e->tag,
                 e->msg);
        fputs(line, s_log.logfile);
        /* lazy flush every ERROR */
        if (e->level >= OS_LOG_ERROR) fflush(s_log.logfile);
    }
}

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */
esp_err_t os_log_init(void)
{
    if (s_log.initialised) return ESP_OK;

    s_log.lock = xSemaphoreCreateMutex();
    if (!s_log.lock) return ESP_ERR_NO_MEM;

    s_log.initialised = true;
    OS_LOGI("OS_LOG", "Logging system ready  (ring=%d  level=%s)",
            OS_LOG_RING_SIZE, level_str(s_log.level));
    return ESP_OK;
}

void os_log_deinit(void)
{
    if (!s_log.initialised) return;
    os_log_flush();
    if (s_log.logfile) { fclose(s_log.logfile); s_log.logfile = NULL; }
    vSemaphoreDelete(s_log.lock);
    s_log.initialised = false;
}

void os_log_set_level(os_log_level_t level)
{
    s_log.level = level;
}

os_log_level_t os_log_get_level(void)
{
    return s_log.level;
}

void os_log_set_file_output(bool enable)
{
    if (!s_log.initialised) return;
    xSemaphoreTake(s_log.lock, portMAX_DELAY);

    if (enable && !s_log.logfile) {
        s_log.logfile = fopen(OS_LOG_FILE_PATH, "a");
        if (!s_log.logfile) {
            xSemaphoreGive(s_log.lock);
            OS_LOGW("OS_LOG", "Could not open log file %s", OS_LOG_FILE_PATH);
            return;
        }
    } else if (!enable && s_log.logfile) {
        fclose(s_log.logfile);
        s_log.logfile = NULL;
    }
    s_log.file_output = enable;
    xSemaphoreGive(s_log.lock);
}

bool os_log_get_file_output(void)
{
    return s_log.file_output;
}

void os_log_write_v(os_log_level_t level, const char *tag,
                    const char *fmt, va_list args)
{
    if (!s_log.initialised) return;
    if (level < s_log.level) return;

    os_log_entry_t *e;

    if (xSemaphoreTake(s_log.lock, pdMS_TO_TICKS(50)) != pdTRUE) return;

    e = &s_log.ring[s_log.head % OS_LOG_RING_SIZE];
    e->timestamp_ms = (uint32_t)(esp_timer_get_time() / 1000ULL);
    e->level        = level;
    strncpy(e->tag, tag ? tag : "???", sizeof(e->tag) - 1);
    e->tag[sizeof(e->tag) - 1] = '\0';
    vsnprintf(e->msg, sizeof(e->msg), fmt, args);

    s_log.head++;
    s_log.count++;

    write_to_sinks(e);
    xSemaphoreGive(s_log.lock);
}

void os_log_write(os_log_level_t level, const char *tag,
                  const char *fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    os_log_write_v(level, tag, fmt, ap);
    va_end(ap);
}

void os_log_puts(const char *str)
{
    uart_write_str(str);
    if (s_log.file_output && s_log.logfile) {
        fputs(str, s_log.logfile);
    }
}

void os_log_dump(int fd, uint32_t lines)
{
    if (!s_log.initialised) return;
    xSemaphoreTake(s_log.lock, portMAX_DELAY);

    uint32_t total = (s_log.count < OS_LOG_RING_SIZE) ? s_log.count : OS_LOG_RING_SIZE;
    if (lines == 0 || lines > total) lines = total;

    /* Start from oldest entry still in ring */
    uint32_t start = (s_log.count > OS_LOG_RING_SIZE)
                   ? s_log.head              /* ring wrapped */
                   : 0;

    char buf[OS_LOG_MAX_MSG_LEN + 80];
    for (uint32_t i = 0; i < lines; i++) {
        os_log_entry_t *e = &s_log.ring[(start + i) % OS_LOG_RING_SIZE];
        uint32_t sec  = e->timestamp_ms / 1000;
        uint32_t frac = e->timestamp_ms % 1000;
        int n = snprintf(buf, sizeof(buf),
                 "[%7" PRIu32 ".%03" PRIu32 "] %s  %-16s %s\r\n",
                 sec, frac,
                 level_str(e->level),
                 e->tag,
                 e->msg);
        if (fd < 0) {
            uart_write_str(buf);
        } else {
            send(fd, buf, n, 0);
        }
    }
    xSemaphoreGive(s_log.lock);
}

int os_log_get_recent(os_log_entry_t *buf, int max_count)
{
    if (!s_log.initialised || !buf || max_count <= 0) return 0;
    xSemaphoreTake(s_log.lock, portMAX_DELAY);

    uint32_t total = (s_log.count < OS_LOG_RING_SIZE) ? s_log.count : OS_LOG_RING_SIZE;
    int copy = (max_count < (int)total) ? max_count : (int)total;
    uint32_t start = (s_log.count > OS_LOG_RING_SIZE)
                   ? s_log.head : 0;

    for (int i = 0; i < copy; i++) {
        buf[i] = s_log.ring[(start + i) % OS_LOG_RING_SIZE];
    }
    xSemaphoreGive(s_log.lock);
    return copy;
}

void os_log_flush(void)
{
    if (s_log.logfile) fflush(s_log.logfile);
}
