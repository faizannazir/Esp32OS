#!/usr/bin/env python3
"""
ESP32OS Integration Test Runner
Usage: python3 tools/test_integration.py [--port /dev/ttyUSB0] [--baud 115200]
"""

import serial
import time
import sys
import argparse
import re

# ────────────────────────────────────────────────────
# Test Definitions
# Each entry: (command, expected_substring, description)
# An empty expected substring requires a completed command with no output.
# ────────────────────────────────────────────────────
TESTS = [
    # System
    ("uname -a",              "ESP32OS",           "uname reports esp32os"),
    ("uptime",                "up",                "uptime responds"),
    ("free",                  "Heap",              "free shows heap"),
    ("echo hello_test",       "hello_test",        "echo works"),
    ("sleep 1",               "",                  "sleep completes"),

    # Process management
    ("ps",                    "PID",               "ps shows process list"),
    ("top",                   "Mem",               "top shows memory"),

    # File system
    ("pwd",                   "/",                 "pwd shows root"),
    ("ls /",                  "item",              "ls root works"),
    ("mkdir /tmp/testdir",    "",                  "mkdir creates dir"),
    ("ls /tmp",               "testdir",           "ls shows new dir"),
    ("write /tmp/t.txt data", "",                  "write creates file"),
    ("cat /tmp/t.txt",        "data",              "cat reads file"),
    ("append /tmp/t.txt more","",                  "append works"),
    ("cat /tmp/t.txt",        "more",              "appended data readable"),
    ("mv /tmp/t.txt /tmp/u.txt","",               "mv renames file"),
    ("cat /tmp/u.txt",        "data",              "renamed file readable"),
    ("rm /tmp/u.txt",         "",                  "rm removes file"),
    ("df",                    "spiffs",            "df shows filesystem"),

    # GPIO
    ("gpio write 2 1",        "GPIO2",             "gpio write high"),
    ("gpio write 2 0",        "GPIO2",             "gpio write low"),
    ("gpio read 2",           "GPIO2",             "gpio read"),
    ("gpio mode 4 in",        "GPIO4",             "gpio mode set"),

    # ADC
    ("adc read 0",            "ADC",               "adc raw read"),
    ("adc readv 0",           "mV",                "adc voltage read"),
    ("adc readall",           "ch0",               "adc readall"),

    # Logging
    ("loglevel info",         "info",              "loglevel set"),
    ("loglevel debug",        "debug",             "loglevel reset"),
    ("dmesg 5",               "] ",                "dmesg shows logs"),
    ("logfile on",            "enabled",           "logfile enable"),
    ("logfile off",           "disabled",          "logfile disable"),

    # NVS
    ("nvs set testkey testval","testkey",          "nvs set"),
    ("nvs get testkey",       "testval",           "nvs get"),
    ("nvs del testkey",       "deleted",           "nvs del"),

    # Help
    ("help",                  "Commands",          "help shows commands"),
    ("nonexistent_cmd",       "not found",         "unknown cmd error"),
]

# ────────────────────────────────────────────────────

GREEN = "\033[32m"
RED   = "\033[31m"
RESET = "\033[0m"
BOLD  = "\033[1m"


PROMPT = b"esp32os> "


def send_cmd(ser: serial.Serial, cmd: str) -> tuple[bool, str]:
    ser.write(f"{cmd}\r".encode())
    raw = ser.read_until(PROMPT)
    complete = raw.endswith(PROMPT)
    if complete:
        raw = raw[:-len(PROMPT)]
    clean = re.sub(r"\x1b\[[0-9;]*m", "", raw.decode(errors="replace"))
    echo, _, output = clean.partition("\r\n")
    return complete and echo.strip() == cmd, output.strip()


def run_tests(port: str, baud: int, verbose: bool, skip_peripherals: bool = False) -> bool:
    print(f"\n{BOLD}ESP32OS Integration Tests{RESET}")
    print(f"Port: {port}  Baud: {baud}\n")

    try:
        ser = serial.Serial(port, baud, timeout=4)
    except serial.SerialException as e:
        print(f"{RED}Cannot open {port}: {e}{RESET}")
        return False

    time.sleep(1.0)
    ser.reset_input_buffer()
    ser.write(b"\r")
    prompt = ser.read_until(PROMPT)
    ready = False
    if prompt.endswith(PROMPT):
        # A just-flashed board may print its first prompt before processing the
        # newline above. Read past any queued prompt to a known command reply.
        marker = "__esp32os_uart_ready__"
        ser.write(f"echo {marker}\r".encode())
        deadline = time.monotonic() + 8
        while time.monotonic() < deadline:
            raw = ser.read_until(PROMPT)
            if raw.endswith(PROMPT) and marker.encode() in raw[:-len(PROMPT)].splitlines():
                ready = True
                break
    if not ready:
        ser.close()
        print(f"{RED}ESP32OS shell prompt did not respond{RESET}")
        return False

    passed = 0
    failed = 0
    errors = []

    for cmd, expected, desc in TESTS:
        if skip_peripherals and cmd.startswith(("gpio ", "adc ")):
            continue
        complete, clean = send_cmd(ser, cmd)
        ok = complete and (expected.lower() in clean.lower() if expected else not clean)

        if ok:
            print(f"  {GREEN}✓{RESET}  {desc:<45} ({cmd})")
            passed += 1
        else:
            print(f"  {RED}✗{RESET}  {desc:<45} ({cmd})")
            errors.append((cmd, expected, clean.strip()[:120]))
            failed += 1

        if verbose and not ok:
            print(f"      Expected: '{expected}'")
            print(f"      Got:      '{clean.strip()[:120]}'")

    ser.close()

    print(f"\n{'─'*60}")
    print(f"{BOLD}Results: {passed}/{passed+failed} passed{RESET}", end="")
    if failed > 0:
        print(f"  {RED}({failed} failed){RESET}")
        print(f"\nFailed tests:")
        for cmd, exp, got in errors:
            print(f"  cmd:      {cmd}")
            print(f"  expected: {exp}")
            print(f"  got:      {got}\n")
    else:
        print(f"  {GREEN}All passed!{RESET}")

    return failed == 0


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description="ESP32OS Integration Tests")
    parser.add_argument("--port",    default="/dev/ttyUSB0", help="Serial port")
    parser.add_argument("--baud",    default=115200, type=int, help="Baud rate")
    parser.add_argument("--verbose", action="store_true", help="Show full responses")
    parser.add_argument("--skip-peripherals", action="store_true",
                        help="Skip GPIO and ADC commands when board wiring is unknown")
    args = parser.parse_args()

    ok = run_tests(args.port, args.baud, args.verbose, args.skip_peripherals)
    sys.exit(0 if ok else 1)
