#!/usr/bin/env python3
"""Validate generated ESPHome setting field sections."""

from __future__ import annotations

import difflib
import sys
from pathlib import Path

import generate_assets
from product_config import load_product


ROOT = Path(__file__).resolve().parent.parent


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def main() -> int:
    product = load_product()
    settings = generate_assets.setting_lookup()
    field_configs = generate_assets.generated_firmware_setting_fields(product)
    errors: list[str] = []

    if not field_configs:
        errors.append("project.generated_firmware_setting_fields must be a non-empty object")

    for key, config in field_configs.items():
        setting = settings.get(key)
        if not setting:
            errors.append(f"Generated firmware setting field {key} does not match a product setting")
            continue
        entity = setting.get("entity") or {}
        domain = str(entity.get("domain", ""))
        if domain not in {"select", "number", "switch", "text"}:
            errors.append(f"Generated firmware setting field {key} uses unsupported domain {domain!r}")
        if domain == "text" and "max_length" not in config:
            errors.append(f"Generated firmware text field {key} must declare max_length")
        for filename in setting.get("firmware_files", []):
            path = ROOT / str(filename)
            text = path.read_text() if path.is_file() else ""
            start = f"# ESPFRAME:SETTING_FIELDS {key} START"
            end = f"# ESPFRAME:SETTING_FIELDS {key} END"
            if text.count(start) != 1 or text.count(end) != 1:
                errors.append(f"{filename} must contain exactly one generated field marker pair for {key}")

    for path in generate_assets.generated_firmware_field_files(settings, field_configs):
        current = path.read_text()
        expected = generate_assets.generated_firmware_yaml(path, settings, field_configs)
        if current != expected:
            diff = "".join(
                difflib.unified_diff(
                    current.splitlines(keepends=True),
                    expected.splitlines(keepends=True),
                    fromfile=f"{rel(path)} (current)",
                    tofile=f"{rel(path)} (generated)",
                )
            )
            errors.append(f"{rel(path)} generated firmware fields are stale:\n{diff}")

    if errors:
        for error in errors:
            print(f"firmware field check failed: {error}", file=sys.stderr)
        return 1
    print("generated firmware field checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
