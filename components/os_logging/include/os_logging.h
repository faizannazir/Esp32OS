#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdarg.h>
#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Log Levels (matches Linux kernel convention)
   ──────────────────────────────────────────────── */
typedef enum {
    OS_LOG_DEBUG   = 0,
    OS_LOG_INFO    = 1,
    OS_LOG_WARN    = 2,
    OS_LOG_ERROR   = 3,
    OS_LOG_NONE    = 4
} os_log_level_t;

/* ────────────────────────────────────────────────
   Log Entry (stored in ring buffer)
   ──────────────────────────────────────────────── */
#define OS_LOG_MAX_MSG_LEN  192
#define OS_LOG_RING_SIZE    64      /* entries in ring buffer  */
#define OS_LOG_FILE_PATH    "/spiffs/system.log"
#define OS_LOG_MAX_FILE_KB  64      /* max file size before rotate */

typedef struct {
    uint32_t        timestamp_ms;
    os_log_level_t  level;
    char            tag[24];
    char            msg[OS_LOG_MAX_MSG_LEN];
} os_log_entry_t;

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */
esp_err_t  os_log_init(void);
void       os_log_deinit(void);

void       os_log_write(os_log_level_t level, const char *tag,
                        const char *fmt, ...) __attribute__((format(printf, 3, 4)));
void       os_log_write_v(os_log_level_t level, const char *tag,
                          const char *fmt, va_list args);

void       os_log_set_level(os_log_level_t level);
os_log_level_t os_log_get_level(void);

/** Enable / disable writing to SPIFFS log file */
void       os_log_set_file_output(bool enable);
bool       os_log_get_file_output(void);

/** Dump ring buffer contents to fd (UART=-1 or socket) */
void       os_log_dump(int fd, uint32_t lines);

/** Read last N entries from ring buffer into user array.
 *  Returns actual count copied. */
int        os_log_get_recent(os_log_entry_t *buf, int max_count);

/** Flush file log and rotate if needed */
void       os_log_flush(void);

/** Write raw string to all active sinks (bypass level filter) */
void       os_log_puts(const char *str);

/* ────────────────────────────────────────────────
   Convenience Macros
   ──────────────────────────────────────────────── */
#define OS_LOGI(tag, fmt, ...)  os_log_write(OS_LOG_INFO,  tag, fmt, ##__VA_ARGS__)
#define OS_LOGW(tag, fmt, ...)  os_log_write(OS_LOG_WARN,  tag, fmt, ##__VA_ARGS__)
#define OS_LOGE(tag, fmt, ...)  os_log_write(OS_LOG_ERROR, tag, fmt, ##__VA_ARGS__)
#define OS_LOGD(tag, fmt, ...)  os_log_write(OS_LOG_DEBUG, tag, fmt, ##__VA_ARGS__)

#ifdef __cplusplus
}
#endif
