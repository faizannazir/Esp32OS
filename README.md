# ESP32OS — Embedded Linux-Style Operating System for ESP32

[![ESP-IDF](https://img.shields.io/badge/ESP--IDF-v5.x-blue)](https://docs.espressif.com/projects/esp-idf)
[![Target](https://img.shields.io/badge/Target-ESP32%20%7C%20ESP32--S3-green)](https://www.espressif.com)
[![License](https://img.shields.io/badge/License-MIT-yellow)](LICENSE)
[![Language](https://img.shields.io/badge/Language-C%20(C17)-lightgrey)]()

> A high-performance, modular embedded operating system kernel for the ESP32, delivering Linux-style command-line power within a microcontroller's constraints.

---

## Overview

ESP32OS layers a clean OS architecture on top of FreeRTOS and ESP-IDF, giving you:

- A **full interactive shell** (UART + Telnet over WiFi) with command history, ANSI colours, and line editing
- **Process management** — create, list, kill, suspend, resume tasks like Linux processes
- **SPIFFS file system** — `ls`, `cd`, `cat`, `write`, `rm`, `mkdir`, `df`
- **Networking** — WiFi scan/connect, ping, HTTP GET, auto-reconnect
- **Hardware control** — GPIO, ADC, I2C scan, SPI via CLI commands
- **Multi-level logging** — ring buffer + SPIFFS log file with rotation
- **Watchdog integration** and graceful crash recovery
- **Extensible command/module API** — register new commands in 5 lines

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
cd esp32os

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
python3 tools/test_integration.py --port /dev/ttyUSB0
```

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
