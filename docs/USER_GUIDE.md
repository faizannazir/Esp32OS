# ESP32OS User Guide

## What You Need

- ESP-IDF 6.0 installed and exported in your shell session
- Python 3.8 or newer
- A supported ESP32 board connected over USB
- A serial terminal or `idf.py monitor`

## Build

From the repository root:

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32
idf.py build
```

If you are using a different board, select its target: `esp32`, `esp32s3`, or `esp32c3`.

## Flash

```bash
idf.py -p /dev/ttyUSB0 flash
```

If you already know the port and want to flash and open the console in one step:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## First Boot

On boot, ESP32OS initializes its core services and UART shell. Remote services are disabled by default, so you should normally see the boot banner followed by the `esp32os>` prompt:

```text
ESP32OS Embedded OS v1.0.0
Type 'help' for commands

Web portal disabled (secure default)
Telnet disabled (secure default)
esp32os>
```

Useful first commands:

```text
help
ps
free
df
wifi status
```

## Optional web portal

The web control portal is disabled by default. It currently has no authentication and exposes shell/device-control actions. Only enable it on an isolated development network that you trust.

To opt in, run:

```bash
idf.py menuconfig
```

Open **ESP32OS Security and Network Services**, enable the web portal, save the configuration, then rebuild and flash. Do not expose the portal to the public internet or an untrusted LAN.

## Connect to the Shell

You can use the UART console or Telnet for command-line access.

### UART

Open `idf.py monitor` or any serial terminal at 115200 baud.

Example session:

```text
esp32os> help
esp32os> uname -a
esp32os ESP32-S3  WiFi  IDF-v6.0  2 core(s)  Flash:4MB
esp32os> free
Heap:                     334684      116600      218084
```

In VS Code, the normal workflow is:

1. Build from the integrated terminal with `idf.py build`
2. Flash from the integrated terminal with `idf.py -p <port> flash`
3. Use the serial monitor or monitor output panel for the shell UI

### Optional Telnet

Telnet is disabled by default. It is plaintext and does not encrypt credentials or commands. Prefer UART. If you enable Telnet for an isolated development network:

1. Run `idf.py menuconfig`.
2. Open **ESP32OS Security and Network Services**.
3. Enable Telnet and set a non-empty username and password locally.
4. Rebuild and flash the firmware.
5. Connect only from a trusted network:

```bash
telnet <esp32-ip> 2222
```

There are no default credentials. Never commit local `sdkconfig` files or reuse production passwords.

### Optional Wi-Fi AP fallback

The device-hosted AP fallback is disabled by default. If explicitly enabled in the same menuconfig section, the current fallback AP is open. Do not enable it on an untrusted or production network.

## Common Shell Commands

- `help` shows all shell commands
- `ps` lists running processes
- `top` shows heap and process status
- `ls`, `cd`, `pwd`, `cat`, `write`, `append`, `rm`, `mkdir`, and `mv` manage files
- `wifi scan`, `wifi connect`, `wifi status`, `ping`, and `http` handle networking
- `gpio`, `adc`, and `i2c scan` expose hardware controls
- `dmesg` shows recent logs

Feature module commands:

- `pwm` controls PWM channels (`init`, `duty`, `freq`, `deinit`, `status`)
- `timer` creates and manages software timers (`create`, `start`, `stop`, `restart`, `delete`, `list`)
- `msgq` manages message queues (`create`, `delete`, `send`, `recv`, `list`)
- `event` manages event groups (`create`, `delete`, `set`, `clear`, `get`, `wait`)
- `env` lists environment variables, while `export`, `unset`, and `printenv` manage values
- `run` launches a command in the background, `at` schedules a one-shot command, and `every` schedules a repeating command
- `jobs` lists scheduled commands and `killjob` cancels one by name
- `mqtt` manages broker connectivity and publish/subscribe flow
- `ota` manages firmware updates (`update`, `status`, `confirm`, `rollback`)

## Feature Module Quickstart

Example session:

```text
esp32os> pwm init 0 2 5000
esp32os> pwm duty 0 20

esp32os> msgq create q1 16 4
esp32os> msgq send q1 hello
esp32os> msgq recv q1 1000

esp32os> mqtt config mqtt://broker.hivemq.com
esp32os> mqtt connect
esp32os> mqtt pub dev/status online -q 1
esp32os> mqtt pubhex dev/raw DEADBEEF

esp32os> export WIFI_SSID=myssid
esp32os> printenv WIFI_SSID
esp32os> run echo background hello
esp32os> at 5000 echo run later
esp32os> every 1000 ps

esp32os> ota status
```

## Example Output

When the device boots successfully, you should see output similar to:

```text
ESP32OS Embedded OS v1.0.0
Type 'help' for commands

esp32os> uname -a
esp32os ESP32-S3  WiFi  IDF-v6.0  2 core(s)  Flash:4MB

esp32os> ls /
logs
tmp
etc
```

If you see the prompt and commands return output, the shell is working.

## Test

Run the integration test script after flashing:

```bash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0 --baud 115200
```

The script checks common shell, filesystem, logging, GPIO, ADC, and NVS commands.

You can also run feature component suites directly from the shell:

```text
esp32os> test mqtt
esp32os> test ipc
esp32os> test ota
esp32os> test pwm
esp32os> test all
```

## Troubleshooting

- If build fails on the first configure step, make sure `IDF_PATH` is exported in the current shell.
- If the serial monitor does not connect, verify the USB port and baud rate.
- If Telnet does not start, confirm the board has joined WiFi and the network stack initialized correctly.