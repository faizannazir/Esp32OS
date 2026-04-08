# ESP32OS Architecture

## Design Philosophy

ESP32OS is built on three core principles:

1. **Layered isolation** — each layer depends only on layers below it; no upward dependencies
2. **Minimal RAM** — static allocation wherever possible; ring buffers instead of dynamic queues
3. **Real-time first** — FreeRTOS scheduling is never bypassed; blocking calls use `xEventGroupWaitBits` with timeouts

---

## Layer Model

```
Layer 4: Applications
         User tasks created via os_process_create()
         Custom shell commands registered via shell_register_command()

Layer 3: Shell (CLI)
         os_shell — line editor, ANSI escape handling, history, telnet server
         shell_commands — built-in command implementations

Layer 2: System Services
         os_kernel   — FreeRTOS task ↔ process abstraction, watchdog
         os_fs       — SPIFFS VFS mount, CWD, file helpers
         os_networking — TCP/IP stack, WiFi STA, ping, HTTP client
         os_logging  — ring buffer + UART + SPIFFS file sink

Layer 1: Hardware Abstraction Layer
         os_hal / os_drivers — GPIO, ADC, I2C, SPI, extra UART

Layer 0: ESP-IDF + FreeRTOS
         Peripheral drivers, TCP/IP (lwIP), event loop, NVS, partition mgmt
```

---

## Subsystem Design Decisions

### Kernel (os_kernel)

**Decision:** Map FreeRTOS `TaskHandle_t` to a `process_t` descriptor in a static table (no heap allocation after init).

**Rationale:**
- A 20-entry static table costs only 20 × 72 = 1440 bytes of RAM
- No fragmentation risk from repeated malloc/free of descriptors
- `os_pid_t` is just a uint16 counter — simple, predictable
- FreeRTOS doesn't have a kernel-space process table; we maintain our own

**Trade-off:** Max process count is a compile-time constant (`OS_MAX_PROCESSES=20`). For most embedded apps this is plenty; increase if needed.

**Thread safety:** All table access is protected by a single `SemaphoreHandle_t` mutex. Operations are fast (microsecond-scale), so mutex hold time is negligible.

---

### Logging (os_logging)

**Decision:** Ring buffer of 64 entries in BSS, plus optional SPIFFS file sink.

**Structure:**
```
UART (always)
  ↑
os_log_write()
  │
  ├── ring buffer (64 × 216 bytes = ~14 KB in BSS)
  │         ↑ os_log_dump() reads from here
  │
  └── [optional] SPIFFS file (/spiffs/system.log)
```

**Why a ring buffer?**
- Bounded memory: no unbounded growth
- `dmesg` can replay the last 64 lines at any time
- No dynamic allocation

**ANSI codes on UART:** Terminals (minicom, PuTTY with ANSI mode) render colours automatically. Plain serial dumps are still readable in monochrome.

---

### Shell (os_shell)

**Decision:** Stateless command dispatch with per-session stack-allocated history.

**Line editor features:**
- Left/Right cursor movement (with mid-line insert)
- Backspace / Delete key
- Ctrl+A/E (home/end), Ctrl+U (clear line), Ctrl+L (redraw), Ctrl+C (cancel)
- Up/Down arrow — navigate history ring buffer
- Quoted string argument support (`"hello world"` as single arg)

**Session model:**
```
UART task (single) ──────────────────────┐
                                         ↓
Telnet server task                  shell_execute(fd, line)
  └── accepts connections                │
      └── per-client task               ├── parse args → argv[]
          (stack-allocated history)      └── dispatch to handler(fd, argc, argv)
```

Each Telnet session runs in its own FreeRTOS task with its own history buffer on the stack. Sessions are fully independent.

**Why Telnet, not SSH?**
Full SSH requires ~80 KB of flash for mbedTLS + key management overhead. Telnet on a firewalled local network is acceptable for embedded dev/debug. Authentication is still enforced (username + password). For production environments requiring encryption, wrap with an SSH tunnel on the host side.

---

### File System (os_fs)

**Decision:** SPIFFS with ESP-IDF VFS layer — files accessed via standard POSIX `fopen`/`fread`/`fwrite`.

**Virtual directory layout:**
```
/ (logical root, maps to /spiffs/)
├── /logs/          ← system.log, crash.log
├── /etc/           ← configuration files
└── /tmp/           ← temporary scratch space
```

**CWD implementation:** The current working directory is stored as a string in module state. `os_fs_abspath()` resolves relative paths to `/spiffs/...` absolute paths before all VFS calls.

**SPIFFS limitations to know:**
- No true directory support (directories are simulated by path prefixes in SPIFFS); ESP-IDF's VFS layer provides the `opendir`/`readdir` illusion
- Max concurrent open files: 10 (`OS_FS_MAX_FILES`)
- Wear levelling is handled internally by SPIFFS

---

### Networking (os_networking)

**Decision:** Standard ESP-IDF WiFi STA mode with event-group-based blocking connect.

**Auto-reconnect:**
- On `WIFI_EVENT_STA_DISCONNECTED`, driver retries up to `WIFI_MAX_RETRY=5` times
- After 5 failures, sets `WIFI_FAIL_BIT` in the event group
- Auto-connect on boot reads NVS key `wifi_cfg/ssid` and `wifi_cfg/pass`

**Ping implementation:** Uses raw ICMP socket. On ESP-IDF, raw sockets require no special privileges. Checksum is computed in software (no hardware assist on ESP32). Each ping sleeps 1 second between packets.

**HTTP client:** Delegates to `esp_http_client` which handles redirect, chunked encoding, and TLS. Response is written into a caller-provided buffer (no heap allocation for response body).

---

### HAL / Drivers (os_drivers)

**GPIO:** Wraps `driver/gpio.h`. Maintains a local `dir[]` array to know pin direction at query time (ESP-IDF doesn't expose a "what mode is this pin currently in" API cleanly).

**ADC:** Oversamples ×4 and uses `esp_adc_cal` for voltage calibration. ADC2 is deliberately omitted as it conflicts with WiFi on ESP32.

**I2C:** Single master port (`I2C_NUM_0`). Can be re-initialised with different SDA/SCL pins by calling `i2c_driver_init()` again (it deinits first). Scan function probes all 7-bit addresses 0x08–0x77.

**SPI:** Uses `SPI2_HOST` with DMA channel `SPI_DMA_CH_AUTO`. Full-duplex transfer via `spi_device_transmit`.

---

## Task Map

| Task Name | Stack | Priority | Description |
|-----------|-------|----------|-------------|
| `uart_shell` | 8192 | 5 | UART line editor loop |
| `telnet_srv` | 4096 | 6 | Telnet accept loop |
| `telnet_<ip>` | 6144 | 6 | Per-client Telnet session |
| `sys_monitor` | 2048 | 3 | Heap monitor + watchdog feed |
| `tiT` (lwIP) | 3584 | 18 | TCP/IP processing (IDF built-in) |
| `wifi` (WiFi) | 3584 | 23 | WiFi stack (IDF built-in) |
| `esp_timer` | 4096 | 22 | Timers (IDF built-in) |

---

## Memory Layout (Flash)

```
0x0000  Boot loader   (32 KB)
0x8000  Partition table
0x9000  NVS           (24 KB)
0xF000  PHY init      (4 KB)
0x10000 Application   (2 MB)   ← ESP32OS binary
0x210000 SPIFFS       (1.9 MB) ← /spiffs filesystem
0x3F0000 Core dump    (64 KB)
```

---

## Error Handling Strategy

1. **Driver init failures** — logged as `OS_LOGE`, boot continues (degraded mode)
2. **Filesystem mount failure** — boot continues without persistent storage
3. **Network failure** — WiFi unavailable, telnet won't bind; UART shell still works
4. **Stack overflow** — FreeRTOS stack canary triggers `panic`, core dump to flash, reboot
5. **Watchdog** — 30-second task watchdog; `sys_monitor` task feeds it regularly
6. **Brownout** — ESP-IDF brownout detector triggers clean reboot
7. **Panic handler** — ESP-IDF default panic handler prints backtrace over UART and writes core dump
