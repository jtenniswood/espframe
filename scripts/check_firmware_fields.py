#!/usr/bin/env python3
"""Validate generated ESPHome setting field sections."""

from __future__ import annotations

import difflib
import sys
from pathlib import Path

import generate_assets
from product_config import load_product


ROOT = Path(__file__).resolve().parent.parent
SAFE_GENERATED_DOMAINS = {"select", "number", "switch", "text"}
UNSAFE_GENERATED_CONTEXTS = {"lambda", "script", "then", "lvgl"}


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def yaml_key_for_line(line: str) -> str | None:
    stripped = line.strip()
    if not stripped or stripped.startswith("#"):
        return None
    if stripped.startswith("- "):
        stripped = stripped[2:].lstrip()
    if ":" not in stripped:
        return None
    key = stripped.split(":", 1)[0].strip().strip('"').strip("'")
    return key or None


def line_indent(line: str) -> int:
    return len(line) - len(line.lstrip(" "))


def marker_contexts(text: str) -> dict[int, tuple[str | None, list[str]]]:
    stack: list[tuple[int, str]] = []
    contexts: dict[int, tuple[str | None, list[str]]] = {}
    current_top_level: str | None = None
    for index, line in enumerate(text.splitlines(), start=1):
        indent = line_indent(line)
        while stack and indent <= stack[-1][0]:
            stack.pop()
        key = yaml_key_for_line(line)
        if key:
            if indent == 0:
                current_top_level = key
            stack.append((indent, key))
        if "ESPFRAME:SETTING_FIELDS" in line:
            contexts[index] = (current_top_level, [item[1] for item in stack])
    return contexts


def check_marker_contexts(path: Path, text: str, settings: dict[str, dict[str, object]], errors: list[str]) -> None:
    contexts = marker_contexts(text)
    for line_number, (top_level, context) in contexts.items():
        line = text.splitlines()[line_number - 1]
        parts = line.split()
        key = parts[2] if len(parts) >= 3 else ""
        setting = settings.get(key)
        entity = setting.get("entity", {}) if setting else {}
        domain = str(entity.get("domain", ""))
        unsafe = sorted({item for item in context if item in UNSAFE_GENERATED_CONTEXTS})
        if unsafe:
            errors.append(
                f"{rel(path)}:{line_number} generated field marker for {key} is inside handwritten logic: "
                + ", ".join(unsafe)
            )
        if domain in SAFE_GENERATED_DOMAINS and top_level != domain:
            errors.append(f"{rel(path)}:{line_number} generated field marker for {key} is not inside a {domain} block")


def main() -> int:
    product = load_product()
    project = product["project"]
    settings = generate_assets.setting_lookup()
    field_configs = generate_assets.generated_firmware_setting_fields(product)
    has_deferred_allowlist = "generated_firmware_deferred_setting_fields" in project
    raw_allowed_deferred = project.get("generated_firmware_deferred_setting_fields", [])
    allowed_deferred_keys = [
        str(key).strip()
        for key in (raw_allowed_deferred if isinstance(raw_allowed_deferred, list) else [])
        if str(key).strip()
    ]
    errors: list[str] = []

    if not field_configs:
        errors.append("project.generated_firmware_setting_fields must be a non-empty object")
    if not has_deferred_allowlist:
        errors.append("project.generated_firmware_deferred_setting_fields is required")
    elif not isinstance(raw_allowed_deferred, list):
        errors.append("project.generated_firmware_deferred_setting_fields must be a list")
    elif any(not isinstance(key, str) or not key.strip() for key in raw_allowed_deferred):
        errors.append("project.generated_firmware_deferred_setting_fields must only contain non-empty strings")
    if len(allowed_deferred_keys) != len(set(allowed_deferred_keys)):
        errors.append("project.generated_firmware_deferred_setting_fields must not contain duplicate keys")

    for key, config in field_configs.items():
        setting = settings.get(key)
        if not setting:
            errors.append(f"Generated firmware setting field {key} does not match a product setting")
            continue
        entity = setting.get("entity") or {}
        domain = str(entity.get("domain", ""))
        if domain not in SAFE_GENERATED_DOMAINS:
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
        check_marker_contexts(path, current, settings, errors)
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
    generated_keys = set(field_configs)
    deferred_keys = sorted(
        key for key, setting in settings.items()
        if setting.get("firmware_files") and key not in generated_keys
    )
    allowed_deferred = set(allowed_deferred_keys)
    unexpected_deferred = sorted(set(deferred_keys) - allowed_deferred)
    unknown_deferred_allowlist = sorted(key for key in allowed_deferred if key not in settings)
    known_allowed_deferred = allowed_deferred - set(unknown_deferred_allowlist)
    stale_deferred_allowlist = sorted(known_allowed_deferred - set(deferred_keys))
    if unexpected_deferred:
        errors.append("project.generated_firmware_deferred_setting_fields is missing: " + ", ".join(unexpected_deferred))
    if stale_deferred_allowlist:
        errors.append(
            "project.generated_firmware_deferred_setting_fields includes generated settings: "
            + ", ".join(stale_deferred_allowlist)
        )
    if unknown_deferred_allowlist:
        errors.append(
            "project.generated_firmware_deferred_setting_fields references unknown settings: "
            + ", ".join(unknown_deferred_allowlist)
        )
    if errors:
        for error in errors:
            print(f"firmware field check failed: {error}", file=sys.stderr)
        return 1
    if deferred_keys:
        print("generated firmware field report: allowed deferred settings: " + ", ".join(deferred_keys))
    else:
        print("generated firmware field report: no product settings are deferred from field generation")
    print(f"generated firmware field checks passed ({len(generated_keys)} generated settings)")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
