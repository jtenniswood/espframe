#!/usr/bin/env python3
"""Generate checked-in ESPFrame assets from their source data.

This keeps the timezone list and the web-server bundle from drifting between
firmware YAML, C++ lookup data, and the browser UI.
"""

from __future__ import annotations

import argparse
import ast
import difflib
import importlib.util
import json
import re
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parents[1]
TIMEZONES_PATH = ROOT / "components" / "espframe" / "timezones.py"
TZ_HEADER_PATH = ROOT / "components" / "espframe" / "tz_data_generated.h"
TIME_YAML_PATH = ROOT / "common" / "addon" / "time.yaml"
WEB_SRC_DIR = ROOT / "docs" / "webserver" / "src"
WEB_TEMPLATE_PATH = WEB_SRC_DIR / "app.template.js"
WEB_STYLE_PATH = WEB_SRC_DIR / "style.css"
WEB_APP_PATH = ROOT / "docs" / "public" / "webserver" / "app.js"


def load_timezones():
    spec = importlib.util.spec_from_file_location("espframe_timezones", TIMEZONES_PATH)
    if spec is None or spec.loader is None:
        raise RuntimeError(f"Unable to load {TIMEZONES_PATH}")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def timezone_options() -> list[str]:
    module = load_timezones()
    return list(module.generate_yaml_options())


def timezone_labels() -> dict[str, str]:
    module = load_timezones()
    return dict(module.generate_web_timezone_labels())


def timezone_header() -> str:
    module = load_timezones()
    return "\n".join(
        [
            "#pragma once",
            '#include "sun_calc.h"',
            "",
            "static const TzInfo TZ_DATA[] = {",
            module.generate_cpp_tz_data(),
            "};",
            "",
            f"static constexpr int TZ_DATA_COUNT = {len(module.TIMEZONES)};",
            "",
        ]
    )


def replace_timezone_yaml(text: str, options: list[str]) -> str:
    pattern = re.compile(
        r'(?P<prefix>  - platform: template\n'
        r'    name: "Clock: Timezone"\n'
        r'(?:(?!^    initial_option:).)*?'
        r'^    options:\n)'
        r'(?P<options>(?:^      - .*\n)+)'
        r'(?P<suffix>^    initial_option:)',
        re.MULTILINE | re.DOTALL,
    )
    rendered = "".join(f'      - "{option}"\n' for option in options)
    result, count = pattern.subn(lambda m: m.group("prefix") + rendered + m.group("suffix"), text, count=1)
    if count != 1:
        raise RuntimeError(f"Unable to locate timezone options block in {TIME_YAML_PATH}")
    return result


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
    if not WEB_TEMPLATE_PATH.exists() or not WEB_STYLE_PATH.exists():
        raise RuntimeError("Webserver sources are missing. Run with --bootstrap-webserver once.")

    template = WEB_TEMPLATE_PATH.read_text()
    css = WEB_STYLE_PATH.read_text().rstrip("\n")
    timezones_json = json.dumps(timezone_options(), separators=(",", ":"))
    timezone_labels_json = json.dumps(timezone_labels(), separators=(",", ":"))
    css_json = json.dumps(css, separators=(",", ":"))
    return (
        template
        .replace("__ESPFRAME_TIMEZONES__", timezones_json)
        .replace("__ESPFRAME_TIMEZONE_LABELS__", timezone_labels_json)
        .replace("__ESPFRAME_CSS__", css_json)
    )


def write_or_check(path: Path, content: str, check: bool) -> bool:
    old = path.read_text() if path.exists() else ""
    if old == content:
        return False
    if check:
        rel = path.relative_to(ROOT)
        diff = "".join(
            difflib.unified_diff(
                old.splitlines(keepends=True),
                content.splitlines(keepends=True),
                fromfile=f"{rel} (current)",
                tofile=f"{rel} (generated)",
            )
        )
        print(diff, file=sys.stderr)
        return True
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(content)
    return True


def generate(check: bool) -> int:
    changed = False
    changed |= write_or_check(TZ_HEADER_PATH, timezone_header(), check)
    changed |= write_or_check(TIME_YAML_PATH, replace_timezone_yaml(TIME_YAML_PATH.read_text(), timezone_options()), check)
    changed |= write_or_check(WEB_APP_PATH, web_app_bundle(), check)
    if check and changed:
        print("Generated files are stale. Run `npm run generate`.", file=sys.stderr)
        return 1
    return 0


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--check", action="store_true", help="Fail if generated files are stale")
    parser.add_argument(
        "--bootstrap-webserver",
        action="store_true",
        help="Create docs/webserver/src files from the current public bundle",
    )
    args = parser.parse_args()

    if args.bootstrap_webserver:
        bootstrap_webserver_sources()
    return generate(args.check)


if __name__ == "__main__":
    raise SystemExit(main())
