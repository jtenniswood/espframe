from __future__ import annotations

import json
import re
from pathlib import Path

from asset_generation.paths import ROOT
from product_config import load_product


def generated_firmware_setting_fields(product: dict[str, object] | None = None) -> dict[str, dict[str, object]]:
    data = product if product is not None else load_product()
    raw = data["project"].get("generated_firmware_setting_fields", {})
    if not isinstance(raw, dict):
        return {}
    return {
        str(key): dict(value) if isinstance(value, dict) else {}
        for key, value in raw.items()
        if str(key).strip()
    }


def render_yaml_scalar(value: object) -> str:
    if isinstance(value, bool):
        return "true" if value else "false"
    if isinstance(value, (int, float)):
        return str(value)
    return json.dumps(str(value))


def render_firmware_setting_fields(setting: dict[str, object], field_config: dict[str, object]) -> str:
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", ""))
    lines: list[str] = []
    if domain == "select":
        if "options" in field_config:
            options = [str(option) for option in field_config.get("options", [])]
        else:
            options = [str(option) for option in setting.get("options", [])]
            options += [str(option) for option in setting.get("developer_options", []) if str(option) not in options]
        lines.append("    options:")
        lines.extend(f"      - {render_yaml_scalar(option)}" for option in options)
        initial = setting.get("firmware_initial_option", setting.get("default", ""))
        lines.append(f"    initial_option: {render_yaml_scalar(initial)}")
        lines.append("    optimistic: true")
        lines.append("    restore_value: true")
    elif domain == "number":
        for product_field, yaml_field in (("min", "min_value"), ("max", "max_value"), ("step", "step")):
            lines.append(f"    {yaml_field}: {render_yaml_scalar(setting[product_field])}")
        lines.append(f"    initial_value: {render_yaml_scalar(setting.get('default', 0))}")
        lines.append("    optimistic: true")
        lines.append("    restore_value: true")
    elif domain == "text":
        lines.append(f"    optimistic: {render_yaml_scalar(field_config.get('optimistic', False))}")
        lines.append(f"    restore_value: {render_yaml_scalar(field_config.get('restore_value', True))}")
        if "initial_value" in field_config:
            lines.append(f"    initial_value: {render_yaml_scalar(field_config['initial_value'])}")
        lines.append(f"    min_length: {render_yaml_scalar(field_config.get('min_length', 0))}")
        lines.append(f"    max_length: {render_yaml_scalar(field_config['max_length'])}")
        lines.append(f"    mode: {field_config.get('mode', 'text')}")
    elif domain == "switch":
        default = bool(setting.get("default", False))
        lines.append(f"    restore_mode: {'RESTORE_DEFAULT_ON' if default else 'RESTORE_DEFAULT_OFF'}")
    else:
        raise RuntimeError(f"Unsupported generated firmware setting domain {domain!r} for {setting.get('key')}")
    return "\n".join(lines)


def replace_firmware_setting_fields(text: str, key: str, content: str, path: Path) -> str:
    start_marker = f"# ESPFRAME:SETTING_FIELDS {key} START"
    end_marker = f"# ESPFRAME:SETTING_FIELDS {key} END"
    pattern = re.compile(
        f"(?P<indent>[ \\t]*){re.escape(start_marker)}.*?^[ \\t]*{re.escape(end_marker)}",
        re.MULTILINE | re.DOTALL,
    )

    def replacement(match: re.Match[str]) -> str:
        indent = match.group("indent")
        return f"{indent}{start_marker}\n{content}\n{indent}{end_marker}"

    result, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"Unable to locate firmware setting field block {key!r} in {path.relative_to(ROOT)}")
    return result


def generated_firmware_yaml(path: Path, product_settings: dict[str, dict[str, object]], field_configs: dict[str, dict[str, object]]) -> str:
    text = path.read_text()
    for key, config in field_configs.items():
        setting = product_settings[key]
        firmware_files = [ROOT / str(filename) for filename in setting.get("firmware_files", [])]
        if path not in firmware_files:
            continue
        text = replace_firmware_setting_fields(text, key, render_firmware_setting_fields(setting, config), path)
    return text


def generated_firmware_field_files(product_settings: dict[str, dict[str, object]], field_configs: dict[str, dict[str, object]]) -> list[Path]:
    result: list[Path] = []
    for key in field_configs:
        for filename in product_settings[key].get("firmware_files", []):
            path = ROOT / str(filename)
            if path not in result:
                result.append(path)
    return result
