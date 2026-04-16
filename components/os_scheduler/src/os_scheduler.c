#include "os_scheduler.h"
#include "os_logging.h"
#include "os_shell.h"
#include "os_kernel.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>
#include <inttypes.h>
#include <stdlib.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

#define TAG "OS_SCHED"

typedef struct os_sched_job_s {
    char name[OS_SCHED_NAME_LEN];
    char command[SHELL_MAX_LINE_LEN];
    int fd;
    uint32_t delay_ms;
    bool repeat;
    bool running;
    bool completed;
    uint32_t fire_count;
    TimerHandle_t timer;
} sched_job_t;

typedef struct {
    bool initialised;
    SemaphoreHandle_t lock;
    QueueHandle_t queue;
    TaskHandle_t worker_task;
    sched_job_t jobs[OS_SCHED_MAX_JOBS];
} sched_state_t;

static sched_state_t s_sched = {0};

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

static sched_job_t *find_free_slot_locked(void)
{
    for (int i = 0; i < OS_SCHED_MAX_JOBS; i++) {
        if (!s_sched.jobs[i].running && !s_sched.jobs[i].completed && s_sched.jobs[i].timer == NULL) {
            return &s_sched.jobs[i];
        }
    }
    return NULL;
}

static sched_job_t *find_job_locked(const char *name)
{
    if (!name) {
        return NULL;
    }

    for (int i = 0; i < OS_SCHED_MAX_JOBS; i++) {
        if ((s_sched.jobs[i].running || s_sched.jobs[i].completed || s_sched.jobs[i].timer != NULL) &&
            strcmp(s_sched.jobs[i].name, name) == 0) {
            return &s_sched.jobs[i];
        }
    }
    return NULL;
}

static void worker_task(void *arg)
{
    (void)arg;
    sched_job_t *job = NULL;
    while (1) {
        if (xQueueReceive(s_sched.queue, &job, portMAX_DELAY) == pdTRUE && job) {
            shell_execute(job->fd, job->command);
            xSemaphoreTake(s_sched.lock, portMAX_DELAY);
            if (!job->repeat) {
                job->completed = true;
                job->running = false;
            }
            xSemaphoreGive(s_sched.lock);
        }
    }
}

static void timer_callback(TimerHandle_t handle)
{
    sched_job_t *job = (sched_job_t *)pvTimerGetTimerID(handle);
    if (!job || !s_sched.queue) {
        return;
    }

    job->fire_count++;
    (void)xQueueSend(s_sched.queue, &job, 0);
}

static void bg_task(void *arg)
{
    typedef struct {
        char name[OS_SCHED_NAME_LEN];
        char command[SHELL_MAX_LINE_LEN];
        int fd;
    } run_arg_t;

    run_arg_t *run = arg;
    shell_execute(run->fd, run->command);
    free(run);
    vTaskDelete(NULL);
}

esp_err_t os_scheduler_init(void)
{
    if (s_sched.initialised) {
        return ESP_OK;
    }

    s_sched.lock = xSemaphoreCreateMutex();
    s_sched.queue = xQueueCreate(OS_SCHED_MAX_JOBS, sizeof(sched_job_t *));
    if (!s_sched.lock || !s_sched.queue) {
        if (s_sched.lock) {
            vSemaphoreDelete(s_sched.lock);
            s_sched.lock = NULL;
        }
        if (s_sched.queue) {
            vQueueDelete(s_sched.queue);
            s_sched.queue = NULL;
        }
        return ESP_ERR_NO_MEM;
    }

    memset(s_sched.jobs, 0, sizeof(s_sched.jobs));
    BaseType_t rc = xTaskCreate(worker_task, "sched_worker", 3072, NULL, 4, &s_sched.worker_task);
    if (rc != pdPASS) {
        vSemaphoreDelete(s_sched.lock);
        vQueueDelete(s_sched.queue);
        s_sched.lock = NULL;
        s_sched.queue = NULL;
        return ESP_ERR_NO_MEM;
    }

    s_sched.initialised = true;
    OS_LOGI(TAG, "Scheduler initialized");
    return ESP_OK;
}

void os_scheduler_deinit(void)
{
    if (!s_sched.initialised) {
        return;
    }

    xSemaphoreTake(s_sched.lock, portMAX_DELAY);
    for (int i = 0; i < OS_SCHED_MAX_JOBS; i++) {
        if (s_sched.jobs[i].timer) {
            xTimerStop(s_sched.jobs[i].timer, portMAX_DELAY);
            xTimerDelete(s_sched.jobs[i].timer, portMAX_DELAY);
        }
        memset(&s_sched.jobs[i], 0, sizeof(s_sched.jobs[i]));
    }
    xSemaphoreGive(s_sched.lock);

    if (s_sched.worker_task) {
        vTaskDelete(s_sched.worker_task);
        s_sched.worker_task = NULL;
    }
    if (s_sched.queue) {
        vQueueDelete(s_sched.queue);
        s_sched.queue = NULL;
    }
    if (s_sched.lock) {
        vSemaphoreDelete(s_sched.lock);
        s_sched.lock = NULL;
    }

    s_sched.initialised = false;
}

esp_err_t os_scheduler_run_background(const char *name, const char *command, int fd)
{
    if (!s_sched.initialised || !command || command[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    typedef struct {
        char name[OS_SCHED_NAME_LEN];
        char command[SHELL_MAX_LINE_LEN];
        int fd;
    } run_arg_t;

    run_arg_t *arg = calloc(1, sizeof(*arg));
    if (!arg) {
        return ESP_ERR_NO_MEM;
    }

    strncpy(arg->name, name ? name : "run", sizeof(arg->name) - 1);
    strncpy(arg->command, command, sizeof(arg->command) - 1);
    arg->fd = fd;

    os_pid_t pid = os_process_create(arg->name, bg_task, arg, 4096, 4, false);
    if (pid == 0) {
        free(arg);
        return ESP_ERR_NO_MEM;
    }

    return ESP_OK;
}

esp_err_t os_scheduler_schedule(const char *name, const char *command, uint32_t delay_ms, bool repeat, int fd)
{
    if (!s_sched.initialised || !name || !command || name[0] == '\0' || command[0] == '\0' || delay_ms == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sched.lock, portMAX_DELAY);
    if (find_job_locked(name) != NULL) {
        xSemaphoreGive(s_sched.lock);
        return ESP_ERR_INVALID_STATE;
    }

    sched_job_t *job = find_free_slot_locked();
    if (!job) {
        xSemaphoreGive(s_sched.lock);
        return ESP_ERR_NO_MEM;
    }

    memset(job, 0, sizeof(*job));
    strncpy(job->name, name, sizeof(job->name) - 1);
    strncpy(job->command, command, sizeof(job->command) - 1);
    job->fd = fd;
    job->delay_ms = delay_ms;
    job->repeat = repeat;
    job->running = true;

    job->timer = xTimerCreate(job->name,
                              pdMS_TO_TICKS(delay_ms),
                              repeat ? pdTRUE : pdFALSE,
                              job,
                              timer_callback);
    if (!job->timer) {
        memset(job, 0, sizeof(*job));
        xSemaphoreGive(s_sched.lock);
        return ESP_ERR_NO_MEM;
    }

    BaseType_t rc = xTimerStart(job->timer, 0);
    if (rc != pdPASS) {
        xTimerDelete(job->timer, portMAX_DELAY);
        memset(job, 0, sizeof(*job));
        xSemaphoreGive(s_sched.lock);
        return ESP_FAIL;
    }

    xSemaphoreGive(s_sched.lock);
    return ESP_OK;
}

esp_err_t os_scheduler_cancel(const char *name)
{
    if (!s_sched.initialised || !name) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_sched.lock, portMAX_DELAY);
    sched_job_t *job = find_job_locked(name);
    if (!job) {
        xSemaphoreGive(s_sched.lock);
        return ESP_ERR_NOT_FOUND;
    }

    if (job->timer) {
        xTimerStop(job->timer, portMAX_DELAY);
        xTimerDelete(job->timer, portMAX_DELAY);
    }
    memset(job, 0, sizeof(*job));
    xSemaphoreGive(s_sched.lock);
    return ESP_OK;
}

void os_scheduler_list(int fd)
{
    if (!s_sched.initialised) {
        fd_printf(fd, "Scheduler not initialised\r\n");
        return;
    }

    fd_printf(fd, "\r\n%-24s %-8s %-8s %-10s %s\r\n",
              "Name", "Delay", "Mode", "State", "Fires");
    fd_printf(fd, "%-24s %-8s %-8s %-10s %s\r\n",
              "----", "-----", "----", "-----", "-----");

    xSemaphoreTake(s_sched.lock, portMAX_DELAY);
    for (int i = 0; i < OS_SCHED_MAX_JOBS; i++) {
        sched_job_t *job = &s_sched.jobs[i];
        if (!job->running && !job->completed && job->timer == NULL) {
            continue;
        }

        fd_printf(fd, "%-24s %-8" PRIu32 " %-8s %-10s %" PRIu32 "\r\n",
                  job->name,
                  job->delay_ms,
                  job->repeat ? "repeat" : "once",
                  job->completed ? "done" : "running",
                  job->fire_count);
    }
    xSemaphoreGive(s_sched.lock);
    fd_write(fd, "\r\n");
}

bool os_scheduler_is_running(const char *name)
{
    if (!s_sched.initialised || !name) {
        return false;
    }

    xSemaphoreTake(s_sched.lock, portMAX_DELAY);
    sched_job_t *job = find_job_locked(name);
    bool running = (job != NULL) && job->running;
    xSemaphoreGive(s_sched.lock);
    return running;
}
