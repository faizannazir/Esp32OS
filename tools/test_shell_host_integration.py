#!/usr/bin/env python3
"""
Host-side shell integration tests for command workflows that do not require ESP32 hardware.

Covers command semantics for:
  env, export, unset, printenv, run, at, every, jobs, killjob

Usage:
  python3 tools/test_shell_host_integration.py
"""

from __future__ import annotations

import shlex
import sys
import time
from dataclasses import dataclass


GREEN = "\033[32m"
RED = "\033[31m"
RESET = "\033[0m"
BOLD = "\033[1m"


@dataclass
class Job:
    name: str
    command: str
    delay_ms: int
    repeat: bool
    state: str = "running"
    fires: int = 0


class HostShell:
    def __init__(self) -> None:
        self.env: dict[str, str] = {}
        self.jobs: dict[str, Job] = {}
        self._clock_seed = int(time.time() * 1_000_000)

    def _next_id(self, prefix: str) -> str:
        self._clock_seed += 1
        return f"{prefix}_{self._clock_seed}"

    def _expand(self, text: str) -> str:
        out = []
        i = 0
        while i < len(text):
            if text[i] == "$" and i + 1 < len(text):
                j = i + 1
                while j < len(text) and (text[j].isalnum() or text[j] == "_"):
                    j += 1
                if j > i + 1:
                    key = text[i + 1 : j]
                    out.append(self.env.get(key, ""))
                    i = j
                    continue
            out.append(text[i])
            i += 1
        return "".join(out)

    def run(self, line: str) -> str:
        line = self._expand(line.strip())
        if not line:
            return ""

        argv = shlex.split(line)
        cmd = argv[0]

        if cmd == "env":
            if not self.env:
                return ""
            return "\n".join(f"{k}={v}" for k, v in sorted(self.env.items()))

        if cmd == "export":
            if len(argv) < 2 or "=" not in argv[1] or argv[1].startswith("="):
                return "Usage: export NAME=VALUE"
            key, value = argv[1].split("=", 1)
            self.env[key] = value
            return f"{key}={value}"

        if cmd == "unset":
            if len(argv) < 2:
                return "Usage: unset NAME"
            key = argv[1]
            if key not in self.env:
                return f"unset: '{key}' not found"
            del self.env[key]
            return ""

        if cmd == "printenv":
            if len(argv) < 2:
                return "Usage: printenv NAME"
            key = argv[1]
            if key not in self.env:
                return f"printenv: '{key}' not found"
            return self.env[key]

        if cmd == "run":
            if len(argv) < 2:
                return "Usage: run <command...>"
            command = " ".join(argv[1:])
            name = self._next_id("run")
            self.jobs[name] = Job(name=name, command=command, delay_ms=0, repeat=False)
            return f"Started background task: {command}"

        if cmd in {"at", "every"}:
            if len(argv) < 3:
                usage = "at <delay_ms> <command...>" if cmd == "at" else "every <period_ms> <command...>"
                return f"Usage: {usage}"
            try:
                delay_ms = int(argv[1])
            except ValueError:
                return f"{cmd}: invalid delay"
            if delay_ms <= 0:
                return f"{cmd}: invalid delay"
            command = " ".join(argv[2:])
            name = self._next_id("job")
            repeat = cmd == "every"
            self.jobs[name] = Job(name=name, command=command, delay_ms=delay_ms, repeat=repeat)
            if repeat:
                return f"Scheduled repeating job '{command}' every {delay_ms} ms"
            return f"Scheduled '{command}' in {delay_ms} ms"

        if cmd == "jobs":
            lines = [
                f"{job.name} {'repeat' if job.repeat else 'once'} {job.state} {job.fires} {job.command}"
                for job in self.jobs.values()
            ]
            return "\n".join(lines)

        if cmd == "killjob":
            if len(argv) < 2:
                return "Usage: killjob <name>"
            name = argv[1]
            if name not in self.jobs:
                return f"killjob: '{name}' not found"
            del self.jobs[name]
            return f"Cancelled job '{name}'"

        return f"{cmd}: command not found"


def assert_contains(text: str, needle: str, msg: str) -> None:
    if needle.lower() not in text.lower():
        raise AssertionError(f"{msg} | expected substring='{needle}', got='{text}'")


def assert_true(cond: bool, msg: str) -> None:
    if not cond:
        raise AssertionError(msg)


def test_env_export_unset_printenv(shell: HostShell) -> None:
    out = shell.run("export NAME=Esp32OS")
    assert_contains(out, "NAME=Esp32OS", "export should echo key/value")

    out = shell.run("printenv NAME")
    assert_true(out == "Esp32OS", "printenv should return value")

    out = shell.run("env")
    assert_contains(out, "NAME=Esp32OS", "env should list exported key")

    out = shell.run("unset NAME")
    assert_true(out == "", "unset should succeed silently")

    out = shell.run("printenv NAME")
    assert_contains(out, "not found", "printenv should fail after unset")


def test_variable_expansion(shell: HostShell) -> None:
    shell.run("export APP=esp32os")
    out = shell.run("run echo $APP")
    assert_contains(out, "echo esp32os", "run should receive expanded variable")


def test_run_background_and_jobs(shell: HostShell) -> None:
    out = shell.run("run echo background")
    assert_contains(out, "Started background task", "run should start a background task")

    out = shell.run("jobs")
    assert_contains(out, "background", "jobs should include background command")


def test_at_every_killjob(shell: HostShell) -> None:
    out = shell.run("at 500 echo hello")
    assert_contains(out, "Scheduled 'echo hello' in 500 ms", "at should schedule one-shot job")

    out = shell.run("every 1000 echo tick")
    assert_contains(out, "Scheduled repeating job 'echo tick' every 1000 ms", "every should schedule repeating job")

    jobs = shell.run("jobs")
    lines = [line for line in jobs.splitlines() if line.strip()]
    assert_true(len(lines) >= 2, "jobs should list both scheduled jobs")

    first_job = lines[0].split()[0]
    out = shell.run(f"killjob {first_job}")
    assert_contains(out, "Cancelled job", "killjob should cancel named job")

    jobs_after = shell.run("jobs")
    assert_true(first_job not in jobs_after, "cancelled job should be removed from jobs list")


def run_all() -> int:
    tests = [
        ("Env/Export/Unset/Printenv", test_env_export_unset_printenv),
        ("Variable Expansion", test_variable_expansion),
        ("Run Background and Jobs", test_run_background_and_jobs),
        ("At/Every/Killjob", test_at_every_killjob),
    ]

    print(f"\n{BOLD}ESP32OS Host Shell Integration Tests{RESET}")
    print("Scope: env/export/unset/printenv/run/at/every/jobs/killjob\n")

    passed = 0
    failed = 0

    for name, fn in tests:
        shell = HostShell()
        try:
            fn(shell)
            print(f"  {GREEN}✓{RESET} {name}")
            passed += 1
        except Exception as exc:  # pylint: disable=broad-except
            print(f"  {RED}✗{RESET} {name}: {exc}")
            failed += 1

    print("\n" + "-" * 56)
    print(f"{BOLD}Results:{RESET} {passed}/{passed + failed} passed")
    if failed:
        print(f"{RED}{failed} test group(s) failed{RESET}")
        return 1
    print(f"{GREEN}All host-side shell integration tests passed.{RESET}")
    return 0


if __name__ == "__main__":
    sys.exit(run_all())
