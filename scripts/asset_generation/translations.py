"""Build the physical-display catalogues using EspControl's key=value format."""
from __future__ import annotations

import json
import re
from pathlib import Path

from asset_generation.paths import ROOT
from product_config import load_product

STRINGS_DIR = ROOT / "product/translations"
HEADER_PATH = ROOT / "components/espframe/i18n_generated.h"
PLACEHOLDER_RE = re.compile(r"\{[a-z][a-z0-9_]*\}")


def unescape_string(value: str) -> str:
    result: list[str] = []
    index = 0
    escapes = {"n": "\n", "\\": "\\", "=": "="}
    while index < len(value):
        if value[index] == "\\":
            index += 1
            if index == len(value) or value[index] not in escapes:
                raise ValueError("Use only \\n, \\\\ or \\= escapes")
            result.append(escapes[value[index]])
        else:
            result.append(value[index])
        index += 1
    return "".join(result)


def load_strings(path: Path) -> dict[str, str]:
    strings: dict[str, str] = {}
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), 1):
        if not line.strip() or line.lstrip().startswith("#"):
            continue
        try:
            key, value = line.split("=", 1)
            if not re.fullmatch(r"[a-z][a-z0-9_]*", key):
                raise ValueError("Keys must be lowercase snake_case")
            if key in strings:
                raise ValueError(f"Duplicate key {key!r}")
            strings[key] = unescape_string(value)
        except ValueError as error:
            raise ValueError(f"{path}:{line_number}: {error}") from error
    return strings


def load_catalogues(directory: Path = STRINGS_DIR) -> dict[str, dict[str, str]]:
    english = load_strings(directory / "strings.en.txt")
    if not english or any(not value for value in english.values()):
        raise ValueError("English catalogue must contain nonempty source strings")
    languages = {"en": english}
    for path in sorted(directory.glob("strings.*.txt")):
        code = path.stem.split(".", 1)[1]
        if code == "en":
            continue
        if not re.fullmatch(r"[a-z]{2,3}(?:-[a-z0-9]+)*", code):
            raise ValueError(f"Invalid language code: {code}")
        translated = load_strings(path)
        if translated.keys() != english.keys():
            raise ValueError(f"{path}: keys must match strings.en.txt "
                             f"(missing={sorted(english.keys() - translated.keys())}, "
                             f"extra={sorted(translated.keys() - english.keys())})")
        for key, source in english.items():
            translated[key] = translated[key] or source
            if sorted(PLACEHOLDER_RE.findall(source)) != sorted(PLACEHOLDER_RE.findall(translated[key])):
                raise ValueError(f"{path}: {key}: placeholders must match English")
        languages[code] = translated
    # Refreshing an existing status needs an unambiguous match, including when
    # two English keys share a source (May is both a full and short month).
    seen: dict[str, tuple[str, ...]] = {}
    for key in english:
        values = tuple(strings[key] for strings in languages.values())
        for value in values:
            if value in seen and seen[value] != values:
                raise ValueError(f"Ambiguous display text {value!r} at {key}")
            seen[value] = values
    return languages


def generated_translation_files() -> dict[Path, str]:
    languages = load_catalogues()
    setting = next(item for item in load_product()["settings"] if item["key"] == "device_language")
    if set(setting["options"]) != set(languages):
        raise ValueError("device_language options must match product/translations/strings.*.txt")
    cpp = lambda value: json.dumps(value, ensure_ascii=False)
    lines = [
        "// ESPFRAME: generated from product/translations; run `npm run generate` to update.",
        "#pragma once", "#include <cstddef>", "", "namespace espframe_i18n_catalogue {",
        "inline constexpr const char *LANGUAGES[] = {" + ", ".join(map(cpp, languages)) + "};",
        f"inline constexpr size_t LANGUAGE_COUNT = {len(languages)};",
        "struct Entry { const char *key; const char *values[LANGUAGE_COUNT]; };",
        "inline constexpr Entry STRINGS[] = {",
    ]
    for key in languages["en"]:
        values = ", ".join(cpp(strings[key]) for strings in languages.values())
        lines.append(f"  {{{cpp(key)}, {{{values}}}}},")
    lines.extend(["};", "}  // namespace espframe_i18n_catalogue", ""])
    return {HEADER_PATH: "\n".join(lines)}
