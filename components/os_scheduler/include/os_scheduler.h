#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include "esp_err.h"

#define OS_SCHED_MAX_JOBS 8
#define OS_SCHED_NAME_LEN 24

esp_err_t os_scheduler_init(void);
void      os_scheduler_deinit(void);
esp_err_t os_scheduler_run_background(const char *name, const char *command, int fd);
esp_err_t os_scheduler_schedule(const char *name, const char *command, uint32_t delay_ms, bool repeat, int fd);
esp_err_t os_scheduler_cancel(const char *name);
void      os_scheduler_list(int fd);
bool      os_scheduler_is_running(const char *name);

#ifdef __cplusplus
}
#endif
