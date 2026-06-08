from __future__ import annotations

import importlib.util
import re

from asset_generation.paths import TIMEZONES_PATH, TIME_YAML_PATH


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
