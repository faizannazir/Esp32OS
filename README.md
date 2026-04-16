# ESP32OS — Embedded Linux-Style Operating System for ESP32

[![Espressif](https://img.shields.io/badge/Espressif-Systems-E7352C?logo=espressif&logoColor=white)](https://www.espressif.com/)
[![ESP32](https://img.shields.io/badge/ESP32-Board-0A84FF?logo=espressif&logoColor=white)](https://www.espressif.com/en/products/socs/esp32)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-Framework-1F6FEB?logo=espressif&logoColor=white)](https://github.com/espressif/esp-idf)
[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v6.0-blue)](https://docs.espressif.com/projects/esp-idf)
[![Target](https://img.shields.io/badge/Target-ESP32%20%7C%20ESP32--S3-green)](https://www.espressif.com)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C%20(C17)-lightgrey)]()
[![Build System](https://img.shields.io/badge/Build-CMake-blue)](CMakeLists.txt)
[![Scripting](https://img.shields.io/badge/Scripting-Python%203.8%2B-blue)](tools/test_integration.py)
[![RTOS](https://img.shields.io/badge/RTOS-FreeRTOS-orange)](https://github.com/FreeRTOS/FreeRTOS-Kernel)

> A high-performance, modular embedded operating system kernel for the ESP32, delivering Linux-style command-line power within a microcontroller's constraints.
> 
⚡ Esp32OS — A lightweight open-source operating system for ESP32  
→ Build multitasking IoT apps faster  
→ Simple APIs, modular kernel  
→ Designed for hobbyists & embedded devs
---

## Overview

ESP32OS layers a clean OS architecture on top of FreeRTOS and ESP-IDF, giving you:

- A **full interactive shell** (UART + Telnet over WiFi) with command history, ANSI colours, and line editing
- **Process management** — create, list, kill, suspend, resume tasks like Linux processes
- **SPIFFS file system** — `ls`, `cd`, `cat`, `write`, `rm`, `mkdir`, `df`
- **Networking** — WiFi scan/connect, ping, HTTP GET, auto-reconnect
- **Hardware control** — GPIO, ADC, I2C scan, SPI via CLI commands
- **Feature modules** — PWM, timer, IPC (message queues/events/shared memory), MQTT, OTA, environment variables, background/scheduled command execution
- **Multi-level logging** — ring buffer + SPIFFS log file with rotation
- **Watchdog integration** and graceful crash recovery
- **Extensible command/module API** — register new commands in 5 lines

---

## Project Idea In Detail

ESP32OS is designed to bring a practical subset of Linux-like operational experience to low-cost microcontrollers.

The core idea is not to replace Linux, but to provide a familiar operational model for embedded products where Linux is too heavy in terms of boot time, RAM, flash footprint, and power budget.

### Why this idea matters

- Many embedded teams need shell-driven diagnostics and remote support, but cannot afford a full Linux stack.
- Traditional firmware projects often hide system internals; ESP32OS exposes runtime state (`ps`, `top`, `free`, `dmesg`) in a clean, operator-friendly way.
- A command-driven service architecture makes field debugging and production validation faster.

### What ESP32OS tries to achieve

- Provide a stable command shell over UART and Telnet for development and field maintenance.
- Offer a modular service layer (`os_kernel`, `os_fs`, `os_networking`, `os_logging`, `os_drivers`) with clean boundaries.
- Keep APIs simple so contributors can add drivers and commands quickly.
- Preserve deterministic embedded behavior using FreeRTOS scheduling and ESP-IDF device support.

### Intended use cases

- Device bring-up labs where engineers need direct runtime visibility.
- Education and training for RTOS + embedded networking concepts.
- Product prototypes that need CLI-managed networking, logging, and peripheral control.
- Factory and support tooling where scripted command execution is valuable.

### Design constraints

- Small memory budget and limited CPU compared with Linux-class systems.
- Real-time responsiveness must be preserved while still offering shell convenience.
- Hardware integration tests still require real boards even when CI is automated.

---

## Architecture

```
┌─────────────────────────────────────────────────────────────────┐
│                     Applications / User Code                     │
├─────────────────────────────────────────────────────────────────┤
│                        Shell (CLI)                               │
│          UART Session          │       Telnet Session            │
├────────────────────────────────┼────────────────────────────────┤
│  System Services                                                 │
│  ┌──────────┐ ┌──────────┐ ┌─────────────┐ ┌────────────────┐  │
│  │ os_kernel│ │  os_fs   │ │os_networking│ │  os_logging    │  │
│  │ (process │ │ (SPIFFS) │ │ (WiFi/TCP)  │ │ (ring+file)    │  │
│  │  mgmt)   │ │          │ │             │ │                │  │
│  └──────────┘ └──────────┘ └─────────────┘ └────────────────┘  │
├─────────────────────────────────────────────────────────────────┤
│                Hardware Abstraction Layer (HAL)                   │
│        os_drivers: GPIO │ ADC │ I2C │ SPI │ UART                │
├─────────────────────────────────────────────────────────────────┤
│                     ESP-IDF Peripheral APIs                       │
├─────────────────────────────────────────────────────────────────┤
│                FreeRTOS Kernel (task scheduler)                   │
├─────────────────────────────────────────────────────────────────┤
│                         ESP32 Hardware                            │
└─────────────────────────────────────────────────────────────────┘
```

---

## Quick Start

If you want a shorter operational guide, see [docs/USER_GUIDE.md](docs/USER_GUIDE.md).

### Fast Path (5 Minutes)

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0
```

If all tests pass, you have a working baseline.

### Prerequisites

| Tool | Version |
|------|---------|
| ESP-IDF | ≥ 5.0 |
| Python | ≥ 3.8 |
| CMake | ≥ 3.16 |
| Ninja | any recent |

### 1. Clone & Setup

```bash
git clone https://github.com/faizannazir/Esp32OS.git
cd Esp32OS

# Source ESP-IDF environment
. $IDF_PATH/export.sh
```

### 2. Configure target

```bash
idf.py set-target esp32      # or esp32s3, esp32c3
idf.py menuconfig            # optional — review sdkconfig
```

### 3. Build

```bash
idf.py build
```

### 4. Test

Run the hardware integration script from the project root after flashing:

```bash
python3 -m pip install pyserial
python3 tools/test_shell_host_integration.py
python3 tools/test_integration.py --port /dev/ttyUSB0
```

Expected result: all checks pass (count may change as the script evolves).

`test_shell_host_integration.py` runs offline command workflow checks for `env/export/unset/printenv/run/at/every/jobs/killjob` and does not require an attached board.

### 5. Flash & Monitor

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

Default baud rate: **115200**

### 6. Connect via UART

Open any serial terminal (minicom, PuTTY, `idf.py monitor`):

```
  _____  ____  ____  ____  ____  ___  ____
 | ____|/ ___||  _ \|___ \|___ \/ _ \/ ___|
 |  _|  \___ \| |_) | __) | __) | | | \___ \
 | |___  ___) |  __/ / __/ / __/| |_| |___) |
 |_____|____/|_|   |_____|_____|\___|____/

  ESP32 Embedded OS  v1.0.0
  Type 'help' for commands

esp32os>
```

### 7. Connect via Telnet (WiFi)

```bash
# First connect to WiFi from the serial shell:
esp32os> wifi connect MySSID MyPassword

# Then from your computer:
telnet <esp32-ip> 2222

# Default credentials:
Username: admin
Password: esp32os
```

### Example Terminal and UI Flow

Use the terminal for build, flash, and local shell work. In VS Code, the same flow can be driven from the integrated terminal and the serial monitor.

```text
$ idf.py set-target esp32s3
$ idf.py build
$ idf.py -p /dev/cu.usbserial-0001 flash monitor

ESP32OS Embedded OS v1.0.0
Type 'help' for commands

esp32os> uname -a
esp32os ESP32-S3  WiFi  IDF-v6.0  2 core(s)  Flash:4MB

esp32os> free
                           total        used        free
Heap:                     334684      116600      218084
```

The UI path is straightforward:

- Use the VS Code terminal for `idf.py build` and `idf.py flash`
- Use `idf.py monitor` or a serial terminal for shell interaction
- Use the GitHub Actions UI to review PR Checks and Master Release runs

---

## Command Reference

### System
| Command | Description | Example |
|---------|-------------|---------|
| `help` | List all commands | `help` |
| `uname -a` | System info | `uname -a` |
| `uptime` | System uptime + process count | `uptime` |
| `free` | Heap memory usage | `free` |
| `reboot` | Restart system | `reboot` |
| `echo` | Print text | `echo hello world` |
| `sleep` | Wait N seconds | `sleep 5` |
| `clear` | Clear screen | `clear` |

### Process Management
| Command | Description | Example |
|---------|-------------|---------|
| `ps` | List all processes | `ps` |
| `top` | Live heap + process view | `top` |
| `kill <pid>` | Terminate process | `kill 4` |
| `suspend <pid>` | Pause process | `suspend 3` |
| `resume <pid>` | Resume process | `resume 3` |

### File System
| Command | Description | Example |
|---------|-------------|---------|
| `ls [path]` | List directory | `ls /logs` |
| `pwd` | Print working directory | `pwd` |
| `cd <path>` | Change directory | `cd /logs` |
| `cat <file>` | Print file | `cat /etc/config.txt` |
| `write <file> <text>` | Write file | `write /tmp/note.txt hello` |
| `append <file> <text>` | Append to file | `append /tmp/log.txt line2` |
| `rm <file>` | Remove file | `rm /tmp/note.txt` |
| `mkdir <dir>` | Create directory | `mkdir /tmp/test` |
| `mv <src> <dst>` | Move/rename | `mv /tmp/a.txt /tmp/b.txt` |
| `df` | Filesystem usage | `df` |

### Networking
| Command | Description | Example |
|---------|-------------|---------|
| `wifi scan` | Scan for APs | `wifi scan` |
| `wifi connect` | Connect to AP | `wifi connect SSID pass` |
| `wifi disconnect` | Disconnect | `wifi disconnect` |
| `wifi status` | Show IP, RSSI, MAC | `wifi status` |
| `ifconfig` | Network interface info | `ifconfig` |
| `ping <host>` | ICMP ping | `ping 8.8.8.8` |
| `http <url>` | HTTP GET | `http http://example.com` |

### Hardware
| Command | Description | Example |
|---------|-------------|---------|
| `gpio read <pin>` | Read GPIO level | `gpio read 4` |
| `gpio write <pin> <val>` | Set GPIO output | `gpio write 2 1` |
| `gpio mode <pin> <mode>` | Set pin mode | `gpio mode 4 in_pullup` |
| `gpio info` | Show all GPIO state | `gpio info` |
| `adc read <ch>` | Read raw ADC | `adc read 0` |
| `adc readv <ch>` | Read ADC millivolts | `adc readv 0` |
| `adc readall` | Read all channels | `adc readall` |
| `i2c scan` | Scan I2C bus | `i2c scan 21 22` |
| `i2c read <addr> <reg> <len>` | I2C read | `i2c read 0x48 0x00 2` |
| `i2c write <addr> <reg> <bytes>` | I2C write | `i2c write 0x48 0x01 0xFF` |

### Logging
| Command | Description | Example |
|---------|-------------|---------|
| `dmesg [n]` | Show log ring buffer | `dmesg 20` |
| `loglevel [level]` | Get/set log level | `loglevel debug` |
| `logfile <on\|off>` | Enable SPIFFS logging | `logfile on` |

### Storage (NVS)
| Command | Description | Example |
|---------|-------------|---------|
| `nvs get <key>` | Read NVS value | `nvs get hostname` |
| `nvs set <key> <val>` | Write NVS value | `nvs set hostname esp32` |
| `nvs del <key>` | Delete NVS key | `nvs del hostname` |
| `nvs erase` | Erase all NVS | `nvs erase` |

### Environment and Scheduling
| Command | Description | Example |
|---------|-------------|---------|
| `env` | List environment variables | `env` |
| `export NAME=VALUE` | Set environment variable | `export WIFI_SSID=myssid` |
| `unset NAME` | Remove environment variable | `unset WIFI_SSID` |
| `printenv NAME` | Print one environment variable | `printenv WIFI_SSID` |
| `run <command...>` | Run a command in the background | `run echo hello` |
| `at <delay_ms> <command...>` | Run a command once later | `at 5000 echo done` |
| `every <period_ms> <command...>` | Run a command repeatedly | `every 1000 ps` |
| `jobs` | List scheduled jobs | `jobs` |
| `killjob <name>` | Cancel a scheduled job | `killjob job_123` |

### Feature Modules
| Command | Description | Example |
|---------|-------------|---------|
| `pwm init <channel> <pin> <freq_hz>` | Initialize PWM channel | `pwm init 0 2 5000` |
| `pwm duty <channel> <percent>` | Set PWM duty in percent | `pwm duty 0 25` |
| `pwm freq <channel> <freq_hz>` | Change PWM frequency | `pwm freq 0 1000` |
| `pwm deinit <channel>` | Deinitialize PWM channel | `pwm deinit 0` |
| `timer create <name> <period_ms> <reload>` | Create software timer | `timer create blink 1000 true` |
| `timer start <name>` | Start timer | `timer start blink` |
| `timer stop <name>` | Stop timer | `timer stop blink` |
| `timer restart <name> <period_ms>` | Change timer period | `timer restart blink 500` |
| `timer delete <name>` | Delete timer | `timer delete blink` |
| `timer list` | List timers | `timer list` |
| `msgq create <name> <size> <count>` | Create IPC message queue | `msgq create q1 16 8` |
| `msgq send <name> <data>` | Send text payload to queue | `msgq send q1 hello` |
| `msgq recv <name> [timeout_ms]` | Receive queue payload | `msgq recv q1 1000` |
| `event create <name>` | Create event group | `event create ev1` |
| `event set <name> <bits>` | Set event bits | `event set ev1 0x03` |
| `event wait <name> <bits> [timeout_ms]` | Wait for event bits | `event wait ev1 0x01 5000` |
| `mqtt config <broker_url>` | Set MQTT broker URL | `mqtt config mqtt://broker.hivemq.com` |
| `mqtt connect` | Connect MQTT client | `mqtt connect` |
| `mqtt pub <topic> <message> [-q QoS]` | Publish text payload | `mqtt pub dev/status online -q 1` |
| `mqtt pubhex <topic> <hex> [-q QoS]` | Publish binary payload from hex | `mqtt pubhex dev/raw DEADBEEF -q 0` |
| `mqtt sub <topic> [-q QoS]` | Subscribe to topic | `mqtt sub dev/status -q 1` |
| `ota update <url>` | Start OTA firmware update | `ota update https://example.com/fw.bin` |
| `ota status` | Show OTA state and progress | `ota status` |
| `ota confirm` | Confirm updated firmware | `ota confirm` |
| `ota rollback` | Roll back to previous firmware | `ota rollback` |

### Test Suites
| Command | Description | Example |
|---------|-------------|---------|
| `test mqtt` | Run MQTT component tests | `test mqtt` |
| `test ipc` | Run IPC component tests | `test ipc` |
| `test ota` | Run OTA component tests | `test ota` |
| `test pwm` | Run PWM component tests | `test pwm` |
| `test timer` | Run timer component tests | `test timer` |
| `test env` | Run environment component tests | `test env` |
| `test sched` | Run scheduler component tests | `test sched` |
| `test all` | Run all feature test suites | `test all` |

---

## Project Structure

```
esp32os/
├── CMakeLists.txt              # Root CMake
├── partitions.csv              # Custom flash partition table
├── sdkconfig.defaults          # Default SDK config
│
├── main/
│   ├── CMakeLists.txt
│   └── main.c                  # Boot sequence (app_main)
│
└── components/
    ├── os_logging/             # Multi-level logging subsystem
    │   ├── include/os_logging.h
    │   └── src/os_logging.c
    │
    ├── os_kernel/              # Process/task management
    │   ├── include/os_kernel.h
    │   └── src/os_kernel.c
    │
    ├── os_fs/                  # SPIFFS file system abstraction
    │   ├── include/os_fs.h
    │   └── src/os_fs.c
    │
    ├── os_hal/                 # HAL umbrella header
    │   └── include/os_hal.h
    │
    ├── os_drivers/             # GPIO, ADC, I2C, SPI, UART drivers
    │   ├── include/os_drivers.h
    │   └── src/os_drivers.c
    │
    ├── os_networking/          # WiFi manager, ping, HTTP client
    │   ├── include/os_networking.h
    │   └── src/os_networking.c
    │
    └── os_shell/               # Shell engine + all built-in commands
        ├── include/os_shell.h
        └── src/
            ├── os_shell.c      # Shell engine (readline, telnet, history)
            └── shell_commands.c # All built-in command handlers
```

---

## CI/CD

The project now includes two GitHub Actions workflows:

- `master-release.yml`:
    - Trigger: push to `master`
    - Builds: `esp32`, `esp32s3`
    - Creates auto tag and GitHub Release
    - Uploads release artifacts (`.bin`, `.elf`, `.map`, `.json`, `.zip`, `sdkconfig`)

- `pr-checks.yml`:
    - Trigger: pull requests targeting `master`
    - Builds: `esp32`, `esp32s3`
    - Runs test-harness sanity checks
    - Uploads build artifacts for reviewer validation

See [docs/TESTING.md](docs/TESTING.md) and [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) for contribution workflow details.

---

## Open Source, Attribution, and Name Usage

ESP32OS is open source under the MIT license.

What this means:

- You can use, modify, and distribute this code in personal and commercial projects.
- You must retain copyright and license notices from this repository.

Attribution and brand rules:

- Credit must be preserved in source distributions and derivative works.
- You may not market a fork or derivative as the official ESP32OS project.
- Use of the `ESP32OS` project name, logo, or branding for product marketing requires prior written permission from project maintainers.

For details, read:

- [LICENSE](LICENSE)
- [NOTICE.md](NOTICE.md)
- [BRANDING_POLICY.md](BRANDING_POLICY.md)

---

## Adding Custom Commands

```c
// In your component source file:
#include "os_shell.h"

static int cmd_hello(int fd, int argc, char **argv) {
    shell_printf(fd, "Hello from custom command! args=%d\r\n", argc - 1);
    return SHELL_CMD_OK;
}

static const shell_command_t my_cmd =
    SHELL_CMD_ENTRY("hello", "Say hello", "hello [name]", cmd_hello);

void my_module_init(void) {
    shell_register_command(&my_cmd);
}
```

Then call `my_module_init()` from `app_main` after `shell_init()`.

---

## Memory Footprint

| Component | Flash | RAM |
|-----------|-------|-----|
| FreeRTOS kernel | ~40 KB | ~8 KB |
| ESP-IDF WiFi | ~300 KB | ~70 KB |
| ESP32OS shell | ~18 KB | ~4 KB |
| os_kernel | ~6 KB | ~2 KB |
| os_fs + SPIFFS | ~20 KB | ~3 KB |
| os_logging | ~4 KB | ~16 KB (ring buf) |
| os_drivers (HAL) | ~8 KB | ~1 KB |
| **Total ESP32OS** | **~56 KB** | **~26 KB** |

ESP32 has 520 KB SRAM and 4 MB flash — ESP32OS uses well under 10% RAM headroom.

---

## Configuration

Edit `sdkconfig.defaults` or run `idf.py menuconfig`:

| Key | Default | Description |
|-----|---------|-------------|
| `FREERTOS_HZ` | 1000 | Tick rate (1 ms resolution) |
| `ESP_MAIN_TASK_STACK_SIZE` | 8192 | app_main stack |
| `ESP_TASK_WDT_TIMEOUT_S` | 30 | Watchdog timeout |
| `ESP_CONSOLE_UART_BAUDRATE` | 115200 | Serial baud |

Telnet credentials are set at compile time in `os_shell.c`:
```c
#define TELNET_USERNAME  "admin"
#define TELNET_PASSWORD  "esp32os"
```

WiFi auto-connect is stored in NVS via `os_wifi_save_credentials()`.

---

## Future Enhancements and Features

The roadmap below highlights high-impact improvements planned for ESP32OS.

### Shell and Usability

- Command auto-completion (tab completion for commands and file paths)
- Persistent shell history across reboots (NVS-backed)
- Command aliases and simple shell variables
- Lightweight scripting mode (`script run <file>`) for batch command execution

### Kernel and Tasking

- Per-task CPU usage tracking integrated into `top`
- Better process supervision with automatic restart policies
- Priority and affinity controls exposed through shell commands
- Improved stack watermark diagnostics and runtime task health checks

### Networking

- MQTT client commands (`mqtt connect`, `pub`, `sub`)
- HTTPS enhancements with certificate pinning support
- mDNS/Bonjour service advertisement for zero-config discovery
- Optional WebSocket-based remote shell gateway

### Storage and File System

- Stronger directory compatibility layer for SPIFFS/LittleFS parity
- Optional LittleFS backend for improved directory semantics
- File integrity utilities (`sha256`, `fsck-lite`, log verification)
- Structured config files under `/etc` with validation helpers

### Drivers and Hardware

- PWM and servo command set
- Expanded sensor driver templates (I2C, SPI, UART)
- Better ADC calibration workflows and per-channel profiles
- Power management helpers (sleep modes, wake source diagnostics)

### Logging and Observability

- Log export commands (serial dump, HTTP upload, compressed archive)
- Metrics endpoint for runtime counters and health snapshots
- Event tracing for major subsystem state transitions
- Optional panic report summarizer from coredump metadata

### Security

- Role-based shell access policies (operator/admin)
- Configurable credential storage hardening for Telnet/WiFi
- Optional secure remote shell transport strategy
- Safer defaults for production (`release` profile configs)

### Build, Test, and Release

- Hardware-in-the-loop CI on self-hosted runners
- Automated regression suites for shell command behavior
- Performance benchmark reporting in CI artifacts
- Versioned changelog generation tied to release tags

If you want to contribute one of these items, see [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) and open an issue describing scope and acceptance criteria.

---

## License

MIT License — see [LICENSE](LICENSE).

---

## Documentation

| File | Description |
|------|-------------|
| [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) | Detailed architecture & design decisions |
| [docs/COMPONENTS.md](docs/COMPONENTS.md) | Per-component API reference |
| [docs/TESTING.md](docs/TESTING.md) | Test strategy and test procedures |
| [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) | Contribution guide |
| [docs/EXTENDING.md](docs/EXTENDING.md) | How to add commands, drivers, services |
| [NOTICE.md](NOTICE.md) | Attribution notice for reuse and distribution |
| [BRANDING_POLICY.md](BRANDING_POLICY.md) | Rules for project name and branding usage |

---

## Acknowledgements and Upstream Projects

ESP32OS is built on top of excellent open-source ecosystems. Thanks to the maintainers and contributors of:

- Espressif Systems
    - ESP-IDF: https://github.com/espressif/esp-idf
    - GitHub profile: https://github.com/espressif

- FreeRTOS
    - Kernel repository: https://github.com/FreeRTOS/FreeRTOS-Kernel
    - GitHub profile: https://github.com/FreeRTOS

- LwIP TCP/IP stack
    - Repository: https://github.com/lwip-tcpip/lwip
    - GitHub profile: https://github.com/lwip-tcpip

- Python Serial ecosystem (used by integration tooling)
    - pyserial: https://github.com/pyserial/pyserial
    - GitHub profile: https://github.com/pyserial

ESP32OS is an independent project and is not officially affiliated with Espressif, FreeRTOS, or LwIP maintainers.
