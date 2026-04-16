#include "os_timer.h"
#include "os_logging.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>

#include "freertos/FreeRTOS.h"
#include "freertos/timers.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

#define TAG "OS_TIMER"

typedef struct os_timer_s {
    TimerHandle_t handle;
    char name[OS_TIMER_NAME_LEN];
    uint32_t period_ms;
    bool reload;
    bool active;
    uint32_t fire_count;
    os_timer_cb_t callback;
    void *arg;
} timer_obj_t;

static struct {
    bool initialised;
    SemaphoreHandle_t lock;
    timer_obj_t timers[OS_TIMER_MAX_TIMERS];
} s_timer = {
    .initialised = false,
    .lock = NULL,
};

static void fd_write(int fd, const char *s)
{
    if (fd < 0) {
        uart_write_bytes(CONFIG_ESP_CONSOLE_UART_NUM, s, strlen(s));
    } else {
        send(fd, s, strlen(s), 0);
    }
}

static void fd_printf(int fd, const char *fmt, ...)
    __attribute__((format(printf, 2, 3)));

static void fd_printf(int fd, const char *fmt, ...)
{
    char buf[256];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    fd_write(fd, buf);
}

static timer_obj_t *find_slot(void)
{
    for (size_t i = 0; i < OS_TIMER_MAX_TIMERS; i++) {
        if (s_timer.timers[i].handle == NULL && s_timer.timers[i].name[0] == '\0') {
            return &s_timer.timers[i];
        }
    }
    return NULL;
}

static timer_obj_t *find_by_name_locked(const char *name)
{
    if (!name) {
        return NULL;
    }

    for (size_t i = 0; i < OS_TIMER_MAX_TIMERS; i++) {
        timer_obj_t *timer = &s_timer.timers[i];
        if (timer->handle != NULL && strcmp(timer->name, name) == 0) {
            return timer;
        }
    }
    return NULL;
}

static void timer_callback(TimerHandle_t handle)
{
    timer_obj_t *timer = (timer_obj_t *)pvTimerGetTimerID(handle);
    if (!timer) {
        return;
    }

    timer->fire_count++;
    if (timer->callback) {
        timer->callback(timer->arg);
    } else {
        OS_LOGI(TAG, "timer '%s' fired (%" PRIu32 ")", timer->name, timer->fire_count);
    }
}

esp_err_t os_timer_init(void)
{
    if (s_timer.initialised) {
        return ESP_OK;
    }

    s_timer.lock = xSemaphoreCreateMutex();
    if (!s_timer.lock) {
        return ESP_ERR_NO_MEM;
    }

    memset(s_timer.timers, 0, sizeof(s_timer.timers));
    s_timer.initialised = true;
    OS_LOGI(TAG, "Timer subsystem initialized (%d slots)", OS_TIMER_MAX_TIMERS);
    return ESP_OK;
}

void os_timer_deinit(void)
{
    if (!s_timer.initialised) {
        return;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    for (size_t i = 0; i < OS_TIMER_MAX_TIMERS; i++) {
        timer_obj_t *timer = &s_timer.timers[i];
        if (timer->handle != NULL) {
            xTimerStop(timer->handle, portMAX_DELAY);
            xTimerDelete(timer->handle, portMAX_DELAY);
            timer->handle = NULL;
        }
        memset(timer, 0, sizeof(*timer));
    }
    xSemaphoreGive(s_timer.lock);

    vSemaphoreDelete(s_timer.lock);
    s_timer.lock = NULL;
    s_timer.initialised = false;
}

os_timer_t os_timer_create(const os_timer_config_t *config)
{
    if (!s_timer.initialised || !config || !config->name || config->name[0] == '\0' || config->period_ms == 0) {
        return NULL;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);

    if (find_by_name_locked(config->name) != NULL) {
        xSemaphoreGive(s_timer.lock);
        return NULL;
    }

    timer_obj_t *timer = find_slot();
    if (!timer) {
        xSemaphoreGive(s_timer.lock);
        return NULL;
    }

    memset(timer, 0, sizeof(*timer));
    strncpy(timer->name, config->name, sizeof(timer->name) - 1);
    timer->period_ms = config->period_ms;
    timer->reload = config->reload;
    timer->callback = config->callback;
    timer->arg = config->arg;

    timer->handle = xTimerCreate(timer->name,
                                 pdMS_TO_TICKS(timer->period_ms),
                                 timer->reload ? pdTRUE : pdFALSE,
                                 timer,
                                 timer_callback);
    if (!timer->handle) {
        memset(timer, 0, sizeof(*timer));
        xSemaphoreGive(s_timer.lock);
        return NULL;
    }

    timer->active = false;
    xSemaphoreGive(s_timer.lock);
    return timer;
}

esp_err_t os_timer_delete(os_timer_t timer)
{
    if (!s_timer.initialised || !timer || timer->handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    xTimerStop(timer->handle, portMAX_DELAY);
    xTimerDelete(timer->handle, portMAX_DELAY);
    memset(timer, 0, sizeof(*timer));
    xSemaphoreGive(s_timer.lock);
    return ESP_OK;
}

esp_err_t os_timer_start(os_timer_t timer)
{
    if (!s_timer.initialised || !timer || timer->handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t rc = xTimerStart(timer->handle, 0);
    if (rc != pdPASS) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    timer->active = true;
    xSemaphoreGive(s_timer.lock);
    return ESP_OK;
}

esp_err_t os_timer_stop(os_timer_t timer)
{
    if (!s_timer.initialised || !timer || timer->handle == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t rc = xTimerStop(timer->handle, 0);
    if (rc != pdPASS) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    timer->active = false;
    xSemaphoreGive(s_timer.lock);
    return ESP_OK;
}

esp_err_t os_timer_restart(os_timer_t timer, uint32_t new_period_ms)
{
    if (!s_timer.initialised || !timer || timer->handle == NULL || new_period_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    BaseType_t rc = xTimerChangePeriod(timer->handle, pdMS_TO_TICKS(new_period_ms), 0);
    if (rc != pdPASS) {
        return ESP_FAIL;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    timer->period_ms = new_period_ms;
    timer->active = true;
    xSemaphoreGive(s_timer.lock);
    return ESP_OK;
}

os_timer_t os_timer_find(const char *name)
{
    if (!s_timer.initialised || !name) {
        return NULL;
    }

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    timer_obj_t *timer = find_by_name_locked(name);
    xSemaphoreGive(s_timer.lock);
    return timer;
}

bool os_timer_is_active(os_timer_t timer)
{
    return timer && timer->handle && timer->active;
}

uint32_t os_timer_get_fire_count(os_timer_t timer)
{
    return timer ? timer->fire_count : 0;
}

const char *os_timer_get_name(os_timer_t timer)
{
    return timer ? timer->name : NULL;
}

void os_timer_list(int fd)
{
    if (!s_timer.initialised) {
        fd_printf(fd, "Timer subsystem not initialised\r\n");
        return;
    }

    fd_printf(fd, "\r\n%-24s %-10s %-8s %-10s %s\r\n",
              "Name", "Period(ms)", "Mode", "State", "Fires");
    fd_printf(fd, "%-24s %-10s %-8s %-10s %s\r\n",
              "----", "----------", "----", "-----", "-----");

    xSemaphoreTake(s_timer.lock, portMAX_DELAY);
    for (size_t i = 0; i < OS_TIMER_MAX_TIMERS; i++) {
        timer_obj_t *timer = &s_timer.timers[i];
        if (timer->handle == NULL) {
            continue;
        }

        fd_printf(fd, "%-24s %-10" PRIu32 " %-8s %-10s %" PRIu32 "\r\n",
                  timer->name,
                  timer->period_ms,
                  timer->reload ? "auto" : "oneshot",
                  timer->active ? "running" : "stopped",
                  timer->fire_count);
    }
    xSemaphoreGive(s_timer.lock);

    fd_write(fd, "\r\n");
}