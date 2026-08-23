from __future__ import annotations

from product_contract.common import (
    ROOT,
    WEB_APP,
    WEB_TEMPLATE,
    read,
    read_web_source,
    rel,
    require_contains,
    yaml_id_block,
)
from product_config import default_public_manifest_urls


def check_device_logging_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    default_level = str(project.get("device_log_level_default", "")).strip()
    component_levels = project.get("device_log_component_levels", {})

    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    if default_level:
        require_contains(device_yaml, f'log_level: "{default_level}"', device_yaml_path, errors)
        require_contains(device_yaml, "level: ${log_level}", device_yaml_path, errors)
    require_contains(device_yaml, "logger:", device_yaml_path, errors)
    require_contains(device_yaml, "logs:", device_yaml_path, errors)
    if isinstance(component_levels, dict):
        for component, level in component_levels.items():
            if not isinstance(component, str) or not isinstance(level, str):
                continue
            component_name = component.strip()
            log_level = level.strip()
            if component_name and log_level:
                require_contains(device_yaml, f"{component_name}: {log_level}", device_yaml_path, errors)


def check_firmware_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    methods = project.get("firmware_update_methods", [])
    source = str(project.get("firmware_update_source", "")).strip()
    channels = project.get("firmware_update_channels", [])
    manual_check_behavior = str(project.get("firmware_manual_check_behavior", "")).strip()
    frequency_hours = project.get("firmware_update_frequency_hours", {})
    default_urls = default_public_manifest_urls(product)

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    firmware_yaml = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    c6_firmware_yaml = read(ROOT / "common" / "addon" / "esp32_c6_firmware_update.yaml", errors)
    web_template = read_web_source(errors)
    web_text = read(WEB_APP, errors)
    recovery_block = yaml_id_block(
        firmware_yaml,
        "firmware_update_recover_display",
        "common/addon/firmware_update.yaml",
        errors,
    )

    for method in methods if isinstance(methods, list) else []:
        if isinstance(method, str) and method.strip():
            require_contains(firmware_docs, method.strip(), "docs/firmware-update.md", errors)
            require_contains(firmware_yaml, method.strip(), "common/addon/firmware_update.yaml", errors)
    if source:
        require_contains(firmware_docs, source, "docs/firmware-update.md", errors)
    if isinstance(channels, list):
        for channel in channels:
            if isinstance(channel, str) and channel.strip():
                require_contains(firmware_docs, channel.strip(), "docs/firmware-update.md", errors)
                require_contains(firmware_yaml, channel.strip(), "common/addon/firmware_update.yaml", errors)
    if manual_check_behavior:
        require_contains(firmware_docs, manual_check_behavior, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, "manual_check_only", "common/addon/firmware_update.yaml", errors)
        require_contains(firmware_yaml, "update.check: firmware_update", "common/addon/firmware_update.yaml", errors)
        require_contains(web_template, 'post(endpoints.firmware_check + "/press")', rel(WEB_TEMPLATE), errors)
    if isinstance(frequency_hours, dict):
        for label, hours in frequency_hours.items():
            if not isinstance(label, str) or not isinstance(hours, int) or isinstance(hours, bool):
                continue
            require_contains(firmware_docs, label, "docs/firmware-update.md", errors)
            if label == "Daily":
                require_contains(firmware_yaml, f"int threshold = {hours}", "common/addon/firmware_update.yaml", errors)
                require_contains(firmware_yaml, 'initial_option: "Daily"', "common/addon/firmware_update.yaml", errors)
            else:
                require_contains(firmware_yaml, f'freq == "{label}"', "common/addon/firmware_update.yaml", errors)
            require_contains(firmware_yaml, f"threshold = {hours}", "common/addon/firmware_update.yaml", errors)
    for needle in (
        "update.perform: firmware_update",
        "firmware_auto_install_available_update",
        "update.is_available: firmware_update",
        "switch.is_on: auto_update_switch",
        "return !id(manual_check_only);",
        "id(firmware_update_reboot_pending) = true;",
        "update_interval: never",
    ):
        require_contains(firmware_yaml, needle, "common/addon/firmware_update.yaml", errors)
    for needle in (
        "id(backlight_manual_off) ||",
        "(id(schedule_enabled).state && id(screen_schedule_asleep))",
        "id(backlight_paused) = true;",
        "id(screensaver_display_off_active) = true;",
        "Preserved display sleep state after firmware update",
        "script.execute: backlight_schedule_display_off",
        "script.execute: screen_schedule_boot_recover",
    ):
        require_contains(recovery_block, needle, "common/addon/firmware_update.yaml firmware_update_recover_display", errors)
    for needle in (
        "Auto updates",
        'productSettingOptions("update_frequency")',
        "Check for Update",
        "Install Update",
        'post(endpoints.update + "/install")',
        "WiFi firmware",
        "Previous firmware",
        "compareFirmwareVersions(latest, installed) > 0",
        'post(endpoints.firmware_prepare_upload + "/press")',
        'post(endpoints.firmware_cancel_upload + "/press")',
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "update_interval: 24h",
        "on_update_available:",
        "id(c6_auto_update_switch).state && !id(c6_manual_check_only)",
        "update.perform:",
        "name: \"WiFi Firmware: Auto Update\"",
        "optimistic: true",
        "restore_mode: RESTORE_DEFAULT_ON",
        "on_turn_on:",
        "update.check:",
        "id(c6_manual_check_only) = true;",
        "id(c6_manual_check_only) = false;",
    ):
        require_contains(
            c6_firmware_yaml,
            needle,
            "common/addon/esp32_c6_firmware_update.yaml",
            errors,
        )
    for label, url in default_urls.items():
        require_contains(firmware_docs, url, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, url, "common/addon/firmware_update.yaml", errors)
        require_contains(web_text, url, rel(WEB_APP), errors)
        require_contains(web_template, f"FIRMWARE_MANIFEST_URLS.{label}", rel(WEB_TEMPLATE), errors)


def check_ota_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    platform = str(project.get("ota_update_platform", "")).strip()
    pre_update_action = str(project.get("ota_pre_update_action", "")).strip()

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    if pre_update_action:
        require_contains(firmware_docs, pre_update_action, "docs/firmware-update.md", errors)
    if platform:
        require_contains(device_yaml, f"platform: {platform}", device_yaml_path, errors)
    for needle in (
        "ota:",
        "on_begin:",
        "firmware_update_reboot_pending",
        "id(firmware_update_reboot_pending) = true;",
        "global_preferences->sync();",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)
    forbidden_ota_needles = (
        "light.turn_off:\n            id: backlight",
        "transition_length: 300ms",
        "delay: 350ms",
    )
    for needle in forbidden_ota_needles:
        if needle in device_yaml:
            errors.append(f"{device_yaml_path} must not turn the backlight off before OTA updates")
