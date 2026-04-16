#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stdint.h>
#include <stddef.h>
#include "esp_err.h"

#define OS_TIMER_MAX_TIMERS 8
#define OS_TIMER_NAME_LEN 24

typedef void (*os_timer_cb_t)(void *arg);

typedef struct {
    const char *name;
    uint32_t period_ms;
    bool reload;
    os_timer_cb_t callback;
    void *arg;
} os_timer_config_t;

typedef struct os_timer_s *os_timer_t;

esp_err_t os_timer_init(void);
void      os_timer_deinit(void);
os_timer_t os_timer_create(const os_timer_config_t *config);
esp_err_t os_timer_delete(os_timer_t timer);
esp_err_t os_timer_start(os_timer_t timer);
esp_err_t os_timer_stop(os_timer_t timer);
esp_err_t os_timer_restart(os_timer_t timer, uint32_t new_period_ms);
os_timer_t os_timer_find(const char *name);
bool      os_timer_is_active(os_timer_t timer);
uint32_t  os_timer_get_fire_count(os_timer_t timer);
const char *os_timer_get_name(os_timer_t timer);
void      os_timer_list(int fd);

#ifdef __cplusplus
}
#endif