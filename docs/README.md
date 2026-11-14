# ESP32OS documentation

[Project overview](../README.md) · [Source](../main/main.c) · [Contributing](CONTRIBUTING.md)

ESP32OS combines an interactive shell and embedded services on top of ESP-IDF and FreeRTOS. Start with a UART-connected board, then explore the optional network interfaces or extend the firmware in C.

| I want to… | Read |
|---|---|
| Build, flash and use my board | [User Guide](USER_GUIDE.md) |
| Open the browser dashboard | [Dashboard](DASHBOARD.md) |
| Understand startup, storage and task management | [Architecture](ARCHITECTURE.md) |
| Find a component API | [Components](COMPONENTS.md) |
| Add a command, service or driver | [Extending](EXTENDING.md) |
| Check a change or report a failure | [Testing](TESTING.md) |
| Submit an improvement | [Contributing](CONTRIBUTING.md) |
| Understand how the documentation images were made | [Visual assets](assets/README.md) |

## Read examples correctly

- `bash` blocks run on your development computer in an ESP-IDF terminal.
- `text` command blocks run at the board's `esp32os>` prompt unless labelled otherwise.
- Replace example ports, network names, addresses and pins with values for your setup.
- Screenshots use sample data unless explicitly identified as a physical board capture.

## Source of truth

Public headers define the component interfaces. [Startup code](../main/main.c), [command handlers](../components/os_shell/src/shell_commands.c), [Kconfig](../Kconfig.projbuild), [SDK defaults](../sdkconfig.defaults) and the [partition table](../partitions.csv) define shipped behavior. The guides describe this checkout; they do not certify every board or peripheral.
