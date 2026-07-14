#!/usr/bin/env python3
"""Generate checked-in ESPFrame assets from their source data."""

from __future__ import annotations

import argparse
import difflib
import sys
from pathlib import Path

from asset_generation.device_packages import generated_device_package_files
from asset_generation.configuration_api import generated_configuration_api_files
from asset_generation.docs_tables import generated_docs, render_settings_table, setting_lookup
from asset_generation.firmware_fields import (
    generated_firmware_field_files,
    generated_firmware_setting_fields,
    generated_firmware_yaml,
)
from asset_generation.paths import (
    LEGACY_PRODUCT_PATH,
    ROOT,
    TIME_YAML_PATH,
    TZ_HEADER_PATH,
    WEB_APP_PATH,
    WEB_PUBLIC_STYLE_PATH,
    WEB_STYLE_PATH,
)
from asset_generation.product_manifest import legacy_product_manifest
from asset_generation.timezones import replace_timezone_yaml, timezone_header, timezone_options
from asset_generation.web_bundle import bootstrap_webserver_sources, web_app_bundle
from product_config import docs_settings_tables, load_product


GENERATED_JS_HEADER = "// ESPFRAME: generated from docs/webserver/src and product/contract; run `npm run generate` to update.\n"
GENERATED_CSS_HEADER = "/* ESPFRAME: generated from docs/webserver/src/style.css; run `npm run generate` to update. */\n"


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
    product = load_product()
    changed |= write_or_check(LEGACY_PRODUCT_PATH, legacy_product_manifest(product), check)
    for path, content in generated_configuration_api_files().items():
        changed |= write_or_check(path, content, check)
    all_settings = setting_lookup()
    firmware_field_configs = generated_firmware_setting_fields()
    changed |= write_or_check(TZ_HEADER_PATH, timezone_header(), check)
    changed |= write_or_check(TIME_YAML_PATH, replace_timezone_yaml(TIME_YAML_PATH.read_text(), timezone_options()), check)
    changed |= write_or_check(WEB_PUBLIC_STYLE_PATH, GENERATED_CSS_HEADER + WEB_STYLE_PATH.read_text(), check)
    changed |= write_or_check(WEB_APP_PATH, GENERATED_JS_HEADER + web_app_bundle(), check)
    for path, content in generated_device_package_files(product).items():
        changed |= write_or_check(path, content, check)
    for path in generated_firmware_field_files(all_settings, firmware_field_configs):
        changed |= write_or_check(path, generated_firmware_yaml(path, all_settings, firmware_field_configs), check)
    for path, table_blocks in docs_settings_tables().items():
        changed |= write_or_check(path, generated_docs(path, table_blocks), check)
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
