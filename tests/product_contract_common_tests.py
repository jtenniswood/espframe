#!/usr/bin/env python3
"""Fast tests for shared product contract helpers."""

from __future__ import annotations

import json
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.common import yaml_id_block  # noqa: E402
from product_contract.integrations import check_home_assistant_metadata  # noqa: E402
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


def test_legacy_build_metadata_aliases_are_preserved() -> None:
    product = load_product()

    for device in product["devices"]:
        assert device["platformio_flash_mode"] == "dio"
        assert device["platformio_build_flags"] == device["build_flags"][:3]


def test_p4_ledc_warning_fix_remains_narrowly_scoped() -> None:
    source = (ROOT / "components" / "ledc" / "ledc_output.cpp").read_text(encoding="utf-8")

    assert "defined(CONFIG_IDF_TARGET_ESP32P4) ||" in source
    assert "periph_module_reset(PERIPH_LEDC_MODULE);" in source
    for device in load_product()["devices"]:
        assert "-Wno-deprecated-declarations" not in device["build_flags"]


def test_contract_manifest_preserves_upgrade_boundaries() -> None:
    manifest = load_contract_manifest()

    assert manifest["contract_version"] == 2
    assert manifest["configuration_api"]["version"] == 1
    assert manifest["configuration_api"]["update_mode"] == "atomic"
    assert load_product()["project"]["backup_config_version"] in manifest["compatibility"]["backup_versions"]
    assert manifest["compatibility"]["preserve_esphome_entity_names"] is True
    assert manifest["compatibility"]["preserve_saved_preferences"] is True


def test_10inch_installer_requires_a_version_and_probes_manifests_independently() -> None:
    installer = (ROOT / "docs/.vitepress/theme/components/EspInstallButton.vue").read_text(encoding="utf-8")

    assert "const selectedDeviceId = ref('')" in installer
    assert "id: 'immich-frame'" in installer and "./firmware/manifest.json" in installer
    assert "id: 'immich-frame-v2'" in installer and "./firmware/jc8012p4a1-v2/manifest.json" in installer
    assert "id: 'immich-frame-v3'" in installer and "./firmware/jc8012p4a1-v3/manifest.json" in installer
    assert "Promise.all(devices.filter((device) => device.requirePublishedManifest).map(async (device) =>" in installer
    assert "if (response.ok) available.add(device.id)" in installer
    assert "selectedDevice.value?.manifest || ''" in installer
    assert 'id="espframe-device-version" v-model="selectedDeviceId"' in installer
    assert '<option value="" disabled>Choose your panel version</option>' in installer
    assert '<esp-web-install-button v-if="selectedDevice"' in installer

    for revision in ("v1", "v2", "v3"):
        guide = ROOT / f"docs/screens/jc8012p4a1-{revision}.md"
        assert guide.exists()


def test_community_docs_and_site_github_star_indicator_are_wired() -> None:
    config = (ROOT / "docs/.vitepress/config.mts").read_text(encoding="utf-8")
    theme = (ROOT / "docs/.vitepress/theme/index.ts").read_text(encoding="utf-8")
    stars = (ROOT / "docs/.vitepress/theme/components/GitHubStars.vue").read_text(encoding="utf-8")
    serial_logs = (ROOT / "docs/.vitepress/theme/components/USBSerialLogs.vue").read_text(encoding="utf-8")

    for page in ("partnerships", "contributing", "collect-usb-logs", "c6-recovery"):
        assert (ROOT / "docs" / f"{page}.md").is_file()
        assert f"'/{page}'" in config
    assert "nav-bar-content-after" in theme and "h(GitHubStars)" in theme
    assert "https://api.github.com/repos/jtenniswood/espframe" in stars
    assert "stargazers_count" in stars
    assert "navigator.serial.requestPort()" in serial_logs
    assert "baudRate: 115200" in serial_logs


def test_home_assistant_api_encryption_contract_is_keyless_for_every_device() -> None:
    product = load_product()
    errors: list[str] = []

    check_home_assistant_metadata(product, errors)

    assert errors == []
    for device in product["devices"]:
        device_yaml = (ROOT / device["device_yaml"]).read_text(encoding="utf-8")
        assert "api:\n" in device_yaml
        assert "  encryption:\n" in device_yaml
        assert "    key:" not in device_yaml
    for directory in (ROOT / "builds", ROOT / "common", ROOT / "devices"):
        for path in directory.rglob("*.yaml"):
            if path.name != "secrets.yaml":
                assert "api_encryption_key" not in path.read_text(encoding="utf-8")


def test_configuration_api_reads_form_post_body() -> None:
    source = (ROOT / "components" / "espframe" / "configuration_api.h").read_text(encoding="utf-8")

    assert 'request->hasArg("configuration")' in source
    assert 'request->arg("configuration")' in source


def test_configuration_api_redacts_secret_fields() -> None:
    source = (ROOT / "components" / "espframe" / "configuration_api.h").read_text(encoding="utf-8")
    generated = (ROOT / "components" / "espframe" / "configuration_contract_generated.h").read_text(encoding="utf-8")
    product = load_product()
    api_key_metadata = product["project"]["web_manual_entities"]["api_key"]

    assert api_key_metadata["secret"] is True
    assert 'root["api_key_configured"] = this->secret_configured_();' in source
    assert "if (field.secret)" in source
    assert 'root["value"] = "";' in source
    assert 'root["api_key_configured"] = configured;' in source
    assert "request->method() != HTTP_GET" in source
    assert "encode_url_path_" in source
    assert "url == encoded_path.c_str()" in source
    assert source.index("if (field.secret)") < source.index('values[field.key] = entity->state;')
    assert '{"api_key", "text", "Connection: API Key", true}' in generated
    assert "api_key" not in product["project"]["web_manual_state_keys"]
    assert "api_key" not in product["project"]["web_local_state_keys"]
    assert "api_key_configured" in product["project"]["web_local_state_keys"]


def test_secret_is_not_logged_or_hydrated_into_browser_state() -> None:
    config_yaml = (ROOT / "common" / "addon" / "immich_config.yaml").read_text(encoding="utf-8")
    runtime = (ROOT / "docs" / "webserver" / "src" / "runtime_state.ts").read_text(encoding="utf-8")
    contracts = (ROOT / "docs" / "webserver" / "src" / "web_contracts.ts").read_text(encoding="utf-8")

    assert 'logger.log:' in config_yaml
    log_lines = [line for line in config_yaml.splitlines() if "ESP_LOG" in line or "format:" in line]
    assert all("immich_api_key_text" not in line and "api_key" not in line.lower() for line in log_lines)
    assert "S.api_key" not in runtime
    assert 'if (key === "api_key") {' in contracts
    assert "if (apiKeyConfigured != null) return null;" in contracts
    assert 'legacyKeys[i] === "api_key"' in runtime
    assert 'settingSaves.receive("api_key_configured", configured);' in runtime
    assert "applyEntityToState" in runtime


def main() -> int:
    run_discovered_tests(globals())
    print("product contract common tests passed")
    return 0


def test_v3_device_artifacts_and_ota_identity_are_isolated() -> None:
    product = load_product()
    device = next(device for device in product["devices"] if device["slug"] == "immich-frame-v3")
    substitutions = device["package_substitutions"]

    assert device["engineering_sample"] is False
    assert device["public_manifest"] == "firmware/jc8012p4a1-v3/manifest.json"
    assert device["public_beta_manifest"] == "firmware/jc8012p4a1-v3/beta/manifest.json"
    assert substitutions["firmware_device_slug"] == "immich-frame-v3"
    assert substitutions["firmware_manifest_url"].endswith("/firmware/jc8012p4a1-v3/manifest.json")
    assert device["public_manifest"] != next(d["public_manifest"] for d in product["devices"] if d["slug"] == "immich-frame-v2")

    package = (ROOT / device["package_yaml"]).read_text(encoding="utf-8")
    ota_source = (ROOT / "common/addon/firmware_update.yaml").read_text(encoding="utf-8")
    assert substitutions["firmware_device_slug"] in package
    assert substitutions["firmware_manifest_url"] in package
    assert "source: ${firmware_manifest_url}" in ota_source
    assert """lambda: 'return {"${firmware_device_slug}"};'""" in ota_source

    for build in (ROOT / device["build_yaml"], ROOT / device["build_yaml"].replace(".factory.yaml", ".yaml")):
        text = build.read_text(encoding="utf-8")
        assert "packages.yaml" in text and "jc8012p4a1-v3" in text
        assert "components: [gsl3680, remote_image, ledc, espframe, mipi_dsi]" in text

if __name__ == "__main__":
    raise SystemExit(main())
