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

IMPORT_SNIPPETS = {
    "connection": "var c = data.connection || {};",
    "photos": "var p = data.photos || {};",
    "frequency": "var f = data.frequency || {};",
    "firmware_updates": "var upd = data.firmware_updates || {};",
    "clock": "var clk = data.clock || {};",
    "screen": "var scr = data.screen || {};",
}

IMPORT_GROUP_VARS = {
    "connection": "c",
    "photos": "p",
    "frequency": "f",
    "firmware_updates": "upd",
    "clock": "clk",
    "screen": "scr",
}


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


def backup_group_keys(product: dict[str, Any], errors: list[str]) -> dict[str, set[str]]:
    raw_fields = product["project"].get("backup_export_fields", {})
    if not isinstance(raw_fields, dict) or not raw_fields:
        errors.append("project.backup_export_fields must be a non-empty object")
        return {}

    fields_by_group: dict[str, set[str]] = {}
    for raw_group, raw_values in raw_fields.items():
        group = str(raw_group).strip()
        if not group:
            errors.append("project.backup_export_fields keys must be non-empty strings")
            continue
        if not isinstance(raw_values, list) or not raw_values:
            errors.append(f"project.backup_export_fields.{group} must be a non-empty list")
            continue
        values = [str(value).strip() for value in raw_values]
        if any(not value for value in values):
            errors.append(f"project.backup_export_fields.{group} must only contain non-empty strings")
            continue
        if len(values) != len(set(values)):
            errors.append(f"project.backup_export_fields.{group} must not contain duplicate fields")
        fields_by_group[group] = set(values)
    return fields_by_group


def validate_internal_contract(
    product: dict[str, Any],
    group_keys: dict[str, set[str]],
    errors: list[str],
) -> None:
    expected_groups = {str(group) for group in product["project"].get("backup_export_groups", [])}
    configured_groups = set(group_keys)
    missing_groups = sorted(expected_groups - configured_groups)
    extra_groups = sorted(configured_groups - expected_groups)
    if missing_groups:
        errors.append(
            f"Backup checker is missing groups from product.backup_export_groups: {', '.join(missing_groups)}"
        )
    if extra_groups:
        errors.append(
            f"Backup checker has groups not listed in product.backup_export_groups: {', '.join(extra_groups)}"
        )

    expected_fields = {field for fields in group_keys.values() for field in fields}
    if len(expected_fields) != sum(len(fields) for fields in group_keys.values()):
        errors.append("project.backup_export_fields field names must be unique across groups")

    expected_group_fields = {(group, field) for group, fields in group_keys.items() for field in fields}
    schema_group_fields = {(str(entry["group"]), str(entry["field"])) for entry in backup_schema(product)}
    missing_schema = sorted(expected_group_fields - schema_group_fields)
    extra_schema = sorted(schema_group_fields - expected_group_fields)
    if missing_schema:
        fields = ", ".join(f"{group}.{field}" for group, field in missing_schema)
        errors.append(f"Backup schema is missing fields: {fields}")
    if extra_schema:
        fields = ", ".join(f"{group}.{field}" for group, field in extra_schema)
        errors.append(f"Backup schema includes unknown fields: {fields}")
    for entry in backup_schema(product):
        state_keys = entry.get("state_keys", [])
        if not isinstance(state_keys, list) or not state_keys:
            errors.append(f"Backup schema {entry['group']}.{entry['field']} must list state keys")

    missing_import_groups = sorted(expected_groups - set(IMPORT_SNIPPETS))
    extra_import_groups = sorted(set(IMPORT_SNIPPETS) - expected_groups)
    if missing_import_groups:
        errors.append(f"Backup checker import snippets are missing groups: {', '.join(missing_import_groups)}")
    if extra_import_groups:
        errors.append(f"Backup checker import snippets include unknown groups: {', '.join(extra_import_groups)}")


def validate_fixture(
    path: Path,
    data: dict[str, Any],
    product: dict[str, Any],
    group_keys: dict[str, set[str]],
    errors: list[str],
) -> None:
    project = product["project"]
    label = rel(path)
    expected_version = project.get("backup_config_version")
    if data.get("version") != expected_version:
        errors.append(f"{label} version must be {expected_version}")

    expected_groups = [str(group) for group in project.get("backup_export_groups", [])]
    groups = [key for key in data if key not in {"version", "exported_at"}]
    unknown_groups = sorted(set(groups) - set(expected_groups))
    if unknown_groups:
        errors.append(f"{label} contains unknown backup groups: {', '.join(unknown_groups)}")
    if "full" in path.stem and groups != expected_groups:
        errors.append(f"{label} full fixture groups must match product.backup_export_groups")

    options = setting_options(product)
    photo_id_limit = int(project.get("backup_import_photo_id_limit", 255))

    for group in groups:
        value = data.get(group)
        if not isinstance(value, dict):
            errors.append(f"{label} {group} must be an object")
            continue
        allowed = group_keys.get(group, set())
        unknown_keys = sorted(set(value) - allowed)
        if unknown_keys:
            errors.append(f"{label} {group} contains unknown keys: {', '.join(unknown_keys)}")
        if "full" in path.stem and allowed and set(value) != allowed:
            errors.append(f"{label} {group} must contain every version-1 key")

    photos = data.get("photos", {})
    if isinstance(photos, dict):
        for key, setting_key in (
            ("source", "photo_source"),
            ("date_filter_mode", "date_filter_mode"),
            ("relative_unit", "relative_unit"),
            ("orientation", "photo_orientation"),
            ("display_mode", "display_mode"),
        ):
            value = photos.get(key)
            if value is not None and str(value) not in options.get(setting_key, set()):
                errors.append(f"{label} photos.{key} has unsupported option {value!r}")
        for key in ("album_ids", "album_labels", "person_ids", "person_labels"):
            value = str(photos.get(key, ""))
            if len(value) > photo_id_limit:
                errors.append(f"{label} photos.{key} exceeds {photo_id_limit} characters")
        for key in ("album_ids", "person_ids"):
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
        require_contains(text, f"version: {product['project']['backup_config_version']}", label, errors)
        require_contains(text, "JSON.stringify(data, null, 2)", label, errors)
        require_contains(text, "buildBackupExportData", label, errors)
        require_contains(text, "BACKUP_SCHEMA.forEach", label, errors)
        require_contains(text, "normalizeScheduleWakeTimeout(S.schedule_wake_timeout)", label, errors)
        require_contains(text, "Settings imported successfully", label, errors)
        for group in product["project"].get("backup_export_groups", []):
            require_contains(text, IMPORT_SNIPPETS[str(group)], label, errors)
        for entry in backup_schema(product):
            group = str(entry["group"])
            field = str(entry["field"])
            var_name = IMPORT_GROUP_VARS.get(group)
            if not var_name:
                errors.append(f"Backup checker does not know import variable for group {group}")
                continue
            if field == "ntp_servers":
                require_contains(text, "if (Array.isArray(clk.ntp_servers))", label, errors)
            else:
                require_contains(text, f"if ({var_name}.{field} !== undefined)", label, errors)


def main() -> int:
    product = load_product()
    project = product["project"]
    fixture_files = project.get("backup_fixture_files", [])
    errors: list[str] = []
    group_keys = backup_group_keys(product, errors)
    if not isinstance(fixture_files, list) or not fixture_files:
        errors.append("project.backup_fixture_files must be a non-empty list")
    else:
        for raw_path in fixture_files:
            path = ROOT / str(raw_path)
            data = load_json(path, errors)
            if data:
                validate_fixture(path, data, product, group_keys, errors)
    validate_internal_contract(product, group_keys, errors)
    validate_web_support(product, errors)

    if errors:
        for error in errors:
            print(f"backup config validation failed: {error}", file=sys.stderr)
        return 1
    print("backup config fixtures passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
