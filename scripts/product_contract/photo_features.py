from __future__ import annotations

from product_contract.common import (
    ROOT,
    WEB_TEMPLATE,
    read,
    read_web_source,
    rel,
    require_contains,
    require_firmware_text_entity_shape,
)


def check_photo_source_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    photo_source_modes = [str(value).strip() for value in project.get("photo_source_modes", []) if str(value).strip()]
    auto_apply_behavior = str(project.get("photo_source_auto_apply_behavior", "")).strip()
    id_limit = project.get("photo_source_id_limit")
    memories_window = str(project.get("photo_source_memories_window", "")).strip()
    memories_fallback = str(project.get("photo_source_memories_fallback", "")).strip()
    album_person_sampling = str(project.get("photo_source_album_person_sampling", "")).strip()

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    photo_source_setting = settings_by_key.get("photo_source")
    if not photo_source_setting:
        errors.append("product settings must include photo_source")
    else:
        if photo_source_modes and photo_source_setting.get("options") != photo_source_modes:
            errors.append("project.photo_source_modes must match the photo_source setting options")
        if photo_source_setting.get("default") != "All Photos":
            errors.append("photo_source default must be All Photos")

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    filter_yaml = read(ROOT / "common" / "addon" / "immich_filter.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    web_template = read_web_source(errors)

    if auto_apply_behavior:
        require_contains(photo_docs, auto_apply_behavior, "docs/photo-sources.md", errors)
    if memories_window:
        require_contains(photo_docs, memories_window, "docs/photo-sources.md", errors)
    if memories_fallback:
        require_contains(photo_docs, memories_fallback, "docs/photo-sources.md", errors)
        require_contains(api_yaml, "falling back to random", "common/addon/immich_api.yaml", errors)
    if album_person_sampling:
        require_contains(photo_docs, album_person_sampling, "docs/photo-sources.md", errors)
        require_contains(api_yaml, "paged metadata search", "common/addon/immich_api.yaml", errors)
    if isinstance(id_limit, int):
        id_limit_text = str(id_limit)
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("docs/backup.md", backup_docs),
            (rel(WEB_TEMPLATE), web_template),
        ):
            require_contains(text, id_limit_text, label, errors)
        if filter_yaml.count(f"max_length: {id_limit}") < 6:
            errors.append("common/addon/immich_filter.yaml must use project.photo_source_id_limit for album/person/tag ID and label fields")

    for mode in photo_source_modes:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("common/addon/immich_filter.yaml", filter_yaml),
        ):
            require_contains(text, mode, label, errors)
    for needle in (
        "whole library",
        "favorites",
        "specific albums",
        "specific people",
        "specific tags",
        '"on this day" memories',
        "chosen date range",
    ):
        require_contains(readme, needle, "README.md", errors)
    for needle in (
        "Photo Sources",
        "favorites only",
        "specific albums",
        "specific people",
        "specific tags",
        '"on this day" memories',
        "date range",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Album IDs", "Album Labels", "Person IDs", "Person Labels", "Tag IDs", "Tag Labels"):
        require_contains(backup_docs, needle, "docs/backup.md", errors)

    for needle in (
        "Photos: Source",
        'initial_option: "All Photos"',
        "auto_apply_photo_source",
        "delay: 1s",
        "Photos: Album IDs",
        "Photos: Person IDs",
        "Photos: Tag IDs",
        "Apply Photo Source",
    ):
        require_contains(filter_yaml, needle, "common/addon/immich_filter.yaml", errors)
    for name in ("Photos: Album IDs", "Photos: Album Labels", "Photos: Person IDs", "Photos: Person Labels", "Photos: Tag IDs", "Photos: Tag Labels"):
        require_firmware_text_entity_shape(filter_yaml, name, "common/addon/immich_filter.yaml", errors)
    for needle in (
        "immich_memory_window_offset",
        "id(immich_memory_window_offset) = -2",
        "id(immich_memory_window_offset) <= 2",
        "build_immich_search_body",
        "pick_album_id_for_metadata_search",
        "pick_one_person_id_for_random_search",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    require_contains(api_key_docs, "memory.read", "docs/api-key.md", errors)
    for needle in (
        "MAX_PHOTO_ID_FIELD_LENGTH",
        "schedulePhotoSourceApply",
        "Add an album",
        "Add a person",
        "Add a tag",
        "Paste album ID from Immich URL",
        "Paste person ID from Immich URL",
        "Paste tag ID from Immich URL",
        "Album IDs exceed 255 characters",
        "Person IDs exceed 255 characters",
        "Tag IDs exceed 255 characters",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)


def check_connection_resilience_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    slideshow_check_interval = str(project.get("slideshow_check_interval", "")).strip()
    slideshow_default_seconds = project.get("slideshow_interval_default_seconds")
    slideshow_range_seconds = project.get("slideshow_interval_range_seconds", [])
    timeout_default = str(project.get("connection_timeout_default", "")).strip()
    timeout_default_seconds = project.get("connection_timeout_default_seconds")
    timeout_range_seconds = project.get("connection_timeout_range_seconds", [])
    timeout_range = str(project.get("connection_timeout_range", "")).strip()
    failure_trigger = str(project.get("connection_failure_trigger", "")).strip()
    invalid_key_title = str(project.get("connection_invalid_api_key_title", "")).strip()
    unavailable_title = str(project.get("connection_unavailable_title", "")).strip()
    max_error_retries = project.get("immich_max_error_retries")
    retry_delays = project.get("immich_api_retry_delay_ms", [])
    retryable_statuses = [str(value).strip() for value in project.get("immich_retryable_http_statuses", []) if str(value).strip()]
    auth_error_status = project.get("immich_auth_error_status")

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    connection_timeout_setting = settings_by_key.get("conn_timeout")
    slideshow_interval_setting = settings_by_key.get("interval")
    if not slideshow_interval_setting:
        errors.append("product settings must include interval")
    elif isinstance(slideshow_default_seconds, int) and not isinstance(slideshow_default_seconds, bool):
        expected_default = f"{slideshow_default_seconds} seconds"
        if slideshow_interval_setting.get("default") != expected_default:
            errors.append("project.slideshow_interval_default_seconds must match interval default")
    if not connection_timeout_setting:
        errors.append("product settings must include conn_timeout")
    else:
        if timeout_default and connection_timeout_setting.get("default") != timeout_default:
            errors.append("project.connection_timeout_default must match conn_timeout default")
        if "30 seconds" not in connection_timeout_setting.get("options", []):
            errors.append("conn_timeout options must include 30 seconds")
        if "30 minutes" not in connection_timeout_setting.get("options", []):
            errors.append("conn_timeout options must include 30 minutes")

    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    screen_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_slideshow.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    immich_config_yaml = read(ROOT / "common" / "addon" / "immich_config.yaml", errors)
    helper_header = read(ROOT / "components" / "espframe" / "espframe_helpers.h", errors)
    helper_tests = read(ROOT / "tests" / "espframe_helper_tests.cpp", errors)
    web_template = read_web_source(errors)

    for needle in (timeout_default, timeout_range, failure_trigger, invalid_key_title, unavailable_title):
        if needle:
            require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    for needle in ("Connection Timeout", "connection-failed screen", "slow server", "large photo library"):
        require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    for needle in ("Immich Connection Problems", "API Key Problems", "Photos Do Not Appear"):
        require_contains(troubleshooting_docs, needle, "docs/troubleshooting.md", errors)
    require_contains(home_assistant_docs, "Screen: Connection Timeout", "docs/home-assistant.md", errors)
    require_contains(backup_docs, f'"conn_timeout": "{timeout_default}"', "docs/backup.md", errors)

    for needle in (
        "Screen: Connection Timeout",
        f'initial_option: "{timeout_default}"',
        "connection_failed_overlay",
        "connection_failed_dim",
        "connection_failed_shift",
        invalid_key_title,
        unavailable_title,
        "Check your Immich API key",
        "Check your internet connection",
    ):
        if needle:
            require_contains(screen_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    for option in ("30 seconds", "30 minutes"):
        require_contains(screen_yaml, f'      - "{option}"', "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    if slideshow_check_interval:
        require_contains(screen_yaml, f'slideshow_check_interval: "{slideshow_check_interval}"', "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        require_contains(screen_yaml, f"interval: ${{slideshow_check_interval}}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
    if isinstance(slideshow_default_seconds, int) and not isinstance(slideshow_default_seconds, bool):
        require_contains(screen_yaml, f"initial_value: '{slideshow_default_seconds}'", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        if isinstance(slideshow_range_seconds, list) and len(slideshow_range_seconds) == 2:
            require_contains(
                screen_yaml,
                f"parse_duration_option_seconds(x, {slideshow_default_seconds}, {slideshow_range_seconds[0]}, {slideshow_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )
            require_contains(
                screen_yaml,
                "parse_duration_option_seconds(\n                id(slideshow_interval).current_option(), "
                f"id(slideshow_interval_sec), {slideshow_range_seconds[0]}, {slideshow_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )
    if isinstance(timeout_default_seconds, int) and not isinstance(timeout_default_seconds, bool):
        require_contains(screen_yaml, f"initial_value: '{timeout_default_seconds}'", "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml", errors)
        if isinstance(timeout_range_seconds, list) and len(timeout_range_seconds) == 2:
            require_contains(
                screen_yaml,
                f"parse_duration_option_seconds(x, {timeout_default_seconds}, {timeout_range_seconds[0]}, {timeout_range_seconds[1]})",
                "devices/guition-esp32-p4-jc8012p4a1/device/screen_slideshow.yaml",
                errors,
            )

    if isinstance(max_error_retries, int) and not isinstance(max_error_retries, bool):
        require_contains(helper_header, f"MAX_ERROR_RETRIES = {max_error_retries}", "components/espframe/espframe_helpers.h", errors)
        for label, text in (
            ("common/addon/immich_api.yaml", api_yaml),
            ("common/addon/immich_slideshow.yaml", slideshow_yaml),
        ):
            require_contains(text, "MAX_ERROR_RETRIES", label, errors)
    if isinstance(retry_delays, list):
        for delay in retry_delays:
            if isinstance(delay, int) and not isinstance(delay, bool):
                require_contains(api_yaml, f"delay_ms = {delay}", "common/addon/immich_api.yaml", errors)
    for status in retryable_statuses:
        if status == "HTTP 5xx":
            require_contains(helper_header, "status >= 500", "components/espframe/espframe_helpers.h", errors)
        elif status == "429":
            require_contains(helper_header, "status == 429", "components/espframe/espframe_helpers.h", errors)
        else:
            errors.append(f"project.immich_retryable_http_statuses has no checker mapping for {status!r}")
    if isinstance(auth_error_status, int) and not isinstance(auth_error_status, bool):
        require_contains(helper_header, f"status == {auth_error_status}", "components/espframe/espframe_helpers.h", errors)
        require_contains(api_yaml, "is_http_auth_error(code)", "common/addon/immich_api.yaml", errors)
    for needle in (
        "immich_fetch_retry",
        "immich_register_fetch_failure",
        "Fetch paused (connection retry cooldown)",
        "Retrying Immich fetch",
        "api retries exhausted",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    for needle in (
        "immich_consecutive_failures",
        "immich_retry_cooldown_until_ms",
        "Download retries exhausted",
        "hide_connection_failed",
    ):
        require_contains(slideshow_yaml + screen_yaml, needle, "connection retry firmware", errors)
    for needle in (
        "Connection settings changed; retrying Immich connection",
        "connection_failed_overlay",
        "immich_api_retries",
        "immich_download_retries",
        "immich_consecutive_failures",
    ):
        require_contains(immich_config_yaml, needle, "common/addon/immich_config.yaml", errors)
    for needle in (
        "Connection Timeout",
        'productSelectSettingField("Connection Timeout", "conn_timeout")',
        "conn_timeout",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "parse_duration_option_seconds",
        "20 minutes",
        "MAX_ERROR_RETRIES",
    ):
        require_contains(helper_tests + helper_header, needle, "connection helper tests", errors)


def check_setup_flow_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    captive_portal_ip = str(project.get("setup_captive_portal_ip", "")).strip()
    wizard_steps = [str(value).strip() for value in project.get("setup_wizard_steps", []) if str(value).strip()]
    connection_fields = [
        str(value).strip() for value in project.get("setup_required_connection_fields", []) if str(value).strip()
    ]
    skip_substitutions = [
        str(value).strip() for value in project.get("setup_skip_substitutions", []) if str(value).strip()
    ]
    ready_condition = str(project.get("setup_connection_ready_condition", "")).strip()
    setup_dim_delay = str(project.get("setup_screen_dim_delay", "")).strip()
    setup_dim_brightness = str(project.get("setup_screen_dim_brightness", "")).strip()
    setup_dim_transition = str(project.get("setup_screen_dim_transition", "")).strip()
    loading_backlight_brightness = str(project.get("setup_loading_backlight_brightness", "")).strip()
    loading_backlight_transition = str(project.get("setup_loading_backlight_transition", "")).strip()
    required_substitutions = [
        str(value).strip() for value in project.get("manual_setup_required_substitutions", []) if str(value).strip()
    ]
    wifi_secrets = [str(value).strip() for value in project.get("manual_setup_wifi_secrets", []) if str(value).strip()]
    package_ref = str(project.get("manual_setup_package_ref", "")).strip()
    package_refresh = str(project.get("manual_setup_package_refresh", "")).strip()

    install_docs = read(ROOT / "docs" / "install.md", errors)
    usb_docs = read(ROOT / "docs" / "usb-flashing.md", errors)
    manual_setup_docs = read(ROOT / "docs" / "manual-setup.md", errors)
    immich_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    web_template = read_web_source(errors)
    connectivity_yaml = read(ROOT / "common" / "addon" / "connectivity.yaml", errors)
    immich_config_yaml = read(ROOT / "common" / "addon" / "immich_config.yaml", errors)
    screen_loading_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_loading.yaml", errors)
    screen_wifi_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "device" / "screen_wifi_setup.yaml", errors)
    packages_yaml = read(ROOT / "devices" / "guition-esp32-p4-jc8012p4a1" / "packages.yaml", errors)
    local_yamls = [
        (str(device.get("local_yaml", "")), read(ROOT / str(device.get("local_yaml", "")), errors))
        for device in product["devices"]
    ]

    for device in product["devices"]:
        esphome_name = str(device.get("esphome_name", "")).strip()
        if not esphome_name:
            continue
        require_contains(install_docs, esphome_name, "docs/install.md", errors)
        require_contains(usb_docs, esphome_name, "docs/usb-flashing.md", errors)
        require_contains(read(ROOT / str(device.get("build_yaml", "")), errors), esphome_name, str(device.get("build_yaml", "")), errors)

    if captive_portal_ip:
        for label, text in (
            ("docs/usb-flashing.md", usb_docs),
            ("devices/guition-esp32-p4-jc8012p4a1/packages.yaml", packages_yaml),
        ):
            require_contains(text, captive_portal_ip, label, errors)
        for label, text in (
            ("devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", screen_loading_yaml),
            ("devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", screen_wifi_yaml),
        ):
            require_contains(text, "${captive_portal_ip}", label, errors)

    for step in wizard_steps:
        require_contains(web_template, step, rel(WEB_TEMPLATE), errors)
    for field in connection_fields:
        require_contains(web_template, field, rel(WEB_TEMPLATE), errors)
        require_contains(install_docs, field, "docs/install.md", errors)
    for substitution in skip_substitutions:
        require_contains(manual_setup_docs, substitution, "docs/manual-setup.md", errors)
        require_contains(immich_config_yaml, substitution, "common/addon/immich_config.yaml", errors)
    if ready_condition:
        require_contains(manual_setup_docs, ready_condition, "docs/manual-setup.md", errors)
    for substitution in required_substitutions:
        require_contains(manual_setup_docs, f"`{substitution}`", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"{substitution}:", label or "device local ESPHome YAML", errors)
    for secret in wifi_secrets:
        require_contains(manual_setup_docs, secret, "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"!secret {secret}", label or "device local ESPHome YAML", errors)
    if package_ref:
        require_contains(manual_setup_docs, f"ref: {package_ref}", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"ref: {package_ref}", label or "device local ESPHome YAML", errors)
    if package_refresh:
        require_contains(manual_setup_docs, f"refresh: {package_refresh}", "docs/manual-setup.md", errors)
        for label, local_yaml in local_yamls:
            require_contains(local_yaml, f"refresh: {package_refresh}", label or "device local ESPHome YAML", errors)
    for needle in ("WiFi", "Immich server URL", "Immich API key"):
        require_contains(immich_frame_docs, needle, "docs/immich-photo-frame.md", errors)

    for needle in (
        "captive_portal:",
        'ssid: "${name}"',
        "wifi.connected",
        "is_valid_http_url(id(immich_url).state)",
        "!id(immich_api_key_text).state.empty()",
        "immich_setup_page",
        "wifi_setup_page",
    ):
        require_contains(connectivity_yaml, needle, "common/addon/connectivity.yaml", errors)
    for needle in (
        "normalize_immich_base_url",
        "Connection: Server URL",
        "Connection: API Key",
        'initial_value: "${immich_base_url}"',
        'initial_value: "${immich_api_key}"',
        "setup_screen_dim",
        "immich_check_config_ready",
        "slideshow_page",
    ):
        require_contains(immich_config_yaml, needle, "common/addon/immich_config.yaml", errors)
    for needle in (
        "Connect to the WiFi hotspot",
        "to configure your network",
        "Then visit ${captive_portal_ip}",
    ):
        require_contains(screen_loading_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
        require_contains(screen_wifi_yaml, needle, "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_delay:
        require_contains(screen_wifi_yaml, f"delay: {setup_dim_delay}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_brightness:
        require_contains(screen_wifi_yaml, f"brightness: {setup_dim_brightness}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if setup_dim_transition:
        require_contains(screen_wifi_yaml, f"transition_length: {setup_dim_transition}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_wifi_setup.yaml", errors)
    if loading_backlight_brightness:
        require_contains(screen_loading_yaml, f"brightness: {loading_backlight_brightness}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
    if loading_backlight_transition:
        require_contains(screen_loading_yaml, f"transition_length: {loading_backlight_transition}", "devices/guition-esp32-p4-jc8012p4a1/device/screen_loading.yaml", errors)
    for needle in (
        "id: boot_grace_period",
        "lv_bar_set_value(id(loading_progress_bar), 25, LV_ANIM_OFF)",
        'lv_label_set_text(id(loading_status_label), "Initializing display")',
        "lvgl.page.show: wifi_setup_page",
        "script.execute: setup_screen_dim",
        "script.stop: setup_screen_dim",
        "script.execute: backlight_apply_brightness",
    ):
        require_contains(screen_loading_yaml + screen_wifi_yaml, needle, "setup screen firmware", errors)
    for needle in (
        "renderWizard",
        "connect your photo frame",
        "saveConnectionValue(endpoints.immich_url",
        "saveConnectionValue(endpoints.api_key",
        "safeGet(endpoints.immich_url)",
        "safeGet(endpoints.api_key)",
        "Done",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)


def check_photo_display_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    date_filter_modes = [str(value).strip() for value in project.get("date_filter_modes", []) if str(value).strip()]
    date_filter_relative_anchor = str(project.get("date_filter_relative_anchor", "")).strip()
    date_filter_time_source = str(project.get("date_filter_time_source", "")).strip()
    portrait_pairing_behavior = str(project.get("portrait_pairing_behavior", "")).strip()
    portrait_pairing_rotation_behavior = str(project.get("portrait_pairing_rotation_behavior", "")).strip()
    metadata_overlay_fields = [
        str(value).strip() for value in project.get("metadata_overlay_fields", []) if str(value).strip()
    ]

    settings_by_key = {str(setting.get("key", "")).strip(): setting for setting in product["settings"]}
    date_filter_setting = settings_by_key.get("date_filter_mode")
    if not date_filter_setting:
        errors.append("product settings must include date_filter_mode")
    elif date_filter_modes and date_filter_setting.get("options") != date_filter_modes:
        errors.append("project.date_filter_modes must match the date_filter_mode setting options")

    for key in ("photo_metadata_date_enabled", "photo_metadata_location_enabled", "portrait_pairing"):
        setting = settings_by_key.get(key)
        if not setting:
            errors.append(f"product settings must include {key}")
        elif setting.get("default") is not True:
            errors.append(f"{key} default must be true")

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    photo_docs = read(ROOT / "docs" / "photo-sources.md", errors)
    web_template = read_web_source(errors)
    filter_yaml = read(ROOT / "common" / "addon" / "immich_filter.yaml", errors)
    api_yaml = read(ROOT / "common" / "addon" / "immich_api.yaml", errors)
    slideshow_yaml = read(ROOT / "common" / "addon" / "immich_slideshow.yaml", errors)
    helper_header = read(ROOT / "components" / "espframe" / "immich_helpers.h", errors)
    helper_tests = read(ROOT / "tests" / "espframe_helper_tests.cpp", errors)

    for mode in date_filter_modes:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            ("common/addon/immich_filter.yaml", filter_yaml),
            (rel(WEB_TEMPLATE), web_template),
            ("tests/espframe_helper_tests.cpp", helper_tests),
        ):
            require_contains(text, mode, label, errors)
    if date_filter_relative_anchor:
        require_contains(photo_docs, date_filter_relative_anchor, "docs/photo-sources.md", errors)
    if date_filter_time_source:
        require_contains(photo_docs, date_filter_time_source, "docs/photo-sources.md", errors)
    for needle in (
        "resolve_immich_date_filter",
        "build_immich_date_filter_extra",
        "build_immich_companion_date_filter_extra",
        "relative_skipped_for_invalid_time",
    ):
        require_contains(helper_header, needle, "components/espframe/immich_helpers.h", errors)
        require_contains(helper_tests, needle, "tests/espframe_helper_tests.cpp", errors)
    for needle in (
        "resolve_immich_date_filter",
        "build_immich_date_filter_extra",
        "Relative date filter skipped",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    for needle in (
        "Filter by Date",
        "From",
        "Until",
        "isValidDate",
        "scheduleFilterApply",
        "Relative Range",
        "Fixed Range",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for key in ("date_from", "date_to"):
        date_setting = settings_by_key.get(key, {})
        date_format = str(date_setting.get("docs_format", "")).strip().strip("`")
        if date_format:
            require_contains(web_template, f'placeholder = "{date_format}"', rel(WEB_TEMPLATE), errors)
            require_contains(web_template, f"Invalid date — use {date_format}", rel(WEB_TEMPLATE), errors)
            require_contains(filter_yaml, f"name: \"{date_setting.get('entity', {}).get('name', '')}\"", "common/addon/immich_filter.yaml", errors)
    date_formats = {
        str(settings_by_key.get(key, {}).get("docs_format", "")).strip().strip("`")
        for key in ("date_from", "date_to")
    }
    date_formats.discard("")
    if len(date_formats) == 1:
        date_length = len(next(iter(date_formats)))
        if filter_yaml.count(f"max_length: {date_length}") < 2:
            errors.append("common/addon/immich_filter.yaml must use date setting format length for date_from and date_to")

    if portrait_pairing_behavior:
        require_contains(readme, portrait_pairing_behavior, "README.md", errors)
    for needle in ("Portrait Pairing", "portrait photos taken on the same day", "side-by-side"):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Portrait Pairing", "side-by-side", "landscape screens"):
        require_contains(photo_docs, needle, "docs/photo-sources.md", errors)
    if portrait_pairing_rotation_behavior:
        require_contains(photo_docs, portrait_pairing_rotation_behavior, "docs/photo-sources.md", errors)
        require_contains(web_template, portrait_pairing_rotation_behavior, rel(WEB_TEMPLATE), errors)
    for needle in (
        "Photos: Portrait Pairing",
        "companion portrait",
        "same day",
        "immich_fetch_portrait_companion",
        "build_immich_companion_date_filter_extra",
        "find_immich_portrait_companion_url",
    ):
        require_contains(api_yaml + slideshow_yaml, needle, "portrait pairing firmware", errors)
    for needle in ("Portrait Pairing", "isPortraitScreenRotation", "portrait_pairing"):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "test_slideshow_component_portrait_flow",
        "test_slideshow_component_companion_result_flow",
        "on_companion_found",
    ):
        require_contains(helper_tests, needle, "tests/espframe_helper_tests.cpp", errors)

    for field in metadata_overlay_fields:
        for label, text in (
            ("docs/photo-sources.md", photo_docs),
            (rel(WEB_TEMPLATE), web_template),
            ("common/addon/immich_slideshow.yaml", slideshow_yaml),
        ):
            require_contains(text, field, label, errors)
    for needle in (
        "Metadata",
        "Date Format",
        "Date Taken Format",
        "Relative Date",
        "Date Taken",
        "photo_metadata_date_enabled",
        "photo_metadata_location_enabled",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for needle in (
        "Device: Metadata Date",
        "Device: Metadata Location",
        "Device: Metadata Date Format",
        "Device: Metadata Date Taken Format",
        "update_photo_metadata_display",
        "location",
        "localDateTime",
    ):
        require_contains(slideshow_yaml + helper_header, needle, "metadata overlay firmware", errors)
