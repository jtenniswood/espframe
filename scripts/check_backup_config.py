#!/usr/bin/env python3
"""Validate saved-configuration backup fixtures against the product contract."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path
from typing import Any

from product_config import backup_schema, load_product


ROOT = Path(__file__).resolve().parent.parent
WEB_SRC_DIR = ROOT / "docs" / "webserver" / "src"
WEB_TEMPLATE = ROOT / "docs" / "webserver" / "src" / "app.template.js"
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
        errors.append(f"Missing backup fixture: {rel(path)}")
        return {}
    except json.JSONDecodeError as exc:
        errors.append(f"{rel(path)} is not valid JSON: {exc}")
        return {}
    if not isinstance(data, dict):
        errors.append(f"{rel(path)} must contain a JSON object")
        return {}
    return data


def require_contains(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{label} is missing {needle!r}")


def web_source_text() -> str:
    files = [WEB_TEMPLATE] + sorted(
        path for path in WEB_SRC_DIR.glob("*.js")
        if path.name != WEB_TEMPLATE.name
    )
    return "\n".join(path.read_text() for path in files)


def setting_options(product: dict[str, Any]) -> dict[str, set[str]]:
    return {
        str(setting["key"]): {str(option) for option in setting.get("options", [])}
        for setting in product["settings"]
    }


def backup_schema_groups(schema: list[dict[str, Any]]) -> list[str]:
    groups: list[str] = []
    for entry in schema:
        group = str(entry.get("group", ""))
        if group and group not in groups:
            groups.append(group)
    return groups


def backup_schema_fields_by_group(schema: list[dict[str, Any]]) -> dict[str, set[str]]:
    fields_by_group: dict[str, set[str]] = {}
    for entry in schema:
        group = str(entry.get("group", ""))
        field = str(entry.get("field", ""))
        if not group or not field:
            continue
        fields_by_group.setdefault(group, set()).add(field)
    return fields_by_group


def validate_fixture(
    path: Path,
    data: dict[str, Any],
    product: dict[str, Any],
    schema_groups: list[str],
    schema_fields_by_group: dict[str, set[str]],
    errors: list[str],
) -> None:
    project = product["project"]
    label = rel(path)
    expected_version = project.get("backup_config_version")
    if data.get("version") != expected_version:
        errors.append(f"{label} version must be {expected_version}")

    groups = [key for key in data if key not in {"version", "exported_at"}]
    unknown_groups = sorted(set(groups) - set(schema_groups))
    if unknown_groups:
        errors.append(f"{label} contains unknown backup groups: {', '.join(unknown_groups)}")
    if "full" in path.stem and groups != schema_groups:
        errors.append(f"{label} full fixture groups must match backup schema groups")

    options = setting_options(product)
    photo_id_limit = int(project.get("backup_import_photo_id_limit", 255))

    for group in groups:
        value = data.get(group)
        if not isinstance(value, dict):
            errors.append(f"{label} {group} must be an object")
            continue
        allowed = schema_fields_by_group.get(group, set())
        unknown_keys = sorted(set(value) - allowed)
        if unknown_keys:
            errors.append(f"{label} {group} contains unknown keys: {', '.join(unknown_keys)}")
        if "full" in path.stem and allowed and set(value) != allowed:
            errors.append(f"{label} {group} must contain every version-1 key")

    photos = data.get("photos", {})
    if isinstance(photos, dict):
        for key, setting_key in (
            ("source", "photo_source"),
            ("album_order", "album_order"),
            ("date_filter_mode", "date_filter_mode"),
            ("relative_unit", "relative_unit"),
            ("orientation", "photo_orientation"),
            ("display_mode", "display_mode"),
        ):
            value = photos.get(key)
            if value is not None and str(value) not in options.get(setting_key, set()):
                errors.append(f"{label} photos.{key} has unsupported option {value!r}")
        for key in ("album_ids", "album_labels", "person_ids", "person_labels", "tag_ids", "tag_labels"):
            value = str(photos.get(key, ""))
            if len(value) > photo_id_limit:
                errors.append(f"{label} photos.{key} exceeds {photo_id_limit} characters")
        for key in ("album_ids", "person_ids", "tag_ids"):
            value = str(photos.get(key, "")).strip()
            if value and not UUID_LIST_RE.match(value):
                errors.append(f"{label} photos.{key} must be a comma-separated UUID list")

    clock = data.get("clock", {})
    if isinstance(clock, dict):
        servers = clock.get("ntp_servers")
        if servers is not None and (not isinstance(servers, list) or len(servers) != 3 or not all(isinstance(item, str) for item in servers)):
            errors.append(f"{label} clock.ntp_servers must be a three-item string list")


def validate_web_support(product: dict[str, Any], errors: list[str]) -> None:
    template = web_source_text()
    app = WEB_APP.read_text()
    labels_and_text = ((rel(WEB_TEMPLATE), template), (rel(WEB_APP), app))
    for label, text in labels_and_text:
        require_contains(text, "version: BACKUP_CONFIG_VERSION", label, errors)
        if label == rel(WEB_TEMPLATE):
            require_contains(text, "var BACKUP_CONFIG_VERSION = __ESPFRAME_BACKUP_CONFIG_VERSION__", label, errors)
        else:
            require_contains(text, f"var BACKUP_CONFIG_VERSION = {product['project']['backup_config_version']}", label, errors)
        require_contains(text, "JSON.stringify(data, null, 2)", label, errors)
        require_contains(text, "buildBackupExportData", label, errors)
        require_contains(text, "validateBackupConfigVersion", label, errors)
        require_contains(text, "BACKUP_VERSION_MIGRATIONS", label, errors)
        require_contains(text, "BACKUP_SCHEMA.forEach", label, errors)
        require_contains(text, "normalizeScheduleWakeTimeout(S.schedule_wake_timeout)", label, errors)
        require_contains(text, "backupImportFieldPresent", label, errors)
        require_contains(text, "backupImportFieldValue", label, errors)
        require_contains(text, "applyBackupImportField", label, errors)
        require_contains(text, "backupEntryKey(entry)", label, errors)
        require_contains(text, "Settings imported successfully", label, errors)
        for special_field in (
            "connection.immich_url",
            "connection.api_key",
            "photos.album_ids",
            "photos.album_labels",
            "photos.person_ids",
            "photos.person_labels",
            "photos.tag_ids",
            "photos.tag_labels",
            "firmware_updates.manifest_url",
            "clock.timezone",
            "clock.ntp_servers",
            "screen.schedule_wake_timeout",
            "screen.rotation",
        ):
            require_contains(text, f'case "{special_field}"', label, errors)


def main() -> int:
    product = load_product()
    project = product["project"]
    fixture_files = project.get("backup_fixture_files", [])
    errors: list[str] = []
    schema = backup_schema(product)
    schema_groups = backup_schema_groups(schema)
    schema_fields_by_group = backup_schema_fields_by_group(schema)
    if not isinstance(fixture_files, list) or not fixture_files:
        errors.append("project.backup_fixture_files must be a non-empty list")
    else:
        for raw_path in fixture_files:
            path = ROOT / str(raw_path)
            data = load_json(path, errors)
            if data:
                validate_fixture(path, data, product, schema_groups, schema_fields_by_group, errors)
    validate_web_support(product, errors)

    if errors:
        for error in errors:
            print(f"backup config validation failed: {error}", file=sys.stderr)
        return 1
    print("backup config fixtures passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
