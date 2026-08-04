from __future__ import annotations

import re

def check_project_integration_feature_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "web_installer_required_browsers",
        "web_installer_unsupported_browsers",
        "immich_server_url_schemes",
        "immich_server_url_targets",
        "immich_server_url_examples",
        "firmware_update_methods",
        "firmware_update_channels",
    ):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    frequency_hours = project.get("firmware_update_frequency_hours", {})
    if not isinstance(frequency_hours, dict) or not frequency_hours:
        errors.append("project.firmware_update_frequency_hours must be a non-empty object")
    else:
        for label, hours in frequency_hours.items():
            if not isinstance(label, str) or not label.strip():
                errors.append("project.firmware_update_frequency_hours keys must be non-empty strings")
            if not isinstance(hours, int) or isinstance(hours, bool) or hours < 1:
                errors.append(f"project.firmware_update_frequency_hours.{label} must be a positive integer")
    home_assistant_features = project.get("home_assistant_integration_features", [])
    if not isinstance(home_assistant_features, list) or not home_assistant_features:
        errors.append("project.home_assistant_integration_features must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in home_assistant_features):
        errors.append("project.home_assistant_integration_features must only contain non-empty strings")
    network_entities = project.get("home_assistant_network_entities", [])
    if not isinstance(network_entities, list) or not network_entities:
        errors.append("project.home_assistant_network_entities must be a non-empty list")
    else:
        for entity in network_entities:
            if not isinstance(entity, dict):
                errors.append("project.home_assistant_network_entities entries must be objects")
                continue
            for field in ("name", "type", "description"):
                if not str(entity.get(field, "")).strip():
                    errors.append(f"project.home_assistant_network_entities entry is missing {field}")
    diagnostic_entities = project.get("home_assistant_diagnostic_entities", [])
    if not isinstance(diagnostic_entities, list) or not diagnostic_entities:
        errors.append("project.home_assistant_diagnostic_entities must be a non-empty list")
    else:
        for entity in diagnostic_entities:
            if not isinstance(entity, dict):
                errors.append("project.home_assistant_diagnostic_entities entries must be objects")
                continue
            for field in ("name", "type", "description"):
                if not str(entity.get(field, "")).strip():
                    errors.append(f"project.home_assistant_diagnostic_entities entry is missing {field}")
    component_log_levels = project.get("device_log_component_levels", {})
    if not isinstance(component_log_levels, dict) or not component_log_levels:
        errors.append("project.device_log_component_levels must be a non-empty object")
    else:
        for component, level in component_log_levels.items():
            if not isinstance(component, str) or not component.strip():
                errors.append("project.device_log_component_levels keys must be non-empty strings")
            if not isinstance(level, str) or not level.strip():
                errors.append(f"project.device_log_component_levels.{component} must be a non-empty string")
    for field in ("network_wifi_strength_source", "network_wifi_strength_update_interval"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")


def check_project_screen_feature_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    touch_controls = project.get("touch_controls", [])
    if not isinstance(touch_controls, list) or not touch_controls:
        errors.append("project.touch_controls must be a non-empty list")
    else:
        for control in touch_controls:
            if not isinstance(control, dict):
                errors.append("project.touch_controls entries must be objects")
                continue
            if not str(control.get("action", "")).strip():
                errors.append("project.touch_controls entry is missing action")
            if not str(control.get("gesture", "")).strip():
                errors.append("project.touch_controls entry is missing gesture")
    for field in ("screen_brightness_day_night_source", "screen_schedule_behavior"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in (
        "screen_rotation_feature_source",
        "screen_rotation_behavior",
        "screen_rotation_developer_behavior",
        "developer_features_query_value",
        "developer_features_label",
        "developer_features_entity",
        "developer_features_guard",
        "developer_features_persistence",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("screen_rotation_user_options", "screen_rotation_developer_options"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    developer_query_params = project.get("developer_features_query_params", [])
    if not isinstance(developer_query_params, list) or not developer_query_params:
        errors.append("project.developer_features_query_params must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in developer_query_params):
        errors.append("project.developer_features_query_params must only contain non-empty strings")
    screen_rotation_mapping = project.get("screen_rotation_native_mapping", {})
    if not isinstance(screen_rotation_mapping, dict) or not screen_rotation_mapping:
        errors.append("project.screen_rotation_native_mapping must be a non-empty object")
    else:
        for user_option, native_option in screen_rotation_mapping.items():
            if not isinstance(user_option, str) or not user_option.strip():
                errors.append("project.screen_rotation_native_mapping keys must be non-empty strings")
            if not isinstance(native_option, str) or not native_option.strip():
                errors.append(f"project.screen_rotation_native_mapping.{user_option} must be a non-empty string")
    for field in (
        "screen_tone_base_purpose",
        "screen_tone_night_timing",
        "screen_tone_night_recovery",
        "screen_tone_override_duration",
        "clock_default_format",
        "clock_default_timezone",
        "clock_update_interval",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    clock_format_options = project.get("clock_format_options", [])
    if not isinstance(clock_format_options, list) or not clock_format_options:
        errors.append("project.clock_format_options must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in clock_format_options):
        errors.append("project.clock_format_options must only contain non-empty strings")
    if not isinstance(project.get("clock_default_show"), bool):
        errors.append("project.clock_default_show must be true or false")
    ntp_default_servers = project.get("ntp_default_servers", [])
    if not isinstance(ntp_default_servers, list) or not ntp_default_servers:
        errors.append("project.ntp_default_servers must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in ntp_default_servers):
        errors.append("project.ntp_default_servers must only contain non-empty strings")
    ntp_server_length_limit = project.get("ntp_server_length_limit")
    if not isinstance(ntp_server_length_limit, int) or isinstance(ntp_server_length_limit, bool) or ntp_server_length_limit < 1:
        errors.append("project.ntp_server_length_limit must be a positive integer")
    timezone_effects = project.get("timezone_change_effects", [])
    if not isinstance(timezone_effects, list) or not timezone_effects:
        errors.append("project.timezone_change_effects must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in timezone_effects):
        errors.append("project.timezone_change_effects must only contain non-empty strings")
    photo_source_modes = project.get("photo_source_modes", [])
    if not isinstance(photo_source_modes, list) or not photo_source_modes:
        errors.append("project.photo_source_modes must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in photo_source_modes):
        errors.append("project.photo_source_modes must only contain non-empty strings")
    for field in (
        "photo_source_auto_apply_behavior",
        "photo_source_memories_window",
        "photo_source_memories_fallback",
        "photo_source_album_person_sampling",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    if not isinstance(project.get("photo_source_id_limit"), int) or isinstance(project.get("photo_source_id_limit"), bool):
        errors.append("project.photo_source_id_limit must be an integer")
    for field in (
        "connection_timeout_default",
        "connection_timeout_range",
        "connection_failure_trigger",
        "connection_invalid_api_key_title",
        "connection_unavailable_title",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    if not isinstance(project.get("immich_max_error_retries"), int) or isinstance(project.get("immich_max_error_retries"), bool):
        errors.append("project.immich_max_error_retries must be an integer")
    retry_delays = project.get("immich_api_retry_delay_ms", [])
    if not isinstance(retry_delays, list) or not retry_delays:
        errors.append("project.immich_api_retry_delay_ms must be a non-empty list")
    elif any(not isinstance(value, int) or isinstance(value, bool) or value < 1 for value in retry_delays):
        errors.append("project.immich_api_retry_delay_ms must only contain positive integers")
    retryable_statuses = project.get("immich_retryable_http_statuses", [])
    if not isinstance(retryable_statuses, list) or not retryable_statuses:
        errors.append("project.immich_retryable_http_statuses must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in retryable_statuses):
        errors.append("project.immich_retryable_http_statuses must only contain non-empty strings")
    if not isinstance(project.get("immich_auth_error_status"), int) or isinstance(project.get("immich_auth_error_status"), bool):
        errors.append("project.immich_auth_error_status must be an integer")
    for field in (
        "slideshow_interval_default_seconds",
        "connection_timeout_default_seconds",
        "docs_firmware_verify_retries",
        "docs_firmware_verify_delay_seconds",
        "firmware_compile_timeout_minutes",
    ):
        if not isinstance(project.get(field), int) or isinstance(project.get(field), bool) or project.get(field) < 1:
            errors.append(f"project.{field} must be a positive integer")
    for field in ("slideshow_interval_range_seconds", "connection_timeout_range_seconds"):
        value = project.get(field)
        if (
            not isinstance(value, list)
            or len(value) != 2
            or any(not isinstance(item, int) or isinstance(item, bool) or item < 1 for item in value)
        ):
            errors.append(f"project.{field} must be a two-item list of positive integers")
        elif value[0] > value[1]:
            errors.append(f"project.{field} minimum must not exceed maximum")


def check_project_setup_feature_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in ("setup_wizard_steps", "setup_required_connection_fields", "setup_skip_substitutions"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    for field in ("manual_setup_required_substitutions", "manual_setup_wifi_secrets"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")


def check_project_photo_display_feature_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in ("date_filter_modes", "metadata_overlay_fields"):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
    for field in (
        "date_filter_relative_anchor",
        "date_filter_time_source",
        "portrait_pairing_behavior",
        "portrait_pairing_rotation_behavior",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    screen_schedule_effects = project.get("screen_schedule_off_effects", [])
    if not isinstance(screen_schedule_effects, list) or not screen_schedule_effects:
        errors.append("project.screen_schedule_off_effects must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in screen_schedule_effects):
        errors.append("project.screen_schedule_off_effects must only contain non-empty strings")


def check_project_api_key_feature_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    permissions = project.get("immich_api_key_permissions", [])
    if not isinstance(permissions, list) or not permissions:
        errors.append("project.immich_api_key_permissions must be a non-empty list")
    else:
        seen_permissions: set[str] = set()
        for permission in permissions:
            if not isinstance(permission, dict):
                errors.append("project.immich_api_key_permissions entries must be objects")
                continue
            name = str(permission.get("name", "")).strip()
            purpose = str(permission.get("purpose", "")).strip()
            if not name:
                errors.append("project.immich_api_key_permissions entry is missing name")
            elif name in seen_permissions:
                errors.append(f"Duplicate Immich API key permission: {name}")
            elif not re.match(r"^[a-z]+\.(read|view)$", name):
                errors.append(f"Immich API key permission should be read/view-only: {name}")
            seen_permissions.add(name)
            if not purpose:
                errors.append(f"Immich API key permission {name or '<missing>'} is missing purpose")
