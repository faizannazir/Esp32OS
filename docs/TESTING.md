# ESP32OS Testing Guide

This guide is optimized for contributors who want a quick, repeatable way to validate changes.

## Quick Local Test Flow

From the repository root:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3
idf.py build
idf.py -p /dev/ttyUSB0 flash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0 --baud 115200
```

Pass criteria:

- Build completes without errors
- Integration script reports all checks as passed

If you are using a different board, change target and serial port accordingly.

---

## Testing Strategy

ESP32OS uses a three-tier test approach:

| Tier | Method | When |
|------|--------|------|
| **Unit** | Host-side mocks + Unity framework | During development |
| **Integration** | Hardware-in-the-loop via UART shell | Before every release |
| **System** | Full soak test on real hardware | Nightly / pre-release |

---

## Tier 1 — Unit Tests (Host Simulation)

ESP-IDF includes the `unity` test framework. This repository does not currently ship host-side unit tests, so the primary automated check is the integration script in `tools/`.

CI sanity checks validate the test harness itself with:

- `python -m py_compile tools/test_integration.py`
- `python tools/test_integration.py --help`

### Integration script

From the project root:

```bash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0 --baud 115200
```

The script exercises shell, filesystem, logging, GPIO, ADC, and NVS commands over the serial console.

---

## Tier 2 — Hardware Integration Tests (Shell-based)

Connect to the device via UART or Telnet and run each test sequence manually or via a script.

Run:
```bash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0
```

For manual validation, the shell prompt should accept `help`, `ps`, `free`, `ls`, `wifi status`, and `dmesg` after boot.

### Pre-PR Minimum Checklist

Before opening a pull request, run and verify:

1. `idf.py set-target esp32 && idf.py build`
2. `idf.py set-target esp32s3 && idf.py build`
3. `python3 tools/test_integration.py --port <your-port>` on at least one real board

This matches what CI validates on PRs and what release automation validates on `master`.

---

## Tier 3 — System / Soak Tests

### 3.1 Memory Leak Test

```bash
# Watch free heap over 1 hour of command cycling
esp32os> free
esp32os> ps
esp32os> top
# ... repeat in a loop via test script
# Heap should remain stable (±2 KB acceptable)
```

Expected: No monotonic decrease in `free` output.

### 3.2 Telnet Concurrent Sessions Test

```bash
# Terminal 1:
telnet <esp32-ip> 2222

# Terminal 2 (simultaneously):
telnet <esp32-ip> 2222

# Both sessions should work independently
# Run `ps` in each — output should show both telnet tasks
```

### 3.3 WiFi Reconnect Test

```bash
esp32os> wifi connect MySSID MyPass
esp32os> wifi status                    # confirm connected
# Power-cycle the router or disable/enable WiFi
# Observe: system should auto-reconnect within 30s
esp32os> wifi status                    # should show Connected again
```

### 3.4 Watchdog Test

```bash
# Simulate watchdog timeout by suspending sys_monitor:
esp32os> ps
esp32os> suspend <pid-of-sys_monitor>
# Wait 30 seconds
# Expected: system reboot with "task watchdog" reset reason
# After reboot:
esp32os> uname -a                       # should print normally
```

### 3.5 Filesystem Persistence Test

```bash
# Write files
esp32os> write /etc/hostname myesp32
esp32os> write /etc/notes.txt "persistent data"

# Reboot
esp32os> reboot

# After reboot, verify files persist
esp32os> cat /etc/hostname              # should print: myesp32
esp32os> cat /etc/notes.txt             # should print: persistent data
```

### 3.6 I2C Scan Test

Connect any I2C device (e.g., SSD1306 OLED at 0x3C):
```bash
esp32os> i2c scan 21 22
# Expected output includes: 3c
```

### 3.7 ADC Calibration Test

```bash
# With 1.65V reference on ADC channel 0 (GPIO36):
esp32os> adc readv 0
# Expected: ~1650 mV (±50 mV with proper calibration)
```

### 3.8 GPIO Loopback Test

Connect GPIO2 → GPIO4 with a jumper:
```bash
esp32os> gpio mode 2 out
esp32os> gpio mode 4 in
esp32os> gpio write 2 1
esp32os> gpio read 4              # expected: 1
esp32os> gpio write 2 0
esp32os> gpio read 4              # expected: 0
```

---

## Performance Benchmarks

| Metric | Target | Measurement Method |
|--------|--------|--------------------|
| Shell response latency | < 5 ms | `time` wrapper on test script |
| WiFi connect time | < 10 s | Log timestamp delta |
| Process create time | < 1 ms | `esp_timer_get_time()` before/after |
| Log write (no file) | < 50 µs | Timer around OS_LOGI |
| SPIFFS write 256B | < 10 ms | Timer around `os_fs_write_file` |
| ADC read (×4 avg) | < 1 ms | Timer |

---

## CI/CD Integration

For automated checks in this repository:

- Pull requests to `master` run build + harness sanity checks via `.github/workflows/pr-checks.yml`
- Pushes to `master` run build + release pipeline via `.github/workflows/master-release.yml`

Hardware-specific tests (GPIO wiring, ADC voltage verification, WiFi environment behavior) still require real devices.

If you want to add emulator-based tests later, use QEMU as an additional tier:

```yaml
# .github/workflows/build.yml
name: Build & Test
on: [push, pull_request]
jobs:
  build:
    runs-on: ubuntu-latest
    container: espressif/idf:v5.2
    steps:
      - uses: actions/checkout@v3
      - run: idf.py set-target esp32
      - run: idf.py build
      - name: Run unit tests (QEMU)
        run: |
          idf.py -C test/ build
          python tools/run_tests_qemu.py
```

Note: Full hardware tests (WiFi, ADC, I2C) require real hardware. Use a self-hosted runner with a connected ESP32 for those.

---

## Reporting Bugs

When filing a bug report, include:

1. Output of `uname -a`
2. Output of `free` and `top`
3. Output of `dmesg 50` at time of issue
4. Steps to reproduce
5. Expected vs actual behaviour
6. IDF version (`idf.py --version`)
