#include "os_env.h"
#include "os_logging.h"

#include <string.h>
#include <stdio.h>
#include <stdarg.h>

#include "nvs.h"
#include "nvs_flash.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "driver/uart.h"
#include "lwip/sockets.h"

#define TAG "OS_ENV"
#define ENV_NAMESPACE "esp32os_env"
#define ENV_BLOB_KEY   "env_blob"

typedef struct {
    char name[OS_ENV_NAME_LEN];
    char value[OS_ENV_VALUE_LEN];
    bool used;
} env_entry_t;

typedef struct {
    uint8_t count;
    env_entry_t entries[OS_ENV_MAX_VARS];
} env_blob_t;

static struct {
    bool initialised;
    SemaphoreHandle_t lock;
    nvs_handle_t nvs;
    env_entry_t entries[OS_ENV_MAX_VARS];
} s_env = {0};

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

static int find_slot_locked(const char *name)
{
    for (int i = 0; i < OS_ENV_MAX_VARS; i++) {
        if (s_env.entries[i].used && strcmp(s_env.entries[i].name, name) == 0) {
            return i;
        }
    }
    return -1;
}

static int find_free_slot_locked(void)
{
    for (int i = 0; i < OS_ENV_MAX_VARS; i++) {
        if (!s_env.entries[i].used) {
            return i;
        }
    }
    return -1;
}

static void save_locked(void)
{
    env_blob_t blob = {0};
    for (int i = 0; i < OS_ENV_MAX_VARS; i++) {
        if (s_env.entries[i].used && blob.count < OS_ENV_MAX_VARS) {
            blob.entries[blob.count++] = s_env.entries[i];
        }
    }
    nvs_set_blob(s_env.nvs, ENV_BLOB_KEY, &blob, sizeof(blob));
    nvs_commit(s_env.nvs);
}

static void load_locked(void)
{
    env_blob_t blob = {0};
    size_t blob_sz = sizeof(blob);
    if (nvs_get_blob(s_env.nvs, ENV_BLOB_KEY, &blob, &blob_sz) != ESP_OK || blob_sz != sizeof(blob)) {
        memset(s_env.entries, 0, sizeof(s_env.entries));
        return;
    }

    memset(s_env.entries, 0, sizeof(s_env.entries));
    for (int i = 0; i < blob.count && i < OS_ENV_MAX_VARS; i++) {
        s_env.entries[i] = blob.entries[i];
        s_env.entries[i].used = true;
    }
}

esp_err_t os_env_init(void)
{
    if (s_env.initialised) {
        return ESP_OK;
    }

    s_env.lock = xSemaphoreCreateMutex();
    if (!s_env.lock) {
        return ESP_ERR_NO_MEM;
    }

    esp_err_t ret = nvs_open(ENV_NAMESPACE, NVS_READWRITE, &s_env.nvs);
    if (ret != ESP_OK) {
        vSemaphoreDelete(s_env.lock);
        s_env.lock = NULL;
        return ret;
    }

    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    load_locked();
    xSemaphoreGive(s_env.lock);

    s_env.initialised = true;
    OS_LOGI(TAG, "Environment store initialized");
    return ESP_OK;
}

void os_env_deinit(void)
{
    if (!s_env.initialised) {
        return;
    }

    nvs_close(s_env.nvs);
    s_env.nvs = 0;
    vSemaphoreDelete(s_env.lock);
    s_env.lock = NULL;
    s_env.initialised = false;
}

esp_err_t os_env_set(const char *name, const char *value)
{
    if (!s_env.initialised || !name || !value || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    int slot = find_slot_locked(name);
    if (slot < 0) {
        slot = find_free_slot_locked();
    }
    if (slot < 0) {
        xSemaphoreGive(s_env.lock);
        return ESP_ERR_NO_MEM;
    }

    strncpy(s_env.entries[slot].name, name, sizeof(s_env.entries[slot].name) - 1);
    s_env.entries[slot].name[sizeof(s_env.entries[slot].name) - 1] = '\0';
    strncpy(s_env.entries[slot].value, value, sizeof(s_env.entries[slot].value) - 1);
    s_env.entries[slot].value[sizeof(s_env.entries[slot].value) - 1] = '\0';
    s_env.entries[slot].used = true;
    save_locked();
    xSemaphoreGive(s_env.lock);
    return ESP_OK;
}

esp_err_t os_env_get(const char *name, char *buf, size_t buf_sz)
{
    if (!s_env.initialised || !name || !buf || buf_sz == 0) {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    int slot = find_slot_locked(name);
    if (slot < 0) {
        xSemaphoreGive(s_env.lock);
        return ESP_ERR_NOT_FOUND;
    }

    strncpy(buf, s_env.entries[slot].value, buf_sz - 1);
    buf[buf_sz - 1] = '\0';
    xSemaphoreGive(s_env.lock);
    return ESP_OK;
}

esp_err_t os_env_unset(const char *name)
{
    if (!s_env.initialised || !name || name[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }

    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    int slot = find_slot_locked(name);
    if (slot < 0) {
        xSemaphoreGive(s_env.lock);
        return ESP_ERR_NOT_FOUND;
    }

    memset(&s_env.entries[slot], 0, sizeof(s_env.entries[slot]));
    save_locked();
    xSemaphoreGive(s_env.lock);
    return ESP_OK;
}

esp_err_t os_env_clear(void)
{
    if (!s_env.initialised) {
        return ESP_ERR_INVALID_STATE;
    }

    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    memset(s_env.entries, 0, sizeof(s_env.entries));
    save_locked();
    xSemaphoreGive(s_env.lock);
    return ESP_OK;
}

int os_env_list(int fd)
{
    if (!s_env.initialised) {
        fd_printf(fd, "Environment store not initialised\r\n");
        return 0;
    }

    int count = 0;
    xSemaphoreTake(s_env.lock, portMAX_DELAY);
    for (int i = 0; i < OS_ENV_MAX_VARS; i++) {
        if (!s_env.entries[i].used) {
            continue;
        }
        fd_printf(fd, "%s=%s\r\n", s_env.entries[i].name, s_env.entries[i].value);
        count++;
    }
    xSemaphoreGive(s_env.lock);
    return count;
}

size_t os_env_expand(const char *input, char *output, size_t out_sz)
{
    if (!input || !output || out_sz == 0) {
        return 0;
    }

    size_t out_len = 0;
    output[0] = '\0';

    for (size_t i = 0; input[i] != '\0' && out_len + 1 < out_sz; i++) {
        if (input[i] != '$') {
            output[out_len++] = input[i];
            continue;
        }

        char name[OS_ENV_NAME_LEN] = {0};
        size_t name_len = 0;

        if (input[i + 1] == '{') {
            i += 2;
            while (input[i] != '\0' && input[i] != '}' && name_len + 1 < sizeof(name)) {
                name[name_len++] = input[i++];
            }
            if (input[i] != '}') {
                continue;
            }
        } else {
            size_t j = i + 1;
            while (input[j] != '\0' && ((input[j] >= 'A' && input[j] <= 'Z') ||
                                        (input[j] >= 'a' && input[j] <= 'z') ||
                                        (input[j] >= '0' && input[j] <= '9') ||
                                        input[j] == '_') && name_len + 1 < sizeof(name)) {
                name[name_len++] = input[j++];
            }
            if (name_len == 0) {
                output[out_len++] = '$';
                continue;
            }
            i += name_len;
        }

        name[name_len] = '\0';

        char value[OS_ENV_VALUE_LEN] = {0};
        if (os_env_get(name, value, sizeof(value)) == ESP_OK) {
            for (size_t v = 0; value[v] != '\0' && out_len + 1 < out_sz; v++) {
                output[out_len++] = value[v];
            }
        }
    }

    output[out_len] = '\0';
    return out_len;
}
