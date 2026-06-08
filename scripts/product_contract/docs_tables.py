"""Validate generated documentation table metadata."""

from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import ROOT, read, rel
from product_config import DOCS_SETTINGS_TABLE_COLUMNS, docs_settings_tables


DOCS_TABLE_ID_RE = re.compile(r"^[A-Za-z0-9_-]+$")


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
