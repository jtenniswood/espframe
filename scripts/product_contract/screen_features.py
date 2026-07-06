from __future__ import annotations

from product_contract.common import (
    ROOT,
    TIME_YAML,
    WEB_APP,
    WEB_TEMPLATE,
    read,
    read_web_source,
    rel,
    require_contains,
    require_firmware_text_entity_shape,
    yaml_id_block,
)
from product_config import web_static_entities


def check_touch_controls_metadata(product: dict, errors: list[str]) -> None:
    controls = product["project"].get("touch_controls", [])
    readme = read(ROOT / "README.md", errors)
    touch_docs = read(ROOT / "docs" / "touch-controls.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    screen_settings_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    slideshow_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_slideshow.yaml", errors)
    backlight_schedule_yaml = read(ROOT / "common" / "addon" / "backlight_schedule.yaml", errors)
    backlight_yaml = read(ROOT / "common" / "addon" / "backlight.yaml", errors)

    if isinstance(controls, list):
        for control in controls:
            if not isinstance(control, dict):
                continue
            action = str(control.get("action", "")).strip()
            gesture = str(control.get("gesture", "")).strip()
            if action:
                require_contains(touch_docs, f"**{action}**", "docs/touch-controls.md", errors)
            if gesture:
                require_contains(touch_docs, gesture, "docs/touch-controls.md", errors)

    for needle in ("tap to wake", "double-tap to advance to the next photo", "press-and-hold to sleep"):
        require_contains(readme, needle, "README.md", errors)
    require_contains(readme, "wake, sleep, or advance to the next photo", "README.md", errors)
    require_contains(troubleshooting_docs, "wake, sleep, or next-photo gestures", "docs/troubleshooting.md", errors)
    require_contains(screen_settings_docs, "same sleep/wake behavior as the touchscreen controls", "docs/screen-settings.md", errors)
    for needle in ("slideshow_on_press", "slideshow_on_short_click", "last_short_tap_ms", "immich_advance_forward"):
        require_contains(slideshow_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    for needle in ("3-second hold timer", "delay: 3s", "screen_schedule_manual_sleep"):
        require_contains(backlight_schedule_yaml, needle, "common/addon/backlight_schedule.yaml", errors)
    for needle in ('name: "Screen: Sleep"', 'name: "Screen: Wake"'):
        require_contains(backlight_yaml, needle, "common/addon/backlight.yaml", errors)


def check_screen_schedule_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    day_night_source = str(project.get("screen_brightness_day_night_source", "")).strip()
    schedule_behavior = str(project.get("screen_schedule_behavior", "")).strip()
    schedule_effects = project.get("screen_schedule_off_effects", [])

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    screen_settings_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    backlight_schedule_yaml = read(ROOT / "common" / "addon" / "backlight_schedule.yaml", errors)
    web_template = read_web_source(errors)
    boot_guard_block = yaml_id_block(
        backlight_schedule_yaml,
        "screen_schedule_boot_guard",
        "common/addon/backlight_schedule.yaml",
        errors,
    )

    if day_night_source:
        require_contains(screen_settings_docs, day_night_source, "docs/screen-settings.md", errors)
        require_contains(backlight_schedule_yaml, day_night_source, "common/addon/backlight_schedule.yaml", errors)
    if schedule_behavior:
        require_contains(screen_settings_docs, schedule_behavior, "docs/screen-settings.md", errors)
    if isinstance(schedule_effects, list):
        for effect in schedule_effects:
            if isinstance(effect, str) and effect.strip():
                require_contains(screen_settings_docs, effect.strip(), "docs/screen-settings.md", errors)
    for needle in (
        "schedule the display to turn off overnight",
        "brightness",
        "screen tone",
    ):
        require_contains(readme, needle, "README.md", errors)
    for needle in (
        "Screen Scheduling",
        "set daytime and night-time brightness levels separately",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in (
        "Daytime brightness",
        "nighttime brightness",
        "wake timeout",
    ):
        require_contains(backup_docs, needle, "docs/backup.md", errors)
    for needle in (
        "screen_schedule_sleep",
        "screen_schedule_wake",
        "screen_schedule_check",
        "backlight_apply_brightness",
        "backlight_recalc_sunrise_sunset",
    ):
        require_contains(backlight_schedule_yaml, needle, "common/addon/backlight_schedule.yaml", errors)
    for needle in (
        "id: screen_schedule_asleep\n                value: 'true'",
        "global_preferences->sync();",
        "script.execute: backlight_schedule_display_off",
        "script.execute: screen_schedule_boot_recover",
    ):
        require_contains(boot_guard_block, needle, "common/addon/backlight_schedule.yaml screen_schedule_boot_guard", errors)
    for key in ("schedule_enabled", "schedule_on_hour", "schedule_off_hour", "schedule_wake_timeout"):
        require_contains(web_template, key, rel(WEB_TEMPLATE), errors)


def check_screen_rotation_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    user_options = [str(value).strip() for value in project.get("screen_rotation_user_options", []) if str(value).strip()]
    developer_options = [
        str(value).strip() for value in project.get("screen_rotation_developer_options", []) if str(value).strip()
    ]
    native_mapping = project.get("screen_rotation_native_mapping", {})
    feature_source = str(project.get("screen_rotation_feature_source", "")).strip()
    rotation_behavior = str(project.get("screen_rotation_behavior", "")).strip()
    developer_behavior = str(project.get("screen_rotation_developer_behavior", "")).strip()

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    rotation_setting = settings_by_key.get("screen_rotation")
    if not rotation_setting:
        errors.append("product settings must include screen_rotation")
    else:
        if user_options and rotation_setting.get("options") != user_options:
            errors.append("project.screen_rotation_user_options must match screen_rotation options")
        if developer_options and rotation_setting.get("developer_options") != developer_options:
            errors.append("project.screen_rotation_developer_options must match screen_rotation developer_options")

    screen_docs = read(ROOT / "docs" / "screen-settings.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    rotation_yaml = read(ROOT / "common" / "addon" / "screen_rotation.yaml", errors)
    developer_yaml = read(ROOT / "common" / "addon" / "developer_features.yaml", errors)
    web_template = read_web_source(errors)

    if feature_source:
        require_contains(screen_docs, feature_source, "docs/screen-settings.md", errors)
    if rotation_behavior:
        require_contains(screen_docs, rotation_behavior, "docs/screen-settings.md", errors)
    if developer_behavior:
        require_contains(screen_docs, developer_behavior, "docs/screen-settings.md", errors)
        require_contains(web_template, "S.developer_features_enabled", rel(WEB_TEMPLATE), errors)
    for option in user_options + developer_options:
        require_contains(rotation_yaml, f'      - "{option}"', "common/addon/screen_rotation.yaml", errors)
        require_contains(web_template, option, rel(WEB_TEMPLATE), errors)
    for option in user_options:
        require_contains(screen_docs, f"{option}", "docs/screen-settings.md", errors)
    for option in developer_options:
        require_contains(screen_docs, option, "docs/screen-settings.md", errors)
    if isinstance(native_mapping, dict):
        for user_option, native_option in native_mapping.items():
            if not isinstance(user_option, str) or not isinstance(native_option, str):
                continue
            marker = f'screen_rotation_{user_option}: "{native_option}"'
            for device in product["devices"]:
                package_yaml = str(device.get("package_yaml", "")).strip()
                if package_yaml:
                    require_contains(read(ROOT / package_yaml, errors), marker, package_yaml, errors)
            require_contains(rotation_yaml, f"${{screen_rotation_{user_option}}}", "common/addon/screen_rotation.yaml", errors)
    for needle in (
        "Screen: Rotation",
        'initial_option: "${screen_rotation}"',
        "screen_apply_rotation",
        "developer_features_enabled",
        "lvgl.display.set_rotation",
        "portrait_pairing_enabled",
        "immich_reapply_current_image_layout",
    ):
        require_contains(rotation_yaml, needle, "common/addon/screen_rotation.yaml", errors)
    for needle in (
        "Developer: Features",
        "RESTORE_DEFAULT_OFF",
        "developer_features_saved",
        "script.execute: screen_apply_rotation",
    ):
        require_contains(developer_yaml, needle, "common/addon/developer_features.yaml", errors)
    for needle in (
        "developerPanelEnabledByUrl",
        'params.get("developer")',
        'params.get("dev")',
        "experimental",
        "screenRotationOptionsForUi",
        "productSettingOptions(\"screen_rotation\", S.developer_features_enabled)",
        "isPortraitScreenRotation",
        "Enable in-development features",
        'saveSetting("screen_rotation"',
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in ("Rotation", "0 degrees"):
        require_contains(backup_docs + screen_docs, needle, "screen rotation docs", errors)


def check_developer_features_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    query_params = [
        str(value).strip() for value in project.get("developer_features_query_params", []) if str(value).strip()
    ]
    query_value = str(project.get("developer_features_query_value", "")).strip()
    label = str(project.get("developer_features_label", "")).strip()
    entity = str(project.get("developer_features_entity", "")).strip()
    guard = str(project.get("developer_features_guard", "")).strip()
    persistence = str(project.get("developer_features_persistence", "")).strip()

    readme = read(ROOT / "README.md", errors)
    developer_yaml = read(ROOT / "common" / "addon" / "developer_features.yaml", errors)
    web_template = read_web_source(errors)
    web_text = read(WEB_APP, errors)

    if query_value:
        require_contains(readme, f"?developer={query_value}", "README.md", errors)
        for text, label_name in ((web_template, rel(WEB_TEMPLATE)), (web_text, rel(WEB_APP))):
            require_contains(text, f'=== "{query_value}"', label_name, errors)
    for param in query_params:
        for text, label_name in ((web_template, rel(WEB_TEMPLATE)), (web_text, rel(WEB_APP))):
            require_contains(text, f'params.get("{param}")', label_name, errors)
    if label:
        require_contains(web_template, label, rel(WEB_TEMPLATE), errors)
        require_contains(web_text, label, rel(WEB_APP), errors)
    if entity:
        require_contains(developer_yaml, f'name: "{entity}"', "common/addon/developer_features.yaml", errors)
        require_contains(web_text, f'"entity":"switch/{entity}"', rel(WEB_APP), errors)
    if guard:
        require_contains(readme, guard, "README.md", errors)
    if persistence:
        require_contains(readme, persistence, "README.md", errors)
    for needle in (
        "hidden developer setting",
        "must stay off",
        "RESTORE_DEFAULT_OFF",
        "initial_value: 'false'",
        "internal: true",
        "developer_features_saved",
        "developerPanelEnabledByUrl",
        "S.developer_features_enabled",
        'saveSetting("developer_features_enabled"',
    ):
        if needle in {"hidden developer setting", "must stay off"}:
            require_contains(readme, needle, "README.md", errors)
        elif needle in {"RESTORE_DEFAULT_OFF", "initial_value: 'false'", "internal: true", "developer_features_saved"}:
            require_contains(developer_yaml, needle, "common/addon/developer_features.yaml", errors)
        else:
            require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
            require_contains(web_text, needle, rel(WEB_APP), errors)


def check_screen_tone_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    base_purpose = str(project.get("screen_tone_base_purpose", "")).strip()
    night_timing = str(project.get("screen_tone_night_timing", "")).strip()
    night_recovery = str(project.get("screen_tone_night_recovery", "")).strip()
    override_duration = str(project.get("screen_tone_override_duration", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    screen_tone_docs = read(ROOT / "docs" / "screen-tone.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    warm_tones_yaml = read(ROOT / "common" / "addon" / "warm_tones.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    web_template = read_web_source(errors)

    if base_purpose:
        require_contains(screen_tone_docs, base_purpose, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, base_purpose, "common/addon/warm_tones.yaml", errors)
    for needle in ("blue cast", "Warmer", "less blue cast"):
        require_contains(screen_tone_docs, needle, "docs/screen-tone.md", errors)
    if night_timing:
        require_contains(screen_tone_docs, night_timing, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, night_timing, "common/addon/warm_tones.yaml", errors)
    if night_recovery:
        require_contains(screen_tone_docs, night_recovery, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, night_recovery, "common/addon/warm_tones.yaml", errors)
    if override_duration:
        require_contains(screen_tone_docs, override_duration, "docs/screen-tone.md", errors)
        require_contains(warm_tones_yaml, override_duration, "common/addon/warm_tones.yaml", errors)
    for needle in (
        "Night Tone",
        "sunset and sunrise",
        "Display Tone Adjustment",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("warm up a panel that looks too blue", "softer night tone after sunset"):
        require_contains(readme, needle, "README.md", errors)
    require_contains(backup_docs, "Screen Tone", "docs/backup.md", errors)
    for needle in (
        "Screen: Tone Adjustment",
        "Screen: Night Tone Adjustment",
        "Screen: Warm Tone Override",
        "Screen: Display Tone",
        "Screen: Warm Tone Intensity",
        "base_tone_enabled",
        "warm_tones_enabled",
        "warm_tone_override",
        "apply_warm_tones",
    ):
        require_contains(warm_tones_yaml, needle, "common/addon/warm_tones.yaml", errors)
    for needle in (
        "Night Tone Adjustment",
        "Turn on until sunrise",
        "base_tone",
        "warm_tone_intensity",
        "warm_tone_override",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    require_contains(slideshow_yaml, "accent + warm tones", "common/addon/immich_slideshow.yaml", errors)


def check_clock_time_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    clock_default_format = str(project.get("clock_default_format", "")).strip()
    clock_format_options = [str(value).strip() for value in project.get("clock_format_options", []) if str(value).strip()]
    clock_default_timezone = str(project.get("clock_default_timezone", "")).strip()
    clock_default_show = project.get("clock_default_show")
    clock_update_interval = str(project.get("clock_update_interval", "")).strip()
    ntp_default_servers = [str(value).strip() for value in project.get("ntp_default_servers", []) if str(value).strip()]
    ntp_server_length_limit = project.get("ntp_server_length_limit")
    timezone_effects = [str(value).strip() for value in project.get("timezone_change_effects", []) if str(value).strip()]

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    clock_format_setting = settings_by_key.get("clock_format")
    if not clock_format_setting:
        errors.append("product settings must include clock_format")
    else:
        if clock_default_format and clock_format_setting.get("default") != clock_default_format:
            errors.append("project.clock_default_format must match the clock_format setting default")
        if clock_format_options and clock_format_setting.get("options") != clock_format_options:
            errors.append("project.clock_format_options must match the clock_format setting options")

    static_entities = web_static_entities(product)
    static_timezone_default = static_entities.get("timezone", {}).get("default")
    if clock_default_timezone and static_timezone_default != clock_default_timezone:
        errors.append("project.clock_default_timezone must match the static web timezone default")
    if isinstance(clock_default_show, bool) and static_entities.get("show_clock", {}).get("default") != clock_default_show:
        errors.append("project.clock_default_show must match the static web show_clock default")
    static_ntp_defaults = [
        str(static_entities.get(key, {}).get("default", ""))
        for key in ("ntp_server_1", "ntp_server_2", "ntp_server_3")
    ]
    if ntp_default_servers and static_ntp_defaults != ntp_default_servers:
        errors.append("project.ntp_default_servers must match the static web NTP defaults")

    install_docs = read(ROOT / "docs" / "install.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    time_yaml = read(TIME_YAML, errors)
    web_template = read_web_source(errors)
    web_text = read(WEB_APP, errors)

    for needle in (
        clock_default_format,
        clock_default_timezone,
        clock_update_interval,
        "shows the clock by default",
        "sunrise/sunset based brightness and night tone",
    ):
        if needle:
            require_contains(install_docs, needle, "docs/install.md", errors)
    for server in ntp_default_servers:
        require_contains(install_docs, server, "docs/install.md", errors)

    for needle in ("Clock Overlay", "current time"):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Show clock", "format", "timezone"):
        require_contains(backup_docs, needle, "docs/backup.md", errors)

    for option in clock_format_options:
        require_contains(time_yaml, f'      - "{option}"', rel(TIME_YAML), errors)
    if clock_default_format:
        require_contains(time_yaml, f'initial_option: "{clock_default_format}"', rel(TIME_YAML), errors)
    if clock_default_timezone:
        require_contains(time_yaml, f'initial_option: "{clock_default_timezone}"', rel(TIME_YAML), errors)
        timezone_id = clock_default_timezone.split(" (", 1)[0]
        require_contains(time_yaml, f'timezone: "{timezone_id}"', rel(TIME_YAML), errors)
    for index, server in enumerate(ntp_default_servers, start=1):
        key = f"ntp_server_{index}"
        require_contains(time_yaml, f'  {key}: "{server}"', rel(TIME_YAML), errors)
        require_contains(time_yaml, f'initial_value: "${{{key}}}"', rel(TIME_YAML), errors)
        require_firmware_text_entity_shape(time_yaml, f"Clock: NTP Server {index}", rel(TIME_YAML), errors)
        require_contains(
            web_template,
            f'{{ key: "{key}", placeholder: "{server}", label: "NTP Server {index}" }}',
            rel(WEB_TEMPLATE),
            errors,
        )
    if isinstance(ntp_server_length_limit, int) and not isinstance(ntp_server_length_limit, bool):
        if time_yaml.count(f"max_length: {ntp_server_length_limit}") < len(ntp_default_servers):
            errors.append(f"{rel(TIME_YAML)} must use project.ntp_server_length_limit for all NTP server text fields")
        require_contains(web_template, f"MAX_NTP_SERVER_LENGTH = {ntp_server_length_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_NTP_SERVER_LENGTH = {ntp_server_length_limit}", rel(WEB_APP), errors)
    if clock_default_show is True:
        require_contains(time_yaml, "restore_mode: RESTORE_DEFAULT_ON", rel(TIME_YAML), errors)
    elif clock_default_show is False:
        require_contains(time_yaml, "restore_mode: RESTORE_DEFAULT_OFF", rel(TIME_YAML), errors)
    if clock_update_interval:
        require_contains(time_yaml, clock_update_interval, rel(TIME_YAML), errors)
    if clock_update_interval == "60 seconds":
        require_contains(time_yaml, "interval: 60s", rel(TIME_YAML), errors)

    effect_markers = {
        "updates clock display": "script.execute: update_clock_display",
        "recalculates sunrise/sunset": "script.execute: backlight_recalc_sunrise_sunset",
        "checks screen schedule": "script.execute: screen_schedule_check",
    }
    for effect in timezone_effects:
        marker = effect_markers.get(effect)
        if marker:
            require_contains(time_yaml, marker, rel(TIME_YAML), errors)
        else:
            errors.append(f"project.timezone_change_effects has no checker mapping for {effect!r}")

    for needle in (
        "Clock: Format",
        "Clock: Timezone",
        "Clock: Show",
        "Clock: NTP Server 1",
        "Clock: NTP Server 2",
        "Clock: NTP Server 3",
        "apply_ntp_servers",
        "update_clock_display",
    ):
        require_contains(time_yaml, needle, rel(TIME_YAML), errors)
    for needle in (
        "Clock & timezone",
        "Clock Format",
        "Timezone",
        "NTP Servers",
        "Show Clock",
        "ntp_server_1",
        "ntp_server_2",
        "ntp_server_3",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
