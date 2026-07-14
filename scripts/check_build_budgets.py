#!/usr/bin/env python3
"""Enforce embedded web and compiled firmware size budgets."""

from __future__ import annotations

import argparse
import gzip
import json
import re
import sys
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
BUDGET_PATH = ROOT / "product" / "budgets.json"
APP_PATH = ROOT / "docs" / "public" / "webserver" / "app.js"
STYLE_PATH = ROOT / "docs" / "public" / "webserver" / "style.css"

MEMORY_RE = re.compile(
    r"^(?P<kind>RAM|Flash):\s+\[[^]]*\]\s+"
    r"(?P<percent>[0-9]+(?:\.[0-9]+)?)%\s+"
    r"\(used\s+(?P<used>[0-9]+)\s+bytes\s+from\s+(?P<total>[0-9]+)\s+bytes\)",
    re.MULTILINE,
)
ANSI_RE = re.compile(r"\x1b\[[0-9;]*[A-Za-z]")


@dataclass(frozen=True)
class MemoryUsage:
    used_bytes: int
    total_bytes: int
    percent: float


@dataclass(frozen=True)
class CompileUsage:
    flash: MemoryUsage
    ram: MemoryUsage


def load_budgets(path: Path = BUDGET_PATH) -> dict[str, object]:
    data = json.loads(path.read_text(encoding="utf-8"))
    if not isinstance(data, dict) or data.get("schema_version") != 1:
        raise ValueError("product/budgets.json schema_version must be 1")
    return data


def parse_compile_usage(text: str) -> CompileUsage:
    clean = ANSI_RE.sub("", text)
    readings: dict[str, MemoryUsage] = {}
    for match in MEMORY_RE.finditer(clean):
        readings[match.group("kind").lower()] = MemoryUsage(
            used_bytes=int(match.group("used")),
            total_bytes=int(match.group("total")),
            percent=float(match.group("percent")),
        )
    if "flash" not in readings or "ram" not in readings:
        raise ValueError("compile log does not contain ESPHome RAM and Flash usage lines")
    return CompileUsage(flash=readings["flash"], ram=readings["ram"])


def check_limit(name: str, actual: int | float, maximum: int | float, errors: list[str]) -> None:
    if actual > maximum:
        errors.append(f"{name} is {actual}, over budget {maximum}")


def check_web_budgets(budgets: dict[str, object], errors: list[str]) -> None:
    web = budgets.get("web")
    if not isinstance(web, dict):
        errors.append("product/budgets.json web must be an object")
        return
    app = APP_PATH.read_bytes()
    style = STYLE_PATH.read_bytes()
    app_gzip = gzip.compress(app, compresslevel=9, mtime=0)
    style_gzip = gzip.compress(style, compresslevel=9, mtime=0)
    actuals = {
        "app_bytes_max": len(app),
        "style_bytes_max": len(style),
        "combined_bytes_max": len(app) + len(style),
        "app_gzip_bytes_max": len(app_gzip),
        "style_gzip_bytes_max": len(style_gzip),
        "combined_gzip_bytes_max": len(app_gzip) + len(style_gzip),
    }
    for key, actual in actuals.items():
        maximum = web.get(key)
        if not isinstance(maximum, int) or isinstance(maximum, bool) or maximum <= 0:
            errors.append(f"web.{key} must be a positive integer")
            continue
        check_limit(f"web.{key.removesuffix('_max')}", actual, maximum, errors)


def check_firmware_budget(
    budgets: dict[str, object], profile: str, usage: CompileUsage,
    binary_path: Path | None, errors: list[str]
) -> None:
    firmware = budgets.get("firmware")
    profile_budget = firmware.get(profile) if isinstance(firmware, dict) else None
    if not isinstance(profile_budget, dict):
        errors.append(f"firmware.{profile} budget must be an object")
        return
    actuals: dict[str, int | float] = {
        "flash_used_bytes_max": usage.flash.used_bytes,
        "flash_used_percent_max": usage.flash.percent,
        "ram_used_bytes_max": usage.ram.used_bytes,
        "ram_used_percent_max": usage.ram.percent,
    }
    for key, actual in actuals.items():
        maximum = profile_budget.get(key)
        if not isinstance(maximum, (int, float)) or isinstance(maximum, bool) or maximum <= 0:
            errors.append(f"firmware.{profile}.{key} must be a positive number")
            continue
        check_limit(f"firmware.{profile}.{key.removesuffix('_max')}", actual, maximum, errors)
    if binary_path is not None:
        check_firmware_binary_budget(budgets, profile, binary_path, errors)


def check_firmware_binary_budget(
    budgets: dict[str, object], profile: str, binary_path: Path, errors: list[str]
) -> None:
    firmware = budgets.get("firmware")
    profile_budget = firmware.get(profile) if isinstance(firmware, dict) else None
    if not isinstance(profile_budget, dict):
        errors.append(f"firmware.{profile} budget must be an object")
        return
    maximum = profile_budget.get("binary_bytes_max")
    if not isinstance(maximum, int) or isinstance(maximum, bool) or maximum <= 0:
        errors.append(f"firmware.{profile}.binary_bytes_max must be a positive integer")
        return
    if not binary_path.is_file():
        errors.append(f"firmware binary does not exist: {binary_path}")
        return
    check_limit(
        f"firmware.{profile}.binary_bytes",
        binary_path.stat().st_size,
        maximum,
        errors,
    )


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--compile-log", type=Path, help="ESPHome compile output to validate")
    parser.add_argument("--profile", choices=("factory", "ota"), help="Firmware budget profile")
    parser.add_argument("--binary", type=Path, help="Compiled firmware binary to size")
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(sys.argv[1:] if argv is None else argv)
    errors: list[str] = []
    try:
        budgets = load_budgets()
    except (OSError, json.JSONDecodeError, ValueError) as exc:
        print(f"build budget error: {exc}", file=sys.stderr)
        return 1
    check_web_budgets(budgets, errors)

    if (args.compile_log or args.binary) and not args.profile:
        errors.append("--profile is required with --compile-log or --binary")
    elif args.compile_log and args.profile:
        try:
            usage = parse_compile_usage(args.compile_log.read_text(encoding="utf-8"))
        except (OSError, ValueError) as exc:
            errors.append(str(exc))
        else:
            check_firmware_budget(budgets, args.profile, usage, args.binary, errors)
    elif args.binary and args.profile:
        check_firmware_binary_budget(budgets, args.profile, args.binary, errors)

    if errors:
        print("build budget errors:", file=sys.stderr)
        for error in errors:
            print(f"- {error}", file=sys.stderr)
        return 1
    print("build budget checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
