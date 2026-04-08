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

## Test

Run the integration test script after flashing:

```bash
python3 -m pip install pyserial
python3 tools/test_integration.py --port /dev/ttyUSB0 --baud 115200
```

The script checks common shell, filesystem, logging, GPIO, ADC, and NVS commands.

## Troubleshooting

- If build fails on the first configure step, make sure `IDF_PATH` is exported in the current shell.
- If the serial monitor does not connect, verify the USB port and baud rate.
- If Telnet does not start, confirm the board has joined WiFi and the network stack initialized correctly.