#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>
#include "esp_err.h"

#define OS_ENV_MAX_VARS 16
#define OS_ENV_NAME_LEN 16
#define OS_ENV_VALUE_LEN 128

esp_err_t os_env_init(void);
void      os_env_deinit(void);
esp_err_t os_env_set(const char *name, const char *value);
esp_err_t os_env_get(const char *name, char *buf, size_t buf_sz);
esp_err_t os_env_unset(const char *name);
esp_err_t os_env_clear(void);
int       os_env_list(int fd);
size_t    os_env_expand(const char *input, char *output, size_t out_sz);

#ifdef __cplusplus
}
#endif
