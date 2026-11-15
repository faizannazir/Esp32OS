# Testing ESP32OS

[Documentation](README.md) · [Contributing](CONTRIBUTING.md) · [User Guide](USER_GUIDE.md)

Separate host checks, firmware builds and device validation when reporting results. A successful Python run or CI build does not establish that peripherals work on a physical board.

## What runs today

| Check | Entry point | What it establishes |
|---|---|---|
| Python syntax | `python3 -m py_compile tools/test_*.py` | Test tools parse successfully |
| Host command model | `tools/test_shell_host_integration.py` | Environment and scheduler scenarios pass in a Python model |
| Firmware build | `idf.py build` | C components compile and link for the selected target |
| Build health | `tools/test_build_health.py --target <target>` | Required artifacts, partition headroom and memory budgets pass |
| UART integration | `tools/test_integration.py --port <port>` | Scripted commands return expected text on a connected board |
| Feature suites | `test <suite>` at the device prompt | The selected built-in C suite executes on the board |
| Manual system checks | Board-specific procedure | Actual wiring, networking, persistence and sustained operation |

The host runner implements its own `HostShell` in Python. It does **not** compile or execute `os_shell.c`, emulate an ESP32, or test FreeRTOS timing. No automated nightly hardware soak test is configured in the repository workflows.

## Without a board

From the repository root:

```bash
python3 -m py_compile tools/test_integration.py tools/test_shell_host_integration.py tools/test_build_health.py
python3 tools/test_shell_host_integration.py
```

The model covers `env`, `export`, `unset`, `printenv`, `run`, `at`, `every`, `jobs` and `killjob`. Run it after changing the documented command contract, but also validate C changes on the actual firmware.

## Firmware builds and budgets

In an ESP-IDF 6.0 terminal:

```bash
idf.py set-target esp32
idf.py build
python3 tools/test_build_health.py --target esp32
```

Repeat for `esp32s3` and `esp32c3` when changing shared firmware. Run the health check immediately after each corresponding build. Target switching regenerates configuration; preserve any required local settings separately.

The [health checker](../tools/test_build_health.py) verifies application, ELF, map, bootloader and partition artifacts, application headroom, IRAM/DRAM budgets and expected component libraries. Its source defines the thresholds; these are build checks rather than measured runtime performance.

## UART integration

Use a development board with suitable wiring. The [serial runner](../tools/test_integration.py) writes/removes files, changes logging, touches NVS keys and drives GPIO2 and GPIO4. Review those operations for your board before running it. Use `--skip-peripherals` to omit GPIO and ADC checks when the wiring is unknown. Some directory expectations may fail with the current SPIFFS wrapper; report those failures rather than treating directory support as verified.

Close the serial monitor so the runner can open the port:

```bash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0 --baud 115200
```

Record the exact board, chip target, commit, ESP-IDF version, wiring and script output. A substring match confirms the response checked by that test; it does not prove voltage accuracy or external circuit behavior.

## ESP32 bench results

On November 15, 2026, an ESP32-D0WD-V3 revision 3.1 with 4 MB flash built and booted with ESP-IDF 6.0 over UART at 115200 baud. The default firmware passed 14 build health checks. The portal-enabled build passed all 29 UART smoke checks with `--skip-peripherals`, covering shell commands, managed task listing, SPIFFS file operations, logging and NVS.

```text
esp32os> uname -a
esp32os ESP32  WiFi  IDF-v6.0  2 core(s)  Flash:4MB
```

These results apply to the tested board and wiring. GPIO, ADC and PWM checks were skipped because LCD wiring was unknown.

The sidebar, overview editor and Wi-Fi screens were exercised locally with example API responses. Browser checks covered terminal output, nearby network selection, secured and open join requests, saved widget order and a 390 px viewport. The portal-enabled ESP32 build passed all 14 build health checks with about 867 KiB free in the 2 MiB factory application partition. The final build was flashed to the bench ESP32. The board joined the shared Wi-Fi, returned nearby scan results and reported the associated SSID in both its UART shell and its status API. Safari opened the live portal and returned the board’s response to `uname -a`. The [dashboard screenshots](DASHBOARD.md#live-screenshots) came from that session.

## Built-in feature suites

From the device shell:

```text
test mqtt
test ipc
test ota
test pwm
test timer
test env
test sched
test all
```

These are the suites dispatched by [`cmd_test()`](../components/os_shell/src/shell_commands.c). Test files also exist in other component directories; their presence does not mean `test all` invokes them. Review suite code before running: suites can allocate resources, change configuration and manipulate peripherals. An OTA suite passing does not add OTA slots to the default partition table.

## Manual validation

Choose checks relevant to the change and state what actually happened:

| Area | Procedure and evidence |
|---|---|
| Boot and shell | Cold boot, capture initialization logs, run `help`, `free`, `ps` and `dmesg` |
| File persistence | Write `/persistence.txt`, read it, reboot, read it again; record any mount/format messages |
| Wi-Fi | Connect to your network, interrupt connectivity, observe reconnect attempts and final `wifi status` |
| GPIO/PWM/I²C/ADC | Record selected pins, wiring, instrument/sensor and measured results |
| Sustained operation | Repeat representative workloads; record heap trend, duration, resets and error logs |
| Dashboard | Enable on an isolated network; test status, browser settings, relevant controls and UART fallback |
| OTA | Use an OTA-capable layout and test update, confirmation and rollback with explicit recovery steps |

Do not use `suspend sys_monitor` as a watchdog test: the main loop feeds the configured watchdog, and `sys_monitor` is a separate diagnostics task. Watchdog validation needs a controlled test that actually stops a subscribed task from feeding it, plus a way to recover the board.

## CI and release artifacts

- [PR checks](../.github/workflows/pr-checks.yml) run Python harness checks, build all three targets in `espressif/idf:v6.0`, validate build health and upload artifacts with source provenance.
- [Master releases](../.github/workflows/master-release.yml) can reuse successful PR artifacts only after checking their source tree and required files. Otherwise, they build from source. The workflow then packages firmware and creates a release.
- Neither workflow runs attached-board integration or peripheral tests.

For documentation-only changes, verify links, examples and rendered images; a new firmware build is unnecessary unless a code/configuration change also needs validation.

## Reporting a failure

Include the commit, board/target, ESP-IDF version, reproduction steps, expected result and observed result. Add relevant `uname -a`, `free`, `ps` and `dmesg` output. Remove credentials and private network information before sharing logs. Clearly list checks you could not run.
