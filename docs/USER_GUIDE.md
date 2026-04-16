# ESP32OS User Guide

## What You Need

- ESP-IDF 5.x installed and exported in your shell session
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

If you are using a different board, replace `esp32` with the target for that chip, such as `esp32s3`.

## Flash

```bash
idf.py -p /dev/ttyUSB0 flash
```

If you already know the port and want to flash and open the console in one step:

```bash
idf.py -p /dev/ttyUSB0 flash monitor
```

## First Boot

On boot, ESP32OS starts the kernel, filesystem, networking, shell, and watchdog tasks. You should see the boot banner followed by the `esp32os>` prompt.

Useful first commands:

```text
help
ps
free
df
wifi status
```

## Connect to the Shell

You can use either the UART console or Telnet.

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

### Telnet

1. Connect the board to WiFi from the UART shell:

```text
wifi connect MySSID MyPassword
```

2. Connect from your computer:

```bash
telnet <esp32-ip> 2222
```

Default Telnet credentials:

- Username: `admin`
- Password: `esp32os`

## Common Commands

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