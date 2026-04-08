# Contributing to ESP32OS

Thank you for your interest in contributing! This document covers everything you need to know to submit quality contributions.

---

## Ground Rules

1. **Respect the constraints** — ESP32 has 520 KB RAM. Every byte counts. Justify new allocations.
2. **No malloc in hot paths** — Use static or stack allocation in interrupt contexts and frequently-called functions.
3. **Layering** — Components must only `#include` headers from layers below them (see Architecture).
4. **Thread safety** — All shared state must be protected. Prefer mutexes over disabling interrupts.
5. **Error propagation** — Never silently swallow `esp_err_t` returns. Log with `OS_LOGE` or return to caller.
6. **C17 standard** — Use C17 features freely; avoid GNU extensions.

---

## Development Setup

```bash
# 1. Fork the repo and clone your fork
git clone https://github.com/<you>/esp32os.git
cd esp32os

# 2. Install ESP-IDF (if not already)
git clone --recursive https://github.com/espressif/esp-idf.git ~/esp-idf
cd ~/esp-idf && ./install.sh esp32
. ~/esp-idf/export.sh

# 3. Create a feature branch
git checkout -b feature/my-cool-feature

# 4. Build to verify
cd ~/esp32os
idf.py set-target esp32
idf.py build
```

---

## Code Style

### Naming Conventions

| Symbol | Convention | Example |
|--------|-----------|---------|
| Function | `snake_case` | `os_kernel_init()` |
| Global / module state | `s_` prefix | `static s_net` |
| Constants / macros | `UPPER_SNAKE` | `OS_MAX_PROCESSES` |
| Types (typedef struct/enum) | `snake_case_t` | `process_t` |
| Public API header guards | `COMPONENT_FILE_H` | `OS_KERNEL_H` |

### File Structure

Every `.c` file should follow this layout:
```c
/* 1. Component includes */
#include "my_component.h"
#include "os_logging.h"

/* 2. ESP-IDF includes */
#include "freertos/FreeRTOS.h"
#include ...

/* 3. Standard library */
#include <string.h>
#include <stdio.h>

/* 4. Module tag */
#define TAG "MY_COMP"

/* 5. Private types and state (static) */
static struct { ... } s_state;

/* 6. Private helper functions (static) */
static void helper(void) { ... }

/* 7. Public API implementations */
esp_err_t my_component_init(void) { ... }
```

### Formatting

- 4-space indentation (no tabs)
- Max line length: 100 characters
- Opening brace on same line: `if (cond) {`
- Always use braces even for single-line bodies
- Group related blank lines, but don't over-space

---

## Adding a New Shell Command

1. Create your handler:
```c
static int cmd_mycommand(int fd, int argc, char **argv)
{
    if (argc < 2) {
        shell_write(fd, "Usage: mycommand <arg>\r\n");
        return SHELL_CMD_ERROR;
    }
    shell_printf(fd, "You said: %s\r\n", argv[1]);
    return SHELL_CMD_OK;
}
```

2. Register in `shell_commands.c` inside `shell_commands_register_all()`:
```c
static const shell_command_t cmd = SHELL_CMD_ENTRY(
    "mycommand",
    "Short description",
    "mycommand <arg>",
    cmd_mycommand
);
shell_register_command(&cmd);
```

3. Alternatively, register from your own component's init function if you want to keep commands co-located with their feature.

---

## Adding a New Component

1. Create the directory structure:
```
components/my_feature/
├── CMakeLists.txt
├── include/
│   └── my_feature.h
└── src/
    └── my_feature.c
```

2. Write `CMakeLists.txt`:
```cmake
idf_component_register(
    SRCS "src/my_feature.c"
    INCLUDE_DIRS "include"
    REQUIRES os_logging freertos
)
```

3. Add to `main/CMakeLists.txt` REQUIRES list.

4. Write a `#pragma once` header with documented public API.

5. Use `OS_LOGI/LOGW/LOGE/LOGD(TAG, ...)` for all log output.

---

## Adding a New Hardware Driver

Drivers belong in `components/os_drivers/`.

1. Add the public API to `os_drivers.h`:
```c
/* My new sensor */
esp_err_t my_sensor_init(int pin);
int       my_sensor_read(void);   // returns value or -1 on error
```

2. Implement in a new file `src/my_sensor.c`.

3. Register any CLI commands via `shell_register_command()` in your `_init()` function or in `shell_commands.c`.

4. Add `my_sensor.c` to the `SRCS` list in `os_drivers/CMakeLists.txt`.

---

## Pull Request Checklist

Before opening a PR, verify:

- [ ] `idf.py build` succeeds with no warnings (`-Werror` is set)
- [ ] New public functions are documented in the header
- [ ] Existing API is not broken (no removal of public symbols)
- [ ] Memory usage impact is documented (if significant)
- [ ] New commands are added to the table in `README.md`
- [ ] Relevant test cases added/updated in `docs/TESTING.md`
- [ ] No magic numbers — all constants have named `#define`s
- [ ] Static analysis: `idf.py clang-check` passes (if available)
- [ ] Stack size is sufficient and documented for new tasks

---

## Commit Message Format

```
component: short description (max 72 chars)

Optional body explaining WHY the change was made, not WHAT.
Reference issues: Fixes #42
```

Examples:
```
os_shell: add tab-completion for command names
os_networking: fix reconnect race condition on rapid disconnect
os_fs: increase OS_FS_PATH_MAX to 256 for deeper paths
docs: add I2C wiring diagram to COMPONENTS.md
```

---

## Review Process

1. Open PR with description of change and motivation
2. At least one maintainer review required
3. All CI checks must pass
4. Address all review comments
5. Squash commits if asked

---

## Roadmap / Ideas for Contribution

- [ ] `cron` — scheduled task runner using FreeRTOS timers
- [ ] `env` — environment variable subsystem (NVS-backed)
- [ ] `script` — simple line-by-line shell script execution
- [ ] MQTT client command (`mqtt pub/sub`)
- [ ] OTA update command (`ota update <url>`)
- [ ] Modbus RTU driver
- [ ] PWM driver and `pwm` shell command
- [ ] mDNS hostname registration
- [ ] SD card driver (SDMMC) and mount command
- [ ] Web-based terminal (WebSocket + xterm.js)
- [ ] Encrypted NVS support
- [ ] Per-session environment variables
- [ ] `grep` — search file contents from shell
- [ ] `tail -f` — follow log file in real time

---

## Community

- **Issues:** Bug reports, feature requests → GitHub Issues
- **Discussions:** Design questions → GitHub Discussions
- **Security vulnerabilities:** Email maintainers directly (do not open public issues)
