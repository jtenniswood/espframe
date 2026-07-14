#!/usr/bin/env python3
"""Fast tests for shared product contract helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.common import yaml_id_block  # noqa: E402
from product_config import PRODUCT_PATH, load_contract_manifest, load_product  # noqa: E402
from script_test_discovery import run_discovered_tests  # noqa: E402


def test_yaml_id_block_reads_one_indented_item() -> None:
    errors: list[str] = []
    text = """\
script:
  - id: screen_schedule_boot_guard
    mode: restart
    then:
      - lambda: |-
          id(screen_schedule_asleep) = true;
      - script.execute: backlight_schedule_display_off
  - id: screen_schedule_boot_recover
    mode: restart
    then:
      - script.execute: screen_schedule_check
"""

    assert yaml_id_block(text, "screen_schedule_boot_guard", "example.yaml", errors) == """\
  - id: screen_schedule_boot_guard
    mode: restart
    then:
      - lambda: |-
          id(screen_schedule_asleep) = true;
      - script.execute: backlight_schedule_display_off"""
    assert errors == []


def test_yaml_id_block_reports_missing_items() -> None:
    errors: list[str] = []

    assert yaml_id_block("script:\n  - id: present\n", "missing", "example.yaml", errors) == ""
    assert errors == ["example.yaml is missing YAML item 'missing'"]


def test_split_contract_matches_generated_legacy_manifest() -> None:
    generated_legacy = json.loads(PRODUCT_PATH.read_text())

    assert load_product() == generated_legacy


def test_contract_manifest_preserves_upgrade_boundaries() -> None:
    manifest = load_contract_manifest()

    assert manifest["contract_version"] == 2
    assert manifest["configuration_api"]["version"] == 1
    assert manifest["configuration_api"]["update_mode"] == "atomic"
    assert load_product()["project"]["backup_config_version"] in manifest["compatibility"]["backup_versions"]
    assert manifest["compatibility"]["preserve_esphome_entity_names"] is True
    assert manifest["compatibility"]["preserve_saved_preferences"] is True


def main() -> int:
    run_discovered_tests(globals())
    print("product contract common tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
