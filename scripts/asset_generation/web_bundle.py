from __future__ import annotations

import ast
import json
import re

from asset_generation.paths import (
    WEB_APP_PATH,
    WEB_COMPAT_HELPERS_PATH,
    WEB_MODULE_PATHS,
    WEB_SRC_DIR,
    WEB_STYLE_PATH,
    WEB_TEMPLATE_PATH,
)
from asset_generation.timezones import timezone_labels, timezone_options
from product_config import (
    backup_schema,
    default_public_manifest_urls,
    load_product,
    project_value,
    public_base_url,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_live_render_state_keys,
    web_live_render_state_prefixes,
    web_manual_entities_metadata,
    web_manual_state_keys,
    web_settings_metadata,
    web_static_entities_metadata,
)


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
    backup_schema_json = json.dumps(backup_schema(), separators=(",", ":"))
    backup_config_version_json = json.dumps(load_product()["project"].get("backup_config_version"), separators=(",", ":"))
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
        .replace("__ESPFRAME_BACKUP_CONFIG_VERSION__", backup_config_version_json)
        .replace("__ESPFRAME_BACKUP_SCHEMA__", backup_schema_json)
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
