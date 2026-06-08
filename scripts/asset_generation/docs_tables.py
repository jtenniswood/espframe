from __future__ import annotations

import re
from pathlib import Path

from asset_generation.paths import ROOT
from product_config import DOCS_SETTINGS_TABLE_COLUMNS, settings


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
