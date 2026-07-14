#!/usr/bin/env python3
"""Focused tests for web and firmware build budget enforcement."""

from __future__ import annotations

import json
import sys
import tempfile
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from check_build_budgets import (  # noqa: E402
    CompileUsage,
    MemoryUsage,
    check_firmware_budget,
    parse_compile_usage,
)


def test_parse_compile_usage_accepts_esphome_output_and_ansi() -> None:
    usage = parse_compile_usage(
        "\x1b[0mRAM:   [=         ]  14.9% (used 76096 bytes from 512000 bytes)\n"
        "\x1b[0mFlash: [===       ]  31.9% (used 2592878 bytes from 8126464 bytes)\n"
    )
    assert usage.ram.used_bytes == 76096
    assert usage.ram.percent == 14.9
    assert usage.flash.used_bytes == 2592878
    assert usage.flash.percent == 31.9


def test_parse_compile_usage_rejects_incomplete_logs() -> None:
    try:
        parse_compile_usage("INFO Successfully compiled program")
    except ValueError as exc:
        assert "RAM and Flash" in str(exc)
    else:
        raise AssertionError("incomplete compile log unexpectedly passed")


def test_firmware_budget_reports_each_overage() -> None:
    budgets = {
        "firmware": {
            "factory": {
                "flash_used_bytes_max": 100,
                "flash_used_percent_max": 10.0,
                "ram_used_bytes_max": 50,
                "ram_used_percent_max": 5.0,
                "binary_bytes_max": 20,
            }
        }
    }
    usage = CompileUsage(
        flash=MemoryUsage(101, 1000, 10.1),
        ram=MemoryUsage(51, 1000, 5.1),
    )
    errors: list[str] = []
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "firmware.bin"
        binary.write_bytes(b"x" * 21)
        check_firmware_budget(budgets, "factory", usage, binary, errors)
    assert len(errors) == 5
    assert all("over budget" in error for error in errors)


def main() -> int:
    tests = [value for name, value in globals().items() if name.startswith("test_") and callable(value)]
    for test in tests:
        test()
    print(f"build budget tests passed ({len(tests)} tests)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
