# Extending ESP32OS

[Documentation](README.md) · [Components](COMPONENTS.md) · [Contributing](CONTRIBUTING.md)

Add firmware features as C components, register command handlers, and use FreeRTOS tasks for sustained work. All extensions compile into the firmware; the shell does not load executable programs from SPIFFS.

## Add a shell command

Create `main/my_commands.c` with a small handler and an explicit registration function:

```c
#include <stddef.h>
#include "os_shell.h"

static int cmd_greet(int fd, int argc, char **argv)
{
    if (argc > 2) {
        shell_write(fd, "Usage: greet [name]\r\n");
        return SHELL_CMD_ERROR;
    }
    const char *name = argc == 2 ? argv[1] : "world";
    shell_printf(fd, "Hello, %s!\r\n", name);
    return SHELL_CMD_OK;
}

esp_err_t my_commands_register(void)
{
    static const shell_command_t command =
        SHELL_CMD_ENTRY("greet", "Print a greeting", "greet [name]", cmd_greet);
    return shell_register_command(&command);
}
```

Add `my_commands.c` to the `SRCS` list in [`main/CMakeLists.txt`](../main/CMakeLists.txt). Declare `esp_err_t my_commands_register(void);` in a header included by `main.c`, then call it after `shell_init()` and `shell_commands_register_all()`, before starting the UART/Telnet shell:

```c
esp_err_t command_result = my_commands_register();
if (command_result != ESP_OK) {
    OS_LOGE("MAIN", "greet registration failed: %s",
            esp_err_to_name(command_result));
}
```

After rebuilding and flashing:

```text
greet
greet "embedded developer"
greet --help
```

Use shell output helpers rather than direct UART writes so the same handler can participate in browser output capture. Prefer one-shot handlers with arguments: the browser command endpoint cannot support a second interactive input prompt. Validate numeric input with bounds checking when adding hardware controls.

## Add a background service

Create a task that blocks between work intervals rather than spinning. This example is intended to run for the life of the firmware:

```c
#include <stddef.h>
#include "os_kernel.h"
#include "os_logging.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static void heartbeat_task(void *arg)
{
    (void)arg;
    for (;;) {
        OS_LOGI("HEARTBEAT", "Service alive");
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}

esp_err_t heartbeat_start(void)
{
    os_pid_t pid = os_process_create("heartbeat", heartbeat_task,
                                    NULL, 4096, 4, false);
    return pid != 0 ? ESP_OK : ESP_FAIL;
}
```

Add the source to your component and call its start function once after kernel/logging initialization. Check the returned status. Validate stack use on the target rather than treating the example size as a guarantee.

For a stoppable service, design cooperative shutdown: signal the worker, release its files, peripheral handles and buffers, then coordinate task deletion with the process manager. Do not return from a FreeRTOS task or use raw `vTaskDelete(NULL)` while assuming the managed process record will automatically be reconciled. Forced shell termination is not application resource cleanup.

## Create a component

A focused feature can live in its own directory:

```text
components/my_feature/
├── CMakeLists.txt
├── include/my_feature.h
└── src/my_feature.c
```

For a component that uses the shell and logging:

```cmake
idf_component_register(
    SRCS "src/my_feature.c"
    INCLUDE_DIRS "include"
    REQUIRES os_shell os_logging
)
```

Add the component to the application's `REQUIRES` list when the application calls it. Add other direct dependencies such as `os_kernel` or `freertos` when used. Keep initialization and command registration separate when their startup order differs.

## Add a driver

1. Define the hardware contract: chip/board, bus, voltage, pin selection and error behavior.
2. Add a documented public API in `os_drivers.h`, or use a separate component for a substantial driver.
3. Add the implementation file to that component's `SRCS` list.
4. Use the existing bus wrappers where appropriate; verify initialization and resource ownership.
5. Register a shell command after the shell is initialized, and return errors when probing or reading fails.
6. Validate on the actual peripheral and record wiring and results.

Use the device datasheet for register addresses, scaling and byte order. Avoid copying a generic temperature-sensor example and assuming its conversion applies to your part. For measurements that may legitimately be negative, return `esp_err_t` separately from the measured value.

[`examples/sensor_logger.c`](../examples/sensor_logger.c) illustrates combining a task, ADC reads, file writes and a command. It is not included in the default `main` source list. Review its channels, file path, buffer lengths and error handling before integrating it on your board.

## Validate an extension

Build for affected targets, inspect heap and stack headroom, and test valid input, invalid input and unavailable hardware. For command handlers, verify UART output and browser capture if applicable. See [Testing](TESTING.md) for the distinction between model checks, compiled firmware and physical device evidence.
