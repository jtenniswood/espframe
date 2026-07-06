from __future__ import annotations

import re

def check_project_generated_web_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "generated_asset_outputs",
        "generated_asset_sources",
        "web_template_placeholders",
        "web_initial_fetch_first_keys",
        "web_live_render_state_keys",
        "web_live_render_state_prefixes",
        "web_local_state_keys",
        "web_manual_state_keys",
    ):
        values = project.get(field, [])
        if not isinstance(values, list) or not values:
            errors.append(f"project.{field} must be a non-empty list")
        elif any(not isinstance(value, str) or not value.strip() for value in values):
            errors.append(f"project.{field} must only contain non-empty strings")
        elif len(values) != len(set(values)):
            errors.append(f"project.{field} must not contain duplicate entries")



def check_project_web_server_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "slideshow_check_interval",
        "docs_dist_artifact_name",
        "docs_firmware_artifact_name",
        "docs_dist_output_path",
        "docs_deploy_path",
        "github_pages_environment",
        "github_pages_concurrency_group",
        "setup_captive_portal_ip",
        "setup_screen_dim_delay",
        "setup_screen_dim_brightness",
        "setup_screen_dim_transition",
        "setup_loading_backlight_brightness",
        "setup_loading_backlight_transition",
        "setup_connection_ready_condition",
        "manual_setup_package_ref",
        "manual_setup_package_refresh",
        "factory_firmware_purpose",
        "factory_firmware_secret_policy",
        "factory_firmware_network_mode",
        "factory_firmware_setup_method",
        "factory_firmware_local_use",
        "web_server_public_app_path",
        "web_server_factory_css_include",
        "web_server_factory_js_include",
        "external_component_git_source_type",
        "external_component_local_source_type",
        "external_component_git_path",
        "external_component_local_path",
        "external_component_ref",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    for field in ("web_server_port", "web_server_version"):
        value = project.get(field)
        if not isinstance(value, int) or isinstance(value, bool) or value < 1:
            errors.append(f"project.{field} must be a positive integer")
    if not isinstance(project.get("web_server_include_internal"), bool):
        errors.append("project.web_server_include_internal must be true or false")
    if not isinstance(project.get("github_pages_cancel_in_progress"), bool):
        errors.append("project.github_pages_cancel_in_progress must be true or false")
    prerelease_lookup_limit = project.get("github_prerelease_lookup_limit")
    if not isinstance(prerelease_lookup_limit, int) or isinstance(prerelease_lookup_limit, bool) or prerelease_lookup_limit < 1:
        errors.append("project.github_prerelease_lookup_limit must be a positive integer")
    for field in ("github_release_download_clobber", "github_release_upload_clobber"):
        if not isinstance(project.get(field), bool):
            errors.append(f"project.{field} must be true or false")
    if "web_server_factory_js_url" not in project or not isinstance(project.get("web_server_factory_js_url"), str):
        errors.append("project.web_server_factory_js_url must be a string")
    sorting_groups = project.get("web_server_sorting_groups", [])
    if not isinstance(sorting_groups, list) or not sorting_groups:
        errors.append("project.web_server_sorting_groups must be a non-empty list")
    else:
        group_ids: set[str] = set()
        for group in sorting_groups:
            if not isinstance(group, dict):
                errors.append("project.web_server_sorting_groups entries must be objects")
                continue
            group_id = str(group.get("id", "")).strip()
            name = str(group.get("name", "")).strip()
            weight = group.get("sorting_weight")
            if not group_id:
                errors.append("project.web_server_sorting_groups entry is missing id")
            elif group_id in group_ids:
                errors.append(f"Duplicate project.web_server_sorting_groups id: {group_id}")
            group_ids.add(group_id)
            if not name:
                errors.append(f"project.web_server_sorting_groups.{group_id or '<missing>'} is missing name")
            if not isinstance(weight, int) or isinstance(weight, bool):
                errors.append(f"project.web_server_sorting_groups.{group_id or '<missing>'}.sorting_weight must be an integer")
    external_components = project.get("external_component_names", [])
    if not isinstance(external_components, list) or not external_components:
        errors.append("project.external_component_names must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in external_components):
        errors.append("project.external_component_names must only contain non-empty strings")


def check_project_web_ui_metadata_shape(product: dict, errors: list[str]) -> None:
    project = product["project"]
    web_ui_tabs = project.get("web_ui_tabs", [])
    if not isinstance(web_ui_tabs, list) or not web_ui_tabs:
        errors.append("project.web_ui_tabs must be a non-empty list")
    else:
        tab_ids: set[str] = set()
        for tab in web_ui_tabs:
            if not isinstance(tab, dict):
                errors.append("project.web_ui_tabs entries must be objects")
                continue
            tab_id = str(tab.get("id", "")).strip()
            tab_label = str(tab.get("label", "")).strip()
            if not tab_id:
                errors.append("project.web_ui_tabs entry is missing id")
            elif tab_id in tab_ids:
                errors.append(f"Duplicate project.web_ui_tabs id: {tab_id}")
            tab_ids.add(tab_id)
            if not tab_label:
                errors.append("project.web_ui_tabs entry is missing label")
    retained_log_lines = project.get("web_ui_logs_retained_lines")
    if not isinstance(retained_log_lines, int) or isinstance(retained_log_lines, bool) or retained_log_lines < 1:
        errors.append("project.web_ui_logs_retained_lines must be a positive integer")
    smoke_scenarios = project.get("web_smoke_required_scenarios", [])
    if not isinstance(smoke_scenarios, list) or not smoke_scenarios:
        errors.append("project.web_smoke_required_scenarios must be a non-empty list")
    else:
        scenario_ids = [str(scenario).strip() for scenario in smoke_scenarios]
        if any(not scenario_id for scenario_id in scenario_ids):
            errors.append("project.web_smoke_required_scenarios must only contain non-empty strings")
        if len(scenario_ids) != len(set(scenario_ids)):
            errors.append("project.web_smoke_required_scenarios must not contain duplicate scenarios")
        for scenario_id in scenario_ids:
            if scenario_id and not re.match(r"^[a-z0-9]+(?:-[a-z0-9]+)*$", scenario_id):
                errors.append(f"project.web_smoke_required_scenarios contains unsupported scenario id: {scenario_id}")
    logs_event_source = str(project.get("web_ui_logs_event_source", "")).strip()
    logs_event_name = str(project.get("web_ui_logs_event_name", "")).strip()
    logs_clear_label = str(project.get("web_ui_logs_clear_label", "")).strip()
    if logs_event_source and (not logs_event_source.startswith("/") or any(char.isspace() for char in logs_event_source)):
        errors.append("project.web_ui_logs_event_source must be a root-relative path without whitespace")
    if logs_event_name and not re.match(r"^[A-Za-z][A-Za-z0-9_-]*$", logs_event_name):
        errors.append("project.web_ui_logs_event_name must be a non-empty event name")
    if logs_clear_label and len(logs_clear_label) > 40:
        errors.append("project.web_ui_logs_clear_label must be 40 characters or fewer")
