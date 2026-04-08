# Extending ESP32OS

This guide shows you how to add new functionality to ESP32OS through the three main extension points:

1. **Shell Commands** — Add CLI commands
2. **Background Services** — Add persistent tasks
3. **Hardware Drivers** — Add device support

---

## Extension Point 1: Shell Commands

The simplest way to add functionality. Commands appear in `help` and are callable from UART and Telnet.

### Minimal Example

```c
// my_commands.c

#include "os_shell.h"
#include <string.h>

/* Handler: int fn(int fd, int argc, char **argv) */
static int cmd_greet(int fd, int argc, char **argv)
{
    const char *name = (argc >= 2) ? argv[1] : "world";
    shell_printf(fd, "Hello, %s!\r\n", name);
    return SHELL_CMD_OK;
}

void my_commands_register(void)
{
    static const shell_command_t cmds[] = {
        SHELL_CMD_ENTRY("greet", "Say hello", "greet [name]", cmd_greet),
    };
    for (size_t i = 0; i < sizeof(cmds)/sizeof(cmds[0]); i++) {
        shell_register_command(&cmds[i]);
    }
}
```

Call `my_commands_register()` from `app_main` after `shell_init()`.

### I/O in Command Handlers

```c
// Output always goes through shell_write / shell_printf:
shell_write(fd, "plain text\r\n");
shell_printf(fd, "number: %d\r\n", 42);

// ANSI colours (rendered in most terminals):
shell_printf(fd, "\033[32mGreen text\033[0m\r\n");
shell_printf(fd, "\033[31mRed error\033[0m\r\n");
shell_printf(fd, "\033[1mBold\033[0m  \033[4mUnderline\033[0m\r\n");
```

### Argument Parsing Patterns

```c
static int cmd_example(int fd, int argc, char **argv)
{
    /* Check minimum args */
    if (argc < 2) {
        shell_write(fd, "Usage: example <subcommand> [args]\r\n");
        return SHELL_CMD_ERROR;
    }

    /* Subcommand dispatch */
    if (strcmp(argv[1], "start") == 0) {
        /* ... */
    } else if (strcmp(argv[1], "stop") == 0) {
        /* ... */
    } else {
        shell_printf(fd, "Unknown subcommand: %s\r\n", argv[1]);
        return SHELL_CMD_ERROR;
    }

    /* Parse optional flags */
    bool verbose = false;
    int value = 0;
    for (int i = 2; i < argc; i++) {
        if (strcmp(argv[i], "-v") == 0) {
            verbose = true;
        } else if (strcmp(argv[i], "-n") == 0 && i+1 < argc) {
            value = atoi(argv[++i]);
        }
    }

    return SHELL_CMD_OK;
}
```

### Reading Input (Interactive)

For multi-step interactive commands:
```c
static int cmd_interactive(int fd, int argc, char **argv)
{
    shell_write(fd, "Enter value: ");

    char buf[64];
    /* For UART (fd == -1): use uart_read_bytes
       For Telnet (fd >= 0): use recv */
    if (fd < 0) {
        uart_read_bytes(CONFIG_ESP_CONSOLE_UART_NUM,
                        (uint8_t*)buf, sizeof(buf)-1,
                        pdMS_TO_TICKS(10000));
    } else {
        struct timeval tv = {.tv_sec = 10};
        setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
        recv(fd, buf, sizeof(buf)-1, 0);
    }
    shell_printf(fd, "You entered: %s\r\n", buf);
    return SHELL_CMD_OK;
}
```

---

## Extension Point 2: Background Services

Background services are FreeRTOS tasks registered with the process manager.

### Minimal Service Example

```c
// my_service.c

#include "os_kernel.h"
#include "os_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

#define TAG "MY_SVC"

static void my_service_task(void *arg)
{
    OS_LOGI(TAG, "Service started");
    while (1) {
        /* Do periodic work */
        OS_LOGD(TAG, "Service heartbeat");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    /* Should never reach here; vTaskDelete(NULL) if exiting */
}

esp_err_t my_service_start(void)
{
    os_pid_t pid = os_process_create(
        "my_service",        // name (appears in ps)
        my_service_task,     // function
        NULL,                // argument
        3072,                // stack size (bytes)
        4,                   // priority (1=lowest, 24=highest)
        false                // is_system flag
    );
    if (pid == 0) {
        OS_LOGE(TAG, "Failed to create service task");
        return ESP_FAIL;
    }
    OS_LOGI(TAG, "Service started with pid=%d", pid);
    return ESP_OK;
}
```

### Service with Shutdown Support

```c
#include "freertos/event_groups.h"

static EventGroupHandle_t s_events;
#define SVC_STOP_BIT BIT0

static void stoppable_service_task(void *arg)
{
    while (1) {
        /* Check for stop signal with a 1-second timeout */
        EventBits_t bits = xEventGroupWaitBits(s_events, SVC_STOP_BIT,
                                                pdTRUE, pdFALSE,
                                                pdMS_TO_TICKS(1000));
        if (bits & SVC_STOP_BIT) break;
        /* Do work */
    }
    OS_LOGI(TAG, "Service stopped cleanly");
    vTaskDelete(NULL);
}

void stoppable_service_stop(void)
{
    xEventGroupSetBits(s_events, SVC_STOP_BIT);
}

esp_err_t stoppable_service_start(void)
{
    s_events = xEventGroupCreate();
    os_process_create("svc_stoppable", stoppable_service_task,
                      NULL, 3072, 4, false);
    return ESP_OK;
}
```

---

## Extension Point 3: Hardware Drivers

Add support for a new peripheral sensor or actuator.

### Driver Template

```c
// components/os_drivers/src/my_sensor.c

#include "os_drivers.h"
#include "os_logging.h"
#include "driver/i2c.h"

#define TAG        "MY_SENSOR"
#define SENSOR_ADDR 0x48

static bool s_init = false;

esp_err_t my_sensor_init(void)
{
    /* I2C must already be init'd by os_hal_init() */
    /* Try a probe read to verify the device is present */
    uint8_t who_am_i = 0;
    esp_err_t ret = i2c_driver_read(SENSOR_ADDR, 0x0F, &who_am_i, 1);
    if (ret != ESP_OK) {
        OS_LOGE(TAG, "Sensor not found at 0x%02X", SENSOR_ADDR);
        return ESP_ERR_NOT_FOUND;
    }
    OS_LOGI(TAG, "Sensor found, WHO_AM_I=0x%02X", who_am_i);
    s_init = true;
    return ESP_OK;
}

int my_sensor_read_temperature(void)
{
    if (!s_init) return -1;
    uint8_t buf[2];
    if (i2c_driver_read(SENSOR_ADDR, 0x00, buf, 2) != ESP_OK) return -1;
    int16_t raw = (int16_t)((buf[0] << 8) | buf[1]);
    return raw / 16;  /* convert to Celsius per datasheet */
}
```

Add a CLI command:
```c
static int cmd_temp(int fd, int argc, char **argv)
{
    int temp = my_sensor_read_temperature();
    if (temp < -200) {
        shell_write(fd, "temp: sensor not available\r\n");
        return SHELL_CMD_ERROR;
    }
    shell_printf(fd, "Temperature: %d°C\r\n", temp);
    return SHELL_CMD_OK;
}
```

---

## Module Pattern (Self-Contained Feature)

For larger features, bundle everything in one component:

```
components/feature_mqtt/
├── CMakeLists.txt
├── include/
│   └── feature_mqtt.h      ← Public API + types
└── src/
    ├── mqtt_core.c         ← MQTT client logic
    ├── mqtt_commands.c     ← Shell commands
    └── mqtt_service.c      ← Background task
```

```c
// feature_mqtt.h
#pragma once
#include "esp_err.h"

esp_err_t mqtt_feature_init(const char *broker_url);
void      mqtt_feature_register_commands(void);
```

```c
// app_main:
mqtt_feature_init("mqtt://192.168.1.100");
mqtt_feature_register_commands();
```

The feature is now fully self-contained and opt-in.

---

## Useful Patterns

### Non-blocking Output (progress bar)
```c
static int cmd_longop(int fd, int argc, char **argv)
{
    for (int i = 0; i <= 100; i += 10) {
        shell_printf(fd, "\r[%.*s%.*s] %d%%",
                     i/5, "████████████████████",
                     20 - i/5, "░░░░░░░░░░░░░░░░░░░░", i);
        vTaskDelay(pdMS_TO_TICKS(200));
    }
    shell_write(fd, "\r\nDone!\r\n");
    return SHELL_CMD_OK;
}
```

### Formatted Table Output
```c
shell_printf(fd, "\033[1m%-20s %8s %8s\033[0m\r\n", "Name", "Value", "Unit");
shell_printf(fd, "%-20s %8.2f %8s\r\n", "Temperature", 23.5, "°C");
shell_printf(fd, "%-20s %8d %8s\r\n", "Humidity",    65,   "%");
```

### Logging from a Feature
```c
#include "os_logging.h"
#define TAG "MY_FEATURE"

OS_LOGI(TAG, "Initialised with value %d", some_value);
OS_LOGW(TAG, "This might be a problem");
OS_LOGE(TAG, "Critical failure: %s", esp_err_to_name(ret));
```

### Persisting Feature Configuration in NVS
```c
#include "nvs_flash.h"

void save_config(const char *key, const char *value)
{
    nvs_handle_t h;
    nvs_open("my_feature", NVS_READWRITE, &h);
    nvs_set_str(h, key, value);
    nvs_commit(h);
    nvs_close(h);
}

bool load_config(const char *key, char *out, size_t out_sz)
{
    nvs_handle_t h;
    if (nvs_open("my_feature", NVS_READONLY, &h) != ESP_OK) return false;
    bool ok = (nvs_get_str(h, key, out, &out_sz) == ESP_OK);
    nvs_close(h);
    return ok;
}
```
