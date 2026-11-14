# Component reference

[Documentation](README.md) · [Architecture](ARCHITECTURE.md) · [Extending](EXTENDING.md)

The links below point to the actual public headers. Use those headers for complete types, limits and signatures; the examples here show the main integration points without duplicating every declaration.

## Component map

| Component / public header | Responsibility | Selected entry points |
|---|---|---|
| [`os_kernel`](../components/os_kernel/include/os_kernel.h) | Managed FreeRTOS tasks and system statistics | `os_process_create`, `os_process_list`, `os_kernel_get_stats` |
| [`os_shell`](../components/os_shell/include/os_shell.h) | UART/Telnet command dispatch and output capture | `shell_register_command`, `shell_execute`, `shell_execute_capture` |
| [`os_fs`](../components/os_fs/include/os_fs.h) | SPIFFS mount and file helpers | `os_fs_read_file`, `os_fs_write_file`, `os_fs_usage` |
| [`os_logging`](../components/os_logging/include/os_logging.h) | UART, recent-message ring and optional file logging | `OS_LOGI`, `OS_LOGW`, `OS_LOGE`, `OS_LOGD` |
| [`os_networking`](../components/os_networking/include/os_networking.h) | Wi-Fi station, ping and HTTP GET | `os_wifi_connect`, `os_wifi_get_status`, `os_http_get` |
| [`os_drivers`](../components/os_drivers/include/os_drivers.h) | GPIO, ADC, I²C, SPI and additional UART wrappers | `gpio_driver_read`, `adc_driver_read_raw`, `i2c_driver_read` |
| [`os_hal`](../components/os_hal/include/os_hal.h) | Umbrella header exposing driver APIs | `os_hal_init` (implemented in `os_drivers`) |
| [`os_pwm`](../components/os_pwm/include/os_pwm.h) | LEDC PWM channel management | `os_pwm_channel_init`, `os_pwm_set_duty`, `os_pwm_channel_deinit` |
| [`os_timer`](../components/os_timer/include/os_timer.h) | Named software timers | `os_timer_create`, `os_timer_start`, `os_timer_delete` |
| [`os_env`](../components/os_env/include/os_env.h) | Shared NVS-backed environment variables | `os_env_set`, `os_env_get`, `os_env_expand` |
| [`os_scheduler`](../components/os_scheduler/include/os_scheduler.h) | Background, delayed and repeating shell commands | `os_scheduler_run_background`, `os_scheduler_schedule`, `os_scheduler_cancel` |
| [`os_ipc`](../components/os_ipc/include/os_ipc.h) | Named message queues, event groups and shared-memory buffers | `os_msgq_create`, `os_event_create`, `os_shm_create` |
| [`os_mqtt`](../components/os_mqtt/include/os_mqtt.h) | MQTT client configuration and messaging | `os_mqtt_config`, `os_mqtt_connect`, `os_mqtt_publish` |
| [`os_ota`](../components/os_ota/include/os_ota.h) | OTA download, status, confirmation and rollback APIs | `os_ota_start`, `os_ota_confirm`, `os_ota_rollback` |
| [`webportal`](../components/webportal/include/webportal.h) | Embedded browser page and HTTP control API | `webportal_start` |

## Task snapshots

Process lookups copy into caller-owned storage. Older pointer-returning signatures are not the current API:

```c
process_t process;
esp_err_t result = os_process_get(pid, &process);
if (result == ESP_OK) {
    /* Read this snapshot; request another snapshot for fresh state. */
}
```

`os_process_create()` returns a nonzero PID on success and zero on failure. The public interface describes the stack argument in bytes; validate actual stack headroom on your target. Managed tasks share memory and resources. Do not assume deleting a task releases application-owned handles or updates every dependent subsystem.

## Shell commands and output

A handler has signature `int handler(int fd, int argc, char **argv)`. Return `SHELL_CMD_OK`, `SHELL_CMD_ERROR` or `SHELL_CMD_EXIT` as appropriate. Use `shell_write()` and `shell_printf()` to respect the output destination and permit browser capture.

The shell has bounded command, argument, line and history capacities defined in its header. Register commands after `shell_init()` and check the registration result. See the complete [extension example](EXTENDING.md#add-a-shell-command).

## File paths and logging

File helpers accept logical paths such as `/notes.txt`, which resolve under `/spiffs`. The current CWD is shared module state. Directory operations are limited by SPIFFS; see [storage](ARCHITECTURE.md#storage).

```c
const char text[] = "sample\n";
esp_err_t result = os_fs_write_file("/sample.txt", text, sizeof(text) - 1, false);
```

`false` overwrites and `true` appends. Check the return value before treating data as persisted. File logging uses `/spiffs/system.log`; the recent log ring and file sink are separate outputs.

## Scheduling, timers and IPC

- A **scheduled job** stores a command string and invokes the shell in a worker task. `os_scheduler_schedule()` takes delay/period in milliseconds and a repeat flag. Job names are used for cancellation.
- A **software timer** takes an `os_timer_config_t` containing a callback, argument, period and reload flag. Keep callbacks short and defer substantial work to a task.
- A **message queue** has fixed-size messages and bounded capacity. The caller must supply buffers appropriate for the configured message size.
- An **event group** communicates bits, not arbitrary payloads.
- A **shared-memory buffer** is an IPC convenience inside the same address space, not an isolation boundary.

These resources are bounded and can fail to allocate. Check creation results and release resources when the owning feature stops. Persistent shell environment values are distinct from these RAM-resident resources.

## Network services

Wi-Fi initialization and connection are separate operations. `os_wifi_connect()` waits for a connection result; `os_wifi_save_credentials()` is the explicit NVS persistence operation. The shell's `wifi connect` command does not invoke that persistence helper.

MQTT requires initialization, configuration and an asynchronous connection before publish/subscribe. `os_mqtt_publish()` includes a payload length; the shell exposes text `mqtt pub` and binary `mqtt pubhex` paths.

The OTA API needs an appropriate partition layout. The shipped factory-only layout does not support the advertised update/rollback lifecycle without configuration changes. See [OTA prerequisites](USER_GUIDE.md#ota-prerequisites).

The portal's HTTP interface, request body formats and capture limitations are documented in [Dashboard](DASHBOARD.md#http-api).

## Adding dependencies

Component dependencies live in each component's `CMakeLists.txt`; startup dependencies live in [`main/CMakeLists.txt`](../main/CMakeLists.txt). Add explicit requirements for the headers and libraries your code uses. The responsibility diagram is not a prohibition on every cross-layer call, and it is not a substitute for reviewing the actual dependency graph.
