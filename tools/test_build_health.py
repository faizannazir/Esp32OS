#!/usr/bin/env python3
"""
Build health checks for ESP32OS firmware artifacts.

This script validates:
- Required build artifacts exist and are non-empty
- App binary fits in the factory partition with minimum free headroom
- IRAM/DRAM usage percentages stay under configured thresholds
- Core component libraries are present in the linker map

Usage:
  python3 tools/test_build_health.py
  python3 tools/test_build_health.py --build-dir build --target esp32
"""

from __future__ import annotations

import argparse
import csv
import os
import re
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Optional


GREEN = "\033[32m"
RED = "\033[31m"
RESET = "\033[0m"
BOLD = "\033[1m"


@dataclass
class CheckResult:
    name: str
    ok: bool
    details: str


def human_bytes(value: int) -> str:
    units = ["B", "KB", "MB", "GB"]
    size = float(value)
    idx = 0
    while size >= 1024.0 and idx < len(units) - 1:
        size /= 1024.0
        idx += 1
    return f"{size:.2f} {units[idx]}"


def parse_int_auto(value: str) -> int:
    value = value.strip()
    return int(value, 16) if value.lower().startswith("0x") else int(value)


def parse_factory_partition_size(partitions_csv: Path) -> int:
    with partitions_csv.open("r", encoding="utf-8") as f:
        reader = csv.reader(f)
        for row in reader:
            if not row:
                continue
            head = row[0].strip()
            if not head or head.startswith("#"):
                continue
            if len(row) < 5:
                continue
            name = row[0].strip()
            p_type = row[1].strip()
            subtype = row[2].strip()
            size = row[4].strip()
            if p_type == "app" and subtype == "factory":
                return parse_int_auto(size)
            if name == "factory" and p_type == "app":
                return parse_int_auto(size)
    raise ValueError(f"Could not find factory app partition in {partitions_csv}")


def find_idf_size_py() -> Optional[Path]:
    candidates = []
    idf_path = os.environ.get("IDF_PATH")
    if idf_path:
        # CI environments often use /opt/esp/idf
        candidates.append(Path(idf_path) / "tools" / "idf_size.py")

    home = Path.home()
    candidates.append(home / ".espressif" / "v6.0" / "esp-idf" / "tools" / "idf_size.py")
    candidates.append(home / ".espressif" / "v5.2" / "esp-idf" / "tools" / "idf_size.py")
    
    # Also check common CI paths
    candidates.append(Path("/esp/idf/tools/idf_size.py"))
    candidates.append(Path("/opt/esp/idf/tools/idf_size.py"))

    for candidate in candidates:
        if candidate.is_file():
            return candidate
    return None


def find_idf_python() -> Optional[Path]:
    candidates = []

    env_python = os.environ.get("IDF_PYTHON_ENV_PATH")
    if env_python:
        candidates.append(Path(env_python) / "bin" / "python")
        candidates.append(Path(env_python) / "bin" / "python3")

    home = Path.home()
    candidates.append(home / ".espressif" / "tools" / "python" / "v6.0" / "venv" / "bin" / "python3")
    candidates.append(home / ".espressif" / "python_env" / "idf6.0_py3.14_env" / "bin" / "python")
    candidates.append(home / ".espressif" / "python_env" / "idf6.0_py3.12_env" / "bin" / "python")
    candidates.append(home / ".espressif" / "python_env" / "idf6.0_py3.11_env" / "bin" / "python")
    candidates.append(home / ".espressif" / "python_env" / "idf6.0_py3.10_env" / "bin" / "python")

    # Also check common CI paths
    candidates.append(Path("/opt/python/*/bin/python3"))
    candidates.append(Path("/opt/esp/python/*/bin/python3"))

    # Fallback to system python if environment-specific python not found
    for python_name in ["python3", "python"]:
        pypath = shutil.which(python_name)
        if pypath:
            candidates.append(Path(pypath))

    for candidate in candidates:
        if candidate.is_file():
            return candidate
        # Handle glob patterns
        if "*" in str(candidate):
            from glob import glob
            for match in glob(str(candidate)):
                if Path(match).is_file():
                    return Path(match)
    return None


def run_idf_size(idf_size_py: Path, map_file: Path) -> str:
    idf_python = find_idf_python() or Path(sys.executable)
    cmd = [str(idf_python), str(idf_size_py), str(map_file)]
    proc = subprocess.run(cmd, capture_output=True, text=True, check=False)
    if proc.returncode != 0:
        error_msg = proc.stderr.strip() if proc.stderr else proc.stdout.strip()
        raise RuntimeError(f"idf_size.py failed (exit {proc.returncode}): {error_msg or 'unknown error'}")
    return proc.stdout


def parse_memory_percent(idf_size_out: str, label: str) -> float:
    # Handles unicode/ascii table lines by column split.
    for line in idf_size_out.splitlines():
        if label not in line:
            continue

        parts = [p.strip() for p in re.split(r"[│|]", line)]
        # Expected columns after split:
        # ["", "IRAM", "94551", "72.14", "36521", "131072", ""]
        if len(parts) >= 4 and parts[1] == label:
            pct = parts[3]
            if pct and pct != '':
                try:
                    return float(pct)
                except (ValueError, IndexError):
                    pass

        # Fallback for plain whitespace table rows.
        tokens = line.split()
        if len(tokens) >= 3 and tokens[0] == label:
            try:
                return float(tokens[2])
            except (ValueError, IndexError):
                pass
        
        # Additional fallback: look for percentage patterns in the line
        # e.g., "IRAM 94551 72.14 36521 131072"
        if tokens and tokens[0] == label:
            for token in tokens[1:]:
                try:
                    val = float(token)
                    # Check if it looks like a percentage (0-100)
                    if 0 <= val <= 100:
                        return val
                except ValueError:
                    continue

    # Provide detailed error message for debugging
    lines_with_label = [line for line in idf_size_out.splitlines() if label in line]
    error_detail = f"Expected '{label}' row not found" if not lines_with_label else \
                   f"Found '{label}' in table but could not parse percentage: {repr(lines_with_label[0] if lines_with_label else '')}"
    raise ValueError(f"Could not parse {label} usage percent from idf_size output. {error_detail}")


def check_required_files(build_dir: Path) -> list[CheckResult]:
    required = {
        "app_bin": build_dir / "esp32os.bin",
        "app_elf": build_dir / "esp32os.elf",
        "app_map": build_dir / "esp32os.map",
        "bootloader_bin": build_dir / "bootloader" / "bootloader.bin",
        "partition_table_bin": build_dir / "partition_table" / "partition-table.bin",
    }
    results = []
    for name, path in required.items():
        ok = path.is_file() and path.stat().st_size > 0
        details = f"{path} exists ({human_bytes(path.stat().st_size)})" if ok else f"missing or empty: {path}"
        results.append(CheckResult(name=f"artifact:{name}", ok=ok, details=details))
    return results


def check_binary_sizes(build_dir: Path, min_bin_bytes: int, min_elf_bytes: int) -> list[CheckResult]:
    app_bin = build_dir / "esp32os.bin"
    app_elf = build_dir / "esp32os.elf"

    results = []
    bin_ok = app_bin.stat().st_size >= min_bin_bytes
    results.append(
        CheckResult(
            name="binary:min_app_bin",
            ok=bin_ok,
            details=f"{human_bytes(app_bin.stat().st_size)} >= {human_bytes(min_bin_bytes)}",
        )
    )

    elf_ok = app_elf.stat().st_size >= min_elf_bytes
    results.append(
        CheckResult(
            name="binary:min_app_elf",
            ok=elf_ok,
            details=f"{human_bytes(app_elf.stat().st_size)} >= {human_bytes(min_elf_bytes)}",
        )
    )
    return results


def check_partition_headroom(repo_root: Path, build_dir: Path, min_free_bytes: int) -> CheckResult:
    partitions_csv = repo_root / "partitions.csv"
    part_size = parse_factory_partition_size(partitions_csv)
    app_bin = build_dir / "esp32os.bin"
    app_size = app_bin.stat().st_size
    free = part_size - app_size
    ok = app_size <= part_size and free >= min_free_bytes
    return CheckResult(
        name="partition:factory_headroom",
        ok=ok,
        details=(
            f"app={human_bytes(app_size)}, partition={human_bytes(part_size)}, "
            f"free={human_bytes(free)} (min {human_bytes(min_free_bytes)})"
        ),
    )


def check_map_has_components(build_dir: Path) -> list[CheckResult]:
    map_file = build_dir / "esp32os.map"
    text = map_file.read_text(encoding="utf-8", errors="ignore")
    required_tokens = [
        "libos_shell.a",
        "libos_env.a",
        "libos_scheduler.a",
        "libos_timer.a",
    ]

    results = []
    for token in required_tokens:
        ok = token in text
        results.append(
            CheckResult(
                name=f"map:contains:{token}",
                ok=ok,
                details=("present" if ok else "missing"),
            )
        )
    return results


def check_memory_usage(build_dir: Path, iram_max_pct: float, dram_max_pct: float) -> list[CheckResult]:
    map_file = build_dir / "esp32os.map"
    idf_size_py = find_idf_size_py()
    if idf_size_py is None:
        return [
            CheckResult(
                name="memory:idf_size_tool",
                ok=False,
                details="idf_size.py not found (set IDF_PATH or install ESP-IDF)",
            )
        ]

    try:
        output = run_idf_size(idf_size_py, map_file)
    except RuntimeError as e:
        return [
            CheckResult(
                name="memory:idf_size_output",
                ok=False,
                details=str(e),
            )
        ]

    try:
        iram_pct = parse_memory_percent(output, "IRAM")
    except ValueError as e:
        import sys
        print(f"DEBUG: idf_size output length: {len(output)}", file=sys.stderr)
        print(f"DEBUG: idf_size output (first 500 chars):\n{output[:500]}", file=sys.stderr)
        return [
            CheckResult(
                name="memory:iram_budget",
                ok=False,
                details=str(e),
            )
        ]

    try:
        dram_pct = parse_memory_percent(output, "DRAM")
    except ValueError as e:
        import sys
        print(f"DEBUG: idf_size output length: {len(output)}", file=sys.stderr)
        print(f"DEBUG: idf_size output (first 500 chars):\n{output[:500]}", file=sys.stderr)
        print(f"DEBUG: Full DRAM parsing error: {e}", file=sys.stderr)
        return [
            CheckResult(
                name="memory:dram_budget",
                ok=False,
                details=str(e),
            )
        ]

    return [
        CheckResult(
            name="memory:iram_budget",
            ok=iram_pct <= iram_max_pct,
            details=f"IRAM {iram_pct:.2f}% <= {iram_max_pct:.2f}%",
        ),
        CheckResult(
            name="memory:dram_budget",
            ok=dram_pct <= dram_max_pct,
            details=f"DRAM {dram_pct:.2f}% <= {dram_max_pct:.2f}%",
        ),
    ]


def run_checks(args: argparse.Namespace) -> int:
    repo_root = Path(args.repo_root).resolve()
    build_dir = Path(args.build_dir).resolve()

    print(f"\n{BOLD}ESP32OS Build Health Checks{RESET}")
    print(f"Target: {args.target}")
    print(f"Build dir: {build_dir}\n")

    checks: list[CheckResult] = []
    checks.extend(check_required_files(build_dir))

    # Continue only if core artifacts are present.
    if all(c.ok for c in checks):
        checks.extend(check_binary_sizes(build_dir, args.min_app_bin, args.min_app_elf))
        checks.append(check_partition_headroom(repo_root, build_dir, args.min_partition_free))
        checks.extend(check_map_has_components(build_dir))
        checks.extend(check_memory_usage(build_dir, args.max_iram_pct, args.max_dram_pct))

    passed = 0
    failed = 0
    for check in checks:
        if check.ok:
            print(f"  {GREEN}✓{RESET} {check.name:<34} {check.details}")
            passed += 1
        else:
            print(f"  {RED}✗{RESET} {check.name:<34} {check.details}")
            failed += 1

    print("\n" + "-" * 72)
    print(f"{BOLD}Results:{RESET} {passed}/{passed + failed} checks passed")
    if failed:
        print(f"{RED}{failed} check(s) failed{RESET}")
        return 1

    print(f"{GREEN}Build health checks passed.{RESET}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Validate ESP32OS build artifacts and memory budgets")
    parser.add_argument("--repo-root", default=".", help="Repository root path")
    parser.add_argument("--build-dir", default="build", help="Build output directory")
    parser.add_argument("--target", default="esp32", help="Target label for reporting")

    parser.add_argument("--min-app-bin", type=int, default=128 * 1024, help="Minimum expected app .bin size")
    parser.add_argument("--min-app-elf", type=int, default=800 * 1024, help="Minimum expected app .elf size")
    parser.add_argument("--min-partition-free", type=int, default=128 * 1024, help="Required free bytes in factory app partition")

    parser.add_argument("--max-iram-pct", type=float, default=80.0, help="Maximum allowed IRAM usage percentage")
    parser.add_argument("--max-dram-pct", type=float, default=70.0, help="Maximum allowed DRAM usage percentage")
    return parser


def main() -> int:
    parser = build_parser()
    args = parser.parse_args()
    try:
        return run_checks(args)
    except Exception as exc:  # pylint: disable=broad-except
        print(f"{RED}Build health checks failed with error:{RESET} {exc}")
        return 1


if __name__ == "__main__":
    sys.exit(main())
