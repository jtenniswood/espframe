#!/usr/bin/env python3
"""Validate the shared product metadata against the checked-in project.

This is the first release gate for the reset architecture. It catches drift
between product metadata, firmware YAML, the custom web UI, docs, and CI before
we start generating larger parts of the project from the product schema.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from product_config import (
    DOCS_SETTINGS_TABLE_COLUMNS,
    default_public_manifest_urls,
    device_public_manifest_urls,
    docs_settings_tables,
    load_product,
    public_base_url,
    public_url,
    release_matrix_devices,
    web_entity_aliases,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_initial_fetch_first_keys,
    web_local_state_keys,
    web_manual_entities,
    web_manual_entities_metadata,
    web_manual_state_keys,
    web_settings_metadata,
    web_static_entities,
    web_static_entities_metadata,
)


ROOT = Path(__file__).resolve().parent.parent
WEB_TEMPLATE = ROOT / "docs" / "webserver" / "src" / "app.template.js"
WEB_APP = ROOT / "docs" / "public" / "webserver" / "app.js"
TIME_YAML = ROOT / "common" / "addon" / "time.yaml"
SETTING_DOMAINS = {"number", "select", "switch", "text"}
DOCS_TABLE_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")
WEB_STATE_REF_RE = re.compile(r"\bS\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_ENDPOINT_REF_RE = re.compile(r"\bendpoints\.([A-Za-z_$][A-Za-z0-9_$]*)")
WEB_PRODUCT_HELPER_REF_RE = re.compile(
    r"\b(?:productSettingOptions|productNumberMin|productNumberMax|productNumberStep)\(\s*\"([^\"]+)\""
)
WEB_PRODUCT_SETTINGS_REF_RE = re.compile(r"\bPRODUCT_SETTINGS\.([A-Za-z_$][A-Za-z0-9_$]*)")


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"Missing file: {rel(path)}")
        return ""
    return path.read_text()


def require_contains(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{label} is missing {needle!r}")


def require_firmware_text_entity_shape(text: str, name: str, filename: str, errors: list[str]) -> None:
    block = firmware_entity_block(text, name, filename, errors)
    if not block:
        return
    for needle in (
        "optimistic: false",
        "restore_value: true",
        "min_length: 0",
        "mode: text",
    ):
        require_contains(block, needle, f"{filename} text entity {name}", errors)


def require_workflow_path_filter(text: str, path: str, label: str, errors: list[str]) -> None:
    needles = (f'- "{path}"', f"- '{path}'", f"- {path}")
    if not any(needle in text for needle in needles):
        errors.append(f"{label} is missing workflow path filter {path!r}")


def require_workflow_needs(text: str, dependencies: list[str], label: str, errors: list[str]) -> None:
    if len(dependencies) == 1:
        needle = f"    needs: {dependencies[0]}"
    else:
        needle = f"    needs: [{', '.join(dependencies)}]"
    require_contains(text, needle, label, errors)


def extract_js_json_var(text: str, var_name: str, errors: list[str]) -> object | None:
    match = re.search(rf"\bvar {re.escape(var_name)} = (.*?);", text)
    if not match:
        errors.append(f"Generated web app is missing {var_name}")
        return None
    try:
        return json.loads(match.group(1))
    except json.JSONDecodeError as exc:
        errors.append(f"Generated web app {var_name} is not valid JSON: {exc}")
        return None


def valid_entity_string(value: object) -> bool:
    if not isinstance(value, str) or "/" not in value:
        return False
    domain, name = value.split("/", 1)
    return bool(domain.strip() and name.strip())


def check_web_entity_default_type(metadata: dict, domain: str, label: str, errors: list[str]) -> None:
    if "default" not in metadata:
        return
    value = metadata.get("default")
    if domain == "switch":
        if not isinstance(value, bool):
            errors.append(f"{label} switch default must be true or false")
    elif domain == "number":
        if not isinstance(value, (int, float)) or isinstance(value, bool):
            errors.append(f"{label} number default must be numeric")
    elif domain in {"select", "text", "text_sensor"}:
        if not isinstance(value, str):
            errors.append(f"{label} {domain} default must be a string")


def firmware_entity_block(text: str, name: str, filename: str, errors: list[str]) -> str:
    needle = f'name: "{name}"'
    lines = text.splitlines()
    name_index = next((idx for idx, line in enumerate(lines) if needle in line), None)
    if name_index is None:
        errors.append(f"{filename} entity is missing {needle!r}")
        return ""

    start = name_index
    while start > 0 and not lines[start].startswith("  - platform:"):
        start -= 1
    if not lines[start].startswith("  - platform:"):
        errors.append(f"{filename} entity block for {name} is missing a platform header")
        return ""

    end = len(lines)
    for idx in range(start + 1, len(lines)):
        if lines[idx].startswith("  - platform:") or (lines[idx] and not lines[idx].startswith((" ", "#"))):
            end = idx
            break

    return "\n".join(lines[start:end])


def check_path_list(setting: dict, key: str, field: str, errors: list[str]) -> list[str]:
    value = setting.get(field, [])
    if not isinstance(value, list) or not value:
        errors.append(f"Setting {key} must have a non-empty {field} list")
        return []
    result: list[str] = []
    seen: set[str] = set()
    for item in value:
        path = str(item).strip()
        if not path:
            errors.append(f"Setting {key} has a blank entry in {field}")
            continue
        if Path(path).is_absolute() or ".." in Path(path).parts:
            errors.append(f"Setting {key} has unsafe {field} path: {path}")
            continue
        if path in seen:
            errors.append(f"Setting {key} has duplicate {field} path: {path}")
            continue
        seen.add(path)
        result.append(path)
    return result


def check_relative_path(value: object, label: str, errors: list[str]) -> str:
    path = str(value or "").strip()
    if not path:
        errors.append(f"{label} is required")
        return ""
    if Path(path).is_absolute() or ".." in Path(path).parts:
        errors.append(f"{label} has unsafe path: {path}")
        return ""
    return path


def check_setting_schema(setting: dict, errors: list[str]) -> None:
    key = str(setting.get("key", "")).strip()
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", "")).strip()
    raw_default = setting.get("default", "")
    options = setting.get("options", [])
    developer_options = setting.get("developer_options", [])

    if domain not in SETTING_DOMAINS:
        errors.append(f"Setting {key or '<missing>'} has unsupported domain: {domain or '<missing>'}")
        return

    if domain == "select":
        if not isinstance(raw_default, str):
            errors.append(f"Select setting {key} default must be a string")
        if not isinstance(options, list) or not options:
            errors.append(f"Select setting {key} must define non-empty options")
        elif any(not isinstance(option, str) or not option for option in options):
            errors.append(f"Select setting {key} options must be non-empty strings")
        elif len(options) != len(set(options)):
            errors.append(f"Select setting {key} options must not contain duplicates")
        elif raw_default and raw_default not in options and not str(setting.get("firmware_initial_option", "")).startswith("${"):
            errors.append(f"Select setting {key} default is not in options")

        if developer_options:
            if not isinstance(developer_options, list):
                errors.append(f"Select setting {key} developer_options must be a list")
            elif any(not isinstance(option, str) or not option for option in developer_options):
                errors.append(f"Select setting {key} developer_options must be non-empty strings")
            elif len(developer_options) != len(set(developer_options)):
                errors.append(f"Select setting {key} developer_options must not contain duplicates")
            elif set(developer_options).intersection(options):
                errors.append(f"Select setting {key} developer_options must not duplicate normal options")
    elif domain == "number":
        for field in ("default", "min", "max", "step"):
            if not isinstance(setting.get(field), (int, float)) or isinstance(setting.get(field), bool):
                errors.append(f"Number setting {key} needs numeric {field}")
                return
        minimum = setting["min"]
        maximum = setting["max"]
        default = setting["default"]
        step = setting["step"]
        if minimum > maximum:
            errors.append(f"Number setting {key} min must not exceed max")
        if not minimum <= default <= maximum:
            errors.append(f"Number setting {key} default must be within min/max")
        if step <= 0:
            errors.append(f"Number setting {key} step must be greater than zero")
        elif abs(((default - minimum) / step) - round((default - minimum) / step)) > 1e-9:
            errors.append(f"Number setting {key} default must align with min/step")
        if options or developer_options:
            errors.append(f"Number setting {key} must not define options")
    elif domain == "switch":
        if not isinstance(raw_default, bool):
            errors.append(f"Switch setting {key} default must be true or false")
        else:
            expected_docs_default = "On" if raw_default else "Off"
            if setting.get("docs_default") and setting.get("docs_default") != expected_docs_default:
                errors.append(f"Switch setting {key} docs_default must be {expected_docs_default}")
        if options or developer_options:
            errors.append(f"Switch setting {key} must not define options")
    elif domain == "text":
        if not isinstance(raw_default, str):
            errors.append(f"Text setting {key} default must be a string")
        if not str(setting.get("docs_type", "")).strip() and not str(setting.get("docs_format", "")).strip():
            errors.append(f"Text setting {key} must define docs_type or docs_format")
        if options or developer_options:
            errors.append(f"Text setting {key} must not define options")


def check_devices(product: dict, errors: list[str]) -> None:
    seen: set[str] = set()
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        if not slug:
            errors.append("A product device is missing slug")
            continue
        if slug in seen:
            errors.append(f"Duplicate product device slug: {slug}")
        seen.add(slug)

        for field in (
            "name",
            "esphome_name",
            "friendly_name",
            "chip",
            "model",
            "esp32_variant",
            "flash_size",
            "framework_type",
            "platformio_flash_mode",
            "esp32_hosted_variant",
            "psram_mode",
            "psram_speed",
            "display_panel",
            "lvgl_buffer_size",
            "lvgl_byte_order",
            "lvgl_rotation_substitution",
            "touch_platform",
            "build_yaml",
            "package_yaml",
            "local_yaml",
            "device_yaml",
            "panel_url",
            "stand_url",
            "public_manifest",
            "public_beta_manifest",
        ):
            if not str(device.get(field, "")).strip():
                errors.append(f"Device {slug} is missing {field}")

        for field in ("build_yaml", "package_yaml", "local_yaml", "device_yaml"):
            path = check_relative_path(device.get(field), f"Device {slug} {field}", errors)
            if path:
                read(ROOT / path, errors)

        for field in ("display_native_width", "display_native_height", "display_ui_width", "display_ui_height"):
            value = device.get(field)
            if not isinstance(value, int) or isinstance(value, bool) or value < 1:
                errors.append(f"Device {slug} {field} must be a positive integer")
        for field in ("engineering_sample", "idf_experimental_features"):
            if not isinstance(device.get(field), bool):
                errors.append(f"Device {slug} {field} must be true or false")
        if not isinstance(device.get("lvgl_resume_on_input"), bool):
            errors.append(f"Device {slug} lvgl_resume_on_input must be true or false")
        sdkconfig_options = device.get("sdkconfig_options", {})
        if not isinstance(sdkconfig_options, dict) or not sdkconfig_options:
            errors.append(f"Device {slug} sdkconfig_options must be a non-empty object")
        else:
            for option, value in sdkconfig_options.items():
                if not isinstance(option, str) or not option.strip():
                    errors.append(f"Device {slug} sdkconfig_options keys must be non-empty strings")
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"Device {slug} sdkconfig_options.{option} must be a non-empty string")
        build_flags = device.get("platformio_build_flags", [])
        if not isinstance(build_flags, list) or not build_flags:
            errors.append(f"Device {slug} platformio_build_flags must be a non-empty list")
        elif any(not isinstance(flag, str) or not flag.strip() for flag in build_flags):
            errors.append(f"Device {slug} platformio_build_flags must only contain non-empty strings")
        hardware_pins = device.get("hardware_pins", {})
        if not isinstance(hardware_pins, dict) or not hardware_pins:
            errors.append(f"Device {slug} hardware_pins must be a non-empty object")
        else:
            for pin_name, pin in hardware_pins.items():
                if not isinstance(pin_name, str) or not pin_name.strip():
                    errors.append(f"Device {slug} hardware_pins keys must be non-empty strings")
                if not isinstance(pin, str) or not pin.strip():
                    errors.append(f"Device {slug} hardware_pins.{pin_name} must be a non-empty string")
        power_rail = device.get("mipi_dsi_power_rail", {})
        if not isinstance(power_rail, dict):
            errors.append(f"Device {slug} mipi_dsi_power_rail must be an object")
        else:
            for field in ("id", "voltage"):
                if not str(power_rail.get(field, "")).strip():
                    errors.append(f"Device {slug} mipi_dsi_power_rail.{field} is required")
            if not isinstance(power_rail.get("channel"), int) or isinstance(power_rail.get("channel"), bool):
                errors.append(f"Device {slug} mipi_dsi_power_rail.channel must be an integer")
        if not str(device.get("i2c_frequency", "")).strip():
            errors.append(f"Device {slug} i2c_frequency is required")
        package_includes = device.get("package_includes", [])
        if not isinstance(package_includes, list) or not package_includes:
            errors.append(f"Device {slug} package_includes must be a non-empty list")
        else:
            seen_includes: set[str] = set()
            for include in package_includes:
                if not isinstance(include, dict):
                    errors.append(f"Device {slug} package_includes entries must be objects")
                    continue
                alias = str(include.get("alias", "")).strip()
                path = str(include.get("path", "")).strip()
                if not alias:
                    errors.append(f"Device {slug} package_includes entry is missing alias")
                if not path:
                    errors.append(f"Device {slug} package_includes entry is missing path")
                if alias in seen_includes:
                    errors.append(f"Device {slug} package_includes has duplicate alias {alias}")
                seen_includes.add(alias)
        package_substitutions = device.get("package_substitutions", {})
        if not isinstance(package_substitutions, dict) or not package_substitutions:
            errors.append(f"Device {slug} package_substitutions must be a non-empty object")
        else:
            for name, value in package_substitutions.items():
                if not isinstance(name, str) or not name.strip():
                    errors.append(f"Device {slug} package_substitutions keys must be non-empty strings")
                if not isinstance(value, str) or not value.strip():
                    errors.append(f"Device {slug} package_substitutions.{name} must be a non-empty string")
        font_assets = device.get("font_assets", [])
        if not isinstance(font_assets, list) or not font_assets:
            errors.append(f"Device {slug} font_assets must be a non-empty list")
        else:
            for font in font_assets:
                if not isinstance(font, dict):
                    errors.append(f"Device {slug} font_assets entries must be objects")
                    continue
                for field in ("id", "file"):
                    if not str(font.get(field, "")).strip():
                        errors.append(f"Device {slug} font_assets entry is missing {field}")
                for field in ("size", "bpp"):
                    if not isinstance(font.get(field), int) or isinstance(font.get(field), bool) or font.get(field) < 1:
                        errors.append(f"Device {slug} font_assets.{font.get('id', '<unknown>')}.{field} must be a positive integer")
        location_font_extra_files = device.get("location_font_extra_files", [])
        if not isinstance(location_font_extra_files, list) or not location_font_extra_files:
            errors.append(f"Device {slug} location_font_extra_files must be a non-empty list")
        elif any(not isinstance(font_file, str) or not font_file.strip() for font_file in location_font_extra_files):
            errors.append(f"Device {slug} location_font_extra_files must only contain non-empty strings")
        icon_font = device.get("icon_font", {})
        if not isinstance(icon_font, dict):
            errors.append(f"Device {slug} icon_font must be an object")
        else:
            for field in ("id", "file"):
                if not str(icon_font.get(field, "")).strip():
                    errors.append(f"Device {slug} icon_font.{field} is required")
            for field in ("size", "bpp"):
                if not isinstance(icon_font.get(field), int) or isinstance(icon_font.get(field), bool) or icon_font.get(field) < 1:
                    errors.append(f"Device {slug} icon_font.{field} must be a positive integer")
        icon_substitutions = device.get("icon_substitutions", {})
        if not isinstance(icon_substitutions, dict) or not icon_substitutions:
            errors.append(f"Device {slug} icon_substitutions must be a non-empty object")
        else:
            for name, glyph in icon_substitutions.items():
                if not isinstance(name, str) or not name.strip():
                    errors.append(f"Device {slug} icon_substitutions keys must be non-empty strings")
                if not isinstance(glyph, str) or not glyph.strip():
                    errors.append(f"Device {slug} icon_substitutions.{name} must be a non-empty string")

        for field in ("panel_url", "stand_url"):
            url = str(device.get(field, "")).strip()
            if url and not url.startswith("https://"):
                errors.append(f"Device {slug} {field} must be an https URL")

        device_yaml_path = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        package_yaml_path = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not device_yaml_path or not package_yaml_path:
            continue
        device_yaml = read(ROOT / device_yaml_path, errors)
        package_yaml = read(ROOT / package_yaml_path, errors)
        package_dir = (ROOT / package_yaml_path).parent
        package_include_paths = {
            str(include.get("alias", "")).strip(): str(include.get("path", "")).strip()
            for include in package_includes
            if isinstance(include, dict)
        } if isinstance(package_includes, list) else {}

        for field, needle in (
            ("esp32_variant", f'variant: {device.get("esp32_variant", "")}'),
            ("flash_size", f'flash_size: {device.get("flash_size", "")}'),
            ("framework_type", f'type: {device.get("framework_type", "")}'),
            ("platformio_flash_mode", f'board_build.flash_mode: {device.get("platformio_flash_mode", "")}'),
            ("esp32_hosted_variant", f'variant: {device.get("esp32_hosted_variant", "")}'),
            ("psram_mode", f'mode: {device.get("psram_mode", "")}'),
            ("psram_speed", f'speed: {device.get("psram_speed", "")}'),
            ("display_panel", f'model: {device.get("display_panel", "")}'),
            ("touch_platform", f'platform: {device.get("touch_platform", "")}'),
        ):
            if str(device.get(field, "")).strip():
                require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)
        for field, needle in (
            ("engineering_sample", "engineering_sample: true"),
            ("idf_experimental_features", "enable_idf_experimental_features: true"),
        ):
            if device.get(field) is True:
                require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)
        if isinstance(sdkconfig_options, dict):
            for option, value in sdkconfig_options.items():
                if isinstance(option, str) and isinstance(value, str) and option.strip() and value.strip():
                    require_contains(device_yaml, f'{option}: "{value}"', rel(ROOT / device_yaml_path), errors)
        if isinstance(build_flags, list):
            for flag in build_flags:
                if isinstance(flag, str) and flag.strip():
                    require_contains(device_yaml, f'- "{flag}"', rel(ROOT / device_yaml_path), errors)

        native_width = device.get("display_native_width")
        native_height = device.get("display_native_height")
        ui_width = device.get("display_ui_width")
        ui_height = device.get("display_ui_height")
        if isinstance(native_width, int):
            require_contains(device_yaml, f"width: {native_width}", rel(ROOT / device_yaml_path), errors)
        if isinstance(native_height, int):
            require_contains(device_yaml, f"height: {native_height}", rel(ROOT / device_yaml_path), errors)
        if isinstance(ui_width, int):
            require_contains(package_yaml, f'display_width: "{ui_width}"', rel(ROOT / package_yaml_path), errors)
        if isinstance(ui_height, int):
            require_contains(package_yaml, f'display_height: "{ui_height}"', rel(ROOT / package_yaml_path), errors)
        if isinstance(hardware_pins, dict):
            pin_markers = {
                "esp32_hosted_reset": "reset_pin",
                "esp32_hosted_cmd": "cmd_pin",
                "esp32_hosted_clk": "clk_pin",
                "esp32_hosted_d0": "d0_pin",
                "esp32_hosted_d1": "d1_pin",
                "esp32_hosted_d2": "d2_pin",
                "esp32_hosted_d3": "d3_pin",
                "backlight_pwm": "pin",
                "display_reset": "reset_pin",
                "touch_reset": "reset_pin",
                "touch_interrupt": "interrupt_pin",
                "i2c_sda": "sda",
                "i2c_scl": "scl",
            }
            for pin_name, marker in pin_markers.items():
                pin = str(hardware_pins.get(pin_name, "")).strip()
                if pin:
                    require_contains(device_yaml, f"{marker}: {pin}", rel(ROOT / device_yaml_path), errors)
        if isinstance(power_rail, dict):
            rail_id = str(power_rail.get("id", "")).strip()
            rail_voltage = str(power_rail.get("voltage", "")).strip()
            rail_channel = power_rail.get("channel")
            if rail_id:
                require_contains(device_yaml, f"id: {rail_id}", rel(ROOT / device_yaml_path), errors)
            if isinstance(rail_channel, int):
                require_contains(device_yaml, f"channel: {rail_channel}", rel(ROOT / device_yaml_path), errors)
            if rail_voltage:
                require_contains(device_yaml, f"voltage: {rail_voltage}", rel(ROOT / device_yaml_path), errors)
        i2c_frequency = str(device.get("i2c_frequency", "")).strip()
        if i2c_frequency:
            require_contains(device_yaml, f"frequency: {i2c_frequency}", rel(ROOT / device_yaml_path), errors)
        if isinstance(package_includes, list):
            for include in package_includes:
                if not isinstance(include, dict):
                    continue
                alias = str(include.get("alias", "")).strip()
                path = str(include.get("path", "")).strip()
                if not alias or not path:
                    continue
                require_contains(package_yaml, f"{alias}:", rel(ROOT / package_yaml_path), errors)
                require_contains(package_yaml, f"!include {path}", rel(ROOT / package_yaml_path), errors)
                include_path = (package_dir / path).resolve()
                if not include_path.is_file():
                    errors.append(f"Missing package include for device {slug}: {rel(include_path)}")
        if isinstance(package_substitutions, dict):
            for name, value in package_substitutions.items():
                if isinstance(name, str) and isinstance(value, str) and name.strip() and value.strip():
                    require_contains(package_yaml, f'{name}: "{value}"', rel(ROOT / package_yaml_path), errors)
        lvgl_base_include = package_include_paths.get("lvgl_base", "")
        if lvgl_base_include:
            lvgl_base_path = (package_dir / lvgl_base_include).resolve()
            lvgl_base_label = rel(lvgl_base_path)
            lvgl_base_yaml = read(lvgl_base_path, errors)
            lvgl_buffer_size = str(device.get("lvgl_buffer_size", "")).strip()
            lvgl_byte_order = str(device.get("lvgl_byte_order", "")).strip()
            lvgl_rotation_substitution = str(device.get("lvgl_rotation_substitution", "")).strip()
            if lvgl_buffer_size:
                require_contains(lvgl_base_yaml, f"buffer_size: {lvgl_buffer_size}", lvgl_base_label, errors)
            if lvgl_byte_order:
                require_contains(lvgl_base_yaml, f"byte_order: {lvgl_byte_order}", lvgl_base_label, errors)
            if lvgl_rotation_substitution:
                require_contains(lvgl_base_yaml, f"rotation: ${{{lvgl_rotation_substitution}}}", lvgl_base_label, errors)
            if device.get("lvgl_resume_on_input") is True:
                require_contains(lvgl_base_yaml, "resume_on_input: true", lvgl_base_label, errors)
            require_contains(lvgl_base_yaml, "displays:", lvgl_base_label, errors)
            require_contains(lvgl_base_yaml, "- tft_display", lvgl_base_label, errors)
        fonts_include = package_include_paths.get("fonts", "")
        if fonts_include:
            fonts_label = rel((package_dir / fonts_include).resolve())
            fonts_yaml = read((package_dir / fonts_include).resolve(), errors)
            if isinstance(font_assets, list):
                for font in font_assets:
                    if not isinstance(font, dict):
                        continue
                    font_id = str(font.get("id", "")).strip()
                    font_file = str(font.get("file", "")).strip()
                    font_size = font.get("size")
                    font_bpp = font.get("bpp")
                    if font_file:
                        require_contains(fonts_yaml, f'file: "{font_file}"', fonts_label, errors)
                    if font_id:
                        require_contains(fonts_yaml, f"id: {font_id}", fonts_label, errors)
                    if isinstance(font_size, int):
                        require_contains(fonts_yaml, f"size: {font_size}", fonts_label, errors)
                    if isinstance(font_bpp, int):
                        require_contains(fonts_yaml, f"bpp: {font_bpp}", fonts_label, errors)
            if isinstance(location_font_extra_files, list):
                for font_file in location_font_extra_files:
                    if isinstance(font_file, str) and font_file.strip():
                        require_contains(fonts_yaml, f'file: "{font_file.strip()}"', fonts_label, errors)
            for needle in (
                "latin_extended_glyphs:",
                "greek_cyrillic_vietnamese_glyphs:",
                "arabic_location_glyphs:",
                "hebrew_location_glyphs:",
                "glyphsets:",
                "GF_Latin_Core",
                "GF_Latin_Beyond",
                "GF_Latin_Vietnamese",
                "GF_Greek_Core",
                "GF_Cyrillic_Plus",
                "extras:",
            ):
                require_contains(fonts_yaml, needle, fonts_label, errors)
        icons_include = package_include_paths.get("icons", "")
        if icons_include:
            icons_label = rel((package_dir / icons_include).resolve())
            icons_yaml = read((package_dir / icons_include).resolve(), errors)
            if isinstance(icon_font, dict):
                icon_font_id = str(icon_font.get("id", "")).strip()
                icon_font_file = str(icon_font.get("file", "")).strip()
                icon_font_size = icon_font.get("size")
                icon_font_bpp = icon_font.get("bpp")
                if icon_font_file:
                    require_contains(icons_yaml, icon_font_file, icons_label, errors)
                if icon_font_id:
                    require_contains(icons_yaml, f"id: {icon_font_id}", icons_label, errors)
                if isinstance(icon_font_size, int):
                    require_contains(icons_yaml, f"size: {icon_font_size}", icons_label, errors)
                if isinstance(icon_font_bpp, int):
                    require_contains(icons_yaml, f"bpp: {icon_font_bpp}", icons_label, errors)
            if isinstance(icon_substitutions, dict):
                for name, glyph in icon_substitutions.items():
                    if isinstance(name, str) and isinstance(glyph, str) and name.strip() and glyph.strip():
                        require_contains(icons_yaml, f'{name}: "{glyph}"', icons_label, errors)
                        require_contains(icons_yaml, f'- "{glyph}"', icons_label, errors)
        for needle in (
            "esp_ldo:",
            "platform: ledc",
            "id: backlight_output",
            "i2c:",
            "scan: true",
            "sda_pullup_enabled: false",
            "scl_pullup_enabled: false",
            "auto_clear_enabled: false",
            "color_order: RGB",
        ):
            require_contains(device_yaml, needle, rel(ROOT / device_yaml_path), errors)


def check_project_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "name",
        "site_description",
        "ai_description",
        "social_image",
        "social_image_alt",
        "usb_flashing_image",
        "usb_flashing_image_alt",
        "web_installer_required_api",
        "web_installer_computer_requirement",
        "usb_cable_requirement",
        "usb_cable_warning",
        "immich_api_key_mode",
        "immich_api_key_privacy_promise",
        "home_assistant_name",
        "home_assistant_url",
        "home_assistant_requirement",
        "home_assistant_integration_platform",
        "device_log_level_default",
        "device_debug_update_interval",
        "firmware_update_source",
        "firmware_beta_channel_label",
        "firmware_manual_check_behavior",
        "firmware_beta_check_requirement",
        "firmware_custom_manifest_requirement",
        "ota_update_platform",
        "ota_pre_update_action",
        "ota_pre_update_transition",
        "ota_pre_update_delay",
        "backup_filename_prefix",
        "backup_filename_date_format",
        "backup_import_write_behavior",
        "backup_partial_config_behavior",
        "backup_invalid_photo_id_behavior",
        "privacy_connection_model",
        "privacy_network_scope",
        "privacy_no_cloud_service",
        "privacy_no_extra_account",
        "privacy_no_uploads",
        "privacy_no_hosted_service",
        "favicon",
        "npm_package_name",
        "license_id",
        "license_name",
        "owner_name",
        "owner_url",
        "package_name",
        "repository_url",
        "github_default_branch",
        "release_url_base",
        "release_artifact_prefix",
        "release_build_output_dir",
        "release_publish_dir",
        "release_uploaded_verify_dir",
        "release_source_factory_binary",
        "release_source_ota_binary",
        "release_esphome_cache_dir",
        "release_esphome_cache_key_prefix",
        "release_version_pattern",
        "stable_release_version_pattern",
        "firmware_version_placeholder_line",
        "firmware_local_build_version",
        "release_changelog_fallback_category",
        "public_base_url",
        "support_url",
        "support_button_image_url",
        "node_package_cache",
        "node_install_command",
        "local_check_command",
        "docs_build_command",
        "esphome_docker_image",
        "esphome_config_mount",
        "github_docs_release_meta_step_id",
        "github_docs_release_tag_env",
        "github_docs_release_tag_output",
        "github_docs_prerelease_tag_env",
        "github_pages_deployment_step_id",
        "github_pages_url_output",
        "web_ui_logs_event_source",
        "web_ui_logs_event_name",
        "web_ui_logs_clear_label",
        "node_version",
        "github_actions_runner",
        "github_docs_workflow_run_success_conclusion",
        "github_release_notes_version_ref",
        "github_release_build_version_ref",
        "github_release_build_ref",
        "github_release_notes_output",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")

    package_name = str(project.get("package_name", "")).strip()
    if package_name and not re.match(r"^[A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+$", package_name):
        errors.append("project.package_name must look like a reverse-DNS package name")

    release_url_base = str(project.get("release_url_base", "")).strip()
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    default_branch = str(project.get("github_default_branch", "")).strip()
    if repository_url and not repository_url.startswith("https://github.com/"):
        errors.append("project.repository_url must be an https GitHub URL")
    if default_branch and not re.match(r"^[A-Za-z0-9._/-]+$", default_branch):
        errors.append("project.github_default_branch contains unsupported characters")
    if default_branch and str(project.get("manual_setup_package_ref", "")).strip() != default_branch:
        errors.append("project.manual_setup_package_ref must match project.github_default_branch")
    if default_branch and str(project.get("external_component_ref", "")).strip() != default_branch:
        errors.append("project.external_component_ref must match project.github_default_branch")
    if release_url_base and not release_url_base.startswith("https://"):
        errors.append("project.release_url_base must be an https URL")
    if release_url_base and not release_url_base.endswith("/"):
        errors.append("project.release_url_base must end with /")
    if repository_url and release_url_base and release_url_base != f"{repository_url}/releases/tag/":
        errors.append("project.release_url_base must be based on project.repository_url")
    release_actions = project.get("release_workflow_actions", {})
    if not isinstance(release_actions, dict) or not release_actions:
        errors.append("project.release_workflow_actions must be a non-empty object")
    else:
        for name, action in release_actions.items():
            if not isinstance(name, str) or not name.strip():
                errors.append("project.release_workflow_actions keys must be non-empty strings")
            if not isinstance(action, str) or not action.strip():
                errors.append(f"project.release_workflow_actions.{name} must be a non-empty string")
    workflow_permissions = project.get("github_workflow_permissions", {})
    expected_workflows = {"compile", "docs", "release"}
    github_cli_env = project.get("github_cli_env", {})
    expected_github_cli_env = {"GH_TOKEN", "GH_REPO"}
    if not isinstance(github_cli_env, dict) or not github_cli_env:
        errors.append("project.github_cli_env must be a non-empty object")
    else:
        configured_env = {str(name).strip() for name in github_cli_env}
        missing_env = sorted(expected_github_cli_env - configured_env)
        extra_env = sorted(configured_env - expected_github_cli_env)
        if missing_env:
            errors.append(f"project.github_cli_env is missing variables: {', '.join(missing_env)}")
        if extra_env:
            errors.append(f"project.github_cli_env contains unknown variables: {', '.join(extra_env)}")
        for raw_name, raw_value in github_cli_env.items():
            name = str(raw_name).strip()
            value = str(raw_value).strip()
            if not name:
                errors.append("project.github_cli_env keys must be non-empty strings")
            if not value:
                errors.append(f"project.github_cli_env.{name or '<missing>'} must be a non-empty string")
    if not isinstance(workflow_permissions, dict) or not workflow_permissions:
        errors.append("project.github_workflow_permissions must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_permissions}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_permissions is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_permissions contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, permissions in workflow_permissions.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_permissions keys must be non-empty strings")
            if not isinstance(permissions, dict) or not permissions:
                errors.append(f"project.github_workflow_permissions.{name or '<missing>'} must be a non-empty object")
                continue
            for raw_scope, raw_access in permissions.items():
                scope = str(raw_scope).strip()
                access = str(raw_access).strip()
                if not scope:
                    errors.append(f"project.github_workflow_permissions.{name or '<missing>'} scopes must be non-empty strings")
                if access not in {"read", "write", "none"}:
                    errors.append(
                        f"project.github_workflow_permissions.{name or '<missing>'}.{scope or '<missing>'} must be read, write, or none"
                    )
    workflow_names = project.get("github_workflow_names", {})
    if not isinstance(workflow_names, dict) or not workflow_names:
        errors.append("project.github_workflow_names must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_names}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_names is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_names contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, raw_label in workflow_names.items():
            name = str(raw_name).strip()
            label = str(raw_label).strip()
            if not name:
                errors.append("project.github_workflow_names keys must be non-empty strings")
            if not label:
                errors.append(f"project.github_workflow_names.{name or '<missing>'} must be a non-empty string")
    workflow_path_filters = project.get("github_workflow_path_filters", {})
    expected_path_filter_sets = {"compile_pull_request", "docs_push"}
    if not isinstance(workflow_path_filters, dict) or not workflow_path_filters:
        errors.append("project.github_workflow_path_filters must be a non-empty object")
    else:
        configured_filter_sets = {str(name).strip() for name in workflow_path_filters}
        missing_filter_sets = sorted(expected_path_filter_sets - configured_filter_sets)
        extra_filter_sets = sorted(configured_filter_sets - expected_path_filter_sets)
        if missing_filter_sets:
            errors.append(f"project.github_workflow_path_filters is missing filters: {', '.join(missing_filter_sets)}")
        if extra_filter_sets:
            errors.append(f"project.github_workflow_path_filters contains unknown filters: {', '.join(extra_filter_sets)}")
        for raw_name, raw_paths in workflow_path_filters.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_path_filters keys must be non-empty strings")
            if not isinstance(raw_paths, list) or not raw_paths:
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must be a non-empty list")
                continue
            paths = [str(path).strip() for path in raw_paths]
            if any(not path for path in paths):
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must only contain non-empty strings")
            if len(paths) != len(set(paths)):
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must not contain duplicate paths")
    workflow_events = project.get("github_workflow_events", {})
    if not isinstance(workflow_events, dict) or not workflow_events:
        errors.append("project.github_workflow_events must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_events}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_events is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_events contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, raw_events in workflow_events.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_events keys must be non-empty strings")
            if not isinstance(raw_events, list) or not raw_events:
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must be a non-empty list")
                continue
            events = [str(event).strip() for event in raw_events]
            if any(not event for event in events):
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must only contain non-empty strings")
            if len(events) != len(set(events)):
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must not contain duplicate events")
    workflow_event_types = project.get("github_workflow_event_types", {})
    if not isinstance(workflow_event_types, dict) or not workflow_event_types:
        errors.append("project.github_workflow_event_types must be a non-empty object")
    else:
        for raw_key, raw_types in workflow_event_types.items():
            key = str(raw_key).strip()
            if "." not in key:
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must use workflow.event format")
            if not isinstance(raw_types, list) or not raw_types:
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must be a non-empty list")
                continue
            event_types = [str(event_type).strip() for event_type in raw_types]
            if any(not event_type for event_type in event_types):
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must only contain non-empty strings")
            if len(event_types) != len(set(event_types)):
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must not contain duplicate event types")
    workflow_jobs = project.get("github_workflow_jobs", {})
    if not isinstance(workflow_jobs, dict) or not workflow_jobs:
        errors.append("project.github_workflow_jobs must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_jobs}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_jobs is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_jobs contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_workflow, raw_jobs in workflow_jobs.items():
            workflow = str(raw_workflow).strip()
            if not workflow:
                errors.append("project.github_workflow_jobs keys must be non-empty strings")
            if not isinstance(raw_jobs, dict) or not raw_jobs:
                errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'} must be a non-empty object")
                continue
            for raw_job_id, raw_job_name in raw_jobs.items():
                job_id = str(raw_job_id).strip()
                job_name = str(raw_job_name).strip()
                if not job_id:
                    errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'} job ids must be non-empty strings")
                if not job_name:
                    errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'}.{job_id or '<missing>'} must be a non-empty string")
    workflow_job_dependencies = project.get("github_workflow_job_dependencies", {})
    if not isinstance(workflow_job_dependencies, dict) or not workflow_job_dependencies:
        errors.append("project.github_workflow_job_dependencies must be a non-empty object")
    else:
        for raw_key, raw_dependencies in workflow_job_dependencies.items():
            key = str(raw_key).strip()
            workflow, _, job_id = key.partition(".")
            if not workflow or not job_id:
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must use workflow.job format")
            if not isinstance(raw_dependencies, list) or not raw_dependencies:
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must be a non-empty list")
                continue
            dependencies = [str(dependency).strip() for dependency in raw_dependencies]
            if any(not dependency for dependency in dependencies):
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must only contain non-empty strings")
            if len(dependencies) != len(set(dependencies)):
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must not contain duplicate jobs")
    sparse_checkout_files = project.get("github_sparse_checkout_files", [])
    if not isinstance(sparse_checkout_files, list) or not sparse_checkout_files:
        errors.append("project.github_sparse_checkout_files must be a non-empty list")
    else:
        paths = [str(path).strip() for path in sparse_checkout_files]
        if any(not path for path in paths):
            errors.append("project.github_sparse_checkout_files must only contain non-empty strings")
        if len(paths) != len(set(paths)):
            errors.append("project.github_sparse_checkout_files must not contain duplicate paths")
        for raw_path in paths:
            path = check_relative_path(raw_path, "project.github_sparse_checkout_files entry", errors)
            if path:
                read(ROOT / path, errors)
    if not isinstance(project.get("github_sparse_checkout_cone_mode"), bool):
        errors.append("project.github_sparse_checkout_cone_mode must be true or false")
    release_notes_fetch_depth = project.get("github_release_notes_fetch_depth")
    if not isinstance(release_notes_fetch_depth, int) or isinstance(release_notes_fetch_depth, bool) or release_notes_fetch_depth < 0:
        errors.append("project.github_release_notes_fetch_depth must be a non-negative integer")
    if not isinstance(project.get("github_release_notes_fetch_tags"), bool):
        errors.append("project.github_release_notes_fetch_tags must be true or false")
    if not isinstance(project.get("github_release_build_fail_fast"), bool):
        errors.append("project.github_release_build_fail_fast must be true or false")
    if not isinstance(project.get("esphome_docker_remove_container"), bool):
        errors.append("project.esphome_docker_remove_container must be true or false")
    release_asset_suffixes = project.get("release_asset_suffixes", [])
    if not isinstance(release_asset_suffixes, list) or not release_asset_suffixes:
        errors.append("project.release_asset_suffixes must be a non-empty list")
    elif any(not isinstance(suffix, str) or not suffix.strip() or not suffix.startswith(".") for suffix in release_asset_suffixes):
        errors.append("project.release_asset_suffixes must only contain non-empty dot-prefixed strings")
    for field in ("release_binary_download_patterns", "release_manifest_download_patterns", "release_uploaded_verify_patterns"):
        patterns = project.get(field, [])
        if not isinstance(patterns, list) or not patterns:
            errors.append(f"project.{field} must be a non-empty list")
        else:
            values = [str(pattern).strip() for pattern in patterns]
            if any(not value for value in values):
                errors.append(f"project.{field} must only contain non-empty strings")
            if len(values) != len(set(values)):
                errors.append(f"project.{field} must not contain duplicate patterns")
    cache_hash_files = project.get("release_esphome_cache_hash_files", [])
    if not isinstance(cache_hash_files, list) or not cache_hash_files:
        errors.append("project.release_esphome_cache_hash_files must be a non-empty list")
    else:
        values = [str(path).strip() for path in cache_hash_files]
        if any(not value for value in values):
            errors.append("project.release_esphome_cache_hash_files must only contain non-empty strings")
        if len(values) != len(set(values)):
            errors.append("project.release_esphome_cache_hash_files must not contain duplicate paths")
    for field in ("release_version_pattern", "stable_release_version_pattern"):
        pattern = str(project.get(field, "")).strip()
        if pattern:
            try:
                re.compile(pattern)
            except re.error as exc:
                errors.append(f"project.{field} must be a valid regular expression: {exc}")
    placeholder_versions = project.get("firmware_placeholder_versions", [])
    local_build_version = str(project.get("firmware_local_build_version", "")).strip()
    if not isinstance(placeholder_versions, list) or not placeholder_versions:
        errors.append("project.firmware_placeholder_versions must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in placeholder_versions):
        errors.append("project.firmware_placeholder_versions must only contain non-empty strings")
    elif "0.0.0" not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include 0.0.0")
    elif default_branch and default_branch not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include project.github_default_branch")
    elif local_build_version and local_build_version not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include project.firmware_local_build_version")
    changelog_categories = project.get("release_changelog_categories", [])
    if not isinstance(changelog_categories, list) or not changelog_categories:
        errors.append("project.release_changelog_categories must be a non-empty list")
    else:
        seen_category_titles: set[str] = set()
        for category in changelog_categories:
            if not isinstance(category, dict):
                errors.append("project.release_changelog_categories entries must be objects")
                continue
            title = str(category.get("title", "")).strip()
            if not title:
                errors.append("project.release_changelog_categories entry is missing title")
            elif title in seen_category_titles:
                errors.append(f"Duplicate release changelog category: {title}")
            seen_category_titles.add(title)
            for field in ("paths", "keywords"):
                values = category.get(field, [])
                if not isinstance(values, list) or not values:
                    errors.append(f"project.release_changelog_categories.{title or '<missing>'}.{field} must be a non-empty list")
                elif any(not isinstance(value, str) or not value.strip() for value in values):
                    errors.append(f"project.release_changelog_categories.{title or '<missing>'}.{field} must only contain non-empty strings")
    for field in (
        "generated_asset_outputs",
        "generated_asset_sources",
        "web_template_placeholders",
        "web_initial_fetch_first_keys",
        "web_local_state_keys",
        "web_manual_state_keys",
    ):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
        elif len(values) != len(set(values)):
            errors.append(f"project.{field} must not contain duplicate entries")

    for field in ("support_url", "support_button_image_url"):
        value = str(project.get(field, "")).strip()
        if value and not value.startswith("https://"):
            errors.append(f"project.{field} must be an https URL")
    home_assistant_url = str(project.get("home_assistant_url", "")).strip()
    if home_assistant_url and not home_assistant_url.startswith("https://"):
        errors.append("project.home_assistant_url must be an https URL")
    owner_url = str(project.get("owner_url", "")).strip()
    if owner_url and not owner_url.startswith("https://"):
        errors.append("project.owner_url must be an https URL")
    for field in ("social_image", "usb_flashing_image", "favicon"):
        value = str(project.get(field, "")).strip()
        if value and (value.startswith("/") or ".." in Path(value).parts):
            errors.append(f"project.{field} must be a relative public asset path")
        if value:
            public_asset = ROOT / "docs" / "public" / value
            if not public_asset.is_file():
                errors.append(f"Missing file: {rel(public_asset)}")
    for field in (
        "web_installer_required_browsers",
        "web_installer_unsupported_browsers",
        "immich_server_url_schemes",
        "immich_server_url_targets",
        "immich_server_url_examples",
        "firmware_update_methods",
        "firmware_update_channels",
    ):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    frequency_hours = project.get("firmware_update_frequency_hours", {})
    manifest_url_length_limit = project.get("firmware_manifest_url_length_limit")
    if not isinstance(manifest_url_length_limit, int) or isinstance(manifest_url_length_limit, bool) or manifest_url_length_limit < 1:
        errors.append("project.firmware_manifest_url_length_limit must be a positive integer")
    if not isinstance(frequency_hours, dict) or not frequency_hours:
        errors.append("project.firmware_update_frequency_hours must be a non-empty object")
    else:
        for label, hours in frequency_hours.items():
            if not isinstance(label, str) or not label.strip():
                errors.append("project.firmware_update_frequency_hours keys must be non-empty strings")
            if not isinstance(hours, int) or isinstance(hours, bool) or hours < 1:
                errors.append(f"project.firmware_update_frequency_hours.{label} must be a positive integer")
    home_assistant_features = project.get("home_assistant_integration_features", [])
    if not isinstance(home_assistant_features, list) or not home_assistant_features:
        errors.append("project.home_assistant_integration_features must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in home_assistant_features):
        errors.append("project.home_assistant_integration_features must only contain non-empty strings")
    network_entities = project.get("home_assistant_network_entities", [])
    if not isinstance(network_entities, list) or not network_entities:
        errors.append("project.home_assistant_network_entities must be a non-empty list")
    else:
        for entity in network_entities:
            if not isinstance(entity, dict):
                errors.append("project.home_assistant_network_entities entries must be objects")
                continue
            for field in ("name", "type", "description"):
                if not str(entity.get(field, "")).strip():
                    errors.append(f"project.home_assistant_network_entities entry is missing {field}")
    diagnostic_entities = project.get("home_assistant_diagnostic_entities", [])
    if not isinstance(diagnostic_entities, list) or not diagnostic_entities:
        errors.append("project.home_assistant_diagnostic_entities must be a non-empty list")
    else:
        for entity in diagnostic_entities:
            if not isinstance(entity, dict):
                errors.append("project.home_assistant_diagnostic_entities entries must be objects")
                continue
            for field in ("name", "type", "description"):
                if not str(entity.get(field, "")).strip():
                    errors.append(f"project.home_assistant_diagnostic_entities entry is missing {field}")
    component_log_levels = project.get("device_log_component_levels", {})
    if not isinstance(component_log_levels, dict) or not component_log_levels:
        errors.append("project.device_log_component_levels must be a non-empty object")
    else:
        for component, level in component_log_levels.items():
            if not isinstance(component, str) or not component.strip():
                errors.append("project.device_log_component_levels keys must be non-empty strings")
            if not isinstance(level, str) or not level.strip():
                errors.append(f"project.device_log_component_levels.{component} must be a non-empty string")
    for field in ("network_wifi_strength_source", "network_wifi_strength_update_interval"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    backup_config_version = project.get("backup_config_version")
    if not isinstance(backup_config_version, int) or isinstance(backup_config_version, bool) or backup_config_version < 1:
        errors.append("project.backup_config_version must be a positive integer")
    backup_import_photo_id_limit = project.get("backup_import_photo_id_limit")
    if (
        not isinstance(backup_import_photo_id_limit, int)
        or isinstance(backup_import_photo_id_limit, bool)
        or backup_import_photo_id_limit < 1
    ):
        errors.append("project.backup_import_photo_id_limit must be a positive integer")
    backup_excluded_values = project.get("backup_excluded_runtime_values", [])
    if not isinstance(backup_excluded_values, list) or not backup_excluded_values:
        errors.append("project.backup_excluded_runtime_values must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_excluded_values}) != len(backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must not contain duplicate values")
    backup_export_groups = project.get("backup_export_groups", [])
    if not isinstance(backup_export_groups, list) or not backup_export_groups:
        errors.append("project.backup_export_groups must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_export_groups):
        errors.append("project.backup_export_groups must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_export_groups}) != len(backup_export_groups):
        errors.append("project.backup_export_groups must not contain duplicate groups")
    backup_export_fields = project.get("backup_export_fields", {})
    if not isinstance(backup_export_fields, dict) or not backup_export_fields:
        errors.append("project.backup_export_fields must be a non-empty object")
    else:
        expected_groups = {str(group).strip() for group in backup_export_groups if str(group).strip()}
        configured_groups = {str(group).strip() for group in backup_export_fields}
        missing_groups = sorted(expected_groups - configured_groups)
        extra_groups = sorted(configured_groups - expected_groups)
        if missing_groups:
            errors.append(f"project.backup_export_fields is missing groups: {', '.join(missing_groups)}")
        if extra_groups:
            errors.append(f"project.backup_export_fields contains unknown groups: {', '.join(extra_groups)}")

        all_fields: set[str] = set()
        field_count = 0
        for raw_group, raw_fields in backup_export_fields.items():
            group = str(raw_group).strip()
            if not group:
                errors.append("project.backup_export_fields keys must be non-empty strings")
            if not isinstance(raw_fields, list) or not raw_fields:
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must be a non-empty list")
                continue
            fields = [str(field).strip() for field in raw_fields]
            if any(not field for field in fields):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must only contain non-empty strings")
            if len(fields) != len(set(fields)):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must not contain duplicate fields")
            all_fields.update(fields)
            field_count += len(fields)
        if len(all_fields) != field_count:
            errors.append("project.backup_export_fields field names must be unique across groups")
    backup_fixture_files = project.get("backup_fixture_files", [])
    if not isinstance(backup_fixture_files, list) or not backup_fixture_files:
        errors.append("project.backup_fixture_files must be a non-empty list")
    elif len({str(fixture_file).strip() for fixture_file in backup_fixture_files}) != len(backup_fixture_files):
        errors.append("project.backup_fixture_files must not contain duplicate files")
    else:
        for fixture_file in backup_fixture_files:
            path = check_relative_path(fixture_file, "project.backup_fixture_files entry", errors)
            if path:
                read(ROOT / path, errors)
    touch_controls = project.get("touch_controls", [])
    if not isinstance(touch_controls, list) or not touch_controls:
        errors.append("project.touch_controls must be a non-empty list")
    else:
        for control in touch_controls:
            if not isinstance(control, dict):
                errors.append("project.touch_controls entries must be objects")
                continue
            if not str(control.get("action", "")).strip():
                errors.append("project.touch_controls entry is missing action")
            if not str(control.get("gesture", "")).strip():
                errors.append("project.touch_controls entry is missing gesture")
    for field in ("screen_brightness_day_night_source", "screen_schedule_behavior"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in (
        "screen_rotation_feature_source",
        "screen_rotation_behavior",
        "screen_rotation_developer_behavior",
        "developer_features_query_value",
        "developer_features_label",
        "developer_features_entity",
        "developer_features_guard",
        "developer_features_persistence",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("screen_rotation_user_options", "screen_rotation_developer_options"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    developer_query_params = project.get("developer_features_query_params", [])
    if not isinstance(developer_query_params, list) or not developer_query_params:
        errors.append("project.developer_features_query_params must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in developer_query_params):
        errors.append("project.developer_features_query_params must only contain non-empty strings")
    screen_rotation_mapping = project.get("screen_rotation_native_mapping", {})
    if not isinstance(screen_rotation_mapping, dict) or not screen_rotation_mapping:
        errors.append("project.screen_rotation_native_mapping must be a non-empty object")
    else:
        for user_option, native_option in screen_rotation_mapping.items():
            if not isinstance(user_option, str) or not user_option.strip():
                errors.append("project.screen_rotation_native_mapping keys must be non-empty strings")
            if not isinstance(native_option, str) or not native_option.strip():
                errors.append(f"project.screen_rotation_native_mapping.{user_option} must be a non-empty string")
    for field in (
        "screen_tone_base_purpose",
        "screen_tone_night_timing",
        "screen_tone_night_recovery",
        "screen_tone_override_duration",
        "clock_default_format",
        "clock_default_timezone",
        "clock_update_interval",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    clock_format_options = project.get("clock_format_options", [])
    if not isinstance(clock_format_options, list) or not clock_format_options:
        errors.append("project.clock_format_options must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in clock_format_options):
        errors.append("project.clock_format_options must only contain non-empty strings")
    if not isinstance(project.get("clock_default_show"), bool):
        errors.append("project.clock_default_show must be true or false")
    ntp_default_servers = project.get("ntp_default_servers", [])
    if not isinstance(ntp_default_servers, list) or not ntp_default_servers:
        errors.append("project.ntp_default_servers must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in ntp_default_servers):
        errors.append("project.ntp_default_servers must only contain non-empty strings")
    ntp_server_length_limit = project.get("ntp_server_length_limit")
    if not isinstance(ntp_server_length_limit, int) or isinstance(ntp_server_length_limit, bool) or ntp_server_length_limit < 1:
        errors.append("project.ntp_server_length_limit must be a positive integer")
    timezone_effects = project.get("timezone_change_effects", [])
    if not isinstance(timezone_effects, list) or not timezone_effects:
        errors.append("project.timezone_change_effects must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in timezone_effects):
        errors.append("project.timezone_change_effects must only contain non-empty strings")
    photo_source_modes = project.get("photo_source_modes", [])
    if not isinstance(photo_source_modes, list) or not photo_source_modes:
        errors.append("project.photo_source_modes must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in photo_source_modes):
        errors.append("project.photo_source_modes must only contain non-empty strings")
    for field in (
        "photo_source_auto_apply_behavior",
        "photo_source_memories_window",
        "photo_source_memories_fallback",
        "photo_source_album_person_sampling",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    if not isinstance(project.get("photo_source_id_limit"), int) or isinstance(project.get("photo_source_id_limit"), bool):
        errors.append("project.photo_source_id_limit must be an integer")
    for field in (
        "connection_timeout_default",
        "connection_timeout_range",
        "connection_failure_trigger",
        "connection_invalid_api_key_title",
        "connection_unavailable_title",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    if not isinstance(project.get("immich_max_error_retries"), int) or isinstance(project.get("immich_max_error_retries"), bool):
        errors.append("project.immich_max_error_retries must be an integer")
    retry_delays = project.get("immich_api_retry_delay_ms", [])
    if not isinstance(retry_delays, list) or not retry_delays:
        errors.append("project.immich_api_retry_delay_ms must be a non-empty list")
    elif any(not isinstance(value, int) or isinstance(value, bool) or value < 1 for value in retry_delays):
        errors.append("project.immich_api_retry_delay_ms must only contain positive integers")
    retryable_statuses = project.get("immich_retryable_http_statuses", [])
    if not isinstance(retryable_statuses, list) or not retryable_statuses:
        errors.append("project.immich_retryable_http_statuses must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in retryable_statuses):
        errors.append("project.immich_retryable_http_statuses must only contain non-empty strings")
    if not isinstance(project.get("immich_auth_error_status"), int) or isinstance(project.get("immich_auth_error_status"), bool):
        errors.append("project.immich_auth_error_status must be an integer")
    for field in (
        "slideshow_interval_default_seconds",
        "connection_timeout_default_seconds",
        "docs_firmware_verify_retries",
        "docs_firmware_verify_delay_seconds",
        "firmware_compile_timeout_minutes",
    ):
        if not isinstance(project.get(field), int) or isinstance(project.get(field), bool) or project.get(field) < 1:
            errors.append(f"project.{field} must be a positive integer")
    for field in ("slideshow_interval_range_seconds", "connection_timeout_range_seconds"):
        value = project.get(field)
        if (
            not isinstance(value, list)
            or len(value) != 2
            or any(not isinstance(item, int) or isinstance(item, bool) or item < 1 for item in value)
        ):
            errors.append(f"project.{field} must be a two-item list of positive integers")
        elif value[0] > value[1]:
            errors.append(f"project.{field} minimum must not exceed maximum")
    for field in (
        "slideshow_check_interval",
        "docs_dist_artifact_name",
        "docs_firmware_artifact_name",
        "docs_dist_output_path",
        "docs_deploy_path",
        "github_pages_environment",
        "github_pages_concurrency_group",
        "setup_captive_portal_ip",
        "setup_screen_dim_delay",
        "setup_screen_dim_brightness",
        "setup_screen_dim_transition",
        "setup_loading_backlight_brightness",
        "setup_loading_backlight_transition",
        "setup_connection_ready_condition",
        "manual_setup_package_ref",
        "manual_setup_package_refresh",
        "factory_firmware_purpose",
        "factory_firmware_secret_policy",
        "factory_firmware_network_mode",
        "factory_firmware_setup_method",
        "factory_firmware_local_use",
        "web_server_public_app_path",
        "web_server_factory_css_include",
        "web_server_factory_js_include",
        "external_component_git_source_type",
        "external_component_local_source_type",
        "external_component_git_path",
        "external_component_local_path",
        "external_component_ref",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("web_server_port", "web_server_version"):
        value = project.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            errors.append(f"project.{field} must be a positive integer")
    if not isinstance(project.get("web_server_include_internal"), bool):
        errors.append("project.web_server_include_internal must be true or false")
    if not isinstance(project.get("github_pages_cancel_in_progress"), bool):
        errors.append("project.github_pages_cancel_in_progress must be true or false")
    prerelease_lookup_limit = project.get("github_prerelease_lookup_limit")
    if not isinstance(prerelease_lookup_limit, int) or isinstance(prerelease_lookup_limit, bool) or prerelease_lookup_limit < 1:
        errors.append("project.github_prerelease_lookup_limit must be a positive integer")
    for field in ("github_release_download_clobber", "github_release_upload_clobber"):
        if not isinstance(project.get(field), bool):
            errors.append(f"project.{field} must be true or false")
    if "web_server_factory_js_url" not in project or not isinstance(project.get("web_server_factory_js_url"), str):
        errors.append("project.web_server_factory_js_url must be a string")
    sorting_groups = project.get("web_server_sorting_groups", [])
    if not isinstance(sorting_groups, list) or not sorting_groups:
        errors.append("project.web_server_sorting_groups must be a non-empty list")
    else:
        group_ids: set[str] = set()
        for group in sorting_groups:
            if not isinstance(group, dict):
                errors.append("project.web_server_sorting_groups entries must be objects")
                continue
            group_id = str(group.get("id", "")).strip()
            name = str(group.get("name", "")).strip()
            weight = group.get("sorting_weight")
            if not group_id:
                errors.append("project.web_server_sorting_groups entry is missing id")
            elif group_id in group_ids:
                errors.append(f"Duplicate project.web_server_sorting_groups id: {group_id}")
            group_ids.add(group_id)
            if not name:
                errors.append(f"project.web_server_sorting_groups.{group_id or '<missing>'} is missing name")
            if not isinstance(weight, int) or isinstance(weight, bool):
                errors.append(f"project.web_server_sorting_groups.{group_id or '<missing>'}.sorting_weight must be an integer")
    external_components = project.get("external_component_names", [])
    if not isinstance(external_components, list) or not external_components:
        errors.append("project.external_component_names must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in external_components):
        errors.append("project.external_component_names must only contain non-empty strings")
    for field in ("setup_wizard_steps", "setup_required_connection_fields", "setup_skip_substitutions"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    for field in ("manual_setup_required_substitutions", "manual_setup_wifi_secrets"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    web_ui_tabs = project.get("web_ui_tabs", [])
    if not isinstance(web_ui_tabs, list) or not web_ui_tabs:
        errors.append("project.web_ui_tabs must be a non-empty list")
    else:
        tab_ids: set[str] = set()
        for tab in web_ui_tabs:
            if not isinstance(tab, dict):
                errors.append("project.web_ui_tabs entries must be objects")
                continue
            tab_id = str(tab.get("id", "")).strip()
            tab_label = str(tab.get("label", "")).strip()
            if not tab_id:
                errors.append("project.web_ui_tabs entry is missing id")
            elif tab_id in tab_ids:
                errors.append(f"Duplicate project.web_ui_tabs id: {tab_id}")
            tab_ids.add(tab_id)
            if not tab_label:
                errors.append("project.web_ui_tabs entry is missing label")
    retained_log_lines = project.get("web_ui_logs_retained_lines")
    if not isinstance(retained_log_lines, int) or isinstance(retained_log_lines, bool) or retained_log_lines < 1:
        errors.append("project.web_ui_logs_retained_lines must be a positive integer")
    logs_event_source = str(project.get("web_ui_logs_event_source", "")).strip()
    logs_event_name = str(project.get("web_ui_logs_event_name", "")).strip()
    logs_clear_label = str(project.get("web_ui_logs_clear_label", "")).strip()
    if logs_event_source and (not logs_event_source.startswith("/") or any(char.isspace() for char in logs_event_source)):
        errors.append("project.web_ui_logs_event_source must be a root-relative path without whitespace")
    if logs_event_name and not re.match(r"^[A-Za-z][A-Za-z0-9_-]*$", logs_event_name):
        errors.append("project.web_ui_logs_event_name must be a non-empty event name")
    if logs_clear_label and len(logs_clear_label) > 40:
        errors.append("project.web_ui_logs_clear_label must be 40 characters or fewer")
    for field in ("date_filter_modes", "metadata_overlay_fields"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    for field in (
        "date_filter_relative_anchor",
        "date_filter_time_source",
        "portrait_pairing_behavior",
        "portrait_pairing_rotation_behavior",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    screen_schedule_effects = project.get("screen_schedule_off_effects", [])
    if not isinstance(screen_schedule_effects, list) or not screen_schedule_effects:
        errors.append("project.screen_schedule_off_effects must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in screen_schedule_effects):
        errors.append("project.screen_schedule_off_effects must only contain non-empty strings")
    permissions = project.get("immich_api_key_permissions", [])
    if not isinstance(permissions, list) or not permissions:
        errors.append("project.immich_api_key_permissions must be a non-empty list")
    else:
        seen_permissions: set[str] = set()
        for permission in permissions:
            if not isinstance(permission, dict):
                errors.append("project.immich_api_key_permissions entries must be objects")
                continue
            name = str(permission.get("name", "")).strip()
            purpose = str(permission.get("purpose", "")).strip()
            if not name:
                errors.append("project.immich_api_key_permissions entry is missing name")
            elif name in seen_permissions:
                errors.append(f"Duplicate Immich API key permission: {name}")
            elif not re.match(r"^[a-z]+\.(read|view)$", name):
                errors.append(f"Immich API key permission should be read/view-only: {name}")
            seen_permissions.add(name)
            if not purpose:
                errors.append(f"Immich API key permission {name or '<missing>'} is missing purpose")

    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    if package_name:
        require_contains(firmware_update, f"name: {package_name}", "common/addon/firmware_update.yaml", errors)


def check_npm_package_metadata(product: dict, errors: list[str]) -> None:
    expected_name = str(product["project"].get("npm_package_name", "")).strip()
    expected_license = str(product["project"].get("license_id", "")).strip()
    if not expected_name:
        errors.append("project.npm_package_name is required")
        return

    try:
        package_json = json.loads(read(ROOT / "package.json", errors) or "{}")
        package_lock = json.loads(read(ROOT / "package-lock.json", errors) or "{}")
    except json.JSONDecodeError as exc:
        errors.append(f"Package metadata JSON is invalid: {exc}")
        return

    if package_json.get("name") != expected_name:
        errors.append("package.json name must match project.npm_package_name")
    if expected_license and package_json.get("license") != expected_license:
        errors.append("package.json license must match project.license_id")
    scripts = package_json.get("scripts", {})
    if not isinstance(scripts, dict):
        errors.append("package.json scripts must be an object")
    else:
        if scripts.get("check:backup") != "python3 scripts/check_backup_config.py":
            errors.append("package.json check:backup must run scripts/check_backup_config.py")
        check_all = str(scripts.get("check:all", ""))
        if "npm run check:backup" not in check_all:
            errors.append("package.json check:all must include check:backup")
    if package_lock.get("name") != expected_name:
        errors.append("package-lock.json name must match project.npm_package_name")
    root_package = package_lock.get("packages", {}).get("", {})
    if root_package.get("name") != expected_name:
        errors.append("package-lock.json root package name must match project.npm_package_name")
    if expected_license and root_package.get("license") != expected_license:
        errors.append("package-lock.json root package license must match project.license_id")


def check_license_metadata(product: dict, errors: list[str]) -> None:
    license_id = str(product["project"].get("license_id", "")).strip()
    license_name = str(product["project"].get("license_name", "")).strip()
    if not license_id:
        errors.append("project.license_id is required")
    if not license_name:
        errors.append("project.license_name is required")

    license_text = read(ROOT / "LICENSE", errors)
    readme = read(ROOT / "README.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    if license_name:
        for label, text in (("LICENSE", license_text), ("README.md", readme), ("docs/license.md", license_docs)):
            require_contains(text, license_name, label, errors)
    if license_id:
        require_contains(read(ROOT / "package.json", errors), f'"license": "{license_id}"', "package.json", errors)


def check_immich_api_key_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    mode = str(project.get("immich_api_key_mode", "")).strip()
    privacy_promise = str(project.get("immich_api_key_privacy_promise", "")).strip()
    permissions = project.get("immich_api_key_permissions", [])

    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)

    if mode:
        require_contains(api_key_docs, f"{mode} API key", "docs/api-key.md", errors)
        require_contains(troubleshooting_docs, f"{mode} Immich API key", "docs/troubleshooting.md", errors)
        require_contains(immich_photo_frame_docs, f"{mode.capitalize()} permissions are recommended", "docs/immich-photo-frame.md", errors)
    if privacy_promise:
        require_contains(api_key_docs, privacy_promise, "docs/api-key.md", errors)

    if not isinstance(permissions, list):
        return

    expected_names: list[str] = []
    for permission in permissions:
        if not isinstance(permission, dict):
            continue
        name = str(permission.get("name", "")).strip()
        purpose = str(permission.get("purpose", "")).strip()
        if not name or not purpose:
            continue
        expected_names.append(name)
        require_contains(api_key_docs, f"| `{name}` | {purpose} |", "docs/api-key.md", errors)

    documented_names = re.findall(r"^\| `([^`]+)` \|", api_key_docs, flags=re.MULTILINE)
    if documented_names != expected_names:
        errors.append(
            "docs/api-key.md permission rows must match project.immich_api_key_permissions "
            f"(expected {expected_names}, found {documented_names})"
        )


def check_immich_connection_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    schemes = project.get("immich_server_url_schemes", [])
    targets = project.get("immich_server_url_targets", [])
    examples = project.get("immich_server_url_examples", [])

    install_docs = read(ROOT / "docs" / "install.md", errors)
    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)

    docs_to_check = (
        ("docs/install.md", install_docs),
        ("docs/api-key.md", api_key_docs),
        ("docs/troubleshooting.md", troubleshooting_docs),
    )
    for field_name, values in (
        ("immich_server_url_schemes", schemes),
        ("immich_server_url_targets", targets),
        ("immich_server_url_examples", examples),
    ):
        if not isinstance(values, list):
            continue
        for label, text in docs_to_check:
            for value in values:
                if isinstance(value, str) and value.strip():
                    require_contains(text, value.strip(), f"{label} {field_name}", errors)


def check_home_assistant_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    name = str(project.get("home_assistant_name", "")).strip()
    url = str(project.get("home_assistant_url", "")).strip()
    requirement = str(project.get("home_assistant_requirement", "")).strip()
    platform = str(project.get("home_assistant_integration_platform", "")).strip()
    features = project.get("home_assistant_integration_features", [])
    network_entities = project.get("home_assistant_network_entities", [])
    diagnostic_entities = project.get("home_assistant_diagnostic_entities", [])
    debug_update_interval = str(project.get("device_debug_update_interval", "")).strip()
    wifi_strength_source = str(project.get("network_wifi_strength_source", "")).strip()
    wifi_strength_update_interval = str(project.get("network_wifi_strength_update_interval", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)
    network_yaml = read(ROOT / "common" / "addon" / "network.yaml", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    docs_to_check = (
        ("README.md", readme),
        ("docs/index.md", index_docs),
        ("docs/immich-photo-frame.md", immich_photo_frame_docs),
        ("docs/home-assistant.md", home_assistant_docs),
    )
    if name:
        for label, text in docs_to_check:
            require_contains(text, name, label, errors)
    if requirement:
        require_contains(readme, requirement, "README.md", errors)
        require_contains(home_assistant_docs, requirement, "docs/home-assistant.md", errors)
    if url:
        require_contains(home_assistant_docs, url, "docs/home-assistant.md", errors)
    if platform:
        for label, text in (
            ("README.md", readme),
            ("docs/immich-photo-frame.md", immich_photo_frame_docs),
            ("docs/home-assistant.md", home_assistant_docs),
        ):
            require_contains(text, platform, label, errors)
    if isinstance(features, list):
        for feature in features:
            if not isinstance(feature, str) or not feature.strip():
                continue
            require_contains(home_assistant_docs, feature.strip(), "docs/home-assistant.md", errors)
    if isinstance(network_entities, list):
        for entity in network_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(network_yaml, f'name: "{entity_name}"', "common/addon/network.yaml", errors)
    if isinstance(diagnostic_entities, list):
        for entity in diagnostic_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(device_yaml, f'name: "{entity_name}"', device_yaml_path, errors)
    if debug_update_interval:
        require_contains(device_yaml, "debug:", device_yaml_path, errors)
        require_contains(device_yaml, f"update_interval: {debug_update_interval}", device_yaml_path, errors)
    for needle in (
        "text_sensor:",
        "platform: debug",
        "reset_reason:",
        "entity_category: diagnostic",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)
    if wifi_strength_source:
        require_contains(network_yaml, f'name: "{wifi_strength_source}"', "common/addon/network.yaml", errors)
        require_contains(network_yaml, "source_id: wifi_signal_db", "common/addon/network.yaml", errors)
    if wifi_strength_update_interval:
        require_contains(network_yaml, f"update_interval: {wifi_strength_update_interval}", "common/addon/network.yaml", errors)
    for needle in (
        "platform: status",
        "platform: wifi_signal",
        "platform: copy",
        "unit_of_measurement: \"%\"",
        "platform: wifi_info",
        "ip_address:",
        "entity_category: diagnostic",
        "return min(max(2 * (x + 100.0), 0.0), 100.0);",
    ):
        require_contains(network_yaml, needle, "common/addon/network.yaml", errors)
    require_contains(home_assistant_docs, "notify when **Network: Online** goes unavailable", "docs/home-assistant.md", errors)


def check_device_logging_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    default_level = str(project.get("device_log_level_default", "")).strip()
    component_levels = project.get("device_log_component_levels", {})

    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    if default_level:
        require_contains(device_yaml, f'log_level: "{default_level}"', device_yaml_path, errors)
        require_contains(device_yaml, "level: ${log_level}", device_yaml_path, errors)
    require_contains(device_yaml, "logger:", device_yaml_path, errors)
    require_contains(device_yaml, "logs:", device_yaml_path, errors)
    if isinstance(component_levels, dict):
        for component, level in component_levels.items():
            if not isinstance(component, str) or not isinstance(level, str):
                continue
            component_name = component.strip()
            log_level = level.strip()
            if component_name and log_level:
                require_contains(device_yaml, f"{component_name}: {log_level}", device_yaml_path, errors)


def check_firmware_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    methods = project.get("firmware_update_methods", [])
    source = str(project.get("firmware_update_source", "")).strip()
    channels = project.get("firmware_update_channels", [])
    beta_label = str(project.get("firmware_beta_channel_label", "")).strip()
    manual_check_behavior = str(project.get("firmware_manual_check_behavior", "")).strip()
    beta_check_requirement = str(project.get("firmware_beta_check_requirement", "")).strip()
    custom_manifest_requirement = str(project.get("firmware_custom_manifest_requirement", "")).strip()
    manifest_url_length_limit = project.get("firmware_manifest_url_length_limit")
    frequency_hours = project.get("firmware_update_frequency_hours", {})
    default_urls = default_public_manifest_urls(product)

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    firmware_yaml = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)

    for method in methods if isinstance(methods, list) else []:
        if isinstance(method, str) and method.strip():
            require_contains(firmware_docs, method.strip(), "docs/firmware-update.md", errors)
            require_contains(firmware_yaml, method.strip(), "common/addon/firmware_update.yaml", errors)
    if source:
        require_contains(firmware_docs, source, "docs/firmware-update.md", errors)
    if isinstance(channels, list):
        for channel in channels:
            if isinstance(channel, str) and channel.strip():
                require_contains(firmware_docs, channel.strip(), "docs/firmware-update.md", errors)
                require_contains(firmware_yaml, channel.strip(), "common/addon/firmware_update.yaml", errors)
    if beta_label:
        require_contains(firmware_docs, beta_label, "docs/firmware-update.md", errors)
        require_contains(web_template, beta_label.capitalize(), rel(WEB_TEMPLATE), errors)
    if manual_check_behavior:
        require_contains(firmware_docs, manual_check_behavior, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, "manual_check_only", "common/addon/firmware_update.yaml", errors)
        require_contains(firmware_yaml, "component.update: firmware_update", "common/addon/firmware_update.yaml", errors)
        require_contains(web_template, 'post(endpoints.firmware_check + "/press")', rel(WEB_TEMPLATE), errors)
    if beta_check_requirement:
        require_contains(firmware_docs, beta_check_requirement, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, "lambda: 'return id(beta_channel_switch).state;'", "common/addon/firmware_update.yaml", errors)
        require_contains(web_template, "if (!S.beta_channel)", rel(WEB_TEMPLATE), errors)
    if custom_manifest_requirement:
        require_contains(firmware_docs, custom_manifest_requirement, "docs/firmware-update.md", errors)
        require_contains(web_template, custom_manifest_requirement, rel(WEB_TEMPLATE), errors)
        require_contains(firmware_yaml, "is_valid_http_url(url)", "common/addon/firmware_update.yaml", errors)
        require_contains(firmware_yaml, "strip_trailing_slashes", "common/addon/firmware_update.yaml", errors)
    if isinstance(manifest_url_length_limit, int) and not isinstance(manifest_url_length_limit, bool):
        if firmware_yaml.count(f"max_length: {manifest_url_length_limit}") < 2:
            errors.append(
                "common/addon/firmware_update.yaml must use project.firmware_manifest_url_length_limit for both manifest URL text fields"
            )
        require_contains(web_template, f"MAX_FIRMWARE_URL_LENGTH = {manifest_url_length_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_FIRMWARE_URL_LENGTH = {manifest_url_length_limit}", rel(WEB_APP), errors)
    if isinstance(frequency_hours, dict):
        for label, hours in frequency_hours.items():
            if not isinstance(label, str) or not isinstance(hours, int) or isinstance(hours, bool):
                continue
            require_contains(firmware_docs, label, "docs/firmware-update.md", errors)
            if label == "Daily":
                require_contains(firmware_yaml, f"int threshold = {hours}", "common/addon/firmware_update.yaml", errors)
                require_contains(firmware_yaml, 'initial_option: "Daily"', "common/addon/firmware_update.yaml", errors)
            else:
                require_contains(firmware_yaml, f'freq == "{label}"', "common/addon/firmware_update.yaml", errors)
            require_contains(firmware_yaml, f"threshold = {hours}", "common/addon/firmware_update.yaml", errors)
    for needle in (
        "update.perform: firmware_update",
        "id(auto_update_switch).state && !id(manual_check_only)",
        "id(beta_channel_switch).state",
        "update_interval: never",
    ):
        require_contains(firmware_yaml, needle, "common/addon/firmware_update.yaml", errors)
    for needle in (
        "Auto updates",
        "Disabled",
        'productSettingOptions("update_frequency")',
        "Check for Update",
        "Install",
        'post(endpoints.update + "/install")',
        'post(endpoints.update_beta + "/install")',
        "Stable Manifest URL",
        "Beta Manifest URL",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for label, url in default_urls.items():
        require_contains(firmware_docs, url, "docs/firmware-update.md", errors)
        require_contains(backup_docs, url, "docs/backup.md", errors)
        require_contains(firmware_yaml, url, "common/addon/firmware_update.yaml", errors)
        require_contains(web_text, url, rel(WEB_APP), errors)
        require_contains(web_template, f"FIRMWARE_MANIFEST_URLS.{label}", rel(WEB_TEMPLATE), errors)


def check_ota_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    platform = str(project.get("ota_update_platform", "")).strip()
    pre_update_action = str(project.get("ota_pre_update_action", "")).strip()
    transition = str(project.get("ota_pre_update_transition", "")).strip()
    delay = str(project.get("ota_pre_update_delay", "")).strip()

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    if pre_update_action:
        require_contains(firmware_docs, pre_update_action, "docs/firmware-update.md", errors)
    if transition:
        require_contains(firmware_docs, transition, "docs/firmware-update.md", errors)
        require_contains(device_yaml, f"transition_length: {transition}", device_yaml_path, errors)
    if delay:
        require_contains(firmware_docs, delay, "docs/firmware-update.md", errors)
        require_contains(device_yaml, f"delay: {delay}", device_yaml_path, errors)
    if platform:
        require_contains(device_yaml, f"platform: {platform}", device_yaml_path, errors)
    for needle in (
        "ota:",
        "on_begin:",
        "light.turn_off:",
        "id: backlight",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)


def check_backup_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    config_version = project.get("backup_config_version")
    filename_prefix = str(project.get("backup_filename_prefix", "")).strip()
    date_format = str(project.get("backup_filename_date_format", "")).strip()
    photo_id_limit = project.get("backup_import_photo_id_limit")
    excluded_values = project.get("backup_excluded_runtime_values", [])
    export_groups = [str(value).strip() for value in project.get("backup_export_groups", []) if str(value).strip()]
    import_write_behavior = str(project.get("backup_import_write_behavior", "")).strip()
    partial_behavior = str(project.get("backup_partial_config_behavior", "")).strip()
    invalid_photo_id_behavior = str(project.get("backup_invalid_photo_id_behavior", "")).strip()

    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)

    if isinstance(config_version, int) and not isinstance(config_version, bool):
        require_contains(backup_docs, f'"version": {config_version}', "docs/backup.md", errors)
        require_contains(web_template, f"version: {config_version}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"version: {config_version}", rel(WEB_APP), errors)
    if filename_prefix:
        require_contains(backup_docs, f"`{filename_prefix}{date_format}.json`", "docs/backup.md", errors)
        require_contains(web_template, f'var name = "{filename_prefix}"', rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f'var name = "{filename_prefix}"', rel(WEB_APP), errors)
    if date_format:
        require_contains(backup_docs, date_format, "docs/backup.md", errors)
    if isinstance(photo_id_limit, int) and not isinstance(photo_id_limit, bool):
        require_contains(backup_docs, f"{photo_id_limit} characters", "docs/backup.md", errors)
        require_contains(web_template, f"MAX_PHOTO_ID_FIELD_LENGTH = {photo_id_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_PHOTO_ID_FIELD_LENGTH = {photo_id_limit}", rel(WEB_APP), errors)
    if isinstance(excluded_values, list):
        for value in excluded_values:
            if isinstance(value, str) and value.strip():
                require_contains(backup_docs, value.strip(), "docs/backup.md", errors)
    for group in export_groups:
        require_contains(backup_docs, f'"{group}"', "docs/backup.md", errors)
        require_contains(web_template, f"{group}: {{", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"{group}: {{", rel(WEB_APP), errors)
        require_contains(web_template, f"data.{group} || {{}}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"data.{group} || {{}}", rel(WEB_APP), errors)
    if import_write_behavior:
        require_contains(backup_docs, import_write_behavior, "docs/backup.md", errors)
    if partial_behavior:
        require_contains(backup_docs, partial_behavior, "docs/backup.md", errors)
    if invalid_photo_id_behavior:
        require_contains(backup_docs, invalid_photo_id_behavior, "docs/backup.md", errors)
        for label in ("Album IDs", "Album labels", "Person IDs", "Person labels"):
            require_contains(web_template, f"{label} exceed {photo_id_limit} characters - not imported", rel(WEB_TEMPLATE), errors)
            require_contains(web_text, f"{label} exceed {photo_id_limit} characters - not imported", rel(WEB_APP), errors)
        for label in ("album IDs", "person IDs"):
            require_contains(web_template, f"Import skipped invalid {label}", rel(WEB_TEMPLATE), errors)
            require_contains(web_text, f"Import skipped invalid {label}", rel(WEB_APP), errors)

    for needle in (
        "display_mode",
        "display mode",
        "Partial config files work",
        "Settings imported successfully",
        "JSON.stringify(data, null, 2)",
    ):
        if needle in {"Settings imported successfully", "JSON.stringify(data, null, 2)"}:
            require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
            require_contains(web_text, needle, rel(WEB_APP), errors)
        else:
            require_contains(backup_docs, needle, "docs/backup.md", errors)
    require_contains(web_template, "display_mode: S.display_mode", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "p.display_mode", rel(WEB_TEMPLATE), errors)


def check_privacy_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    connection_model = str(project.get("privacy_connection_model", "")).strip()
    network_scope = str(project.get("privacy_network_scope", "")).strip()
    no_cloud_service = str(project.get("privacy_no_cloud_service", "")).strip()
    no_extra_account = str(project.get("privacy_no_extra_account", "")).strip()
    no_uploads = str(project.get("privacy_no_uploads", "")).strip()
    no_hosted_service = str(project.get("privacy_no_hosted_service", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)

    if connection_model:
        require_contains(readme, connection_model, "README.md", errors)
        require_contains(immich_photo_frame_docs, connection_model, "docs/immich-photo-frame.md", errors)
    if network_scope:
        require_contains(readme, network_scope, "README.md", errors)
        require_contains(immich_photo_frame_docs, network_scope, "docs/immich-photo-frame.md", errors)
    if no_cloud_service:
        require_contains(readme, no_cloud_service, "README.md", errors)
    if no_extra_account:
        require_contains(readme, no_extra_account, "README.md", errors)
    if no_uploads:
        require_contains(immich_photo_frame_docs, no_uploads, "docs/immich-photo-frame.md", errors)
    if no_hosted_service:
        require_contains(immich_photo_frame_docs, no_hosted_service, "docs/immich-photo-frame.md", errors)
    for needle in ("No hub, cloud, or extra software required", "self-hosted Immich"):
        require_contains(ai_txt, needle, "docs/public/ai.txt", errors)
    for needle in ("cloud account", "separate bridge service"):
        require_contains(index_docs, needle, "docs/index.md", errors)


def check_touch_controls_metadata(product: dict, errors: list[str]) -> None:
    controls = product["project"].get("touch_controls", [])
    readme = read(ROOT / "README.md", errors)
    touch_docs = read(ROOT / "docs" / "touch-controls.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    screen_settings_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    slideshow_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_slideshow.yaml", errors)
    backlight_schedule_yaml = read(ROOT / "common" / "addon" / "backlight_schedule.yaml", errors)
    backlight_yaml = read(ROOT / "common" / "addon" / "backlight.yaml", errors)

    if isinstance(controls, list):
        for control in controls:
            if not isinstance(control, dict):
                continue
            action = str(control.get("action", "")).strip()
            gesture = str(control.get("gesture", "")).strip()
            if action:
                require_contains(touch_docs, f"**{action}**", "docs/touch-controls.md", errors)
            if gesture:
                require_contains(touch_docs, gesture, "docs/touch-controls.md", errors)

    for needle in ("tap to wake", "double-tap to advance to the next photo", "press-and-hold to sleep"):
        require_contains(readme, needle, "README.md", errors)
    require_contains(readme, "wake, sleep, or advance to the next photo", "README.md", errors)
    require_contains(troubleshooting_docs, "wake, sleep, or next-photo gestures", "docs/troubleshooting.md", errors)
    require_contains(screen_settings_docs, "same sleep/wake behavior as the touchscreen controls", "docs/screen-settings.md", errors)
    for needle in ("slideshow_on_press", "slideshow_on_short_click", "last_short_tap_ms", "immich_advance_forward"):
        require_contains(slideshow_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    for needle in ("3-second hold timer", "delay: 3s", "screen_schedule_manual_sleep"):
        require_contains(backlight_schedule_yaml, needle, "common/addon/backlight_schedule.yaml", errors)
    for needle in ('name: "Screen: Sleep"', 'name: "Screen: Wake"'):
        require_contains(backlight_yaml, needle, "common/addon/backlight.yaml", errors)


def check_screen_schedule_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    day_night_source = str(project.get("screen_brightness_day_night_source", "")).strip()
    schedule_behavior = str(project.get("screen_schedule_behavior", "")).strip()
    schedule_effects = project.get("screen_schedule_off_effects", [])

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    screen_settings_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    backlight_schedule_yaml = read(ROOT / "common" / "addon" / "backlight_schedule.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)

    if day_night_source:
        require_contains(screen_settings_docs, day_night_source, "docs/screen-settings.md", errors)
        require_contains(backlight_schedule_yaml, day_night_source, "common/addon/backlight_schedule.yaml", errors)
    if schedule_behavior:
        require_contains(screen_settings_docs, schedule_behavior, "docs/screen-settings.md", errors)
    if isinstance(schedule_effects, list):
        for effect in schedule_effects:
            if isinstance(effect, str) and effect.strip():
                require_contains(screen_settings_docs, effect.strip(), "docs/screen-settings.md", errors)
    for needle in (
        "schedule the display to turn off overnight",
        "brightness",
        "screen tone",
    ):
        require_contains(readme, needle, "README.md", errors)
    for needle in (
        "Screen Scheduling",
        "set daytime and night-time brightness levels separately",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in (
        "Daytime brightness",
        "nighttime brightness",
        "wake timeout",
    ):
        require_contains(backup_docs, needle, "docs/backup.md", errors)
    for needle in (
        "screen_schedule_sleep",
        "screen_schedule_wake",
        "screen_schedule_check",
        "backlight_apply_brightness",
        "backlight_recalc_sunrise_sunset",
    ):
        require_contains(backlight_schedule_yaml, needle, "common/addon/backlight_schedule.yaml", errors)
    for key in ("schedule_enabled", "schedule_on_hour", "schedule_off_hour", "schedule_wake_timeout"):
        require_contains(web_template, key, rel(WEB_TEMPLATE), errors)


def check_screen_rotation_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    user_options = [str(value).strip() for value in project.get("screen_rotation_user_options", []) if str(value).strip()]
    developer_options = [
        str(value).strip() for value in project.get("screen_rotation_developer_options", []) if str(value).strip()
    ]
    native_mapping = project.get("screen_rotation_native_mapping", {})
    feature_source = str(project.get("screen_rotation_feature_source", "")).strip()
    rotation_behavior = str(project.get("screen_rotation_behavior", "")).strip()
    developer_behavior = str(project.get("screen_rotation_developer_behavior", "")).strip()

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    rotation_setting = settings_by_key.get("screen_rotation")
    if not rotation_setting:
        errors.append("product settings must include screen_rotation")
    else:
        if user_options and rotation_setting.get("options") != user_options:
            errors.append("project.screen_rotation_user_options must match screen_rotation options")
        if developer_options and rotation_setting.get("developer_options") != developer_options:
            errors.append("project.screen_rotation_developer_options must match screen_rotation developer_options")

    screen_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    rotation_yaml = read(ROOT / "common" / "addon" / "screen_rotation.yaml", errors)
    developer_yaml = read(ROOT / "common" / "addon" / "developer_features.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)

    if feature_source:
        require_contains(screen_docs, feature_source, "docs/screen-settings.md", errors)
    if rotation_behavior:
        require_contains(screen_docs, rotation_behavior, "docs/screen-settings.md", errors)
    if developer_behavior:
        require_contains(screen_docs, developer_behavior, "docs/screen-settings.md", errors)
        require_contains(web_template, "S.developer_features_enabled", rel(WEB_TEMPLATE), errors)
    for option in user_options + developer_options:
        require_contains(rotation_yaml, f'      - "{option}"', "common/addon/screen_rotation.yaml", errors)
        require_contains(web_template, option, rel(WEB_TEMPLATE), errors)
    for option in user_options:
        require_contains(screen_docs, f"{option}", "docs/screen-settings.md", errors)
    for option in developer_options:
        require_contains(screen_docs, option, "docs/screen-settings.md", errors)
    if isinstance(native_mapping, dict):
        for user_option, native_option in native_mapping.items():
            if not isinstance(user_option, str) or not isinstance(native_option, str):
                continue
            marker = f'screen_rotation_{user_option}: "{native_option}"'
            for device in product["devices"]:
                package_yaml = str(device.get("package_yaml", "")).strip()
                if package_yaml:
                    require_contains(read(ROOT / package_yaml, errors), marker, package_yaml, errors)
            require_contains(rotation_yaml, f"${{screen_rotation_{user_option}}}", "common/addon/screen_rotation.yaml", errors)
    for needle in (
        "Screen: Rotation",
        'initial_option: "${screen_rotation}"',
        "screen_apply_rotation",
        "developer_features_enabled",
        "lvgl.display.set_rotation",
        "portrait_pairing_enabled",
        "immich_reapply_current_image_layout",
    ):
        require_contains(rotation_yaml, needle, "common/addon/screen_rotation.yaml", errors)
    for needle in (
        "Developer: Features",
        "RESTORE_DEFAULT_OFF",
        "developer_features_saved",
        "script.execute: screen_apply_rotation",
    ):
        require_contains(developer_yaml, needle, "common/addon/developer_features.yaml", errors)
    for needle in (
        "developerPanelEnabledByUrl",
        'params.get("developer")',
        'params.get("dev")',
        "experimental",
        "screenRotationOptionsForUi",
        "productSettingOptions(\"screen_rotation\", S.developer_features_enabled)",
        "isPortraitScreenRotation",
        "Enable in-development features",
        'post(endpoints.screen_rotation + "/set"',
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in ("Rotation", "0 degrees"):
        require_contains(backup_docs + screen_docs, needle, "screen rotation docs", errors)


def check_developer_features_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    query_params = [
        str(value).strip() for value in project.get("developer_features_query_params", []) if str(value).strip()
    ]
    query_value = str(project.get("developer_features_query_value", "")).strip()
    label = str(project.get("developer_features_label", "")).strip()
    entity = str(project.get("developer_features_entity", "")).strip()
    guard = str(project.get("developer_features_guard", "")).strip()
    persistence = str(project.get("developer_features_persistence", "")).strip()

    readme = read(ROOT / "README.md", errors)
    developer_yaml = read(ROOT / "common" / "addon" / "developer_features.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)

    if query_value:
        require_contains(readme, f"?developer={query_value}", "README.md", errors)
        for text, label_name in ((web_template, rel(WEB_TEMPLATE)), (web_text, rel(WEB_APP))):
            require_contains(text, f'=== "{query_value}"', label_name, errors)
    for param in query_params:
        for text, label_name in ((web_template, rel(WEB_TEMPLATE)), (web_text, rel(WEB_APP))):
            require_contains(text, f'params.get("{param}")', label_name, errors)
    if label:
        require_contains(web_template, label, rel(WEB_TEMPLATE), errors)
        require_contains(web_text, label, rel(WEB_APP), errors)
    if entity:
        require_contains(developer_yaml, f'name: "{entity}"', "common/addon/developer_features.yaml", errors)
        require_contains(web_text, f'"entity":"switch/{entity}"', rel(WEB_APP), errors)
    if guard:
        require_contains(readme, guard, "README.md", errors)
    if persistence:
        require_contains(readme, persistence, "README.md", errors)
    for needle in (
        "hidden developer setting",
        "must stay off",
        "RESTORE_DEFAULT_OFF",
        "initial_value: 'false'",
        "internal: true",
        "developer_features_saved",
        "developerPanelEnabledByUrl",
        "S.developer_features_enabled",
        "post(endpoints.developer_features_enabled",
    ):
        if needle in {"hidden developer setting", "must stay off"}:
            require_contains(readme, needle, "README.md", errors)
        elif needle in {"RESTORE_DEFAULT_OFF", "initial_value: 'false'", "internal: true", "developer_features_saved"}:
            require_contains(developer_yaml, needle, "common/addon/developer_features.yaml", errors)
        else:
            require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
            require_contains(web_text, needle, rel(WEB_APP), errors)


def check_screen_tone_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    base_purpose = str(project.get("screen_tone_base_purpose", "")).strip()
    night_timing = str(project.get("screen_tone_night_timing", "")).strip()
    night_recovery = str(project.get("screen_tone_night_recovery", "")).strip()
    override_duration = str(project.get("screen_tone_override_duration", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    screen_tone_docs = read(ROOT / "docs" / "screen-tone.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    warm_tones_yaml = read(ROOT / "common" / "addon" / "warm_tones.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)

    if base_purpose:
        require_contains(screen_tone_docs, base_purpose, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, base_purpose, "common/addon/warm_tones.yaml", errors)
    for needle in ("blue cast", "Warmer", "less blue cast"):
        require_contains(screen_tone_docs, needle, "docs/screen-tone.md", errors)
    if night_timing:
        require_contains(screen_tone_docs, night_timing, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, night_timing, "common/addon/warm_tones.yaml", errors)
    if night_recovery:
        require_contains(screen_tone_docs, night_recovery, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, night_recovery, "common/addon/warm_tones.yaml", errors)
    if override_duration:
        require_contains(screen_tone_docs, override_duration, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, override_duration, "common/addon/warm_tones.yaml", errors)
    for needle in (
        "Night Tone",
        "sunset and sunrise",
        "Display Tone Adjustment",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("warm up a panel that looks too blue", "softer night tone after sunset"):
        require_contains(readme, needle, "README.md", errors)
    require_contains(backup_docs, "Screen Tone", "docs/backup.md", errors)
    for needle in (
        "Screen: Tone Adjustment",
        "Screen: Night Tone Adjustment",
        "Screen: Warm Tone Override",
        "Screen: Display Tone",
        "Screen: Warm Tone Intensity",
        "base_tone_enabled",
        "warm_tones_enabled",
        "warm_tone_override",
        "apply_warm_tones",
    ):
        require_contains(warm_tones_yaml, needle, "common/addon/warm_tones.yaml", errors)
    for needle in (
        "Night Tone Adjustment",
        "Turn on until sunrise",
        "base_tone",
        "warm_tone_intensity",
        "warm_tone_override",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    require_contains(slideshow_yaml, "accent + warm tones", "common/addon/immich_slideshow.yaml", errors)


def check_clock_time_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    clock_default_format = str(project.get("clock_default_format", "")).strip()
    clock_format_options = [str(value).strip() for value in project.get("clock_format_options", []) if str(value).strip()]
    clock_default_timezone = str(project.get("clock_default_timezone", "")).strip()
    clock_default_show = project.get("clock_default_show")
    clock_update_interval = str(project.get("clock_update_interval", "")).strip()
    ntp_default_servers = [str(value).strip() for value in project.get("ntp_default_servers", []) if str(value).strip()]
    ntp_server_length_limit = project.get("ntp_server_length_limit")
    timezone_effects = [str(value).strip() for value in project.get("timezone_change_effects", []) if str(value).strip()]

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    clock_format_setting = settings_by_key.get("clock_format")
    if not clock_format_setting:
        errors.append("product settings must include clock_format")
    else:
        if clock_default_format and clock_format_setting.get("default") != clock_default_format:
            errors.append("project.clock_default_format must match the clock_format setting default")
        if clock_format_options and clock_format_setting.get("options") != clock_format_options:
            errors.append("project.clock_format_options must match the clock_format setting options")

    static_entities = web_static_entities(product)
    static_timezone_default = static_entities.get("timezone", {}).get("default")
    if clock_default_timezone and static_timezone_default != clock_default_timezone:
        errors.append("project.clock_default_timezone must match the static web timezone default")
    if isinstance(clock_default_show, bool) and static_entities.get("show_clock", {}).get("default") != clock_default_show:
        errors.append("project.clock_default_show must match the static web show_clock default")
    static_ntp_defaults = [
        str(static_entities.get(key, {}).get("default", ""))
        for key in ("ntp_server_1", "ntp_server_2", "ntp_server_3")
    ]
    if ntp_default_servers and static_ntp_defaults != ntp_default_servers:
        errors.append("project.ntp_default_servers must match the static web NTP defaults")

    install_docs = read(ROOT / "docs" / "install.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    time_yaml = read(TIME_YAML, errors)
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)

    for needle in (
        clock_default_format,
        clock_default_timezone,
        clock_update_interval,
        "shows the clock by default",
        "sunrise/sunset based brightness and night tone",
    ):
        if needle:
            require_contains(install_docs, needle, "docs/install.md", errors)
    for server in ntp_default_servers:
        require_contains(install_docs, server, "docs/install.md", errors)

    for needle in ("Clock Overlay", "current time"):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Show clock", "format", "timezone"):
        require_contains(backup_docs, needle, "docs/backup.md", errors)

    for option in clock_format_options:
        require_contains(time_yaml, f'      - "{option}"', rel(TIME_YAML), errors)
    if clock_default_format:
        require_contains(time_yaml, f'initial_option: "{clock_default_format}"', rel(TIME_YAML), errors)
    if clock_default_timezone:
        require_contains(time_yaml, f'initial_option: "{clock_default_timezone}"', rel(TIME_YAML), errors)
        timezone_id = clock_default_timezone.split(" (", 1)[0]
        require_contains(time_yaml, f'timezone: "{timezone_id}"', rel(TIME_YAML), errors)
    for index, server in enumerate(ntp_default_servers, start=1):
        key = f"ntp_server_{index}"
        require_contains(time_yaml, f'  {key}: "{server}"', rel(TIME_YAML), errors)
        require_contains(time_yaml, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)
        require_firmware_text_entity_shape(time_yaml, f"Clock: NTP Server {index}", rel(TIME_YAML), errors)
        require_contains(
            web_template,
            f'{{ key: "{key}", placeholder: "{server}", label: "NTP Server {index}" }}',
            rel(WEB_TEMPLATE),
            errors,
        )
    if isinstance(ntp_server_length_limit, int) and not isinstance(ntp_server_length_limit, bool):
        if time_yaml.count(f"max_length: {ntp_server_length_limit}") < len(ntp_default_servers):
            errors.append(f"{rel(TIME_YAML)} must use project.ntp_server_length_limit for all NTP server text fields")
        require_contains(web_template, f"MAX_NTP_SERVER_LENGTH = {ntp_server_length_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_NTP_SERVER_LENGTH = {ntp_server_length_limit}", rel(WEB_APP), errors)
    if clock_default_show is True:
        require_contains(time_yaml, "restore_mode: RESTORE_DEFAULT_ON", rel(TIME_YAML), errors)
    elif clock_default_show is False:
        require_contains(time_yaml, "restore_mode: RESTORE_DEFAULT_OFF", rel(TIME_YAML), errors)
    if clock_update_interval:
        require_contains(time_yaml, clock_update_interval, rel(TIME_YAML), errors)
    if clock_update_interval == "60 seconds":
        require_contains(time_yaml, "interval: 60s", rel(TIME_YAML), errors)

    effect_markers = {
        "updates clock display": "script.execute: update_clock_display",
        "recalculates sunrise/sunset": "script.execute: backlight_recalc_sunrise_sunset",
        "checks screen schedule": "script.execute: screen_schedule_check",
    }
    for effect in timezone_effects:
        marker = effect_markers.get(effect)
        if marker:
            require_contains(time_yaml, marker, rel(TIME_YAML), errors)
        else:
            errors.append(f"project.timezone_change_effects has no checker mapping for {effect!r}")

    for needle in (
        "Clock: Format",
        "Clock: Timezone",
        "Clock: Show",
        "Clock: NTP Server 1",
        "Clock: NTP Server 2",
        "Clock: NTP Server 3",
        "apply_ntp_servers",
        "update_clock_display",
    ):
        require_contains(time_yaml, needle, rel(TIME_YAML), errors)
    for needle in (
        "Clock & timezone",
        "Clock Format",
        "Timezone",
        "NTP Servers",
        "Show Clock",
        "ntp_server_1",
        "ntp_server_2",
        "ntp_server_3",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)


def check_photo_source_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    photo_source_modes = [str(value).strip() for value in project.get("photo_source_modes", []) if str(value).strip()]
    auto_apply_behavior = str(project.get("photo_source_auto_apply_behavior", "")).strip()
    id_limit = project.get("photo_source_id_limit")
    memories_window = str(project.get("photo_source_memories_window", "")).strip()
    memories_fallback = str(project.get("photo_source_memories_fallback", "")).strip()
    album_person_sampling = str(project.get("photo_source_album_person_sampling", "")).strip()

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    photo_source_setting = settings_by_key.get("photo_source")
    if not photo_source_setting:
        errors.append("product settings must include photo_source")
    else:
        if photo_source_modes and photo_source_setting.get("options") != photo_source_modes:
            errors.append("project.photo_source_modes must match the photo_source setting options")
        if photo_source_setting.get("default") != "All Photos":
            errors.append("photo_source default must be All Photos")

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    filter_yaml = read(ROOT / "common" / "addon" / "immich_filter.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    web_template = read(WEB_TEMPLATE, errors)

    if auto_apply_behavior:
        require_contains(photo_docs, auto_apply_behavior, "docs/photo-sources.md", errors)
    if memories_window:
        require_contains(photo_docs, memories_window, "docs/photo-sources.md", errors)
    if memories_fallback:
        require_contains(photo_docs, memories_fallback, "docs/photo-sources.md", errors)
        require_contains(api_yaml, "falling back to random", "common/addon/immich_api.yaml", errors)
    if album_person_sampling:
        require_contains(photo_docs, album_person_sampling, "docs/photo-sources.md", errors)
        require_contains(api_yaml, "paged metadata search", "common/addon/immich_api.yaml", errors)
    if isinstance(id_limit, int):
        id_limit_text = str(id_limit)
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("docs/backup.md", backup_docs),
            (rel(WEB_TEMPLATE), web_template),
        ):
            require_contains(text, id_limit_text, label, errors)
        if filter_yaml.count(f"max_length: {id_limit}") < 4:
            errors.append("common/addon/immich_filter.yaml must use project.photo_source_id_limit for album/person ID and label fields")

    for mode in photo_source_modes:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("common/addon/immich_filter.yaml", filter_yaml),
        ):
            require_contains(text, mode, label, errors)
    for needle in (
        "whole library",
        "favorites",
        "specific albums",
        "specific people",
        '"on this day" memories',
        "chosen date range",
    ):
        require_contains(readme, needle, "README.md", errors)
    for needle in (
        "Photo Sources",
        "favorites only",
        "specific albums",
        "specific people",
        '"on this day" memories',
        "date range",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Album IDs", "Album Labels", "Person IDs", "Person Labels"):
        require_contains(backup_docs, needle, "docs/backup.md", errors)

    for needle in (
        "Photos: Source",
        'initial_option: "All Photos"',
        "auto_apply_photo_source",
        "delay: 1s",
        "Photos: Album IDs",
        "Photos: Person IDs",
        "Apply Photo Source",
    ):
        require_contains(filter_yaml, needle, "common/addon/immich_filter.yaml", errors)
    for name in ("Photos: Album IDs", "Photos: Album Labels", "Photos: Person IDs", "Photos: Person Labels"):
        require_firmware_text_entity_shape(filter_yaml, name, "common/addon/immich_filter.yaml", errors)
    for needle in (
        "immich_memory_window_offset",
        "id(immich_memory_window_offset) = -2",
        "id(immich_memory_window_offset) <= 2",
        "build_immich_search_body",
        "pick_one_uuid_from_csv",
        "pick_one_person_id_for_random_search",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    require_contains(api_key_docs, "memory.read", "docs/api-key.md", errors)
    for needle in (
        "MAX_PHOTO_ID_FIELD_LENGTH",
        "schedulePhotoSourceApply",
        "Add an album",
        "Add a person",
        "Paste album ID from Immich URL",
        "Paste person ID from Immich URL",
        "Album IDs exceed 255 characters",
        "Person IDs exceed 255 characters",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)


def check_connection_resilience_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    slideshow_check_interval = str(project.get("slideshow_check_interval", "")).strip()
    slideshow_default_seconds = project.get("slideshow_interval_default_seconds")
    slideshow_range_seconds = project.get("slideshow_interval_range_seconds", [])
    timeout_default = str(project.get("connection_timeout_default", "")).strip()
    timeout_default_seconds = project.get("connection_timeout_default_seconds")
    timeout_range_seconds = project.get("connection_timeout_range_seconds", [])
    timeout_range = str(project.get("connection_timeout_range", "")).strip()
    failure_trigger = str(project.get("connection_failure_trigger", "")).strip()
    invalid_key_title = str(project.get("connection_invalid_api_key_title", "")).strip()
    unavailable_title = str(project.get("connection_unavailable_title", "")).strip()
    max_error_retries = project.get("immich_max_error_retries")
    retry_delays = project.get("immich_api_retry_delay_ms", [])
    retryable_statuses = [str(value).strip() for value in project.get("immich_retryable_http_statuses", []) if str(value).strip()]
    auth_error_status = project.get("immich_auth_error_status")

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    connection_timeout_setting = settings_by_key.get("conn_timeout")
    slideshow_interval_setting = settings_by_key.get("interval")
    if not slideshow_interval_setting:
        errors.append("product settings must include interval")
    elif isinstance(slideshow_default_seconds, int) and not isinstance(slideshow_default_seconds, bool):
        expected_default = f"{slideshow_default_seconds} seconds"
        if slideshow_interval_setting.get("default") != expected_default:
            errors.append("project.slideshow_interval_default_seconds must match interval default")
    if not connection_timeout_setting:
        errors.append("product settings must include conn_timeout")
    else:
        if timeout_default and connection_timeout_setting.get("default") != timeout_default:
            errors.append("project.connection_timeout_default must match conn_timeout default")
        if "30 seconds" not in connection_timeout_setting.get("options", []):
            errors.append("conn_timeout options must include 30 seconds")
        if "30 minutes" not in connection_timeout_setting.get("options", []):
            errors.append("conn_timeout options must include 30 minutes")

    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    screen_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_slideshow.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    immich_config_yaml = read(ROOT / "common" / "addon" / "immich_config.yaml", errors)
    helper_header = read(ROOT / "components" / "espframe" / "espframe_helpers.h", errors)
    helper_tests = read(ROOT / "tests" / "espframe_helper_tests.cpp", errors)
    web_template = read(WEB_TEMPLATE, errors)

    for needle in (timeout_default, timeout_range, failure_trigger, invalid_key_title, unavailable_title):
        if needle:
            require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    for needle in ("Connection Timeout", "connection-failed screen", "slow server", "large photo library"):
        require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    for needle in ("Immich Connection Problems", "API Key Problems", "Photos Do Not Appear"):
        require_contains(troubleshooting_docs, needle, "docs/troubleshooting.md", errors)
    require_contains(home_assistant_docs, "Screen: Connection Timeout", "docs/home-assistant.md", errors)
    require_contains(backup_docs, f'"conn_timeout": "{timeout_default}"', "docs/backup.md", errors)

    for needle in (
        "Screen: Connection Timeout",
        f'initial_option: "{timeout_default}"',
        "connection_failed_overlay",
        "connection_failed_dim",
        "connection_failed_shift",
        invalid_key_title,
        unavailable_title,
        "Check your Immich API key",
        "Check your internet connection",
    ):
        if needle:
            require_contains(screen_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    for option in ("30 seconds", "30 minutes"):
        require_contains(screen_yaml, f'      - "{option}"', "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    if slideshow_check_interval:
        require_contains(screen_yaml, f'slideshow_check_interval: "{slideshow_check_interval}"', "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        require_contains(screen_yaml, f"interval: ${{slideshow_check_interval}}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    if isinstance(slideshow_default_seconds, int) and not isinstance(slideshow_default_seconds, bool):
        require_contains(screen_yaml, f"initial_value: '{slideshow_default_seconds}'", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        if isinstance(slideshow_range_seconds, list) and len(slideshow_range_seconds) == 2:
            require_contains(
                screen_yaml,
                f"parse_duration_option_seconds(x, {slideshow_default_seconds}, {slideshow_range_seconds[0]}, {slideshow_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )
            require_contains(
                screen_yaml,
                "parse_duration_option_seconds(\n                id(slideshow_interval).current_option(), "
                f"id(slideshow_interval_sec), {slideshow_range_seconds[0]}, {slideshow_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )
    if isinstance(timeout_default_seconds, int) and not isinstance(timeout_default_seconds, bool):
        require_contains(screen_yaml, f"initial_value: '{timeout_default_seconds}'", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        if isinstance(timeout_range_seconds, list) and len(timeout_range_seconds) == 2:
            require_contains(
                screen_yaml,
                f"parse_duration_option_seconds(x, {timeout_default_seconds}, {timeout_range_seconds[0]}, {timeout_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )

    if isinstance(max_error_retries, int) and not isinstance(max_error_retries, bool):
        require_contains(helper_header, f"MAX_ERROR_RETRIES = {max_error_retries}", "components/espframe/espframe_helpers.h", errors)
        for label, text in (
            ("common/addon/immich_api.yaml", api_yaml),
            ("common/addon/immich_slideshow.yaml", slideshow_yaml),
        ):
            require_contains(text, "MAX_ERROR_RETRIES", label, errors)
    if isinstance(retry_delays, list):
        for delay in retry_delays:
            if isinstance(delay, int) and not isinstance(delay, bool):
                require_contains(api_yaml, f"delay_ms = {delay}", "common/addon/immich_api.yaml", errors)
    for status in retryable_statuses:
        if status == "HTTP 5xx":
            require_contains(helper_header, "status >= 500", "components/espframe/espframe_helpers.h", errors)
        elif status == "429":
            require_contains(helper_header, "status == 429", "components/espframe/espframe_helpers.h", errors)
        else:
            errors.append(f"project.immich_retryable_http_statuses has no checker mapping for {status!r}")
    if isinstance(auth_error_status, int) and not isinstance(auth_error_status, bool):
        require_contains(helper_header, f"status == {auth_error_status}", "components/espframe/espframe_helpers.h", errors)
        require_contains(api_yaml, "is_http_auth_error(code)", "common/addon/immich_api.yaml", errors)
    for needle in (
        "immich_fetch_retry",
        "immich_register_fetch_failure",
        "Fetch paused (connection retry cooldown)",
        "Retrying Immich fetch",
        "api retries exhausted",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    for needle in (
        "immich_consecutive_failures",
        "immich_retry_cooldown_until_ms",
        "Download retries exhausted",
        "hide_connection_failed",
    ):
        require_contains(slideshow_yaml + screen_yaml, needle, "connection retry firmware", errors)
    for needle in (
        "Connection settings changed; retrying Immich connection",
        "connection_failed_overlay",
        "immich_api_retries",
        "immich_download_retries",
        "immich_consecutive_failures",
    ):
        require_contains(immich_config_yaml, needle, "common/addon/immich_config.yaml", errors)
    for needle in (
        "Connection Timeout",
        "productSettingOptions(\"conn_timeout\")",
        "conn_timeout",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "parse_duration_option_seconds",
        "20 minutes",
        "MAX_ERROR_RETRIES",
    ):
        require_contains(helper_tests + helper_header, needle, "connection helper tests", errors)


def check_setup_flow_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    captive_portal_ip = str(project.get("setup_captive_portal_ip", "")).strip()
    wizard_steps = [str(value).strip() for value in project.get("setup_wizard_steps", []) if str(value).strip()]
    connection_fields = [
        str(value).strip() for value in project.get("setup_required_connection_fields", []) if str(value).strip()
    ]
    skip_substitutions = [
        str(value).strip() for value in project.get("setup_skip_substitutions", []) if str(value).strip()
    ]
    ready_condition = str(project.get("setup_connection_ready_condition", "")).strip()
    setup_dim_delay = str(project.get("setup_screen_dim_delay", "")).strip()
    setup_dim_brightness = str(project.get("setup_screen_dim_brightness", "")).strip()
    setup_dim_transition = str(project.get("setup_screen_dim_transition", "")).strip()
    loading_backlight_brightness = str(project.get("setup_loading_backlight_brightness", "")).strip()
    loading_backlight_transition = str(project.get("setup_loading_backlight_transition", "")).strip()
    required_substitutions = [
        str(value).strip() for value in project.get("manual_setup_required_substitutions", []) if str(value).strip()
    ]
    wifi_secrets = [str(value).strip() for value in project.get("manual_setup_wifi_secrets", []) if str(value).strip()]
    package_ref = str(project.get("manual_setup_package_ref", "")).strip()
    package_refresh = str(project.get("manual_setup_package_refresh", "")).strip()

    install_docs = read(ROOT / "docs" / "install.md", errors)
    usb_docs = read(ROOT / "docs" / "usb-flashing.md", errors)
    manual_setup_docs = read(ROOT / "docs" / "manual-setup.md", errors)
    immich_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    web_template = read(WEB_TEMPLATE, errors)
    connectivity_yaml = read(ROOT / "common" / "addon" / "connectivity.yaml", errors)
    immich_config_yaml = read(ROOT / "common" / "addon" / "immich_config.yaml", errors)
    screen_loading_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_loading.yaml", errors)
    screen_wifi_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_wifi_setup.yaml", errors)
    packages_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "packages.yaml", errors)
    local_yamls = [
        (str(device.get("local_yaml", "")), read(ROOT / str(device.get("local_yaml", "")), errors))
        for device in product["devices"]
    ]

    for device in product["devices"]:
        esphome_name = str(device.get("esphome_name", "")).strip()
        if not esphome_name:
            continue
        require_contains(install_docs, esphome_name, "docs/install.md", errors)
        require_contains(usb_docs, esphome_name, "docs/usb-flashing.md", errors)
        require_contains(read(ROOT / str(device.get("build_yaml", "")), errors), esphome_name, str(device.get("build_yaml", "")), errors)

    if captive_portal_ip:
        for label, text in (
            ("docs/usb-flashing.md", usb_docs),
            ("devices/guition-esp32-p4-jc8012p4a1/packages.yaml", packages_yaml),
        ):
            require_contains(text, captive_portal_ip, label, errors)
        for label, text in (
            ("devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", screen_loading_yaml),
            ("devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", screen_wifi_yaml),
        ):
            require_contains(text, "${captive_portal_ip}", label, errors)

    for step in wizard_steps:
        require_contains(web_template, step, rel(WEB_TEMPLATE), errors)
    for field in connection_fields:
        require_contains(web_template, field, rel(WEB_TEMPLATE), errors)
        require_contains(install_docs, field, "docs/install.md", errors)
    for substitution in skip_substitutions:
        require_contains(manual_setup_docs, substitution, "docs/manual-setup.md", errors)
        require_contains(immich_config_yaml, substitution, "common/addon/immich_config.yaml", errors)
    if ready_condition:
        require_contains(manual_setup_docs, ready_condition, "docs/manual-setup.md", errors)
    for substitution in required_substitutions:
        require_contains(manual_setup_docs, f"`{substitution}`", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"{substitution}:", label or "device local ESPHome YAML", errors)
    for secret in wifi_secrets:
        require_contains(manual_setup_docs, secret, "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"!secret {secret}", label or "device local ESPHome YAML", errors)
    if package_ref:
        require_contains(manual_setup_docs, f"ref: {package_ref}", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"ref: {package_ref}", label or "device local ESPHome YAML", errors)
    if package_refresh:
        require_contains(manual_setup_docs, f"refresh: {package_refresh}", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"refresh: {package_refresh}", label or "device local ESPHome YAML", errors)
    for needle in ("WiFi", "Immich server URL", "Immich API key"):
        require_contains(immich_frame_docs, needle, "docs/immich-photo-frame.md", errors)

    for needle in (
        "captive_portal:",
        'ssid: "${name}"',
        "wifi.connected",
        "is_valid_http_url(id(immich_url).state)",
        "!id(immich_api_key_text).state.empty()",
        "immich_setup_page",
        "wifi_setup_page",
    ):
        require_contains(connectivity_yaml, needle, "common/addon/connectivity.yaml", errors)
    for needle in (
        "normalize_immich_base_url",
        "Connection: Server URL",
        "Connection: API Key",
        'initial_value: "${immich_base_url}"',
        'initial_value: "${immich_api_key}"',
        "setup_screen_dim",
        "immich_check_config_ready",
        "slideshow_page",
    ):
        require_contains(immich_config_yaml, needle, "common/addon/immich_config.yaml", errors)
    for needle in (
        "Connect to the WiFi hotspot",
        "to configure your network",
        "Then visit ${captive_portal_ip}",
    ):
        require_contains(screen_loading_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
        require_contains(screen_wifi_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_delay:
        require_contains(screen_wifi_yaml, f"delay: {setup_dim_delay}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_brightness:
        require_contains(screen_wifi_yaml, f"brightness: {setup_dim_brightness}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_transition:
        require_contains(screen_wifi_yaml, f"transition_length: {setup_dim_transition}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if loading_backlight_brightness:
        require_contains(screen_loading_yaml, f"brightness: {loading_backlight_brightness}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
    if loading_backlight_transition:
        require_contains(screen_loading_yaml, f"transition_length: {loading_backlight_transition}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
    for needle in (
        "id: boot_grace_period",
        "lv_bar_set_value(id(loading_progress_bar), 25, LV_ANIM_OFF)",
        'lv_label_set_text(id(loading_status_label), "Initializing display")',
        "lvgl.page.show: wifi_setup_page",
        "script.execute: setup_screen_dim",
        "script.stop: setup_screen_dim",
        "script.execute: backlight_apply_brightness",
    ):
        require_contains(screen_loading_yaml + screen_wifi_yaml, needle, "setup screen firmware", errors)
    for needle in (
        "renderWizard",
        "connect your photo frame",
        "saveConnectionValue(endpoints.immich_url",
        "saveConnectionValue(endpoints.api_key",
        "safeGet(endpoints.immich_url)",
        "safeGet(endpoints.api_key)",
        "Done",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)


def check_photo_display_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    date_filter_modes = [str(value).strip() for value in project.get("date_filter_modes", []) if str(value).strip()]
    date_filter_relative_anchor = str(project.get("date_filter_relative_anchor", "")).strip()
    date_filter_time_source = str(project.get("date_filter_time_source", "")).strip()
    portrait_pairing_behavior = str(project.get("portrait_pairing_behavior", "")).strip()
    portrait_pairing_rotation_behavior = str(project.get("portrait_pairing_rotation_behavior", "")).strip()
    metadata_overlay_fields = [
        str(value).strip() for value in project.get("metadata_overlay_fields", []) if str(value).strip()
    ]

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    date_filter_setting = settings_by_key.get("date_filter_mode")
    if not date_filter_setting:
        errors.append("product settings must include date_filter_mode")
    elif date_filter_modes and date_filter_setting.get("options") != date_filter_modes:
        errors.append("project.date_filter_modes must match the date_filter_mode setting options")

    for key in ("photo_metadata_date_enabled", "photo_metadata_location_enabled", "portrait_pairing"):
        setting = settings_by_key.get(key)
        if not setting:
            errors.append(f"product settings must include {key}")
        elif setting.get("default") is not True:
            errors.append(f"{key} default must be true")

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    web_template = read(WEB_TEMPLATE, errors)
    filter_yaml = read(ROOT / "common" / "addon" / "immich_filter.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    helper_header = read(ROOT / "components" / "espframe" / "immich_helpers.h", errors)
    helper_tests = read(ROOT / "tests" / "espframe_helper_tests.cpp", errors)

    for mode in date_filter_modes:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("common/addon/immich_filter.yaml", filter_yaml),
            (rel(WEB_TEMPLATE), web_template),
            ("tests/espframe_helper_tests.cpp", helper_tests),
        ):
            require_contains(text, mode, label, errors)
    if date_filter_relative_anchor:
        require_contains(photo_docs, date_filter_relative_anchor, "docs/photo-sources.md", errors)
    if date_filter_time_source:
        require_contains(photo_docs, date_filter_time_source, "docs/photo-sources.md", errors)
    for needle in (
        "resolve_immich_date_filter",
        "build_immich_date_filter_extra",
        "build_immich_companion_date_filter_extra",
        "relative_skipped_for_invalid_time",
    ):
        require_contains(helper_header, needle, "components/espframe/immich_helpers.h", errors)
        require_contains(helper_tests, needle, "tests/espframe_helper_tests.cpp", errors)
    for needle in (
        "resolve_immich_date_filter",
        "build_immich_date_filter_extra",
        "Relative date filter skipped",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    for needle in (
        "Filter by Date",
        "From",
        "Until",
        "isValidDate",
        "scheduleFilterApply",
        "Relative Range",
        "Fixed Range",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for key in ("date_from", "date_to"):
        date_setting = settings_by_key.get(key, {})
        date_format = str(date_setting.get("docs_format", "")).strip().strip("`")
        if date_format:
            require_contains(web_template, f'placeholder = "{date_format}"', rel(WEB_TEMPLATE), errors)
            require_contains(web_template, f"Invalid date — use {date_format}", rel(WEB_TEMPLATE), errors)
            require_contains(filter_yaml, f"name: \"{date_setting.get('entity', {}).get('name', '')}\"", "common/addon/immich_filter.yaml", errors)
    date_formats = {
        str(settings_by_key.get(key, {}).get("docs_format", "")).strip().strip("`")
        for key in ("date_from", "date_to")
    }
    date_formats.discard("")
    if len(date_formats) == 1:
        date_length = len(next(iter(date_formats)))
        if filter_yaml.count(f"max_length: {date_length}") < 2:
            errors.append("common/addon/immich_filter.yaml must use date setting format length for date_from and date_to")

    if portrait_pairing_behavior:
        require_contains(readme, portrait_pairing_behavior, "README.md", errors)
    for needle in ("Portrait Pairing", "portrait photos taken on the same day", "side-by-side"):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Portrait Pairing", "side-by-side", "landscape screens"):
        require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    if portrait_pairing_rotation_behavior:
        require_contains(photo_docs, portrait_pairing_rotation_behavior, "docs/photo-sources.md", errors)
        require_contains(web_template, portrait_pairing_rotation_behavior, rel(WEB_TEMPLATE), errors)
    for needle in (
        "Photos: Portrait Pairing",
        "companion portrait",
        "same day",
        "immich_fetch_portrait_companion",
        "build_immich_companion_date_filter_extra",
        "find_immich_portrait_companion_url",
    ):
        require_contains(api_yaml + slideshow_yaml, needle, "portrait pairing firmware", errors)
    for needle in ("Portrait Pairing", "isPortraitScreenRotation", "portrait_pairing"):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "test_slideshow_component_portrait_flow",
        "test_slideshow_component_companion_result_flow",
        "on_companion_found",
    ):
        require_contains(helper_tests, needle, "tests/espframe_helper_tests.cpp", errors)

    for field in metadata_overlay_fields:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            (rel(WEB_TEMPLATE), web_template),
            ("common/addon/immich_slideshow.yaml", slideshow_yaml),
        ):
            require_contains(text, field, label, errors)
    for needle in (
        "Metadata",
        "Date Format",
        "Date Taken Format",
        "Relative Date",
        "Date Taken",
        "photo_metadata_date_enabled",
        "photo_metadata_location_enabled",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "Device: Metadata Date",
        "Device: Metadata Location",
        "Device: Metadata Date Format",
        "Device: Metadata Date Taken Format",
        "update_photo_metadata_display",
        "location",
        "localDateTime",
    ):
        require_contains(slideshow_yaml + helper_header, needle, "metadata overlay firmware", errors)


def check_public_manifest_urls(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    if not base_url.startswith("https://"):
        errors.append("project.public_base_url must be an https URL")

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        urls = urls_by_slug.get(slug, {})
        for label, field in (("stable", "public_manifest"), ("beta", "public_beta_manifest")):
            path = str(device.get(field, "")).strip()
            if not path or path.startswith("/") or ".." in Path(path).parts:
                errors.append(f"Device {slug} has invalid {field}: {path}")
            url = urls.get(label, "")
            if not url.startswith(f"{base_url}/"):
                errors.append(f"Device {slug} {field} URL must be under project.public_base_url")

    default_urls = default_public_manifest_urls(product)
    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    for label, url in default_urls.items():
        if not url.startswith("https://"):
            errors.append(f"Default {label} firmware manifest URL must be an https URL")
        for filename, text in (
            ("common/addon/firmware_update.yaml", firmware_update),
            ("docs/backup.md", backup_docs),
        ):
            require_contains(text, url, filename, errors)
    require_contains(docs_workflow, f'--base-url "{base_url}"', ".github/workflows/docs.yml", errors)

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not package_yaml:
            continue
        package_text = read(ROOT / package_yaml, errors)
        for url in urls_by_slug.get(slug, {}).values():
            require_contains(package_text, url, package_yaml, errors)


def check_public_site_references(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    install_url = public_url("install", product)
    web_app_path = str(product["project"].get("web_server_public_app_path", "")).strip()
    web_app_url = public_url(web_app_path, product) if web_app_path else ""
    project_name = str(product["project"].get("name", "")).strip()
    social_image_alt = str(product["project"].get("social_image_alt", "")).strip()
    usb_flashing_image = str(product["project"].get("usb_flashing_image", "")).strip()
    usb_flashing_image_alt = str(product["project"].get("usb_flashing_image_alt", "")).strip()
    web_installer_required_browsers = product["project"].get("web_installer_required_browsers", [])
    web_installer_required_api = str(product["project"].get("web_installer_required_api", "")).strip()
    web_installer_unsupported_browsers = product["project"].get("web_installer_unsupported_browsers", [])
    repository_url = str(product["project"].get("repository_url", "")).strip().rstrip("/")
    default_branch = str(product["project"].get("github_default_branch", "")).strip()
    support_url = str(product["project"].get("support_url", "")).strip()
    support_button_image_url = str(product["project"].get("support_button_image_url", "")).strip()
    esphome_config_mount = str(product["project"].get("esphome_config_mount", "")).strip()

    robots = read(ROOT / "docs" / "public" / "robots.txt", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)
    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    install_docs = read(ROOT / "docs" / "install.md", errors)
    manual_setup = read(ROOT / "docs" / "manual-setup.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    roadmap = read(ROOT / "docs" / "roadmap.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    usb_flashing_docs = read(ROOT / "docs" / "usb-flashing.md", errors)
    release_changelog = read(ROOT / "scripts" / "release_changelog.py", errors)

    require_contains(robots, f"Sitemap: {public_url('sitemap.xml', product)}", "docs/public/robots.txt", errors)

    if project_name:
        require_contains(ai_txt, f"name: {project_name}", "docs/public/ai.txt", errors)
        require_contains(ai_txt, f'attribute to "{project_name}"', "docs/public/ai.txt", errors)
    if repository_url:
        require_contains(ai_txt, f"Source and issues: {repository_url}", "docs/public/ai.txt", errors)
        require_contains(manual_setup, f"url: {repository_url}", "docs/manual-setup.md", errors)
        if default_branch:
            require_contains(license_docs, f"({repository_url}/blob/{default_branch}/LICENSE)", "docs/license.md", errors)
        require_contains(roadmap, f"({repository_url}/issues)", "docs/roadmap.md", errors)
        require_contains(release_changelog, 'project_value("repository_url"', "scripts/release_changelog.py", errors)
    require_contains(ai_txt, f"url: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Prefer canonical URLs: {docs_url} and {install_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Documentation: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(readme, f"]({install_url})", "README.md installer link", errors)
    require_contains(readme, f"]({docs_url})", "README.md docs link", errors)
    require_contains(readme, base_url, "README.md public base URL", errors)
    for label, text in (("README.md", readme), ("docs/index.md", index_docs)):
        if support_url:
            require_contains(text, support_url, label, errors)
        if support_button_image_url:
            require_contains(text, support_button_image_url, label, errors)
    if social_image_alt:
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, f'alt="{social_image_alt}"', label, errors)
    if usb_flashing_image:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'src="/{usb_flashing_image}"', label, errors)
    if usb_flashing_image_alt:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'alt="{usb_flashing_image_alt}"', label, errors)
    if isinstance(web_installer_required_browsers, list):
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            for browser in web_installer_required_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)
    web_installer_computer_requirement = str(product["project"].get("web_installer_computer_requirement", "")).strip()
    if web_installer_computer_requirement:
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_computer_requirement, label, errors)
    if web_installer_required_api:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_required_api, label, errors)
    if isinstance(web_installer_unsupported_browsers, list):
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            for browser in web_installer_unsupported_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)
    usb_cable_requirement = str(product["project"].get("usb_cable_requirement", "")).strip()
    usb_cable_warning = str(product["project"].get("usb_cable_warning", "")).strip()
    for field_name, value in (
        ("usb_cable_requirement", usb_cable_requirement),
        ("usb_cable_warning", usb_cable_warning),
    ):
        if not value:
            continue
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, value, f"{label} {field_name}", errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        esphome_name = str(device.get("esphome_name", "")).strip()
        friendly_name = str(device.get("friendly_name", "")).strip()
        model = str(device.get("model", "")).strip()
        model_code = model.split()[-1] if model else ""
        panel_url = str(device.get("panel_url", "")).strip()
        stand_url = str(device.get("stand_url", "")).strip()
        local_yaml = check_relative_path(device.get("local_yaml"), f"Device {slug} local_yaml", errors)
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/install.md", install_docs),
        ):
            if model_code:
                require_contains(text, model_code, label, errors)
            if panel_url:
                require_contains(text, panel_url, label, errors)
            if stand_url:
                require_contains(text, stand_url, label, errors)
        if esphome_name:
            for label, text in (
                ("docs/install.md", install_docs),
                ("docs/troubleshooting.md", troubleshooting_docs),
                ("docs/usb-flashing.md", usb_flashing_docs),
            ):
                require_contains(text, esphome_name, label, errors)
        if package_yaml:
            require_contains(manual_setup, f"files: [{package_yaml}]", "docs/manual-setup.md", errors)
        if build_yaml:
            if esphome_config_mount:
                require_contains(readme, f"compile {esphome_config_mount}/{build_yaml}", "README.md compile example", errors)
            build_text = read(ROOT / build_yaml, errors)
            if esphome_name:
                require_contains(build_text, f'name: "{esphome_name}"', build_yaml, errors)
            if friendly_name:
                require_contains(build_text, f'friendly_name: "{friendly_name}"', build_yaml, errors)
        if local_yaml and repository_url:
            local_text = read(ROOT / local_yaml, errors)
            if esphome_name:
                require_contains(local_text, f'name: "{esphome_name}"', local_yaml, errors)
            if friendly_name:
                require_contains(local_text, f'friendly_name: "{friendly_name}"', local_yaml, errors)
            require_contains(local_text, f"url: {repository_url}", local_yaml, errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, f'js_url: "{web_app_url}"', device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)


def check_docs_site_config(product: dict, errors: list[str]) -> None:
    config = read(ROOT / "docs" / ".vitepress" / "config.mts", errors)
    project = product["project"]
    project_name = str(project.get("name", "")).strip()
    site_description = str(project.get("site_description", "")).strip()
    ai_description = str(project.get("ai_description", "")).strip()
    social_image = str(project.get("social_image", "")).strip()
    social_image_alt = str(project.get("social_image_alt", "")).strip()
    favicon = str(project.get("favicon", "")).strip()
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    base_path = f"/{base_url.rstrip('/').rsplit('/', 1)[-1]}/"
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    default_branch = str(project.get("github_default_branch", "")).strip()
    owner_name = str(project.get("owner_name", "")).strip()
    owner_url = str(project.get("owner_url", "")).strip()

    if project_name:
        require_contains(config, f"title: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"content: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"name: '{project_name}'", "docs/.vitepress/config.mts", errors)
    if site_description:
        require_contains(config, f"description: '{site_description}'", "docs/.vitepress/config.mts", errors)
    if social_image:
        require_contains(config, f"content: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"image: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
    if social_image_alt:
        require_contains(config, f"content: '{social_image_alt}'", "docs/.vitepress/config.mts", errors)
    if favicon:
        require_contains(config, f"href: '{base_path}{favicon}'", "docs/.vitepress/config.mts", errors)
    if ai_description:
        require_contains(read(ROOT / "docs" / "public" / "ai.txt", errors), f"description: {ai_description}", "docs/public/ai.txt", errors)
    require_contains(config, f"const hostname = '{docs_url}'", "docs/.vitepress/config.mts", errors)
    require_contains(config, f"base: '{base_path}'", "docs/.vitepress/config.mts", errors)
    if repository_url:
        require_contains(config, f"link: '{repository_url}'", "docs/.vitepress/config.mts", errors)
        if default_branch:
            require_contains(config, f"pattern: '{repository_url}/edit/{default_branch}/docs/:path'", "docs/.vitepress/config.mts", errors)
    if owner_name:
        require_contains(config, f"name: '{owner_name}'", "docs/.vitepress/config.mts", errors)
    if owner_url:
        require_contains(config, f"url: '{owner_url}'", "docs/.vitepress/config.mts", errors)


def check_device_workflow_contract(product: dict, errors: list[str]) -> None:
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    project = product["project"]
    release_actions = project.get("release_workflow_actions", {})
    artifact_prefix = str(project.get("release_artifact_prefix", "")).strip()
    release_build_output_dir = str(project.get("release_build_output_dir", "")).strip()
    release_publish_dir = str(project.get("release_publish_dir", "")).strip()
    release_uploaded_verify_dir = str(project.get("release_uploaded_verify_dir", "")).strip()
    release_source_factory_binary = str(project.get("release_source_factory_binary", "")).strip()
    release_source_ota_binary = str(project.get("release_source_ota_binary", "")).strip()
    release_esphome_cache_dir = str(project.get("release_esphome_cache_dir", "")).strip()
    release_esphome_cache_key_prefix = str(project.get("release_esphome_cache_key_prefix", "")).strip()
    release_esphome_cache_hash_files = [
        str(path).strip() for path in project.get("release_esphome_cache_hash_files", []) if str(path).strip()
    ]
    asset_suffixes = [str(value).strip() for value in project.get("release_asset_suffixes", []) if str(value).strip()]
    binary_download_patterns = [
        str(value).strip() for value in project.get("release_binary_download_patterns", []) if str(value).strip()
    ]
    manifest_download_patterns = [
        str(value).strip() for value in project.get("release_manifest_download_patterns", []) if str(value).strip()
    ]
    uploaded_verify_patterns = [
        str(value).strip() for value in project.get("release_uploaded_verify_patterns", []) if str(value).strip()
    ]
    release_download_clobber = project.get("github_release_download_clobber")
    release_upload_clobber = project.get("github_release_upload_clobber")
    release_version_pattern = str(project.get("release_version_pattern", "")).strip()
    stable_release_version_pattern = str(project.get("stable_release_version_pattern", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).rstrip("\n")
    local_build_version = str(project.get("firmware_local_build_version", "")).strip()
    placeholder_versions = [str(value).strip() for value in project.get("firmware_placeholder_versions", []) if str(value).strip()]
    changelog_categories = project.get("release_changelog_categories", [])
    changelog_fallback = str(project.get("release_changelog_fallback_category", "")).strip()
    docs_dist_artifact_name = str(project.get("docs_dist_artifact_name", "")).strip()
    docs_firmware_artifact_name = str(project.get("docs_firmware_artifact_name", "")).strip()
    docs_dist_output_path = str(project.get("docs_dist_output_path", "")).strip()
    docs_deploy_path = str(project.get("docs_deploy_path", "")).strip()
    pages_environment = str(project.get("github_pages_environment", "")).strip()
    pages_concurrency_group = str(project.get("github_pages_concurrency_group", "")).strip()
    pages_cancel_in_progress = project.get("github_pages_cancel_in_progress")
    docs_workflow_success_conclusion = str(project.get("github_docs_workflow_run_success_conclusion", "")).strip()
    release_notes_fetch_depth = project.get("github_release_notes_fetch_depth")
    release_notes_fetch_tags = project.get("github_release_notes_fetch_tags")
    release_notes_version_ref = str(project.get("github_release_notes_version_ref", "")).strip()
    release_build_version_ref = str(project.get("github_release_build_version_ref", "")).strip()
    release_build_ref = str(project.get("github_release_build_ref", "")).strip()
    release_build_fail_fast = project.get("github_release_build_fail_fast")
    release_notes_output = str(project.get("github_release_notes_output", "")).strip()
    sparse_checkout_files = [
        str(path).strip() for path in project.get("github_sparse_checkout_files", []) if str(path).strip()
    ]
    sparse_checkout_cone_mode = project.get("github_sparse_checkout_cone_mode")
    docs_verify_retries = project.get("docs_firmware_verify_retries")
    docs_verify_delay = project.get("docs_firmware_verify_delay_seconds")
    docs_release_meta_step_id = str(project.get("github_docs_release_meta_step_id", "")).strip()
    docs_release_tag_env = str(project.get("github_docs_release_tag_env", "")).strip()
    docs_release_tag_output = str(project.get("github_docs_release_tag_output", "")).strip()
    docs_prerelease_tag_env = str(project.get("github_docs_prerelease_tag_env", "")).strip()
    pages_deployment_step_id = str(project.get("github_pages_deployment_step_id", "")).strip()
    pages_url_output = str(project.get("github_pages_url_output", "")).strip()
    prerelease_lookup_limit = project.get("github_prerelease_lookup_limit")
    actions_runner = str(project.get("github_actions_runner", "")).strip()
    github_cli_env = project.get("github_cli_env", {})
    firmware_compile_timeout = project.get("firmware_compile_timeout_minutes")
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    slugs = [str(device.get("slug", "")).strip() for device in product["devices"]]
    expected_slugs = " ".join(slugs)
    if actions_runner:
        for label, text in (
            (".github/workflows/release.yml", release_workflow),
            (".github/workflows/docs.yml", docs_workflow),
            (".github/workflows/compile.yml", compile_workflow),
        ):
            require_contains(text, f"runs-on: {actions_runner}", label, errors)
    if isinstance(release_notes_fetch_depth, int) and not isinstance(release_notes_fetch_depth, bool):
        require_contains(release_workflow, f"fetch-depth: {release_notes_fetch_depth}", ".github/workflows/release.yml", errors)
    if isinstance(release_notes_fetch_tags, bool):
        require_contains(
            release_workflow,
            f"fetch-tags: {str(release_notes_fetch_tags).lower()}",
            ".github/workflows/release.yml",
            errors,
        )
    if release_build_ref:
        require_contains(release_workflow, f"ref: {release_build_ref}", ".github/workflows/release.yml", errors)
    if isinstance(release_build_fail_fast, bool):
        require_contains(
            release_workflow,
            f"fail-fast: {str(release_build_fail_fast).lower()}",
            ".github/workflows/release.yml",
            errors,
        )
    if release_notes_version_ref:
        require_contains(release_workflow, f"VERSION: {release_notes_version_ref}", ".github/workflows/release.yml", errors)
    if release_build_version_ref:
        require_contains(release_workflow, f"VERSION: {release_build_version_ref}", ".github/workflows/release.yml", errors)
    if release_notes_output:
        for needle in (
            f'--output "{release_notes_output}"',
            f'--notes-file "{release_notes_output}"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    for label, text in (
        (".github/workflows/release.yml", release_workflow),
        (".github/workflows/docs.yml", docs_workflow),
    ):
        if isinstance(github_cli_env, dict):
            for raw_name, raw_value in github_cli_env.items():
                name = str(raw_name).strip()
                value = str(raw_value).strip()
                if name and value:
                    require_contains(text, f"{name}: {value}", label, errors)
        require_contains(text, f"DEVICE_SLUGS: {expected_slugs}", label, errors)
        if sparse_checkout_files:
            require_contains(text, "sparse-checkout: |", label, errors)
            for path in sparse_checkout_files:
                require_contains(text, f"            {path}", label, errors)
        if isinstance(sparse_checkout_cone_mode, bool):
            require_contains(text, f"sparse-checkout-cone-mode: {str(sparse_checkout_cone_mode).lower()}", label, errors)
    if isinstance(release_actions, dict):
        for action in release_actions.values():
            if not isinstance(action, str) or not action.strip():
                continue
            if (
                "download-artifact" in action
                or "upload-artifact" in action
                or "cache" in action
                or "upload-pages-artifact" in action
                or "deploy-pages" in action
                or "setup-node" in action
            ):
                if "upload-pages-artifact" in action or "deploy-pages" in action or "setup-node" in action:
                    require_contains(docs_workflow, action.strip(), ".github/workflows/docs.yml", errors)
                    continue
                require_contains(release_workflow, action.strip(), ".github/workflows/release.yml", errors)
            elif "checkout" in action:
                for label, text in (
                    (".github/workflows/release.yml", release_workflow),
                    (".github/workflows/docs.yml", docs_workflow),
                    (".github/workflows/compile.yml", compile_workflow),
                ):
                    require_contains(text, action.strip(), label, errors)
    if artifact_prefix:
        require_contains(release_workflow, f"name: {artifact_prefix}${{{{ matrix.slug }}}}", ".github/workflows/release.yml", errors)
        require_contains(release_workflow, f"pattern: {artifact_prefix}*", ".github/workflows/release.yml", errors)
    if release_build_output_dir:
        for needle in (
            f"mkdir -p {release_build_output_dir}",
            f"path: {release_build_output_dir}/",
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.factory.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.ota.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.manifest.json"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_factory_binary:
        for needle in (
            f'"${{BUILD_DIR}}/{release_source_factory_binary}"',
            f"factory binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_ota_binary:
        for needle in (
            f'"${{BUILD_DIR}}/{release_source_ota_binary}"',
            f"OTA binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_publish_dir:
        for needle in (
            f"path: {release_publish_dir}",
            f"--dir {release_publish_dir}",
            f"{release_publish_dir}/* --clobber",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_uploaded_verify_dir:
        for needle in (
            f"mkdir -p {release_uploaded_verify_dir}",
            f"--dir {release_uploaded_verify_dir}",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_dir:
        for needle in (
            f"path: {release_esphome_cache_dir}",
            f"if [ -d {release_esphome_cache_dir} ]; then",
            f"sudo chown -R \"$USER:$USER\" {release_esphome_cache_dir}",
            f"chmod -R u+rwX {release_esphome_cache_dir}",
            f"BUILD_DIR=\"{release_esphome_cache_dir}/build/${{{{ matrix.build_name }}}}/.pioenvs/${{{{ matrix.build_name }}}}\"",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_key_prefix:
        require_contains(
            release_workflow,
            f"key: {release_esphome_cache_key_prefix}-${{{{ matrix.slug }}}}-",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            release_workflow,
            "restore-keys: |",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            release_workflow,
            f"            {release_esphome_cache_key_prefix}-${{{{ matrix.slug }}}}-",
            ".github/workflows/release.yml",
            errors,
        )
    if release_esphome_cache_hash_files:
        hash_files = "', '".join(release_esphome_cache_hash_files)
        require_contains(
            release_workflow,
            f"hashFiles('{hash_files}')",
            ".github/workflows/release.yml",
            errors,
        )
    for pattern in binary_download_patterns:
        require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in manifest_download_patterns:
        require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in uploaded_verify_patterns:
        require_contains(release_workflow, f'--pattern "{pattern}"', ".github/workflows/release.yml", errors)
    if release_download_clobber is True:
        require_contains(docs_workflow, "--clobber", ".github/workflows/docs.yml", errors)
    if release_upload_clobber is True:
        require_contains(release_workflow, f"{release_publish_dir}/* --clobber", ".github/workflows/release.yml", errors)
    for suffix in asset_suffixes:
        require_contains(release_workflow, suffix, ".github/workflows/release.yml", errors)
        if suffix == ".manifest.json":
            require_contains(docs_workflow, suffix, ".github/workflows/docs.yml", errors)
        require_contains(read(ROOT / "scripts" / "firmware_release.py", errors), suffix, "scripts/firmware_release.py", errors)
    firmware_release_script = read(ROOT / "scripts" / "firmware_release.py", errors)
    release_changelog_script = read(ROOT / "scripts" / "release_changelog.py", errors)
    if release_version_pattern:
        require_contains(firmware_release_script, "release_version_pattern", "scripts/firmware_release.py", errors)
    if stable_release_version_pattern:
        require_contains(release_changelog_script, "stable_release_version_pattern", "scripts/release_changelog.py", errors)
    if isinstance(changelog_categories, list) and changelog_categories:
        require_contains(release_changelog_script, "release_changelog_categories", "scripts/release_changelog.py", errors)
    if changelog_fallback:
        require_contains(release_changelog_script, "release_changelog_fallback_category", "scripts/release_changelog.py", errors)
    if firmware_version_placeholder:
        require_contains(firmware_release_script, "firmware_version_placeholder_line", "scripts/firmware_release.py", errors)
        for device in product["devices"]:
            build_yaml = check_relative_path(device.get("build_yaml"), f"Device {device.get('slug', '<missing>')} build_yaml", errors)
            if build_yaml:
                require_contains(read(ROOT / build_yaml, errors), firmware_version_placeholder, build_yaml, errors)
    if placeholder_versions:
        require_contains(firmware_release_script, "firmware_placeholder_versions", "scripts/firmware_release.py", errors)
    if local_build_version:
        require_contains(
            read(ROOT / "common" / "addon" / "firmware_update.yaml", errors),
            f'firmware_version: "{local_build_version}"',
            "common/addon/firmware_update.yaml",
            errors,
        )
    if docs_dist_artifact_name:
        require_contains(docs_workflow, f"name: {docs_dist_artifact_name}", ".github/workflows/docs.yml", errors)
    if docs_dist_output_path:
        require_contains(docs_workflow, f"path: {docs_dist_output_path}", ".github/workflows/docs.yml", errors)
    if docs_firmware_artifact_name:
        require_contains(docs_workflow, f"name: {docs_firmware_artifact_name}", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"mkdir -p {docs_firmware_artifact_name}", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"path: {docs_firmware_artifact_name}/", ".github/workflows/docs.yml", errors)
        if docs_deploy_path:
            require_contains(
                docs_workflow,
                f"path: {docs_deploy_path}/{docs_firmware_artifact_name}",
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f"rm -rf {docs_deploy_path}/{docs_firmware_artifact_name}",
                ".github/workflows/docs.yml",
                errors,
            )
    if docs_deploy_path:
        require_contains(docs_workflow, f"path: {docs_deploy_path}", ".github/workflows/docs.yml", errors)
    if pages_environment:
        require_contains(docs_workflow, "environment:", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"name: {pages_environment}", ".github/workflows/docs.yml", errors)
    if pages_deployment_step_id and pages_url_output:
        require_contains(docs_workflow, f"id: {pages_deployment_step_id}", ".github/workflows/docs.yml", errors)
        require_contains(
            docs_workflow,
            f"url: ${{{{ steps.{pages_deployment_step_id}.outputs.{pages_url_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    if pages_concurrency_group:
        require_contains(docs_workflow, "concurrency:", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"group: {pages_concurrency_group}", ".github/workflows/docs.yml", errors)
    if isinstance(pages_cancel_in_progress, bool):
        require_contains(
            docs_workflow,
            f"cancel-in-progress: {str(pages_cancel_in_progress).lower()}",
            ".github/workflows/docs.yml",
            errors,
        )
    if docs_workflow_success_conclusion:
        require_contains(
            docs_workflow,
            "github.event_name != 'workflow_run' ||",
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"github.event.workflow_run.conclusion == '{docs_workflow_success_conclusion}'",
            ".github/workflows/docs.yml",
            errors,
        )
    if isinstance(docs_verify_retries, int) and not isinstance(docs_verify_retries, bool):
        require_contains(docs_workflow, f"--retries {docs_verify_retries}", ".github/workflows/docs.yml", errors)
    if isinstance(docs_verify_delay, int) and not isinstance(docs_verify_delay, bool):
        require_contains(docs_workflow, f"--delay {docs_verify_delay}", ".github/workflows/docs.yml", errors)
    if docs_release_tag_env:
        release_tag_ref = f"${docs_release_tag_env}"
        for needle in (
            f"{docs_release_tag_env}=$(gh release view --json tagName -q .tagName)",
            f'echo "{docs_release_tag_env}=${{{docs_release_tag_env}}}" >> "$GITHUB_ENV"',
            f'gh release download "{release_tag_ref}"',
            f'--version "{release_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    if docs_release_tag_env and docs_release_tag_output:
        if docs_release_meta_step_id:
            require_contains(docs_workflow, f"id: {docs_release_meta_step_id}", ".github/workflows/docs.yml", errors)
            require_contains(
                docs_workflow,
                f"{docs_release_tag_output}: ${{{{ steps.{docs_release_meta_step_id}.outputs.{docs_release_tag_output} }}}}",
                ".github/workflows/docs.yml",
                errors,
            )
        require_contains(
            docs_workflow,
            f'echo "{docs_release_tag_output}=${{{docs_release_tag_env}}}" >> "$GITHUB_OUTPUT"',
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"${{{{ needs.download-firmware.outputs.{docs_release_tag_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    if docs_prerelease_tag_env:
        prerelease_tag_ref = f"${docs_prerelease_tag_env}"
        for needle in (
            f"{docs_prerelease_tag_env}=$(gh release list",
            f'if [ -n "{prerelease_tag_ref}" ]; then',
            f'gh release download "{prerelease_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    if isinstance(firmware_compile_timeout, int) and not isinstance(firmware_compile_timeout, bool):
        for label, text in (
            (".github/workflows/release.yml", release_workflow),
            (".github/workflows/compile.yml", compile_workflow),
        ):
            require_contains(text, f"timeout-minutes: {firmware_compile_timeout}", label, errors)
    docs_release_lookup_needles = [
        "gh release view --json tagName",
        "python3 scripts/firmware_release.py verify-directory",
        "python3 scripts/firmware_release.py verify-pages",
        f'--base-url "{public_base_url(product)}"',
    ]
    if isinstance(prerelease_lookup_limit, int) and not isinstance(prerelease_lookup_limit, bool):
        docs_release_lookup_needles.append(f"gh release list --limit {prerelease_lookup_limit} --json tagName,isPrerelease")
    for needle in docs_release_lookup_needles:
        require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)

    try:
        release_devices = release_matrix_devices(product)
    except RuntimeError as exc:
        errors.append(str(exc))
        return

    devices_by_slug = {str(device.get("slug", "")).strip(): device for device in product["devices"]}
    for release_device in release_devices:
        slug = release_device["slug"]
        build_yaml = str(devices_by_slug.get(slug, {}).get("build_yaml", "")).strip()
        local_yaml = str(devices_by_slug.get(slug, {}).get("local_yaml", "")).strip()
        device_dir = str(Path(local_yaml).parent) if local_yaml else ""
        for needle in (
            f"- slug: {slug}",
            f"yaml: {release_device['yaml']}",
            f"build_name: {release_device['build_name']}",
            f"chip: {release_device['chip']}",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
        if esphome_config_mount:
            require_contains(
                release_workflow,
                f"compile {esphome_config_mount}/builds/${{{{ matrix.yaml }}}}.factory.yaml",
                ".github/workflows/release.yml",
                errors,
            )
            require_contains(
                compile_workflow,
                f"compile {esphome_config_mount}/{build_yaml}",
                ".github/workflows/compile.yml",
                errors,
            )
        if device_dir:
            require_contains(
                compile_workflow,
                f'"{device_dir}/**"',
                ".github/workflows/compile.yml",
                errors,
            )
        public_manifest_dirs = []
        for field in ("public_manifest", "public_beta_manifest"):
            public_manifest = str(devices_by_slug.get(slug, {}).get(field, "")).strip()
            if public_manifest:
                public_manifest_dirs.append(Path(public_manifest).parent.as_posix())
        for prefix in dict.fromkeys(public_manifest_dirs):
            require_contains(
                docs_workflow,
                f"mkdir -p {prefix}",
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f"if [ -f {prefix}/{slug}.manifest.json ]; then",
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f"cp {prefix}/{slug}.manifest.json {prefix}/manifest.json",
                ".github/workflows/docs.yml",
                errors,
            )


def check_generated_asset_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    outputs = [str(value).strip() for value in project.get("generated_asset_outputs", []) if str(value).strip()]
    sources = [str(value).strip() for value in project.get("generated_asset_sources", []) if str(value).strip()]
    placeholders = [str(value).strip() for value in project.get("web_template_placeholders", []) if str(value).strip()]

    generator = read(ROOT / "scripts" / "generate_assets.py", errors)
    web_template = read(WEB_TEMPLATE, errors)
    package_json = read(ROOT / "package.json", errors)

    expected_outputs = {
        "components/espframe/tz_data_generated.h",
        "common/addon/time.yaml",
        "docs/public/webserver/app.js",
        "docs/public/webserver/style.css",
    }
    expected_sources = {
        "components/espframe/timezones.py",
        "docs/webserver/src/app.template.js",
        "docs/webserver/src/style.css",
        "product/espframe.json",
        "scripts/product_config.py",
    }
    missing_outputs = sorted(expected_outputs - set(outputs))
    missing_sources = sorted(expected_sources - set(sources))
    overlapping_paths = sorted(set(outputs).intersection(sources))
    if missing_outputs:
        errors.append("project.generated_asset_outputs is missing paths: " + ", ".join(missing_outputs))
    if missing_sources:
        errors.append("project.generated_asset_sources is missing paths: " + ", ".join(missing_sources))
    if overlapping_paths:
        errors.append("Generated asset paths must not be listed as both sources and outputs: " + ", ".join(overlapping_paths))

    for filename in outputs + sources:
        path = check_relative_path(filename, f"Generated asset path {filename}", errors)
        if path:
            read(ROOT / path, errors)
            path_name = Path(path).name
            if path_name in {"espframe.json", "product_config.py"}:
                require_contains(generator, "load_product", "scripts/generate_assets.py", errors)
            else:
                require_contains(generator, path_name, "scripts/generate_assets.py", errors)
    template_placeholders = set(re.findall(r"__ESPFRAME_[A-Z0-9_]+__", web_template))
    configured_placeholders = set(placeholders)
    missing_placeholders = sorted(template_placeholders - configured_placeholders)
    extra_placeholders = sorted(configured_placeholders - template_placeholders)
    if missing_placeholders:
        errors.append(
            "project.web_template_placeholders is missing template placeholders: "
            + ", ".join(missing_placeholders)
        )
    if extra_placeholders:
        errors.append(
            "project.web_template_placeholders lists placeholders not used by the web template: "
            + ", ".join(extra_placeholders)
        )
    for placeholder in placeholders:
        require_contains(web_template, placeholder, rel(WEB_TEMPLATE), errors)
        require_contains(generator, placeholder, "scripts/generate_assets.py", errors)
    for needle in (
        "python3 scripts/generate_assets.py",
        "python3 scripts/generate_assets.py --check",
        "check:generated",
        "webserver:build",
    ):
        require_contains(package_json, needle, "package.json", errors)
    for needle in (
        "write_or_check",
        "replace_timezone_yaml",
        "web_app_bundle",
        "render_settings_table",
    ):
        require_contains(generator, needle, "scripts/generate_assets.py", errors)


def check_factory_firmware_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    purpose = str(project.get("factory_firmware_purpose", "")).strip()
    secret_policy = str(project.get("factory_firmware_secret_policy", "")).strip()
    network_mode = str(project.get("factory_firmware_network_mode", "")).strip()
    setup_method = str(project.get("factory_firmware_setup_method", "")).strip()
    local_use = str(project.get("factory_firmware_local_use", "")).strip()
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).strip()
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()

    install_docs = read(ROOT / "docs" / "install.md", errors)
    connectivity_yaml = read(ROOT / "common" / "addon" / "connectivity.yaml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if not build_yaml:
            continue
        build_text = read(ROOT / build_yaml, errors)
        for value in (purpose, secret_policy, network_mode, setup_method, local_use):
            if value:
                require_contains(build_text, value, build_yaml, errors)
        if firmware_version_placeholder:
            require_contains(build_text, firmware_version_placeholder, build_yaml, errors)
        if factory_css_include:
            require_contains(build_text, f'css_include: "{factory_css_include}"', build_yaml, errors)
        if factory_js_include:
            require_contains(build_text, f'js_include: "{factory_js_include}"', build_yaml, errors)
        if esphome_config_mount:
            require_contains(
                compile_workflow,
                f"compile {esphome_config_mount}/{build_yaml}",
                ".github/workflows/compile.yml",
                errors,
            )
            require_contains(
                release_workflow,
                f"compile {esphome_config_mount}/builds/${{{{ matrix.yaml }}}}.factory.yaml",
                ".github/workflows/release.yml",
                errors,
            )

    if network_mode:
        require_contains(install_docs, "hotspot", "docs/install.md", errors)
        require_contains(connectivity_yaml, 'ssid: "${name}"', "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "wifi:", "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "ap:", "common/addon/connectivity.yaml", errors)
    if setup_method:
        require_contains(connectivity_yaml, "captive_portal:", "common/addon/connectivity.yaml", errors)
        require_contains(install_docs, setup_method.replace("_", " "), "docs/install.md", errors)


def check_web_server_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    port = project.get("web_server_port")
    version = project.get("web_server_version")
    include_internal = project.get("web_server_include_internal")
    public_app_path = str(project.get("web_server_public_app_path", "")).strip()
    factory_js_url = str(project.get("web_server_factory_js_url", ""))
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()
    sorting_groups = project.get("web_server_sorting_groups", [])
    group_ids = {
        str(group.get("id", "")).strip()
        for group in sorting_groups
        if isinstance(group, dict) and str(group.get("id", "")).strip()
    }

    if public_app_path and (public_app_path.startswith("/") or ".." in Path(public_app_path).parts):
        errors.append("project.web_server_public_app_path must be a relative public asset path")
    public_app_url = public_url(public_app_path, product) if public_app_path else ""

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(device_text, f"  port: {port}", device_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(device_text, f"  version: {version}", device_yaml, errors)
            if isinstance(include_internal, bool):
                require_contains(device_text, f"  include_internal: {str(include_internal).lower()}", device_yaml, errors)
            if public_app_url:
                require_contains(device_text, f'  js_url: "{public_app_url}"', device_yaml, errors)
            for group in sorting_groups if isinstance(sorting_groups, list) else []:
                if not isinstance(group, dict):
                    continue
                group_id = str(group.get("id", "")).strip()
                name = str(group.get("name", "")).strip()
                weight = group.get("sorting_weight")
                if group_id:
                    require_contains(device_text, f"    - id: {group_id}", device_yaml, errors)
                if name:
                    require_contains(device_text, f'      name: "{name}"', device_yaml, errors)
                if isinstance(weight, int) and not isinstance(weight, bool):
                    require_contains(device_text, f"      sorting_weight: {weight}", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(build_text, f"  port: {port}", build_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(build_text, f"  version: {version}", build_yaml, errors)
            require_contains(build_text, f'  js_url: "{factory_js_url}"', build_yaml, errors)
            if factory_css_include:
                require_contains(build_text, f'  css_include: "{factory_css_include}"', build_yaml, errors)
            if factory_js_include:
                require_contains(build_text, f'  js_include: "{factory_js_include}"', build_yaml, errors)

    if group_ids:
        for yaml_path in list((ROOT / "common").rglob("*.yaml")) + list((ROOT / "devices").rglob("*.yaml")):
            text = read(yaml_path, errors)
            for group_id in re.findall(r"sorting_group_id:\s*([A-Za-z0-9_-]+)", text):
                if group_id not in group_ids:
                    errors.append(f"{rel(yaml_path)} references unknown web_server sorting group {group_id}")

    for include_path in (factory_css_include, factory_js_include):
        if not include_path:
            continue
        normalized = include_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_file():
            errors.append(f"project web server include is missing: {normalized}")


def check_external_components_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    component_names = [
        str(value).strip() for value in project.get("external_component_names", []) if str(value).strip()
    ]
    git_source_type = str(project.get("external_component_git_source_type", "")).strip()
    local_source_type = str(project.get("external_component_local_source_type", "")).strip()
    git_path = str(project.get("external_component_git_path", "")).strip()
    local_path = str(project.get("external_component_local_path", "")).strip()
    component_ref = str(project.get("external_component_ref", "")).strip()
    components_inline = f"components: [{', '.join(component_names)}]" if component_names else ""
    repository_url = str(project.get("repository_url", "")).strip()

    for component in component_names:
        component_dir = ROOT / "components" / component
        if not component_dir.is_dir():
            errors.append(f"Missing external component directory: {rel(component_dir)}")
        init_file = component_dir / "__init__.py"
        if not init_file.is_file():
            errors.append(f"Missing external component entrypoint: {rel(init_file)}")

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, "external_components:", device_yaml, errors)
            if git_source_type:
                require_contains(device_text, f"      type: {git_source_type}", device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)
                require_contains(device_text, "      url: ${espframe_component_url}", device_yaml, errors)
            if component_ref:
                require_contains(device_text, f'espframe_component_ref: "{component_ref}"', device_yaml, errors)
                require_contains(device_text, "      ref: ${espframe_component_ref}", device_yaml, errors)
            if git_path:
                require_contains(device_text, f"      path: {git_path}", device_yaml, errors)
            if components_inline:
                require_contains(device_text, f"    {components_inline}", device_yaml, errors)
            require_contains(device_text, "    refresh: 0s", device_yaml, errors)
            require_contains(device_text, "espframe:", device_yaml, errors)
            require_contains(device_text, "  id: espframe_core", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            require_contains(build_text, "external_components:", build_yaml, errors)
            if local_source_type:
                require_contains(build_text, f"      type: {local_source_type}", build_yaml, errors)
            if local_path:
                require_contains(build_text, f"      path: {local_path}", build_yaml, errors)
            if components_inline:
                require_contains(build_text, f"    {components_inline}", build_yaml, errors)

    if local_path:
        normalized = local_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_dir():
            errors.append(f"project.external_component_local_path is missing: {normalized}")
    if git_path and not (ROOT / git_path).is_dir():
        errors.append(f"project.external_component_git_path is missing: {git_path}")


def check_esphome_version(product: dict, errors: list[str]) -> None:
    project = product["project"]
    version = str(project.get("esphome_version", "")).strip()
    docker_image = str(project.get("esphome_docker_image", "")).strip().rstrip(":")
    config_mount = str(project.get("esphome_config_mount", "")).strip()
    remove_container = project.get("esphome_docker_remove_container")
    if not version:
        errors.append("project.esphome_version is required")
        return

    required_refs = [
        ROOT / ".github" / "workflows" / "compile.yml",
        ROOT / ".github" / "workflows" / "release.yml",
        ROOT / "README.md",
        ROOT / "docs" / "install.md",
        ROOT / "docs" / "manual-setup.md",
    ]
    for path in required_refs:
        text = read(path, errors)
        require_contains(text, version, rel(path), errors)

    for path in (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "release.yml", ROOT / "README.md"):
        text = read(path, errors)
        if docker_image:
            require_contains(text, f"{docker_image}:{version}", rel(path), errors)
        if config_mount:
            require_contains(text, f'-v "${{PWD}}:{config_mount}"', rel(path), errors)
        if remove_container is True:
            require_contains(text, "docker run --rm", rel(path), errors)


def check_workflows(product: dict, errors: list[str]) -> None:
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(compile_workflow, '"product/**"', ".github/workflows/compile.yml", errors)

    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    default_branch = str(product["project"].get("github_default_branch", "")).strip()
    if default_branch:
        require_contains(docs_workflow, f"branches: [{default_branch}]", ".github/workflows/docs.yml", errors)
    workflow_path_filters = product["project"].get("github_workflow_path_filters", {})
    if isinstance(workflow_path_filters, dict):
        for path in workflow_path_filters.get("compile_pull_request", []):
            if isinstance(path, str) and path.strip():
                require_workflow_path_filter(compile_workflow, path.strip(), ".github/workflows/compile.yml", errors)
        for path in workflow_path_filters.get("docs_push", []):
            if isinstance(path, str) and path.strip():
                require_workflow_path_filter(docs_workflow, path.strip(), ".github/workflows/docs.yml", errors)
    workflow_texts = {
        "compile": (".github/workflows/compile.yml", compile_workflow),
        "docs": (".github/workflows/docs.yml", docs_workflow),
        "release": (".github/workflows/release.yml", release_workflow),
    }
    workflow_events = product["project"].get("github_workflow_events", {})
    if isinstance(workflow_events, dict):
        for workflow, raw_events in workflow_events.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_events, list):
                continue
            label, text = workflow_texts[workflow_name]
            for event in raw_events:
                event_name = str(event).strip()
                if event_name:
                    require_contains(text, f"  {event_name}:", label, errors)
    workflow_event_types = product["project"].get("github_workflow_event_types", {})
    if isinstance(workflow_event_types, dict):
        for key, raw_types in workflow_event_types.items():
            workflow_name, _, event_name = str(key).strip().partition(".")
            if workflow_name not in workflow_texts or not event_name or not isinstance(raw_types, list):
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, f"  {event_name}:", label, errors)
            for event_type in raw_types:
                event_type_name = str(event_type).strip()
                if event_type_name:
                    require_contains(text, f"types: [{event_type_name}]", label, errors)
    workflow_jobs = product["project"].get("github_workflow_jobs", {})
    if isinstance(workflow_jobs, dict):
        for workflow, raw_jobs in workflow_jobs.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_jobs, dict):
                continue
            label, text = workflow_texts[workflow_name]
            for raw_job_id, raw_job_name in raw_jobs.items():
                job_id = str(raw_job_id).strip()
                job_name = str(raw_job_name).strip()
                if job_id:
                    require_contains(text, f"  {job_id}:", label, errors)
                if job_name:
                    require_contains(text, f"    name: {job_name}", label, errors)
    workflow_job_dependencies = product["project"].get("github_workflow_job_dependencies", {})
    if isinstance(workflow_job_dependencies, dict):
        for key, raw_dependencies in workflow_job_dependencies.items():
            workflow_name, _, job_id = str(key).strip().partition(".")
            if workflow_name not in workflow_texts or not job_id or not isinstance(raw_dependencies, list):
                continue
            dependencies = [str(dependency).strip() for dependency in raw_dependencies if str(dependency).strip()]
            if dependencies:
                label, text = workflow_texts[workflow_name]
                require_contains(text, f"  {job_id}:", label, errors)
                require_workflow_needs(text, dependencies, label, errors)
    workflow_permissions = product["project"].get("github_workflow_permissions", {})
    workflow_names = product["project"].get("github_workflow_names", {})
    if isinstance(workflow_names, dict):
        for workflow, raw_name in workflow_names.items():
            workflow_name = str(workflow).strip()
            name = str(raw_name).strip()
            if workflow_name not in workflow_texts or not name:
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, f"name: {name}", label, errors)
        release_workflow_name = str(workflow_names.get("release", "")).strip()
        if release_workflow_name:
            require_contains(docs_workflow, f'workflows: ["{release_workflow_name}"]', ".github/workflows/docs.yml", errors)
    if isinstance(workflow_permissions, dict):
        for workflow, raw_permissions in workflow_permissions.items():
            workflow_name = str(workflow).strip()
            if workflow_name not in workflow_texts or not isinstance(raw_permissions, dict):
                continue
            label, text = workflow_texts[workflow_name]
            require_contains(text, "permissions:", label, errors)
            for raw_scope, raw_access in raw_permissions.items():
                scope = str(raw_scope).strip()
                access = str(raw_access).strip()
                if scope and access:
                    require_contains(text, f"  {scope}: {access}", label, errors)
    for label, text in (
        (".github/workflows/docs.yml", docs_workflow),
        (".github/workflows/release.yml", release_workflow),
    ):
        require_contains(text, "scripts/product_config.py", label, errors)
        require_contains(text, "product/espframe.json", label, errors)


def check_node_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("node_version", "")).strip()
    package_cache = str(product["project"].get("node_package_cache", "")).strip()
    install_command = str(product["project"].get("node_install_command", "")).strip()
    local_check_command = str(product["project"].get("local_check_command", "")).strip()
    docs_build_command = str(product["project"].get("docs_build_command", "")).strip()
    node24_env = str(product["project"].get("github_actions_node24_env", "")).strip()
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")
    if version == "24" and not node24_env:
        errors.append("project.github_actions_node24_env is required when project.node_version is 24")

    node_workflow_paths = (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "docs.yml")
    for path in node_workflow_paths:
        text = read(path, errors)
        require_contains(text, f"node-version: {version}", rel(path), errors)
        if package_cache:
            require_contains(text, f"cache: {package_cache}", rel(path), errors)
        if install_command:
            require_contains(text, f"run: {install_command}", rel(path), errors)
        if version == "24" and node24_env:
            require_contains(text, node24_env, rel(path), errors)

    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    if local_check_command:
        require_contains(compile_workflow, f"run: {local_check_command}", ".github/workflows/compile.yml", errors)
    if docs_build_command:
        require_contains(docs_workflow, f"run: {docs_build_command}", ".github/workflows/docs.yml", errors)

    if version == "24" and node24_env:
        release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
        require_contains(release_workflow, node24_env, ".github/workflows/release.yml", errors)


def check_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_key_domains = {
        str(setting.get("key", "")).strip(): str(setting.get("entity", {}).get("domain", "")).strip()
        for setting in product["settings"]
    }
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities = web_static_entities(product)
    initial_fetch_first_keys = web_initial_fetch_first_keys(product)
    static_entities_seen: set[str] = set()
    state_domains = dict(product_key_domains)
    local_state_keys = web_local_state_keys(product)

    if not static_entities:
        errors.append("project.web_static_entities must be a non-empty object")
    if not initial_fetch_first_keys:
        errors.append("project.web_initial_fetch_first_keys must be a non-empty list")
    for key in initial_fetch_first_keys:
        metadata = static_entities.get(key)
        if not metadata:
            errors.append(f"project.web_initial_fetch_first_keys {key} must point at a static web entity")
        elif metadata.get("fetch") is not True:
            errors.append(f"project.web_initial_fetch_first_keys {key} must point at a fetch-enabled static web entity")

    for key, metadata in static_entities.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Static web entity keys must be non-empty strings")
        if key in product_keys:
            errors.append(f"Static web entity {key} duplicates a product setting key")
        if not isinstance(metadata, dict):
            errors.append(f"Static web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Static web entity {key} has invalid entity {entity!r}")
        elif entity in static_entities_seen:
            errors.append(f"Duplicate static web entity: {entity}")
        else:
            static_entities_seen.add(str(entity))
            domain = str(entity).split("/", 1)[0]
            state_domains[str(key)] = domain
            check_web_entity_default_type(metadata, domain, f"Static web entity {key}", errors)
            if domain == "switch" and metadata.get("boolFromState") is not True:
                errors.append(f"Static web entity {key} switch entity must set boolFromState: true")
            if domain != "switch" and metadata.get("boolFromState") is True:
                errors.append(f"Static web entity {key} boolFromState is only valid for switch entities")
            if domain == "number" and metadata.get("number") is not True:
                errors.append(f"Static web entity {key} number entity must set number: true")
            if domain != "number" and metadata.get("number") is True:
                errors.append(f"Static web entity {key} number is only valid for number entities")
        if entity in product_entities:
            errors.append(f"Static web entity {key} duplicates product entity {entity}")
        for field in ("fetch", "boolFromState", "number"):
            if field in metadata and not isinstance(metadata[field], bool):
                errors.append(f"Static web entity {key} {field} must be true or false")
        if "optionsKey" in metadata:
            options_key = str(metadata["optionsKey"]).strip()
            if not options_key:
                errors.append(f"Static web entity {key} optionsKey must be non-empty")
            elif options_key not in local_state_keys:
                errors.append(f"Static web entity {key} optionsKey must point at a project.web_local_state_keys entry")
        firmware_file = str(metadata.get("firmware_file", "")).strip()
        if not firmware_file:
            errors.append(f"Static web entity {key} is missing firmware_file")
        elif Path(firmware_file).is_absolute() or ".." in Path(firmware_file).parts:
            errors.append(f"Static web entity {key} has unsafe firmware_file path: {firmware_file}")
        elif valid_entity_string(entity):
            domain, name = str(entity).split("/", 1)
            text = read(ROOT / firmware_file, errors)
            require_contains(text, f"{domain}:", firmware_file, errors)
            require_contains(text, f"name: \"{name}\"", firmware_file, errors)

    alias_entities_seen: set[str] = set()
    valid_state_keys = product_keys | set(static_entities)
    entity_aliases = web_entity_aliases(product)
    if not entity_aliases:
        errors.append("project.web_entity_aliases must be a non-empty object")
    for key, aliases in entity_aliases.items():
        if key not in valid_state_keys:
            errors.append(f"Web entity alias {key} does not point at a known state key")
        if not isinstance(aliases, list) or not aliases:
            errors.append(f"Web entity alias {key} must define at least one alias")
            continue
        for alias in aliases:
            if not isinstance(alias, dict):
                errors.append(f"Web entity alias {key} entries must be objects")
                continue
            entity = alias.get("entity")
            if not valid_entity_string(entity):
                errors.append(f"Web entity alias {key} has invalid entity {entity!r}")
            else:
                alias_domain = str(entity).split("/", 1)[0]
                expected_domain = state_domains.get(key)
                check_web_entity_default_type(alias, alias_domain, f"Web entity alias {key}", errors)
                if expected_domain and alias_domain != expected_domain:
                    errors.append(
                        f"Web entity alias {key} domain {alias_domain!r} must match canonical domain {expected_domain!r}"
                    )
                if alias_domain == "switch" and alias.get("boolFromState") is not True:
                    errors.append(f"Web entity alias {key} switch alias must set boolFromState: true")
                if alias_domain != "switch" and alias.get("boolFromState") is True:
                    errors.append(f"Web entity alias {key} boolFromState is only valid for switch aliases")
                if alias_domain == "number" and alias.get("number") is not True:
                    errors.append(f"Web entity alias {key} number alias must set number: true")
                if alias_domain != "number" and alias.get("number") is True:
                    errors.append(f"Web entity alias {key} number is only valid for number aliases")
                if entity in alias_entities_seen:
                    errors.append(f"Duplicate web entity alias: {entity}")
                else:
                    alias_entities_seen.add(str(entity))
            if entity in product_entities or entity in static_entities_seen:
                errors.append(f"Web entity alias {key} duplicates canonical entity {entity}")
            for field in ("boolFromState", "number"):
                if field in alias and not isinstance(alias[field], bool):
                    errors.append(f"Web entity alias {key} {field} must be true or false")
            if "optionsKey" in alias:
                options_key = str(alias["optionsKey"]).strip()
                if not options_key:
                    errors.append(f"Web entity alias {key} optionsKey must be non-empty")
                elif options_key not in local_state_keys:
                    errors.append(f"Web entity alias {key} optionsKey must point at a project.web_local_state_keys entry")


def check_manual_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities = web_static_entities(product)
    static_entity_values = {
        str(metadata.get("entity", "")).strip()
        for metadata in static_entities.values()
        if valid_entity_string(metadata.get("entity"))
    }
    alias_entity_values = {
        str(alias.get("entity", "")).strip()
        for aliases in web_entity_aliases(product).values()
        for alias in aliases
        if valid_entity_string(alias.get("entity"))
    }
    manual_entities = web_manual_entities(product)
    local_state_keys = web_local_state_keys(product)
    manual_state_keys = web_manual_state_keys(product)
    seen_entities: set[str] = set()
    if not manual_entities:
        errors.append("project.web_manual_entities must be a non-empty object")
    if not manual_state_keys:
        errors.append("project.web_manual_state_keys must be a non-empty list")
    for key in manual_state_keys:
        if key not in manual_entities:
            errors.append(f"project.web_manual_state_keys {key} must point at a manual web entity")
        if key not in local_state_keys:
            errors.append(f"project.web_manual_state_keys {key} must also be listed in project.web_local_state_keys")

    for key, metadata in manual_entities.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Manual web entity keys must be non-empty strings")
        if key in product_keys:
            errors.append(f"Manual web entity {key} duplicates a product setting key")
        if key in static_entities:
            errors.append(f"Manual web entity {key} duplicates a static web entity key")
        if not isinstance(metadata, dict):
            errors.append(f"Manual web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Manual web entity {key} has invalid entity {entity!r}")
            continue
        if entity in seen_entities:
            errors.append(f"Duplicate manual web entity: {entity}")
        if entity in product_entities:
            errors.append(f"Manual web entity {key} duplicates product entity {entity}")
        if entity in static_entity_values:
            errors.append(f"Manual web entity {key} duplicates static web entity {entity}")
        if entity in alias_entity_values:
            errors.append(f"Manual web entity {key} duplicates web entity alias {entity}")
        seen_entities.add(str(entity))
        domain, name = str(entity).split("/", 1)
        firmware_file = str(metadata.get("firmware_file", "")).strip()
        if not firmware_file:
            errors.append(f"Manual web entity {key} is missing firmware_file")
            continue
        if Path(firmware_file).is_absolute() or ".." in Path(firmware_file).parts:
            errors.append(f"Manual web entity {key} has unsafe firmware_file path: {firmware_file}")
            continue
        text = read(ROOT / firmware_file, errors)
        require_contains(text, f"{domain}:", firmware_file, errors)
        require_contains(text, f"name: \"{name}\"", firmware_file, errors)


def check_generated_web_metadata(product: dict, web_text: str, errors: list[str]) -> None:
    product_settings = extract_js_json_var(web_text, "PRODUCT_SETTINGS", errors)
    if product_settings is not None and product_settings != web_settings_metadata(product["settings"]):
        errors.append("Generated web PRODUCT_SETTINGS does not match product/espframe.json")

    static_entities = extract_js_json_var(web_text, "STATIC_ENTITIES", errors)
    if static_entities is not None and static_entities != web_static_entities_metadata(product):
        errors.append("Generated web STATIC_ENTITIES does not match product/espframe.json")

    manual_entities = extract_js_json_var(web_text, "MANUAL_ENTITIES", errors)
    if manual_entities is not None and manual_entities != web_manual_entities_metadata(product):
        errors.append("Generated web MANUAL_ENTITIES does not match product/espframe.json")

    manual_state_keys = extract_js_json_var(web_text, "MANUAL_STATE_KEYS", errors)
    if manual_state_keys is not None and manual_state_keys != web_manual_state_keys(product):
        errors.append("Generated web MANUAL_STATE_KEYS does not match product/espframe.json")

    entity_aliases = extract_js_json_var(web_text, "ENTITY_ALIASES", errors)
    if entity_aliases is not None and entity_aliases != web_entity_aliases_metadata(product):
        errors.append("Generated web ENTITY_ALIASES does not match product/espframe.json")

    initial_fetch_keys = extract_js_json_var(web_text, "INITIAL_FETCH_KEYS", errors)
    if initial_fetch_keys is not None and initial_fetch_keys != web_initial_fetch_keys(product["settings"]):
        errors.append("Generated web INITIAL_FETCH_KEYS does not match product/espframe.json")

    firmware_manifest_urls = extract_js_json_var(web_text, "FIRMWARE_MANIFEST_URLS", errors)
    if firmware_manifest_urls is not None and firmware_manifest_urls != default_public_manifest_urls(product):
        errors.append("Generated web FIRMWARE_MANIFEST_URLS does not match product/espframe.json")

    docs_base_url = extract_js_json_var(web_text, "DOCS_BASE_URL", errors)
    if docs_base_url is not None and docs_base_url != public_base_url(product):
        errors.append("Generated web DOCS_BASE_URL does not match product/espframe.json")

    web_ui_tabs = extract_js_json_var(web_text, "WEB_UI_TABS", errors)
    if web_ui_tabs is not None and web_ui_tabs != product["project"].get("web_ui_tabs"):
        errors.append("Generated web WEB_UI_TABS does not match product/espframe.json")

    support_url = extract_js_json_var(web_text, "SUPPORT_URL", errors)
    if support_url is not None and support_url != product["project"].get("support_url"):
        errors.append("Generated web SUPPORT_URL does not match product/espframe.json")

    support_button_image_url = extract_js_json_var(web_text, "SUPPORT_BUTTON_IMAGE_URL", errors)
    if support_button_image_url is not None and support_button_image_url != product["project"].get("support_button_image_url"):
        errors.append("Generated web SUPPORT_BUTTON_IMAGE_URL does not match product/espframe.json")


def check_static_web_defaults_against_firmware(product: dict, errors: list[str]) -> None:
    text = read(TIME_YAML, errors)
    static_entities = web_static_entities(product)
    timezone_default = static_entities.get("timezone", {}).get("default")
    if not isinstance(timezone_default, str) or not timezone_default:
        errors.append("Static web entity timezone default must match the firmware initial_option")
    else:
        require_contains(
            text,
            f'initial_option: "{timezone_default}"',
            rel(TIME_YAML),
            errors,
        )

    for key in ("ntp_server_1", "ntp_server_2", "ntp_server_3"):
        default = static_entities.get(key, {}).get("default")
        if not isinstance(default, str) or not default:
            errors.append(f"Static web entity {key} default must match the firmware substitution")
            continue
        require_contains(text, f'  {key}: "{default}"', rel(TIME_YAML), errors)
        require_contains(text, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)

    show_clock_default = static_entities.get("show_clock", {}).get("default")
    if not isinstance(show_clock_default, bool):
        errors.append("Static web entity show_clock default must be true or false")
        return
    restore_mode = "RESTORE_DEFAULT_ON" if show_clock_default is True else "RESTORE_DEFAULT_OFF"
    require_contains(text, f"restore_mode: {restore_mode}", rel(TIME_YAML), errors)


def check_web_ui_metadata(product: dict, web_template: str, web_text: str, errors: list[str]) -> None:
    project = product["project"]
    tabs = project.get("web_ui_tabs", [])
    event_source = str(project.get("web_ui_logs_event_source", "")).strip()
    event_name = str(project.get("web_ui_logs_event_name", "")).strip()
    clear_label = str(project.get("web_ui_logs_clear_label", "")).strip()
    retained_lines = project.get("web_ui_logs_retained_lines")
    labels_and_text = ((rel(WEB_TEMPLATE), web_template), (rel(WEB_APP), web_text))

    for label, text in labels_and_text:
        require_contains(text, "WEB_UI_TABS", label, errors)
        require_contains(text, "webUiTabs()", label, errors)
        require_contains(text, "switchTab(t.id)", label, errors)
        require_contains(text, 'els["tab_" + t]', label, errors)
        require_contains(text, "buildLogsPage(root)", label, errors)
        require_contains(text, "appendLog(d.msg || e.data, d.lvl)", label, errors)
        require_contains(text, "ANSI_LEVEL", label, errors)
        require_contains(text, "sp-log-error", label, errors)
        require_contains(text, "sp-log-warn", label, errors)
        require_contains(text, "sp-log-info", label, errors)
        if event_source:
            require_contains(text, f'new EventSource("{event_source}")', label, errors)
        if event_name:
            require_contains(text, f'addEventListener("{event_name}"', label, errors)
        if clear_label:
            require_contains(text, f'textContent = "{clear_label}"', label, errors)
        if isinstance(retained_lines, int) and not isinstance(retained_lines, bool):
            require_contains(text, f"childNodes.length - {retained_lines}", label, errors)

    if isinstance(tabs, list):
        seen_tab_ids: set[str] = set()
        for tab in tabs:
            if not isinstance(tab, dict):
                errors.append("project.web_ui_tabs entries must be objects")
                continue
            tab_id = str(tab.get("id", "")).strip()
            tab_label = str(tab.get("label", "")).strip()
            if not tab_id:
                errors.append("project.web_ui_tabs entries must include id")
            elif not re.match(r"^[A-Za-z][A-Za-z0-9_]*$", tab_id):
                errors.append(f"project.web_ui_tabs id {tab_id!r} must be a JavaScript-safe identifier")
            elif tab_id in seen_tab_ids:
                errors.append(f"project.web_ui_tabs has duplicate id {tab_id!r}")
            else:
                seen_tab_ids.add(tab_id)
            if not tab_label:
                errors.append(f"project.web_ui_tabs {tab_id or '<missing>'} must include label")
            if tab_id:
                require_contains(web_template + web_text, f"{tab_id}Page", f"web UI page for {tab_id}", errors)
            if tab_label:
                require_contains(web_text, f'"label":"{tab_label}"', f"generated web UI tab {tab_label}", errors)
    else:
        errors.append("project.web_ui_tabs must be a list")


def check_web_template_key_references(product: dict, web_template: str, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    static_keys = set(web_static_entities(product))
    manual_keys = set(web_manual_entities(product))
    local_state_keys = web_local_state_keys(product)
    known_state_keys = product_keys | static_keys | local_state_keys
    known_endpoint_keys = product_keys | static_keys | manual_keys

    for key in local_state_keys:
        if key in product_keys or key in static_keys:
            errors.append(f"project.web_local_state_keys {key} duplicates generated state metadata")

    state_refs = set(WEB_STATE_REF_RE.findall(web_template))
    unused_local_state_keys = sorted(local_state_keys - state_refs)
    if unused_local_state_keys:
        errors.append(
            "project.web_local_state_keys contains keys not referenced by the web template: "
            + ", ".join(unused_local_state_keys)
        )

    for key in sorted(state_refs):
        if key not in known_state_keys:
            errors.append(f"Web template references unknown state key S.{key}")

    for key in sorted(set(WEB_ENDPOINT_REF_RE.findall(web_template))):
        if key not in known_endpoint_keys:
            errors.append(f"Web template references unknown endpoint key endpoints.{key}")

    helper_keys = set(WEB_PRODUCT_HELPER_REF_RE.findall(web_template))
    helper_keys.update(WEB_PRODUCT_SETTINGS_REF_RE.findall(web_template))
    for key in sorted(helper_keys):
        if key not in product_keys:
            errors.append(f"Web template references unknown product setting {key}")


def check_docs_table_membership(product: dict, errors: list[str]) -> None:
    settings_by_key = {str(setting.get("key", "")): setting for setting in product["settings"]}
    table_memberships: set[tuple[str, str]] = set()
    tables = docs_settings_tables(product)
    for path, table_blocks in tables.items():
        relative_path = rel(path)
        for block_id, table in table_blocks.items():
            for key in [str(item) for item in table["settings"]]:
                table_memberships.add((relative_path, key))
                setting = settings_by_key.get(key)
                if not setting:
                    errors.append(f"{relative_path} settings table {block_id} references unknown setting {key}")
                    continue
                docs_files = [str(item) for item in setting.get("docs_files", [])]
                if relative_path not in docs_files:
                    errors.append(
                        f"{relative_path} settings table {block_id} includes {key}, "
                        f"but product docs_files does not include {relative_path}"
                    )
    generated_docs_files = {rel(path) for path in tables}
    for key, setting in settings_by_key.items():
        for docs_file in [str(item) for item in setting.get("docs_files", [])]:
            if docs_file in generated_docs_files and (docs_file, key) not in table_memberships:
                errors.append(f"{key} declares {docs_file} but is not included in a generated settings table")


def check_docs_table_metadata(product: dict, errors: list[str]) -> None:
    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    seen_tables: set[tuple[str, str]] = set()
    all_table_refs: set[tuple[str, str]] = set()

    tables = docs_settings_tables(product)
    if not tables:
        errors.append("project.docs_settings_tables must be a non-empty object")

    for path, table_blocks in tables.items():
        if not isinstance(path, Path):
            errors.append(f"Generated docs table path {path!r} must be a Path")
            relative_path = str(path)
        else:
            try:
                relative_path = rel(path)
            except ValueError:
                relative_path = str(path)
                errors.append(f"Generated docs table path {relative_path} must be inside the repository")
            else:
                if path.suffix != ".md" or not relative_path.startswith("docs/"):
                    errors.append(f"Generated docs table path {relative_path} must be a docs markdown file")
                read(path, errors)

        if not isinstance(table_blocks, dict) or not table_blocks:
            errors.append(f"{relative_path} must define at least one generated settings table")
            continue

        for block_id, table in table_blocks.items():
            if not isinstance(block_id, str) or not DOCS_TABLE_ID_RE.match(block_id):
                errors.append(f"{relative_path} has invalid settings table id {block_id!r}")
                continue
            table_key = (relative_path, block_id)
            if table_key in seen_tables:
                errors.append(f"{relative_path} defines duplicate settings table {block_id}")
            seen_tables.add(table_key)

            if not isinstance(table, dict):
                errors.append(f"{relative_path} settings table {block_id} metadata must be an object")
                continue
            columns = table.get("columns", ["Setting", "Default", "Description"])
            if not isinstance(columns, list) or not columns:
                errors.append(f"{relative_path} settings table {block_id} columns must be a non-empty list")
            else:
                seen_columns: set[str] = set()
                for column in columns:
                    if not isinstance(column, str) or column not in DOCS_SETTINGS_TABLE_COLUMNS:
                        errors.append(f"{relative_path} settings table {block_id} has unsupported column {column!r}")
                    elif column in seen_columns:
                        errors.append(f"{relative_path} settings table {block_id} includes column {column} more than once")
                    seen_columns.add(str(column))
                if not {"Setting", "Control"}.intersection(seen_columns):
                    errors.append(f"{relative_path} settings table {block_id} must include Setting or Control column")
                if columns and columns[0] not in {"Setting", "Control"}:
                    errors.append(f"{relative_path} settings table {block_id} must start with Setting or Control column")
            table_columns = {str(column) for column in columns if isinstance(column, str)}

            setting_keys = table.get("settings")
            if not isinstance(setting_keys, list) or not setting_keys:
                errors.append(f"{relative_path} settings table {block_id} settings must be a non-empty list")
                continue
            seen_setting_keys: set[str] = set()
            for raw_key in setting_keys:
                key = str(raw_key).strip()
                if not key:
                    errors.append(f"{relative_path} settings table {block_id} has a blank setting key")
                    continue
                if key in seen_setting_keys:
                    errors.append(f"{relative_path} settings table {block_id} includes {key} more than once")
                seen_setting_keys.add(key)
                if key not in settings_by_key:
                    errors.append(f"{relative_path} settings table {block_id} references unknown setting {key}")
                elif "Description" in table_columns and not str(settings_by_key[key].get("docs_description", "")).strip():
                    errors.append(f"{relative_path} settings table {block_id} {key} must define docs_description")
                table_ref = (relative_path, key)
                if table_ref in all_table_refs:
                    errors.append(f"{relative_path} includes {key} in more than one generated settings table")
                all_table_refs.add(table_ref)


def check_docs_table_markers(errors: list[str]) -> None:
    marker_re = re.compile(r"<!-- ESPFRAME:SETTINGS_TABLE ([A-Za-z0-9_-]+) (START|END) -->")
    expected = {
        (rel(path), block_id)
        for path, table_blocks in docs_settings_tables().items()
        for block_id in table_blocks
    }
    seen: dict[tuple[str, str], dict[str, int]] = {}
    for docs_path in sorted(ROOT.glob("docs/**/*.md")):
        relative_path = rel(docs_path)
        text = read(docs_path, errors)
        for match in marker_re.finditer(text):
            block_id, side = match.groups()
            key = (relative_path, block_id)
            if key not in expected:
                errors.append(f"{relative_path} has unregistered settings table marker {block_id}")
            seen.setdefault(key, {"START": 0, "END": 0})[side] += 1

    for key in sorted(expected):
        counts = seen.get(key, {"START": 0, "END": 0})
        if counts["START"] != 1 or counts["END"] != 1:
            errors.append(
                f"{key[0]} settings table {key[1]} needs exactly one START and one END marker "
                f"(found {counts['START']} START, {counts['END']} END)"
            )
    for key, counts in sorted(seen.items()):
        if key in expected and (counts["START"] != 1 or counts["END"] != 1):
            continue
        if key not in expected and (counts["START"] != counts["END"] or counts["START"] > 1):
            errors.append(
                f"{key[0]} unregistered settings table {key[1]} has "
                f"{counts['START']} START and {counts['END']} END markers"
            )


def check_setting(setting: dict, web_text: str, errors: list[str]) -> None:
    key = str(setting.get("key", "")).strip()
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", "")).strip()
    name = str(entity.get("name", "")).strip()
    raw_default = setting.get("default", "")
    default = str(raw_default)
    web_default = json.dumps(raw_default, separators=(",", ":"))
    docs_default = str(setting.get("docs_default", default))
    options = [str(option) for option in setting.get("options", [])]
    developer_options = [str(option) for option in setting.get("developer_options", [])]

    if not key or not domain or not name:
        errors.append(f"Setting {key or '<missing>'} needs key, entity.domain, and entity.name")
        return
    check_setting_schema(setting, errors)

    entity_id = f"{domain}/{name}"
    require_contains(web_text, f'"{entity_id}"', f"web UI mapping for {key}", errors)
    require_contains(web_text, key, f"web UI state key for {key}", errors)
    require_contains(web_text, web_default, f"web UI default for {key}", errors)
    for option in options:
        require_contains(web_text, option, f"web UI option for {key}", errors)
    for option in developer_options:
        require_contains(web_text, option, f"web UI developer option for {key}", errors)

    firmware_files = check_path_list(setting, key, "firmware_files", errors)
    for filename in firmware_files:
        text = read(ROOT / str(filename), errors)
        block = firmware_entity_block(text, name, str(filename), errors)
        for option in options:
            require_contains(block, f'"{option}"', f"{filename} option for {key}", errors)
        for option in developer_options:
            require_contains(block, f'"{option}"', f"{filename} developer option for {key}", errors)
        if entity.get("domain") == "select":
            initial_option = str(setting.get("firmware_initial_option", raw_default))
            if initial_option.startswith("${"):
                if (
                    f"initial_option: {initial_option}" not in block
                    and f'initial_option: "{initial_option}"' not in block
                ):
                    errors.append(f"{filename} initial_option for {key} is missing {initial_option!r}")
            else:
                require_contains(block, f'initial_option: "{initial_option}"', f"{filename} initial_option for {key}", errors)
        if entity.get("domain") == "number":
            for product_field, firmware_field in (
                ("default", "initial_value"),
                ("min", "min_value"),
                ("max", "max_value"),
                ("step", "step"),
            ):
                if product_field in setting:
                    value = str(setting[product_field])
                    require_contains(block, f"{firmware_field}: {value}", f"{filename} {firmware_field} for {key}", errors)
        if entity.get("domain") == "switch" and isinstance(raw_default, bool):
            restore_mode = "RESTORE_DEFAULT_ON" if raw_default else "RESTORE_DEFAULT_OFF"
            require_contains(block, f"restore_mode: {restore_mode}", f"{filename} restore_mode for {key}", errors)
        if entity.get("domain") == "text":
            require_firmware_text_entity_shape(text, name, str(filename), errors)

    docs_files = check_path_list(setting, key, "docs_files", errors)
    for filename in docs_files:
        text = read(ROOT / str(filename), errors)
        require_contains(text, docs_default, f"{filename} default for {key}", errors)
        for field in ("docs_label", "docs_description", "docs_format", "docs_type"):
            if setting.get(field):
                require_contains(text, str(setting[field]), f"{filename} {field} for {key}", errors)


def check_settings(product: dict, errors: list[str]) -> None:
    web_template = read(WEB_TEMPLATE, errors)
    web_text = read(WEB_APP, errors)
    check_web_entity_metadata(product, errors)
    check_manual_web_entity_metadata(product, errors)
    check_generated_web_metadata(product, web_text, errors)
    check_static_web_defaults_against_firmware(product, errors)
    check_web_ui_metadata(product, web_template, web_text, errors)
    check_web_template_key_references(product, web_template, errors)
    check_docs_table_metadata(product, errors)
    check_docs_table_membership(product, errors)
    check_docs_table_markers(errors)
    for placeholder in product["project"].get("web_template_placeholders", []):
        if isinstance(placeholder, str) and placeholder.strip():
            require_contains(web_template, placeholder.strip(), rel(WEB_TEMPLATE), errors)
    for needle in (
        "registerStaticEntityStateDefaults",
        "registerProductSettingStateDefaults",
        "registerManualEntityEndpoints",
        "registerProductSettingEndpoints",
        "registerManualStateEntities",
        "registerProductSettingEntities",
        "endpoints[key] = eid(parts.domain, parts.name);",
        "ENTITY_STATE_MAP[productSpec.entity] = stateSpec;",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    seen: set[str] = set()
    seen_entities: set[str] = set()
    for setting in product["settings"]:
        key = str(setting.get("key", "")).strip()
        if key in seen:
            errors.append(f"Duplicate product setting key: {key}")
        seen.add(key)
        entity = setting.get("entity") or {}
        entity_id = f'{entity.get("domain", "")}/{entity.get("name", "")}'
        if entity_id in seen_entities:
            errors.append(f"Duplicate product setting entity: {entity_id}")
        seen_entities.add(entity_id)
        check_setting(setting, web_text, errors)


def main() -> int:
    errors: list[str] = []
    product = load_product()
    check_project_metadata(product, errors)
    check_npm_package_metadata(product, errors)
    check_license_metadata(product, errors)
    check_immich_api_key_metadata(product, errors)
    check_immich_connection_metadata(product, errors)
    check_home_assistant_metadata(product, errors)
    check_device_logging_metadata(product, errors)
    check_firmware_update_metadata(product, errors)
    check_ota_update_metadata(product, errors)
    check_backup_metadata(product, errors)
    check_privacy_metadata(product, errors)
    check_touch_controls_metadata(product, errors)
    check_screen_schedule_metadata(product, errors)
    check_screen_rotation_metadata(product, errors)
    check_developer_features_metadata(product, errors)
    check_screen_tone_metadata(product, errors)
    check_clock_time_metadata(product, errors)
    check_photo_source_metadata(product, errors)
    check_connection_resilience_metadata(product, errors)
    check_setup_flow_metadata(product, errors)
    check_photo_display_metadata(product, errors)
    check_devices(product, errors)
    check_public_manifest_urls(product, errors)
    check_public_site_references(product, errors)
    check_docs_site_config(product, errors)
    check_device_workflow_contract(product, errors)
    check_generated_asset_metadata(product, errors)
    check_factory_firmware_metadata(product, errors)
    check_web_server_metadata(product, errors)
    check_external_components_metadata(product, errors)
    check_esphome_version(product, errors)
    check_node_version(product, errors)
    check_workflows(product, errors)
    check_settings(product, errors)

    if errors:
        for error in errors:
            print(f"product contract error: {error}", file=sys.stderr)
        return 1

    print("product contract validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
