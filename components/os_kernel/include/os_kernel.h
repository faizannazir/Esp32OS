#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>
#include <stdbool.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"

/* ────────────────────────────────────────────────
   Limits
   ──────────────────────────────────────────────── */
#define OS_MAX_PROCESSES       20
#define OS_PROC_NAME_LEN       24
#define OS_DEFAULT_STACK_SIZE  4096
#define OS_DEFAULT_PRIORITY    5
#define OS_KERNEL_TASK_PRIORITY 20   /* higher than user tasks */

/* ────────────────────────────────────────────────
   Process States
   ──────────────────────────────────────────────── */
typedef enum {
    PROC_STATE_RUNNING   = 0,
    PROC_STATE_READY     = 1,
    PROC_STATE_BLOCKED   = 2,
    PROC_STATE_SUSPENDED = 3,
    PROC_STATE_DELETED   = 4,
    PROC_STATE_INVALID   = 5
} proc_state_t;

/* ────────────────────────────────────────────────
   Process Descriptor
   ──────────────────────────────────────────────── */
typedef uint16_t os_pid_t;

typedef struct {
   os_pid_t        pid;
    char            name[OS_PROC_NAME_LEN];
    TaskHandle_t    handle;
    proc_state_t    state;
    UBaseType_t     priority;
    uint32_t        stack_size;
    uint32_t        stack_high_water;   /* min free stack bytes seen  */
    uint64_t        runtime_ticks;      /* from FreeRTOS run-time stats */
    TickType_t      created_at;         /* xTaskGetTickCount() at birth */
    bool            is_system;          /* kernel/service task flag     */
} process_t;

/* ────────────────────────────────────────────────
   Signal-like actions
   ──────────────────────────────────────────────── */
typedef enum {
    OS_SIG_KILL    = 9,
    OS_SIG_SUSPEND = 19,
    OS_SIG_CONT    = 18,
} os_signal_t;

/* ────────────────────────────────────────────────
   Kernel Statistics
   ──────────────────────────────────────────────── */
typedef struct {
    uint32_t free_heap_bytes;
    uint32_t min_free_heap_bytes;
    uint32_t total_heap_bytes;
    uint32_t largest_free_block;
    uint32_t uptime_ms;
    uint8_t  cpu_load_pct;     /* approximate 0-100 */
    uint16_t process_count;
} kernel_stats_t;

/* ────────────────────────────────────────────────
   Task Function Type
   ──────────────────────────────────────────────── */
typedef void (*os_task_fn_t)(void *arg);

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */

/** Initialise the kernel layer (must be called once from app_main) */
esp_err_t  os_kernel_init(void);

/** Create a new managed process.
 *  Returns valid PID on success, 0 on failure. */
os_pid_t   os_process_create(const char   *name,
                              os_task_fn_t  func,
                              void         *arg,
                              uint32_t      stack_size,
                              UBaseType_t   priority,
                              bool          is_system);

/** Send a signal to a process */
esp_err_t  os_process_signal(os_pid_t pid, os_signal_t sig);

/** Convenience wrappers */
esp_err_t  os_process_kill(os_pid_t pid);
esp_err_t  os_process_suspend(os_pid_t pid);
esp_err_t  os_process_resume(os_pid_t pid);

/** Fill buf with up to max_count snapshots.
 *  Returns actual count written.             */
int        os_process_list(process_t *buf, int max_count);

/** Find by PID or name (returns pointer into internal table, read-only use) */
const process_t *os_process_get(os_pid_t pid);
const process_t *os_process_find_by_name(const char *name);

/** Get PID of the calling task (0 if unmanaged) */
os_pid_t   os_process_self(void);

/** Populate kernel statistics */
void       os_kernel_get_stats(kernel_stats_t *out);

/** Pretty-print process list to fd (UART=-1 or socket) */
void       os_kernel_print_ps(int fd);

/** Pretty-print top-style output to fd */
void       os_kernel_print_top(int fd);

/* ────────────────────────────────────────────────
   Watchdog helpers
   ──────────────────────────────────────────────── */
void       os_watchdog_feed(void);
void       os_watchdog_enable(uint32_t timeout_ms);
void       os_watchdog_disable(void);

#ifdef __cplusplus
}
#endif
