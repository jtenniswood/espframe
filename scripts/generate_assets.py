#!/usr/bin/env python3
"""Generate checked-in ESPFrame assets from their source data.

This keeps the timezone list and the web-server bundle from drifting between
firmware YAML, C++ lookup data, and the browser UI.
"""

from __future__ import annotations

import argparse
import ast
import difflib
import importlib.util
import json
import re
import sys
from pathlib import Path

from product_config import (
    DOCS_SETTINGS_TABLE_COLUMNS,
    default_public_manifest_urls,
    docs_settings_tables,
    load_product,
    project_value,
    public_base_url,
    settings,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_live_render_state_keys,
    web_live_render_state_prefixes,
    web_manual_entities_metadata,
    web_manual_state_keys,
    web_settings_metadata,
    web_static_entities_metadata,
)


ROOT = Path(__file__).resolve().parents[1]
TIMEZONES_PATH = ROOT / "components" / "espframe" / "timezones.py"
TZ_HEADER_PATH = ROOT / "components" / "espframe" / "tz_data_generated.h"
TIME_YAML_PATH = ROOT / "common" / "addon" / "time.yaml"
WEB_SRC_DIR = ROOT / "docs" / "webserver" / "src"
WEB_TEMPLATE_PATH = WEB_SRC_DIR / "app.template.js"
WEB_MODULE_PATHS = {
    "__ESPFRAME_WEB_APP_SHELL__": WEB_SRC_DIR / "app_shell.js",
    "__ESPFRAME_WEB_ENDPOINTS__": WEB_SRC_DIR / "endpoints.js",
    "__ESPFRAME_WEB_RUNTIME_STATE__": WEB_SRC_DIR / "runtime_state.js",
    "__ESPFRAME_WEB_STARTUP_WIZARD__": WEB_SRC_DIR / "startup_wizard.js",
    "__ESPFRAME_WEB_SETTINGS_CONTROLS__": WEB_SRC_DIR / "settings_controls.js",
    "__ESPFRAME_WEB_LIVE_HELPERS__": WEB_SRC_DIR / "live_helpers.js",
    "__ESPFRAME_WEB_BACKUP_IMPORT__": WEB_SRC_DIR / "backup_import.js",
}
WEB_COMPAT_HELPERS_PATH = WEB_SRC_DIR / "compat.js"
WEB_STYLE_PATH = WEB_SRC_DIR / "style.css"
WEB_PUBLIC_STYLE_PATH = ROOT / "docs" / "public" / "webserver" / "style.css"
WEB_APP_PATH = ROOT / "docs" / "public" / "webserver" / "app.js"

def load_timezones():
    spec = importlib.util.spec_from_file_location("espframe_timezones", TIMEZONES_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {TIMEZONES_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def timezone_options() -> list[str]:
    module = load_timezones()
    return list(module.generate_yaml_options())


def timezone_labels() -> dict[str, str]:
    module = load_timezones()
    return dict(module.generate_web_timezone_labels())


def timezone_header() -> str:
    module = load_timezones()
    return "\n".join(
        [
            "#pragma once",
            '#include "sun_calc.h"',
            "",
            "static const TzInfo TZ_DATA[] = {",
            module.generate_cpp_tz_data(),
            "};",
            "",
            f"static constexpr int TZ_DATA_COUNT = {len(module.TIMEZONES)};",
            "",
        ]
    )


def replace_timezone_yaml(text: str, options: list[str]) -> str:
    pattern = re.compile(
        r'(?P<prefix>  - platform: template\n'
        r'    name: "Clock: Timezone"\n'
        r'(?:(?!^    initial_option:).)*?'
        r'^    options:\n)'
        r'(?P<options>(?:^      - .*\n)+)'
        r'(?P<suffix>^    initial_option:)',
        re.MULTILINE | re.DOTALL,
    )
    rendered = "".join(f'      - "{option}"\n' for option in options)
    result, count = pattern.subn(lambda m: m.group("prefix") + rendered + m.group("suffix"), text, count=1)
    if count != 1:
        raise RuntimeError(f"Unable to locate timezone options block in {TIME_YAML_PATH}")
    return result


def extract_first_array_block(text: str, var_name: str) -> tuple[int, int]:
    start = text.index(f"  var {var_name} = [")
    depth = 0
    in_string = False
    escape = False
    for i in range(start, len(text)):
        ch = text[i]
        if in_string:
            if escape:
                escape = False
            elif ch == "\\":
                escape = True
            elif ch == '"':
                in_string = False
            continue
        if ch == '"':
            in_string = True
        elif ch == "[":
            depth += 1
        elif ch == "]":
            depth -= 1
            if depth == 0:
                end = text.index(";", i) + 1
                return start, end
    raise RuntimeError(f"Unable to locate {var_name} array in {WEB_APP_PATH}")


def extract_css_assignment(text: str) -> tuple[int, int, str]:
    marker = "  var CSS ="
    start = text.index(marker)
    style_marker = '\n\n  var style = document.createElement("style");'
    end = text.index(style_marker, start)
    assignment = text[start:end]
    literals = re.findall(r'"(?:\\.|[^"\\])*"', assignment, flags=re.DOTALL)
    if not literals:
        raise RuntimeError("Unable to extract CSS string literals from web app")
    css = "".join(ast.literal_eval(item) for item in literals)
    return start, end, css


def bootstrap_webserver_sources() -> None:
    """Create editable web UI sources from the current shipped bundle."""
    WEB_SRC_DIR.mkdir(parents=True, exist_ok=True)
    text = WEB_APP_PATH.read_text()

    tz_start, tz_end = extract_first_array_block(text, "TIMEZONES")
    text = text[:tz_start] + "  var TIMEZONES = __ESPFRAME_TIMEZONES__;" + text[tz_end:]

    label_marker = "  var TIMEZONE_LABELS = "
    try:
        label_start = text.index(label_marker)
        label_end = text.index(";", label_start) + 1
        text = text[:label_start] + "  var TIMEZONE_LABELS = __ESPFRAME_TIMEZONE_LABELS__;" + text[label_end:]
    except ValueError:
        text = text.replace(
            "  var TIMEZONES = __ESPFRAME_TIMEZONES__;",
            "  var TIMEZONES = __ESPFRAME_TIMEZONES__;\n"
            "  var TIMEZONE_LABELS = __ESPFRAME_TIMEZONE_LABELS__;",
            1,
        )

    css_start, css_end, css = extract_css_assignment(text)
    text = text[:css_start] + "  var CSS = __ESPFRAME_CSS__;" + text[css_end:]

    WEB_TEMPLATE_PATH.write_text(text)
    WEB_STYLE_PATH.write_text(css + "\n")


def web_app_bundle() -> str:
    source_paths = [WEB_TEMPLATE_PATH, WEB_COMPAT_HELPERS_PATH, WEB_STYLE_PATH, *WEB_MODULE_PATHS.values()]
    if not all(path.exists() for path in source_paths):
        raise RuntimeError("Webserver sources are missing. Run with --bootstrap-webserver once.")

    template = WEB_TEMPLATE_PATH.read_text()
    compat_helpers = WEB_COMPAT_HELPERS_PATH.read_text().rstrip("\n")
    web_modules = {
        placeholder: path.read_text().rstrip("\n")
        for placeholder, path in WEB_MODULE_PATHS.items()
    }
    css = WEB_STYLE_PATH.read_text().rstrip("\n")
    timezones_json = json.dumps(timezone_options(), separators=(",", ":"))
    timezone_labels_json = json.dumps(timezone_labels(), separators=(",", ":"))
    product_settings_json = json.dumps(web_settings_metadata(), separators=(",", ":"))
    static_entities_json = json.dumps(web_static_entities_metadata(), separators=(",", ":"))
    manual_entities_json = json.dumps(web_manual_entities_metadata(), separators=(",", ":"))
    manual_state_keys_json = json.dumps(web_manual_state_keys(), separators=(",", ":"))
    entity_aliases_json = json.dumps(web_entity_aliases_metadata(), separators=(",", ":"))
    initial_fetch_keys_json = json.dumps(web_initial_fetch_keys(), separators=(",", ":"))
    live_render_state_keys_json = json.dumps(web_live_render_state_keys(), separators=(",", ":"))
    live_render_state_prefixes_json = json.dumps(web_live_render_state_prefixes(), separators=(",", ":"))
    firmware_manifest_urls_json = json.dumps(default_public_manifest_urls(), separators=(",", ":"))
    docs_base_url_json = json.dumps(public_base_url(), separators=(",", ":"))
    web_ui_tabs_json = json.dumps(load_product()["project"].get("web_ui_tabs", []), separators=(",", ":"))
    web_ui_logs_retained_lines_json = json.dumps(load_product()["project"].get("web_ui_logs_retained_lines"), separators=(",", ":"))
    support_url_json = json.dumps(project_value("support_url"), separators=(",", ":"))
    support_button_image_url_json = json.dumps(project_value("support_button_image_url"), separators=(",", ":"))
    css_json = json.dumps(css, separators=(",", ":"))
    bundle = template
    for placeholder, module_source in web_modules.items():
        bundle = bundle.replace(placeholder, module_source)
    return (
        bundle
        .replace("__ESPFRAME_TIMEZONES__", timezones_json)
        .replace("__ESPFRAME_TIMEZONE_LABELS__", timezone_labels_json)
        .replace("__ESPFRAME_PRODUCT_SETTINGS__", product_settings_json)
        .replace("__ESPFRAME_STATIC_ENTITIES__", static_entities_json)
        .replace("__ESPFRAME_MANUAL_ENTITIES__", manual_entities_json)
        .replace("__ESPFRAME_MANUAL_STATE_KEYS__", manual_state_keys_json)
        .replace("__ESPFRAME_ENTITY_ALIASES__", entity_aliases_json)
        .replace("__ESPFRAME_INITIAL_FETCH_KEYS__", initial_fetch_keys_json)
        .replace("__ESPFRAME_LIVE_RENDER_STATE_KEYS__", live_render_state_keys_json)
        .replace("__ESPFRAME_LIVE_RENDER_STATE_PREFIXES__", live_render_state_prefixes_json)
        .replace("__ESPFRAME_FIRMWARE_MANIFEST_URLS__", firmware_manifest_urls_json)
        .replace("__ESPFRAME_DOCS_BASE_URL__", docs_base_url_json)
        .replace("__ESPFRAME_WEB_UI_TABS__", web_ui_tabs_json)
        .replace("__ESPFRAME_WEB_UI_LOGS_RETAINED_LINES__", web_ui_logs_retained_lines_json)
        .replace("__ESPFRAME_SUPPORT_URL__", support_url_json)
        .replace("__ESPFRAME_SUPPORT_BUTTON_IMAGE_URL__", support_button_image_url_json)
        .replace("__ESPFRAME_WEB_COMPAT_HELPERS__", compat_helpers)
        .replace("__ESPFRAME_CSS__", css_json)
    )


def setting_lookup() -> dict[str, dict[str, object]]:
    return {str(setting["key"]): setting for setting in settings()}


def docs_setting_label(setting: dict[str, object]) -> str:
    if setting.get("docs_label"):
        return str(setting["docs_label"])
    entity = setting.get("entity") or {}
    name = str(entity.get("name", ""))
    return name.split(": ", 1)[-1]


def docs_setting_default(setting: dict[str, object]) -> str:
    return str(setting.get("docs_default", setting.get("default", "")))


def docs_setting_description(setting: dict[str, object]) -> str:
    if setting.get("docs_description"):
        return str(setting["docs_description"])
    parts: list[str] = []
    if "min" in setting and "max" in setting:
        parts.append(f'{setting["min"]}-{setting["max"]}')
    if "step" in setting:
        parts.append(f'step {setting["step"]}')
    return ", ".join(parts)


def docs_setting_type(setting: dict[str, object]) -> str:
    if setting.get("docs_type"):
        return str(setting["docs_type"])
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", ""))
    return domain.replace("_", " ").title()


def docs_setting_format(setting: dict[str, object]) -> str:
    if setting.get("docs_format"):
        return str(setting["docs_format"])
    if setting.get("options"):
        return "Select"
    entity = setting.get("entity") or {}
    domain = str(entity.get("domain", ""))
    if domain == "switch":
        return "Toggle"
    if domain == "number":
        return "Number"
    return docs_setting_type(setting)


def render_docs_cell(column: str, setting: dict[str, object]) -> str:
    if column not in DOCS_SETTINGS_TABLE_COLUMNS:
        raise RuntimeError(f"Unsupported generated docs table column: {column}")
    if column in ("Setting", "Control"):
        return f"**{docs_setting_label(setting)}**"
    if column == "Default":
        return docs_setting_default(setting)
    if column == "Description":
        return docs_setting_description(setting)
    if column == "Type":
        return docs_setting_type(setting)
    if column == "Format":
        return docs_setting_format(setting)
    raise RuntimeError(f"Unhandled generated docs table column: {column}")


def render_settings_table(
    setting_keys: list[str],
    all_settings: dict[str, dict[str, object]],
    columns: list[str] | None = None,
) -> str:
    columns = columns or ["Setting", "Default", "Description"]
    lines = [
        "| " + " | ".join(columns) + " |",
        "|" + "|".join("-" * (len(column) + 2) for column in columns) + "|",
    ]
    for key in setting_keys:
        setting = all_settings[key]
        lines.append("| " + " | ".join(render_docs_cell(column, setting) for column in columns) + " |")
    return "\n".join(lines)


def replace_marked_block(text: str, block_id: str, content: str, path: Path) -> str:
    start_marker = f"<!-- ESPFRAME:SETTINGS_TABLE {block_id} START -->"
    end_marker = f"<!-- ESPFRAME:SETTINGS_TABLE {block_id} END -->"
    pattern = re.compile(
        f"{re.escape(start_marker)}.*?{re.escape(end_marker)}",
        re.DOTALL,
    )
    replacement = f"{start_marker}\n{content}\n{end_marker}"
    result, count = pattern.subn(replacement, text, count=1)
    if count != 1:
        raise RuntimeError(f"Unable to locate settings table block {block_id!r} in {path.relative_to(ROOT)}")
    return result


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


def generated_docs(path: Path, table_blocks: dict[str, dict[str, object]]) -> str:
    text = path.read_text()
    all_settings = setting_lookup()
    for block_id, table in table_blocks.items():
        setting_keys = [str(key) for key in table["settings"]]
        columns = [str(column) for column in table.get("columns", [])] or None
        missing = [key for key in setting_keys if key not in all_settings]
        if missing:
            raise RuntimeError(f"{path.relative_to(ROOT)} references unknown settings: {', '.join(missing)}")
        text = replace_marked_block(text, block_id, render_settings_table(setting_keys, all_settings, columns), path)
    return text


def write_or_check(path: Path, content: str, check: bool) -> bool:
    old = path.read_text() if path.exists() else ""
    if old == content:
        return False
    if check:
        rel = path.relative_to(ROOT)
        diff = "".join(
            difflib.unified_diff(
                old.splitlines(keepends=True),
                content.splitlines(keepends=True),
                fromfile=f"{rel} (current)",
                tofile=f"{rel} (generated)",
            )
        )
        print(diff, file=sys.stderr)
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    return True


def generate(check: bool) -> int:
    changed = False
    all_settings = setting_lookup()
    firmware_field_configs = generated_firmware_setting_fields()
    changed |= write_or_check(TZ_HEADER_PATH, timezone_header(), check)
    changed |= write_or_check(TIME_YAML_PATH, replace_timezone_yaml(TIME_YAML_PATH.read_text(), timezone_options()), check)
    changed |= write_or_check(WEB_PUBLIC_STYLE_PATH, WEB_STYLE_PATH.read_text(), check)
    changed |= write_or_check(WEB_APP_PATH, web_app_bundle(), check)
    for path in generated_firmware_field_files(all_settings, firmware_field_configs):
        changed |= write_or_check(path, generated_firmware_yaml(path, all_settings, firmware_field_configs), check)
    for path, table_blocks in docs_settings_tables().items():
        changed |= write_or_check(path, generated_docs(path, table_blocks), check)
    if check and changed:
        print("Generated files are stale. Run `npm run generate`.", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Fail if generated files are stale")
    parser.add_argument(
        "--bootstrap-webserver",
        action="store_true",
        help="Create docs/webserver/src files from the current public bundle",
    )
    args = parser.parse_args()

    if args.bootstrap_webserver:
        bootstrap_webserver_sources()
    return generate(args.check)


if __name__ == "__main__":
    raise SystemExit(main())
