#include "os_kernel.h"
#include "os_logging.h"

#include <string.h>
#include <stdio.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "driver/uart.h"
#include "lwip/sockets.h"   /* send() for fd output */

#define TAG "OS_KERNEL"

/* ────────────────────────────────────────────────
   Internal State
   ──────────────────────────────────────────────── */
typedef struct {
    process_t       table[OS_MAX_PROCESSES];
    uint16_t        count;
    os_pid_t        next_pid;
    SemaphoreHandle_t lock;
    bool            initialised;
    uint64_t        boot_us;          /* esp_timer value at kernel_init */
} kernel_t;

static kernel_t s_k = {
    .count       = 0,
    .next_pid    = 1,
    .lock        = NULL,
    .initialised = false,
    .boot_us     = 0
};

/* ────────────────────────────────────────────────
   Helpers
   ──────────────────────────────────────────────── */
static void fd_printf(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void fd_printf(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    if (fd < 0) {
        uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, buf, strlen(buf));
    } else {
        send(fd, buf, strlen(buf), 0);
    }
}

static const char *state_str(proc_state_t s)
{
    switch (s) {
    case PROC_STATE_RUNNING:   return "R";
    case PROC_STATE_READY:     return "S";   /* sleeping/ready */
    case PROC_STATE_BLOCKED:   return "D";
    case PROC_STATE_SUSPENDED: return "T";
    case PROC_STATE_DELETED:   return "Z";
    default:                   return "?";
    }
}

/* Map FreeRTOS eTaskState → proc_state_t */
static proc_state_t freertos_state(eTaskState s)
{
    switch (s) {
    case eRunning:   return PROC_STATE_RUNNING;
    case eReady:     return PROC_STATE_READY;
    case eBlocked:   return PROC_STATE_BLOCKED;
    case eSuspended: return PROC_STATE_SUSPENDED;
    case eDeleted:   return PROC_STATE_DELETED;
    default:         return PROC_STATE_INVALID;
    }
}

/** Refresh live fields (state, stack, runtime) for all entries */
static void refresh_entries_locked(void)
{
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        process_t *p = &s_k.table[i];
        if (p->state == PROC_STATE_INVALID) continue;
        if (p->state == PROC_STATE_DELETED) continue;
        if (!p->handle) continue;

        eTaskState ts = eTaskGetState(p->handle);
        p->state           = freertos_state(ts);
        p->stack_high_water = uxTaskGetStackHighWaterMark(p->handle) * sizeof(StackType_t);
        p->priority        = uxTaskPriorityGet(p->handle);
    }
}

static process_t *find_slot(void)
{
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (s_k.table[i].state == PROC_STATE_INVALID ||
            s_k.table[i].state == PROC_STATE_DELETED) {
            return &s_k.table[i];
        }
    }
    return NULL;
}

static process_t *find_by_pid_locked(os_pid_t pid)
{
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (s_k.table[i].pid == pid &&
            s_k.table[i].state != PROC_STATE_INVALID) {
            return &s_k.table[i];
        }
    }
    return NULL;
}

/* ────────────────────────────────────────────────
   Public API
   ──────────────────────────────────────────────── */
esp_err_t os_kernel_init(void)
{
    if (s_k.initialised) return ESP_OK;

    memset(s_k.table, 0, sizeof(s_k.table));
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        s_k.table[i].state = PROC_STATE_INVALID;
    }

    s_k.lock = xSemaphoreCreateMutex();
    if (!s_k.lock) return ESP_ERR_NO_MEM;

    s_k.boot_us     = esp_timer_get_time();
    s_k.next_pid    = 1;
    s_k.count       = 0;
    s_k.initialised = true;

    OS_LOGI(TAG, "Kernel init OK  (max_procs=%d)", OS_MAX_PROCESSES);
    return ESP_OK;
}

os_pid_t os_process_create(const char *name, os_task_fn_t func, void *arg,
                         uint32_t stack_size, UBaseType_t priority, bool is_system)
{
    if (!s_k.initialised || !func) return 0;
    if (stack_size < 1024) stack_size = OS_DEFAULT_STACK_SIZE;

    xSemaphoreTake(s_k.lock, portMAX_DELAY);

    process_t *slot = find_slot();
    if (!slot) {
        xSemaphoreGive(s_k.lock);
        OS_LOGE(TAG, "Process table full (max=%d)", OS_MAX_PROCESSES);
        return 0;
    }

    memset(slot, 0, sizeof(*slot));
    slot->pid        = s_k.next_pid++;
    slot->priority   = priority ? priority : OS_DEFAULT_PRIORITY;
    slot->stack_size = stack_size;
    slot->is_system  = is_system;
    slot->created_at = xTaskGetTickCount();
    slot->state      = PROC_STATE_READY;

    strncpy(slot->name, name ? name : "unnamed", OS_PROC_NAME_LEN - 1);
    slot->name[OS_PROC_NAME_LEN - 1] = '\0';

    BaseType_t rc = xTaskCreate(func, slot->name, stack_size / sizeof(StackType_t),
                                arg, slot->priority, &slot->handle);
    if (rc != pdPASS) {
        slot->state = PROC_STATE_INVALID;
        xSemaphoreGive(s_k.lock);
        OS_LOGE(TAG, "xTaskCreate failed for '%s'", name);
        return 0;
    }

    s_k.count++;
    os_pid_t pid = slot->pid;
    xSemaphoreGive(s_k.lock);

    OS_LOGD(TAG, "Created process '%s' pid=%d prio=%d stack=%"PRIu32,
            name, pid, priority, stack_size);
    return pid;
}

esp_err_t os_process_signal(os_pid_t pid, os_signal_t sig)
{
    if (!s_k.initialised) return ESP_ERR_INVALID_STATE;

    xSemaphoreTake(s_k.lock, portMAX_DELAY);
    process_t *p = find_by_pid_locked(pid);
    if (!p) {
        xSemaphoreGive(s_k.lock);
        return ESP_ERR_NOT_FOUND;
    }

    esp_err_t ret = ESP_OK;
    switch (sig) {
    case OS_SIG_KILL:
        if (p->handle) {
            vTaskDelete(p->handle);
            p->handle = NULL;
        }
        p->state = PROC_STATE_DELETED;
        s_k.count = (s_k.count > 0) ? s_k.count - 1 : 0;
        OS_LOGI(TAG, "Killed pid=%d ('%s')", pid, p->name);
        break;

    case OS_SIG_SUSPEND:
        if (p->handle) vTaskSuspend(p->handle);
        p->state = PROC_STATE_SUSPENDED;
        OS_LOGD(TAG, "Suspended pid=%d", pid);
        break;

    case OS_SIG_CONT:
        if (p->handle) vTaskResume(p->handle);
        p->state = PROC_STATE_READY;
        OS_LOGD(TAG, "Resumed pid=%d", pid);
        break;

    default:
        ret = ESP_ERR_INVALID_ARG;
    }
    xSemaphoreGive(s_k.lock);
    return ret;
}

esp_err_t os_process_kill(os_pid_t pid)    { return os_process_signal(pid, OS_SIG_KILL);    }
esp_err_t os_process_suspend(os_pid_t pid) { return os_process_signal(pid, OS_SIG_SUSPEND); }
esp_err_t os_process_resume(os_pid_t pid)  { return os_process_signal(pid, OS_SIG_CONT);    }

int os_process_list(process_t *buf, int max_count)
{
    if (!s_k.initialised || !buf || max_count <= 0) return 0;
    xSemaphoreTake(s_k.lock, portMAX_DELAY);
    refresh_entries_locked();
    int n = 0;
    for (int i = 0; i < OS_MAX_PROCESSES && n < max_count; i++) {
        if (s_k.table[i].state != PROC_STATE_INVALID) {
            buf[n++] = s_k.table[i];
        }
    }
    xSemaphoreGive(s_k.lock);
    return n;
}

esp_err_t os_process_get(os_pid_t pid, process_t *buf)
{
    if (!s_k.initialised || !buf) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_k.lock, portMAX_DELAY);
    process_t *p = find_by_pid_locked(pid);
    if (p) {
        memcpy(buf, p, sizeof(process_t));
        xSemaphoreGive(s_k.lock);
        return ESP_OK;
    }
    xSemaphoreGive(s_k.lock);
    return ESP_ERR_NOT_FOUND;
}

esp_err_t os_process_find_by_name(const char *name, process_t *buf)
{
    if (!s_k.initialised || !name || !buf) return ESP_ERR_INVALID_ARG;
    xSemaphoreTake(s_k.lock, portMAX_DELAY);
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (s_k.table[i].state != PROC_STATE_INVALID &&
            strncmp(s_k.table[i].name, name, OS_PROC_NAME_LEN) == 0) {
            memcpy(buf, &s_k.table[i], sizeof(process_t));
            xSemaphoreGive(s_k.lock);
            return ESP_OK;
        }
    }
    xSemaphoreGive(s_k.lock);
    return ESP_ERR_NOT_FOUND;
}

os_pid_t os_process_self(void)
{
    if (!s_k.initialised) return 0;
    TaskHandle_t self = xTaskGetCurrentTaskHandle();
    xSemaphoreTake(s_k.lock, portMAX_DELAY);
    for (int i = 0; i < OS_MAX_PROCESSES; i++) {
        if (s_k.table[i].handle == self &&
            s_k.table[i].state != PROC_STATE_INVALID) {
            os_pid_t p = s_k.table[i].pid;
            xSemaphoreGive(s_k.lock);
            return p;
        }
    }
    xSemaphoreGive(s_k.lock);
    return 0;
}

void os_kernel_get_stats(kernel_stats_t *out)
{
    if (!out) return;
    out->free_heap_bytes     = heap_caps_get_free_size(MALLOC_CAP_DEFAULT);
    out->min_free_heap_bytes = heap_caps_get_minimum_free_size(MALLOC_CAP_DEFAULT);
    out->total_heap_bytes    = heap_caps_get_total_size(MALLOC_CAP_DEFAULT);
    out->largest_free_block  = heap_caps_get_largest_free_block(MALLOC_CAP_DEFAULT);
    out->uptime_ms           = (uint32_t)((esp_timer_get_time() - s_k.boot_us) / 1000ULL);
    out->process_count       = s_k.count;

    /* CPU load: idle task high-water / theoretical idle
       (rough approximation using FreeRTOS vTaskGetRunTimeStats) */
    out->cpu_load_pct = 0; /* TODO: implement via runtime counter comparison */
}

void os_kernel_print_ps(int fd)
{
    process_t buf[OS_MAX_PROCESSES];
    int n = os_process_list(buf, OS_MAX_PROCESSES);

    fd_printf(fd, "\r\n%-5s %-24s %-4s %-5s %-10s %-10s %-6s\r\n",
              "PID", "NAME", "ST", "PRIO", "STACK_SZ", "STK_LEFT", "SYS");
    fd_printf(fd, "%-5s %-24s %-4s %-5s %-10s %-10s %-6s\r\n",
              "---", "----", "--", "----", "--------", "--------", "---");

    for (int i = 0; i < n; i++) {
        process_t *p = &buf[i];
        fd_printf(fd, "%-5d %-24s %-4s %-5d %-10"PRIu32" %-10"PRIu32" %-6s\r\n",
                  p->pid, p->name,
                  state_str(p->state),
                  (int)p->priority,
                  p->stack_size,
                  p->stack_high_water,
                  p->is_system ? "yes" : "no");
    }
    fd_printf(fd, "\r\n%d processes\r\n", n);
}

void os_kernel_print_top(int fd)
{
    kernel_stats_t st;
    os_kernel_get_stats(&st);

    uint32_t uptime_s  = st.uptime_ms / 1000;
    uint32_t uptime_m  = uptime_s / 60;
    uint32_t uptime_h  = uptime_m / 60;
    uptime_s %= 60; uptime_m %= 60;

    fd_printf(fd, "\r\n\033[1m esp32os top\033[0m  up %02"PRIu32":%02"PRIu32":%02"PRIu32
              "  procs: %d\r\n",
              uptime_h, uptime_m, uptime_s, st.process_count);
    fd_printf(fd, "Heap: %"PRIu32" / %"PRIu32" bytes free  "
              "(min=%"PRIu32"  largest=%"PRIu32")\r\n",
              st.free_heap_bytes, st.total_heap_bytes,
              st.min_free_heap_bytes, st.largest_free_block);

    uint32_t used = st.total_heap_bytes - st.free_heap_bytes;
    uint32_t pct  = (st.total_heap_bytes > 0)
                  ? (used * 100 / st.total_heap_bytes) : 0;

    /* ASCII bar */
    fd_printf(fd, "Mem  [");
    for (int i = 0; i < 40; i++) {
        fd_printf(fd, "%s", i < (int)(pct * 40 / 100) ? "█" : "░");
    }
    fd_printf(fd, "] %"PRIu32"%%\r\n\r\n", pct);

    os_kernel_print_ps(fd);
}

/* ────────────────────────────────────────────────
   Watchdog
   ──────────────────────────────────────────────── */
void os_watchdog_feed(void)
{
#if CONFIG_ESP_TASK_WDT
    esp_task_wdt_reset();
#endif
}

void os_watchdog_enable(uint32_t timeout_ms)
{
#if CONFIG_ESP_TASK_WDT
    esp_task_wdt_config_t cfg = {
        .timeout_ms     = timeout_ms,
        .idle_core_mask = (1 << portNUM_PROCESSORS) - 1,
        .trigger_panic  = true,
    };
    esp_task_wdt_reconfigure(&cfg);
    esp_task_wdt_add(NULL);
    OS_LOGI(TAG, "Watchdog enabled (%"PRIu32" ms)", timeout_ms);
#endif
}

void os_watchdog_disable(void)
{
#if CONFIG_ESP_TASK_WDT
    esp_task_wdt_delete(NULL);
    OS_LOGI(TAG, "Watchdog disabled");
#endif
}
