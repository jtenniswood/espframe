"""Shared helpers for product contract validators."""

from __future__ import annotations

import json
import re
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent.parent
WEB_SRC_DIR = ROOT / "docs" / "webserver" / "src"
WEB_TEMPLATE = ROOT / "docs" / "webserver" / "src" / "app.template.ts"
WEB_APP = ROOT / "docs" / "public" / "webserver" / "app.js"
TIME_YAML = ROOT / "common" / "addon" / "time.yaml"


def rel(path: Path) -> str:
    return str(path.relative_to(ROOT))


def read(path: Path, errors: list[str]) -> str:
    if not path.is_file():
        errors.append(f"Missing file: {rel(path)}")
        return ""
    return path.read_text()


def read_web_source(errors: list[str]) -> str:
    files = [WEB_TEMPLATE] + sorted(
        path for path in WEB_SRC_DIR.glob("*.ts")
        if path.name != WEB_TEMPLATE.name
    )
    return "\n".join(read(path, errors) for path in files)


def require_contains(text: str, needle: str, label: str, errors: list[str]) -> None:
    if needle not in text:
        errors.append(f"{label} is missing {needle!r}")


def yaml_id_block(text: str, item_id: str, filename: str, errors: list[str]) -> str:
    needle = f"- id: {item_id}"
    lines = text.splitlines()
    start = next((index for index, line in enumerate(lines) if line.strip() == needle), None)
    if start is None:
        errors.append(f"{filename} is missing YAML item {item_id!r}")
        return ""

    indent = len(lines[start]) - len(lines[start].lstrip(" "))
    end = len(lines)
    for index in range(start + 1, len(lines)):
        line = lines[index]
        line_indent = len(line) - len(line.lstrip(" "))
        if line.strip().startswith("- ") and line_indent == indent:
            end = index
            break
        if line and line_indent < indent:
            end = index
            break
    return "\n".join(lines[start:end])


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


def check_relative_path(value: object, label: str, errors: list[str]) -> str:
    path = str(value or "").strip()
    if not path:
        errors.append(f"{label} is required")
        return ""
    if Path(path).is_absolute() or ".." in Path(path).parts:
        errors.append(f"{label} has unsafe path: {path}")
        return ""
    return path


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
