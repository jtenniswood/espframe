from __future__ import annotations

import ast
import json
import re
import subprocess
import tempfile
import textwrap

from asset_generation.paths import (
    ROOT,
    WEB_APP_PATH,
    WEB_MODULE_PATHS,
    WEB_IMPORTED_MODULE_PATHS,
    WEB_SRC_DIR,
    WEB_STYLE_PATH,
    WEB_SUPPORT_BUTTON_IMAGE_PATH,
    WEB_TEMPLATE_PATH,
)
from asset_generation.timezones import timezone_labels, timezone_options
from asset_generation.configuration_api import configuration_capabilities
from product_config import (
    backup_schema,
    load_product,
    project_value,
    public_base_url,
    web_entity_aliases_metadata,
    web_firmware_manifest_urls,
    web_initial_fetch_keys,
    web_live_render_state_keys,
    web_live_render_state_prefixes,
    web_manual_entities_metadata,
    web_manual_state_keys,
    web_settings_metadata,
    web_static_entities_metadata,
    web_ui_cards_metadata,
)


PLACEHOLDER_RE = re.compile(r"__ESPFRAME_[A-Z0-9_]+__")
# api_client.ts is an authored imported web source and is bundled through app.template.ts.


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


def replace_placeholder_once(text: str, placeholder: str, value: str) -> str:
    count = text.count(placeholder)
    if count != 1:
        raise RuntimeError(f"Expected exactly one {placeholder} placeholder, found {count}")
    return text.replace(placeholder, value, 1)


def assert_no_unreplaced_placeholders(text: str) -> None:
    leftovers = sorted(set(PLACEHOLDER_RE.findall(text)))
    if leftovers:
        raise RuntimeError("Unreplaced web app placeholders: " + ", ".join(leftovers))


def compile_web_source(path: str | Path, *options: str) -> str:
    compiled = subprocess.run(
        ["node", str(ROOT / "scripts" / "build_web_app.mjs"), str(path), *options],
        cwd=ROOT,
        capture_output=True,
        text=True,
    )
    if compiled.returncode:
        raise RuntimeError("Web app bundle failed:\n" + compiled.stdout + compiled.stderr)
    return compiled.stdout


def compile_typescript(source: str, *options: str) -> str:
    with tempfile.NamedTemporaryFile("w", suffix=".ts", encoding="utf-8") as source_file:
        source_file.write(source)
        source_file.flush()
        return compile_web_source(source_file.name, *options)


def setting_state_types() -> str:
    fields = {}
    for key, spec in {**web_static_entities_metadata(), **web_settings_metadata()}.items():
        domain = spec.get("domain") or spec.get("entity", "").split("/", 1)[0]
        fields[key] = "boolean" if domain == "switch" else "number" if domain == "number" else "string"
    for key in web_manual_state_keys():
        fields[key] = "string"
    return "{ " + "; ".join(f"{json.dumps(key)}: {value}" for key, value in fields.items()) + " }"


def legacy_shared_controls(source: str) -> str:
    # The ESPHome bundle keeps its existing helper order for its gzip budget.
    # The native entry imports the same definitions as a normal TS module.
    controls = (WEB_SRC_DIR / "shared_controls.ts").read_text()
    blocks = dict(re.findall(r"^// Shared controls: ([A-Z]+)\n(.*?)(?=^// Shared controls: |\Z)", controls, re.M | re.S))
    for placeholder, name in {
        "__ESPFRAME_SHARED_CONTROL_SELECT__": "SELECT",
        "__ESPFRAME_SHARED_CONTROL_ELEMENT__": "ELEMENT",
        "__ESPFRAME_SHARED_CONTROL_ACTIONS__": "ACTIONS",
        "__ESPFRAME_SHARED_CONTROL_CARDS__": "CARDS",
        "__ESPFRAME_SHARED_CONTROL_FIELD__": "FIELD",
    }.items():
        fragment = re.sub(r"^export ", "", blocks[name].strip(), flags=re.M)
        source = replace_placeholder_once(source, f"  {placeholder}", textwrap.indent(fragment, "  "))
    return source


def web_app_source(embedded_css: str | None = None) -> str:
    source_paths = [
        WEB_TEMPLATE_PATH,
        *WEB_IMPORTED_MODULE_PATHS,
        WEB_STYLE_PATH,
        WEB_SUPPORT_BUTTON_IMAGE_PATH,
        *WEB_MODULE_PATHS.values(),
    ]
    if not all(path.exists() for path in source_paths):
        raise RuntimeError("Webserver sources are missing. Run with --bootstrap-webserver once.")

    template = WEB_TEMPLATE_PATH.read_text()
    web_modules = {
        placeholder: path.read_text().rstrip("\n")
        for placeholder, path in WEB_MODULE_PATHS.items()
    }
    web_modules["__ESPFRAME_WEB_LIVE_HELPERS__"] = legacy_shared_controls(web_modules["__ESPFRAME_WEB_LIVE_HELPERS__"])
    css = WEB_STYLE_PATH.read_text().rstrip("\n") if embedded_css is None else embedded_css
    timezones_json = json.dumps(timezone_options(), separators=(",", ":"))
    timezone_labels_json = json.dumps(timezone_labels(), separators=(",", ":"))
    product_settings_json = json.dumps(web_settings_metadata(), separators=(",", ":"))
    static_entities_json = json.dumps(web_static_entities_metadata(), separators=(",", ":"))
    manual_entities_json = json.dumps(web_manual_entities_metadata(), separators=(",", ":"))
    manual_state_keys_json = json.dumps(web_manual_state_keys(), separators=(",", ":"))
    entity_aliases_json = json.dumps(web_entity_aliases_metadata(), separators=(",", ":"))
    backup_schema_json = json.dumps(backup_schema(), separators=(",", ":"))
    backup_config_version_json = json.dumps(load_product()["project"].get("backup_config_version"), separators=(",", ":"))
    initial_fetch_keys_json = json.dumps(web_initial_fetch_keys(), separators=(",", ":"))
    live_render_state_keys_json = json.dumps(web_live_render_state_keys(), separators=(",", ":"))
    live_render_state_prefixes_json = json.dumps(web_live_render_state_prefixes(), separators=(",", ":"))
    firmware_manifest_urls_json = json.dumps(web_firmware_manifest_urls(), separators=(",", ":"))
    firmware_device_slug_json = json.dumps(str(load_product()["devices"][0]["slug"]), separators=(",", ":"))
    docs_base_url_json = json.dumps(public_base_url(), separators=(",", ":"))
    web_ui_tabs_json = json.dumps(load_product()["project"].get("web_ui_tabs", []), separators=(",", ":"))
    web_ui_cards_json = json.dumps(web_ui_cards_metadata(), separators=(",", ":"))
    web_ui_logs_retained_lines_json = json.dumps(load_product()["project"].get("web_ui_logs_retained_lines"), separators=(",", ":"))
    support_url_json = json.dumps(project_value("support_url"), separators=(",", ":"))
    support_button_image_data_uri_json = json.dumps(
        "data:image/webp;base64," + WEB_SUPPORT_BUTTON_IMAGE_PATH.read_text().strip(),
        separators=(",", ":"),
    )
    css_json = json.dumps(css, separators=(",", ":"))
    configuration_capabilities_path_json = json.dumps(configuration_capabilities()["capabilities_path"])
    bundle = template
    for placeholder, module_source in web_modules.items():
        bundle = replace_placeholder_once(bundle, placeholder, module_source)

    replacements = {
        "__ESPFRAME_SETTING_STATE_TYPES__": setting_state_types(),
        "__ESPFRAME_TIMEZONES__": timezones_json,
        "__ESPFRAME_TIMEZONE_LABELS__": timezone_labels_json,
        "__ESPFRAME_PRODUCT_SETTINGS__": product_settings_json,
        "__ESPFRAME_STATIC_ENTITIES__": static_entities_json,
        "__ESPFRAME_MANUAL_ENTITIES__": manual_entities_json,
        "__ESPFRAME_MANUAL_STATE_KEYS__": manual_state_keys_json,
        "__ESPFRAME_ENTITY_ALIASES__": entity_aliases_json,
        "__ESPFRAME_BACKUP_CONFIG_VERSION__": backup_config_version_json,
        "__ESPFRAME_BACKUP_SCHEMA__": backup_schema_json,
        "__ESPFRAME_INITIAL_FETCH_KEYS__": initial_fetch_keys_json,
        "__ESPFRAME_LIVE_RENDER_STATE_KEYS__": live_render_state_keys_json,
        "__ESPFRAME_LIVE_RENDER_STATE_PREFIXES__": live_render_state_prefixes_json,
        "__ESPFRAME_FIRMWARE_MANIFEST_URLS__": firmware_manifest_urls_json,
        "__ESPFRAME_FIRMWARE_DEVICE_SLUG__": firmware_device_slug_json,
        "__ESPFRAME_DOCS_BASE_URL__": docs_base_url_json,
        "__ESPFRAME_WEB_UI_TABS__": web_ui_tabs_json,
        "__ESPFRAME_WEB_UI_CARDS__": web_ui_cards_json,
        "__ESPFRAME_WEB_UI_LOGS_RETAINED_LINES__": web_ui_logs_retained_lines_json,
        "__ESPFRAME_SUPPORT_URL__": support_url_json,
        "__ESPFRAME_SUPPORT_BUTTON_IMAGE_DATA_URI__": support_button_image_data_uri_json,
        "__ESPFRAME_CONFIGURATION_CAPABILITIES_PATH__": configuration_capabilities_path_json,
        "__ESPFRAME_CSS__": css_json,
    }
    for placeholder, value in replacements.items():
        bundle = replace_placeholder_once(bundle, placeholder, value)
    assert_no_unreplaced_placeholders(bundle)
    return bundle


def web_app_bundle(*, native_admin: bool = False) -> str:
    if native_admin:
        return compile_typescript(web_app_source(""))
    css = compile_web_source(WEB_STYLE_PATH, "--embedded-css")
    return compile_typescript(web_app_source(css), "--esphome-admin")


def native_web_app_bundle() -> str:
    source = (WEB_SRC_DIR / "native_settings.ts").read_text()
    replacements = {
        "__ESPFRAME_NATIVE_PRODUCT_SETTINGS__": web_settings_metadata(),
        "__ESPFRAME_NATIVE_TIMEZONES__": timezone_options(),
        "__ESPFRAME_NATIVE_TIMEZONE_LABELS__": timezone_labels(),
        "__ESPFRAME_NATIVE_CAPABILITIES_PATH__": configuration_capabilities()["capabilities_path"],
    }
    for placeholder, value in replacements.items():
        source = replace_placeholder_once(source, placeholder, json.dumps(value, separators=(",", ":")))
    assert_no_unreplaced_placeholders(source)
    return compile_typescript(source)
