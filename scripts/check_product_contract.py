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
    DOCS_SETTINGS_TABLES,
    WEB_ENTITY_ALIASES,
    WEB_LOCAL_STATE_KEYS,
    WEB_MANUAL_ENTITIES,
    WEB_STATIC_ENTITIES,
    default_public_manifest_urls,
    device_public_manifest_urls,
    load_product,
    public_base_url,
    public_url,
    release_matrix_devices,
    web_entity_aliases_metadata,
    web_initial_fetch_keys,
    web_manual_entities_metadata,
    web_settings_metadata,
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
    for item in value:
        path = str(item).strip()
        if not path:
            errors.append(f"Setting {key} has a blank entry in {field}")
            continue
        if Path(path).is_absolute() or ".." in Path(path).parts:
            errors.append(f"Setting {key} has unsafe {field} path: {path}")
            continue
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
        elif raw_default and raw_default not in options and not str(setting.get("firmware_initial_option", "")).startswith("${"):
            errors.append(f"Select setting {key} default is not in options")

        if developer_options:
            if not isinstance(developer_options, list):
                errors.append(f"Select setting {key} developer_options must be a list")
            elif any(not isinstance(option, str) or not option for option in developer_options):
                errors.append(f"Select setting {key} developer_options must be non-empty strings")
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
        if options or developer_options:
            errors.append(f"Number setting {key} must not define options")
    elif domain == "switch":
        if not isinstance(raw_default, bool):
            errors.append(f"Switch setting {key} default must be true or false")
        if options or developer_options:
            errors.append(f"Switch setting {key} must not define options")
    elif domain == "text":
        if not isinstance(raw_default, str):
            errors.append(f"Text setting {key} default must be a string")
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

        for field in ("panel_url", "stand_url"):
            url = str(device.get(field, "")).strip()
            if url and not url.startswith("https://"):
                errors.append(f"Device {slug} {field} must be an https URL")


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
        "firmware_update_source",
        "firmware_beta_channel_label",
        "firmware_manual_check_behavior",
        "firmware_beta_check_requirement",
        "firmware_custom_manifest_requirement",
        "backup_filename_prefix",
        "backup_filename_date_format",
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
        "release_url_base",
        "public_base_url",
        "support_url",
        "support_button_image_url",
        "node_version",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")

    package_name = str(project.get("package_name", "")).strip()
    if package_name and not re.match(r"^[A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+$", package_name):
        errors.append("project.package_name must look like a reverse-DNS package name")

    release_url_base = str(project.get("release_url_base", "")).strip()
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    if repository_url and not repository_url.startswith("https://github.com/"):
        errors.append("project.repository_url must be an https GitHub URL")
    if release_url_base and not release_url_base.startswith("https://"):
        errors.append("project.release_url_base must be an https URL")
    if release_url_base and not release_url_base.endswith("/"):
        errors.append("project.release_url_base must end with /")
    if repository_url and release_url_base and release_url_base != f"{repository_url}/releases/tag/":
        errors.append("project.release_url_base must be based on project.repository_url")

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
    if not isinstance(project.get("backup_config_version"), int) or isinstance(project.get("backup_config_version"), bool):
        errors.append("project.backup_config_version must be an integer")
    if not isinstance(project.get("backup_import_photo_id_limit"), int) or isinstance(project.get("backup_import_photo_id_limit"), bool):
        errors.append("project.backup_import_photo_id_limit must be an integer")
    backup_excluded_values = project.get("backup_excluded_runtime_values", [])
    if not isinstance(backup_excluded_values, list) or not backup_excluded_values:
        errors.append("project.backup_excluded_runtime_values must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must only contain non-empty strings")
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
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("screen_rotation_user_options", "screen_rotation_developer_options"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
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
    for field in ("setup_captive_portal_ip", "setup_connection_ready_condition"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("setup_wizard_steps", "setup_required_connection_fields", "setup_skip_substitutions"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
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

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)

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


def check_firmware_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    methods = project.get("firmware_update_methods", [])
    source = str(project.get("firmware_update_source", "")).strip()
    channels = project.get("firmware_update_channels", [])
    beta_label = str(project.get("firmware_beta_channel_label", "")).strip()
    manual_check_behavior = str(project.get("firmware_manual_check_behavior", "")).strip()
    beta_check_requirement = str(project.get("firmware_beta_check_requirement", "")).strip()
    custom_manifest_requirement = str(project.get("firmware_custom_manifest_requirement", "")).strip()
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


def check_backup_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    config_version = project.get("backup_config_version")
    filename_prefix = str(project.get("backup_filename_prefix", "")).strip()
    date_format = str(project.get("backup_filename_date_format", "")).strip()
    photo_id_limit = project.get("backup_import_photo_id_limit")
    excluded_values = project.get("backup_excluded_runtime_values", [])

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

    for needle in (
        "display_mode",
        "display mode",
        "Partial config files work",
        "everything else stays unchanged",
    ):
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

    static_timezone_default = WEB_STATIC_ENTITIES["timezone"].get("default")
    if clock_default_timezone and static_timezone_default != clock_default_timezone:
        errors.append("project.clock_default_timezone must match the static web timezone default")
    if isinstance(clock_default_show, bool) and WEB_STATIC_ENTITIES["show_clock"].get("default") != clock_default_show:
        errors.append("project.clock_default_show must match the static web show_clock default")
    static_ntp_defaults = [
        str(WEB_STATIC_ENTITIES[key].get("default", ""))
        for key in ("ntp_server_1", "ntp_server_2", "ntp_server_3")
    ]
    if ntp_default_servers and static_ntp_defaults != ntp_default_servers:
        errors.append("project.ntp_default_servers must match the static web NTP defaults")

    install_docs = read(ROOT / "docs" / "install.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    time_yaml = read(TIME_YAML, errors)
    web_template = read(WEB_TEMPLATE, errors)

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
            ("common/addon/immich_filter.yaml", filter_yaml),
        ):
            require_contains(text, id_limit_text, label, errors)

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
    timeout_default = str(project.get("connection_timeout_default", "")).strip()
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
        'parse_duration_option_seconds(x, 600, 30, 1800)',
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
    web_app_url = public_url("webserver/app.js", product)
    project_name = str(product["project"].get("name", "")).strip()
    social_image_alt = str(product["project"].get("social_image_alt", "")).strip()
    usb_flashing_image = str(product["project"].get("usb_flashing_image", "")).strip()
    usb_flashing_image_alt = str(product["project"].get("usb_flashing_image_alt", "")).strip()
    web_installer_required_browsers = product["project"].get("web_installer_required_browsers", [])
    web_installer_required_api = str(product["project"].get("web_installer_required_api", "")).strip()
    web_installer_unsupported_browsers = product["project"].get("web_installer_unsupported_browsers", [])
    repository_url = str(product["project"].get("repository_url", "")).strip().rstrip("/")
    support_url = str(product["project"].get("support_url", "")).strip()
    support_button_image_url = str(product["project"].get("support_button_image_url", "")).strip()

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
        require_contains(license_docs, f"({repository_url}/blob/main/LICENSE)", "docs/license.md", errors)
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
            require_contains(readme, f"compile /config/{build_yaml}", "README.md compile example", errors)
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
        require_contains(config, f"pattern: '{repository_url}/edit/main/docs/:path'", "docs/.vitepress/config.mts", errors)
    if owner_name:
        require_contains(config, f"name: '{owner_name}'", "docs/.vitepress/config.mts", errors)
    if owner_url:
        require_contains(config, f"url: '{owner_url}'", "docs/.vitepress/config.mts", errors)


def check_device_workflow_contract(product: dict, errors: list[str]) -> None:
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    slugs = [str(device.get("slug", "")).strip() for device in product["devices"]]
    expected_slugs = " ".join(slugs)
    for label, text in (
        (".github/workflows/release.yml", release_workflow),
        (".github/workflows/docs.yml", docs_workflow),
    ):
        require_contains(text, f"DEVICE_SLUGS: {expected_slugs}", label, errors)

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
        require_contains(
            release_workflow,
            f"compile /config/builds/${{{{ matrix.yaml }}}}.factory.yaml",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            compile_workflow,
            f"compile /config/{build_yaml}",
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
        for prefix in ("firmware", "firmware/beta"):
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


def check_esphome_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("esphome_version", "")).strip()
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

    for path in (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "release.yml"):
        text = read(path, errors)
        require_contains(text, f"ghcr.io/esphome/esphome:{version}", rel(path), errors)


def check_workflows(errors: list[str]) -> None:
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(compile_workflow, '"product/**"', ".github/workflows/compile.yml", errors)

    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    for label, text in (
        (".github/workflows/docs.yml", docs_workflow),
        (".github/workflows/release.yml", release_workflow),
    ):
        require_contains(text, "scripts/product_config.py", label, errors)
        require_contains(text, "product/espframe.json", label, errors)


def check_node_version(product: dict, errors: list[str]) -> None:
    version = str(product["project"].get("node_version", "")).strip()
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")

    for path in (ROOT / ".github" / "workflows" / "compile.yml", ROOT / ".github" / "workflows" / "docs.yml"):
        text = read(path, errors)
        require_contains(text, f"node-version: {version}", rel(path), errors)


def check_web_entity_metadata(product: dict, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    product_entities = {
        f'{setting.get("entity", {}).get("domain", "")}/{setting.get("entity", {}).get("name", "")}'
        for setting in product["settings"]
    }
    static_entities_seen: set[str] = set()

    for key, metadata in WEB_STATIC_ENTITIES.items():
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
        if entity in product_entities:
            errors.append(f"Static web entity {key} duplicates product entity {entity}")
        for field in ("fetch", "boolFromState", "number"):
            if field in metadata and not isinstance(metadata[field], bool):
                errors.append(f"Static web entity {key} {field} must be true or false")
        if "optionsKey" in metadata and not str(metadata["optionsKey"]).strip():
            errors.append(f"Static web entity {key} optionsKey must be non-empty")

    alias_entities_seen: set[str] = set()
    valid_state_keys = product_keys | set(WEB_STATIC_ENTITIES)
    for key, aliases in WEB_ENTITY_ALIASES.items():
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
            elif entity in alias_entities_seen:
                errors.append(f"Duplicate web entity alias: {entity}")
            else:
                alias_entities_seen.add(str(entity))
            if entity in product_entities or entity in static_entities_seen:
                errors.append(f"Web entity alias {key} duplicates canonical entity {entity}")
            for field in ("boolFromState", "number"):
                if field in alias and not isinstance(alias[field], bool):
                    errors.append(f"Web entity alias {key} {field} must be true or false")
            if "optionsKey" in alias and not str(alias["optionsKey"]).strip():
                errors.append(f"Web entity alias {key} optionsKey must be non-empty")


def check_manual_web_entity_metadata(errors: list[str]) -> None:
    seen_entities: set[str] = set()
    for key, metadata in WEB_MANUAL_ENTITIES.items():
        if not isinstance(key, str) or not key.strip():
            errors.append("Manual web entity keys must be non-empty strings")
        if not isinstance(metadata, dict):
            errors.append(f"Manual web entity {key} metadata must be an object")
            continue
        entity = metadata.get("entity")
        if not valid_entity_string(entity):
            errors.append(f"Manual web entity {key} has invalid entity {entity!r}")
            continue
        if entity in seen_entities:
            errors.append(f"Duplicate manual web entity: {entity}")
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
        require_contains(text, f"name: \"{name}\"", firmware_file, errors)
        if domain == "button":
            require_contains(text, "button:", firmware_file, errors)
        elif domain == "update":
            require_contains(text, "update:", firmware_file, errors)


def check_generated_web_metadata(product: dict, web_text: str, errors: list[str]) -> None:
    product_settings = extract_js_json_var(web_text, "PRODUCT_SETTINGS", errors)
    if product_settings is not None and product_settings != web_settings_metadata(product["settings"]):
        errors.append("Generated web PRODUCT_SETTINGS does not match product/espframe.json")

    static_entities = extract_js_json_var(web_text, "STATIC_ENTITIES", errors)
    if static_entities is not None and static_entities != web_static_entities_metadata():
        errors.append("Generated web STATIC_ENTITIES does not match product_config.py")

    manual_entities = extract_js_json_var(web_text, "MANUAL_ENTITIES", errors)
    if manual_entities is not None and manual_entities != web_manual_entities_metadata():
        errors.append("Generated web MANUAL_ENTITIES does not match product_config.py")

    entity_aliases = extract_js_json_var(web_text, "ENTITY_ALIASES", errors)
    if entity_aliases is not None and entity_aliases != web_entity_aliases_metadata():
        errors.append("Generated web ENTITY_ALIASES does not match product_config.py")

    initial_fetch_keys = extract_js_json_var(web_text, "INITIAL_FETCH_KEYS", errors)
    if initial_fetch_keys is not None and initial_fetch_keys != web_initial_fetch_keys(product["settings"]):
        errors.append("Generated web INITIAL_FETCH_KEYS does not match product/espframe.json")

    firmware_manifest_urls = extract_js_json_var(web_text, "FIRMWARE_MANIFEST_URLS", errors)
    if firmware_manifest_urls is not None and firmware_manifest_urls != default_public_manifest_urls(product):
        errors.append("Generated web FIRMWARE_MANIFEST_URLS does not match product/espframe.json")

    docs_base_url = extract_js_json_var(web_text, "DOCS_BASE_URL", errors)
    if docs_base_url is not None and docs_base_url != public_base_url(product):
        errors.append("Generated web DOCS_BASE_URL does not match product/espframe.json")

    support_url = extract_js_json_var(web_text, "SUPPORT_URL", errors)
    if support_url is not None and support_url != product["project"].get("support_url"):
        errors.append("Generated web SUPPORT_URL does not match product/espframe.json")

    support_button_image_url = extract_js_json_var(web_text, "SUPPORT_BUTTON_IMAGE_URL", errors)
    if support_button_image_url is not None and support_button_image_url != product["project"].get("support_button_image_url"):
        errors.append("Generated web SUPPORT_BUTTON_IMAGE_URL does not match product/espframe.json")


def check_static_web_defaults_against_firmware(errors: list[str]) -> None:
    text = read(TIME_YAML, errors)
    timezone_default = WEB_STATIC_ENTITIES["timezone"].get("default")
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
        default = WEB_STATIC_ENTITIES[key].get("default")
        if not isinstance(default, str) or not default:
            errors.append(f"Static web entity {key} default must match the firmware substitution")
            continue
        require_contains(text, f'  {key}: "{default}"', rel(TIME_YAML), errors)
        require_contains(text, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)

    show_clock_default = WEB_STATIC_ENTITIES["show_clock"].get("default")
    if not isinstance(show_clock_default, bool):
        errors.append("Static web entity show_clock default must be true or false")
        return
    restore_mode = "RESTORE_DEFAULT_ON" if show_clock_default is True else "RESTORE_DEFAULT_OFF"
    require_contains(text, f"restore_mode: {restore_mode}", rel(TIME_YAML), errors)


def check_web_template_key_references(product: dict, web_template: str, errors: list[str]) -> None:
    product_keys = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    static_keys = set(WEB_STATIC_ENTITIES)
    manual_keys = set(WEB_MANUAL_ENTITIES)
    known_state_keys = product_keys | static_keys | set(WEB_LOCAL_STATE_KEYS)
    known_endpoint_keys = product_keys | static_keys | manual_keys

    for key in sorted(set(WEB_STATE_REF_RE.findall(web_template))):
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
    for path, table_blocks in DOCS_SETTINGS_TABLES.items():
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
    generated_docs_files = {rel(path) for path in DOCS_SETTINGS_TABLES}
    for key, setting in settings_by_key.items():
        for docs_file in [str(item) for item in setting.get("docs_files", [])]:
            if docs_file in generated_docs_files and (docs_file, key) not in table_memberships:
                errors.append(f"{key} declares {docs_file} but is not included in a generated settings table")


def check_docs_table_metadata(product: dict, errors: list[str]) -> None:
    settings_by_key = {str(setting.get("key", "")).strip() for setting in product["settings"]}
    seen_tables: set[tuple[str, str]] = set()
    all_table_refs: set[tuple[str, str]] = set()

    for path, table_blocks in DOCS_SETTINGS_TABLES.items():
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
                table_ref = (relative_path, key)
                if table_ref in all_table_refs:
                    errors.append(f"{relative_path} includes {key} in more than one generated settings table")
                all_table_refs.add(table_ref)


def check_docs_table_markers(errors: list[str]) -> None:
    marker_re = re.compile(r"<!-- ESPFRAME:SETTINGS_TABLE ([A-Za-z0-9_-]+) (START|END) -->")
    expected = {
        (rel(path), block_id)
        for path, table_blocks in DOCS_SETTINGS_TABLES.items()
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
    check_manual_web_entity_metadata(errors)
    check_generated_web_metadata(product, web_text, errors)
    check_static_web_defaults_against_firmware(errors)
    check_web_template_key_references(product, web_template, errors)
    check_docs_table_metadata(product, errors)
    check_docs_table_membership(product, errors)
    check_docs_table_markers(errors)
    require_contains(web_template, "__ESPFRAME_PRODUCT_SETTINGS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_STATIC_ENTITIES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_MANUAL_ENTITIES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_ENTITY_ALIASES__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_INITIAL_FETCH_KEYS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_FIRMWARE_MANIFEST_URLS__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_DOCS_BASE_URL__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_SUPPORT_URL__", rel(WEB_TEMPLATE), errors)
    require_contains(web_template, "__ESPFRAME_SUPPORT_BUTTON_IMAGE_URL__", rel(WEB_TEMPLATE), errors)
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
    check_firmware_update_metadata(product, errors)
    check_backup_metadata(product, errors)
    check_privacy_metadata(product, errors)
    check_touch_controls_metadata(product, errors)
    check_screen_schedule_metadata(product, errors)
    check_screen_rotation_metadata(product, errors)
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
    check_esphome_version(product, errors)
    check_node_version(product, errors)
    check_workflows(errors)
    check_settings(product, errors)

    if errors:
        for error in errors:
            print(f"product contract error: {error}", file=sys.stderr)
        return 1

    print("product contract validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
