#!/usr/bin/env python3
"""Validate upgrade-sensitive compatibility contracts."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any

from product_config import (
    backup_schema,
    default_public_manifest_urls,
    load_product,
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


ROOT = Path(__file__).resolve().parent.parent
WEB_APP = ROOT / "docs" / "public" / "webserver" / "app.js"
UUID_LIST_RE = re.compile(
    r"^[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12}"
    r"(,[0-9a-fA-F]{8}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{4}-[0-9a-fA-F]{12})*$"
)

def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def load_json(path: Path, errors: list[str]) -> dict[str, Any]:
    try:
        data = json.loads(path.read_text())
    except FileNotFoundError:
        errors.append(f"Missing compatibility fixture: {rel(path)}")
        return {}
    except json.JSONDecodeError as exc:
        errors.append(f"{rel(path)} is not valid JSON: {exc}")
        return {}
    if not isinstance(data, dict):
        errors.append(f"{rel(path)} must contain a JSON object")
        return {}
    return data


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


def require_contains(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{label} is missing {needle!r}")


def check_generated_web_metadata(product: dict[str, Any], errors: list[str]) -> None:
    web_text = WEB_APP.read_text()
    expected = {
        "PRODUCT_SETTINGS": web_settings_metadata(product["settings"]),
        "STATIC_ENTITIES": web_static_entities_metadata(product),
        "MANUAL_ENTITIES": web_manual_entities_metadata(product),
        "MANUAL_STATE_KEYS": web_manual_state_keys(product),
        "ENTITY_ALIASES": web_entity_aliases_metadata(product),
        "BACKUP_CONFIG_VERSION": product["project"].get("backup_config_version"),
        "BACKUP_SCHEMA": backup_schema(product),
        "INITIAL_FETCH_KEYS": web_initial_fetch_keys(product["settings"]),
        "LIVE_RENDER_STATE_KEYS": web_live_render_state_keys(product),
        "LIVE_RENDER_STATE_PREFIXES": web_live_render_state_prefixes(product),
        "FIRMWARE_MANIFEST_URLS": default_public_manifest_urls(product),
        "DOCS_BASE_URL": public_base_url(product),
        "WEB_UI_TABS": product["project"].get("web_ui_tabs"),
        "WEB_UI_LOGS_RETAINED_LINES": product["project"].get("web_ui_logs_retained_lines"),
    }
    for name, value in expected.items():
        actual = extract_js_json_var(web_text, name, errors)
        if actual is not None and actual != value:
            errors.append(f"Generated web {name} must match product metadata")


def check_backup_version_contract(product: dict[str, Any], errors: list[str]) -> None:
    version = product["project"].get("backup_config_version")
    if version != 1:
        errors.append("Phase 4 compatibility keeps backup_config_version at 1")
    web_text = WEB_APP.read_text()
    require_contains(web_text, "var BACKUP_CONFIG_VERSION = ", rel(WEB_APP), errors)
    require_contains(web_text, "validateBackupConfigVersion", rel(WEB_APP), errors)
    require_contains(web_text, "BACKUP_VERSION_MIGRATIONS", rel(WEB_APP), errors)


def fixture_validation_errors(data: dict[str, Any], product: dict[str, Any]) -> list[str]:
    errors: list[str] = []
    project = product["project"]
    if data.get("version") != project.get("backup_config_version"):
        errors.append("missing or unsupported version")

    options = {
        str(setting["key"]): {str(option) for option in setting.get("options", [])}
        for setting in product["settings"]
    }
    photos = data.get("photos", {})
    if isinstance(photos, dict):
        for backup_key, setting_key in (
            ("source", "photo_source"),
            ("album_order", "album_order"),
            ("date_filter_mode", "date_filter_mode"),
            ("relative_unit", "relative_unit"),
            ("orientation", "photo_orientation"),
            ("display_mode", "display_mode"),
        ):
            value = photos.get(backup_key)
            if value is not None and str(value) not in options.get(setting_key, set()):
                errors.append(f"photos.{backup_key} has unsupported option")
        limit = int(project.get("backup_import_photo_id_limit", 255))
        for key in ("album_ids", "album_labels", "person_ids", "person_labels", "tag_ids", "tag_labels"):
            if len(str(photos.get(key, ""))) > limit:
                errors.append(f"photos.{key} exceeds {limit} characters")
        for key in ("album_ids", "person_ids", "tag_ids"):
            value = str(photos.get(key, "")).strip()
            if value and not UUID_LIST_RE.match(value):
                errors.append(f"photos.{key} is not a UUID list")

    firmware_updates = data.get("firmware_updates", {})
    if isinstance(firmware_updates, dict):
        for key in ("manifest_url",):
            value = str(firmware_updates.get(key, "")).strip()
            if value and not (value.startswith("http://") or value.startswith("https://")):
                errors.append(f"firmware_updates.{key} is not an HTTP URL")
    return errors


def check_accepted_fixture_group_coverage(
    accepted_paths: list[str],
    product: dict[str, Any],
    errors: list[str],
) -> None:
    project = product["project"]
    backup_export_groups = [str(group) for group in project.get("backup_export_groups", [])]
    backup_export_fields = project.get("backup_export_fields", {})
    if not isinstance(backup_export_fields, dict):
        backup_export_fields = {}

    expected_groups = set(backup_export_groups)
    expected_fields_by_group = {
        str(group): {str(field) for field in fields}
        for group, fields in backup_export_fields.items()
        if isinstance(fields, list)
    }
    covered_groups: set[str] = set()
    covered_fields_by_group: dict[str, set[str]] = {group: set() for group in expected_groups}
    for raw_path in accepted_paths:
        path = ROOT / str(raw_path)
        data = load_json(path, errors)
        if not data:
            continue
        for group in expected_groups:
            value = data.get(group)
            if isinstance(value, dict) and value:
                covered_groups.add(group)
                covered_fields_by_group.setdefault(group, set()).update(
                    str(field) for field in value if str(field) in expected_fields_by_group.get(group, set())
                )

    missing_groups = sorted(expected_groups - covered_groups)
    if missing_groups:
        errors.append(
            "Accepted compatibility fixtures must cover every backup group; missing: "
            + ", ".join(missing_groups)
        )
    missing_fields: list[str] = []
    for group in backup_export_groups:
        expected_fields = expected_fields_by_group.get(group, set())
        covered_fields = covered_fields_by_group.get(group, set())
        for field in backup_export_fields.get(group, []):
            field_name = str(field)
            if field_name in expected_fields and field_name not in covered_fields:
                missing_fields.append(f"{group}.{field_name}")
    if missing_fields:
        errors.append(
            "Accepted compatibility fixtures must cover every backup field; missing: "
            + ", ".join(missing_fields)
        )


def check_compatibility_fixtures(product: dict[str, Any], errors: list[str]) -> None:
    fixtures = product["project"].get("compatibility_fixture_files", {})
    if not isinstance(fixtures, dict) or not fixtures:
        errors.append("project.compatibility_fixture_files must be a non-empty object")
        return

    accepted = fixtures.get("accepted", [])
    if not isinstance(accepted, list) or not accepted:
        errors.append("project.compatibility_fixture_files.accepted must be a non-empty list")
    else:
        accepted_paths = [str(path) for path in accepted]
        check_accepted_fixture_group_coverage(accepted_paths, product, errors)
        for raw_path in accepted:
            path = ROOT / str(raw_path)
            data = load_json(path, errors)
            if not data:
                continue
            fixture_errors = fixture_validation_errors(data, product)
            if fixture_errors:
                errors.append(f"{rel(path)} should be import-compatible: {', '.join(fixture_errors)}")

    rejected_fields = fixtures.get("rejected_fields", [])
    if not isinstance(rejected_fields, list) or not rejected_fields:
        errors.append("project.compatibility_fixture_files.rejected_fields must be a non-empty list")
    else:
        web_text = WEB_APP.read_text()
        for item in rejected_fields:
            if not isinstance(item, dict):
                errors.append("project.compatibility_fixture_files.rejected_fields entries must be objects")
                continue
            path = ROOT / str(item.get("path", ""))
            data = load_json(path, errors)
            if data and not fixture_validation_errors(data, product):
                errors.append(f"{rel(path)} must contain at least one rejected import field")
            messages = item.get("messages", [])
            if not isinstance(messages, list) or not messages:
                errors.append(f"{rel(path)} rejected fixture must list expected web UI messages")
                continue
            for message in [str(value) for value in messages]:
                require_contains(web_text, message, rel(WEB_APP), errors)

    rejected_versions = fixtures.get("rejected_versions", [])
    if not isinstance(rejected_versions, list) or not rejected_versions:
        errors.append("project.compatibility_fixture_files.rejected_versions must be a non-empty list")
    else:
        web_text = WEB_APP.read_text()
        for item in rejected_versions:
            if not isinstance(item, dict):
                errors.append("project.compatibility_fixture_files.rejected_versions entries must be objects")
                continue
            path = ROOT / str(item.get("path", ""))
            data = load_json(path, errors)
            if data and not fixture_validation_errors(data, product):
                errors.append(f"{rel(path)} must contain a rejected backup version")
            message = str(item.get("message", "")).strip()
            if not message:
                errors.append(f"{rel(path)} rejected version fixture must list an expected web UI message")
            elif "Unsupported backup version" in message:
                require_contains(web_text, "Unsupported backup version ", rel(WEB_APP), errors)
                if "this device supports version" in message:
                    require_contains(web_text, " - this device supports version ", rel(WEB_APP), errors)
            else:
                require_contains(web_text, message, rel(WEB_APP), errors)


def main() -> int:
    product = load_product()
    errors: list[str] = []
    check_generated_web_metadata(product, errors)
    check_backup_version_contract(product, errors)
    check_compatibility_fixtures(product, errors)

    if errors:
        for error in errors:
            print(f"compatibility check failed: {error}", file=sys.stderr)
        return 1
    print("compatibility checks passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
