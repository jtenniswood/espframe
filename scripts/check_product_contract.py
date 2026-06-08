#!/usr/bin/env python3
"""Validate the shared product metadata against the checked-in project.

This is the first release gate for the reset architecture. It catches drift
between product metadata, firmware YAML, the custom web UI, docs, and CI before
we start generating larger parts of the project from the product schema.
"""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path

from product_contract.backup_metadata import check_backup_metadata
from product_contract.common import (
    ROOT,
    TIME_YAML,
    WEB_APP,
    WEB_TEMPLATE,
    check_relative_path,
    read,
    read_web_source,
    rel,
    require_contains,
    require_firmware_text_entity_shape,
)
from product_contract.devices import check_devices
from product_contract.settings import check_settings
from product_contract.workflows import (
    check_device_workflow_contract,
    check_esphome_version,
    check_node_version,
    check_workflows,
)
from product_config import (
    default_public_manifest_urls,
    device_public_manifest_urls,
    load_product,
    public_base_url,
    public_url,
    web_manual_entities,
    web_static_entities,
)


def check_project_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    for field in (
        "name",
        "site_description",
        "ai_description",
        "social_image",
        "social_image_alt",
        "usb_flashing_image",
        "usb_flashing_image_alt",
        "web_installer_required_api",
        "web_installer_computer_requirement",
        "usb_cable_requirement",
        "usb_cable_warning",
        "immich_api_key_mode",
        "immich_api_key_privacy_promise",
        "home_assistant_name",
        "home_assistant_url",
        "home_assistant_requirement",
        "home_assistant_integration_platform",
        "device_log_level_default",
        "device_debug_update_interval",
        "firmware_update_source",
        "firmware_beta_channel_label",
        "firmware_manual_check_behavior",
        "firmware_beta_check_requirement",
        "firmware_custom_manifest_requirement",
        "ota_update_platform",
        "ota_pre_update_action",
        "ota_pre_update_transition",
        "ota_pre_update_delay",
        "backup_filename_prefix",
        "backup_filename_date_format",
        "backup_import_write_behavior",
        "backup_partial_config_behavior",
        "backup_invalid_photo_id_behavior",
        "privacy_connection_model",
        "privacy_network_scope",
        "privacy_no_cloud_service",
        "privacy_no_extra_account",
        "privacy_no_uploads",
        "privacy_no_hosted_service",
        "favicon",
        "npm_package_name",
        "license_id",
        "license_name",
        "owner_name",
        "owner_url",
        "package_name",
        "repository_url",
        "github_default_branch",
        "release_url_base",
        "release_artifact_prefix",
        "release_build_output_dir",
        "release_publish_dir",
        "release_uploaded_verify_dir",
        "release_source_factory_binary",
        "release_source_ota_binary",
        "release_esphome_cache_dir",
        "release_esphome_cache_key_prefix",
        "release_version_pattern",
        "stable_release_version_pattern",
        "firmware_version_placeholder_line",
        "firmware_local_build_version",
        "release_changelog_fallback_category",
        "phase_1_status_note",
        "public_base_url",
        "support_url",
        "support_button_image_url",
        "node_package_cache",
        "node_install_command",
        "local_check_command",
        "docs_build_command",
        "esphome_docker_image",
        "esphome_config_mount",
        "github_docs_release_meta_step_id",
        "github_docs_release_tag_env",
        "github_docs_release_tag_output",
        "github_docs_prerelease_tag_env",
        "github_pages_deployment_step_id",
        "github_pages_url_output",
        "web_ui_logs_event_source",
        "web_ui_logs_event_name",
        "web_ui_logs_clear_label",
        "node_version",
        "github_actions_runner",
        "github_docs_workflow_run_success_conclusion",
        "github_release_notes_version_ref",
        "github_release_build_version_ref",
        "github_release_build_ref",
        "github_release_notes_output",
    ):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")

    package_name = str(project.get("package_name", "")).strip()
    if package_name and not re.match(r"^[A-Za-z0-9_.-]+\.[A-Za-z0-9_.-]+$", package_name):
        errors.append("project.package_name must look like a reverse-DNS package name")

    release_url_base = str(project.get("release_url_base", "")).strip()
    phase_1_status_note = check_relative_path(project.get("phase_1_status_note"), "project.phase_1_status_note", errors)
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    default_branch = str(project.get("github_default_branch", "")).strip()
    if repository_url and not repository_url.startswith("https://github.com/"):
        errors.append("project.repository_url must be an https GitHub URL")
    if default_branch and not re.match(r"^[A-Za-z0-9._/-]+$", default_branch):
        errors.append("project.github_default_branch contains unsupported characters")
    if default_branch and str(project.get("manual_setup_package_ref", "")).strip() != default_branch:
        errors.append("project.manual_setup_package_ref must match project.github_default_branch")
    if default_branch and str(project.get("external_component_ref", "")).strip() != default_branch:
        errors.append("project.external_component_ref must match project.github_default_branch")
    if release_url_base and not release_url_base.startswith("https://"):
        errors.append("project.release_url_base must be an https URL")
    if release_url_base and not release_url_base.endswith("/"):
        errors.append("project.release_url_base must end with /")
    if repository_url and release_url_base and release_url_base != f"{repository_url}/releases/tag/":
        errors.append("project.release_url_base must be based on project.repository_url")
    if phase_1_status_note:
        phase_1_path = ROOT / phase_1_status_note
        if not phase_1_status_note.startswith("docs/") or phase_1_path.suffix != ".md":
            errors.append("project.phase_1_status_note must point at a docs markdown file")
        phase_1_text = read(phase_1_path, errors)
        for needle in (
            "Product-owned behavior",
            "Generated outputs",
            "Validation gates",
            "Phase 2 boundary",
            "npm run check:product",
            "npm run check:all",
        ):
            require_contains(phase_1_text, needle, phase_1_status_note, errors)
    release_actions = project.get("release_workflow_actions", {})
    if not isinstance(release_actions, dict) or not release_actions:
        errors.append("project.release_workflow_actions must be a non-empty object")
    else:
        for name, action in release_actions.items():
            if not isinstance(name, str) or not name.strip():
                errors.append("project.release_workflow_actions keys must be non-empty strings")
            if not isinstance(action, str) or not action.strip():
                errors.append(f"project.release_workflow_actions.{name} must be a non-empty string")
    workflow_permissions = project.get("github_workflow_permissions", {})
    expected_workflows = {"compile", "docs", "release"}
    github_cli_env = project.get("github_cli_env", {})
    expected_github_cli_env = {"GH_TOKEN", "GH_REPO"}
    if not isinstance(github_cli_env, dict) or not github_cli_env:
        errors.append("project.github_cli_env must be a non-empty object")
    else:
        configured_env = {str(name).strip() for name in github_cli_env}
        missing_env = sorted(expected_github_cli_env - configured_env)
        extra_env = sorted(configured_env - expected_github_cli_env)
        if missing_env:
            errors.append(f"project.github_cli_env is missing variables: {', '.join(missing_env)}")
        if extra_env:
            errors.append(f"project.github_cli_env contains unknown variables: {', '.join(extra_env)}")
        for raw_name, raw_value in github_cli_env.items():
            name = str(raw_name).strip()
            value = str(raw_value).strip()
            if not name:
                errors.append("project.github_cli_env keys must be non-empty strings")
            if not value:
                errors.append(f"project.github_cli_env.{name or '<missing>'} must be a non-empty string")
    if not isinstance(workflow_permissions, dict) or not workflow_permissions:
        errors.append("project.github_workflow_permissions must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_permissions}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_permissions is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_permissions contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, permissions in workflow_permissions.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_permissions keys must be non-empty strings")
            if not isinstance(permissions, dict) or not permissions:
                errors.append(f"project.github_workflow_permissions.{name or '<missing>'} must be a non-empty object")
                continue
            for raw_scope, raw_access in permissions.items():
                scope = str(raw_scope).strip()
                access = str(raw_access).strip()
                if not scope:
                    errors.append(f"project.github_workflow_permissions.{name or '<missing>'} scopes must be non-empty strings")
                if access not in {"read", "write", "none"}:
                    errors.append(
                        f"project.github_workflow_permissions.{name or '<missing>'}.{scope or '<missing>'} must be read, write, or none"
                    )
    workflow_names = project.get("github_workflow_names", {})
    if not isinstance(workflow_names, dict) or not workflow_names:
        errors.append("project.github_workflow_names must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_names}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_names is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_names contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, raw_label in workflow_names.items():
            name = str(raw_name).strip()
            label = str(raw_label).strip()
            if not name:
                errors.append("project.github_workflow_names keys must be non-empty strings")
            if not label:
                errors.append(f"project.github_workflow_names.{name or '<missing>'} must be a non-empty string")
    workflow_path_filters = project.get("github_workflow_path_filters", {})
    expected_path_filter_sets = {"compile_pull_request", "docs_push"}
    if not isinstance(workflow_path_filters, dict) or not workflow_path_filters:
        errors.append("project.github_workflow_path_filters must be a non-empty object")
    else:
        configured_filter_sets = {str(name).strip() for name in workflow_path_filters}
        missing_filter_sets = sorted(expected_path_filter_sets - configured_filter_sets)
        extra_filter_sets = sorted(configured_filter_sets - expected_path_filter_sets)
        if missing_filter_sets:
            errors.append(f"project.github_workflow_path_filters is missing filters: {', '.join(missing_filter_sets)}")
        if extra_filter_sets:
            errors.append(f"project.github_workflow_path_filters contains unknown filters: {', '.join(extra_filter_sets)}")
        for raw_name, raw_paths in workflow_path_filters.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_path_filters keys must be non-empty strings")
            if not isinstance(raw_paths, list) or not raw_paths:
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must be a non-empty list")
                continue
            paths = [str(path).strip() for path in raw_paths]
            if any(not path for path in paths):
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must only contain non-empty strings")
            if len(paths) != len(set(paths)):
                errors.append(f"project.github_workflow_path_filters.{name or '<missing>'} must not contain duplicate paths")
    workflow_events = project.get("github_workflow_events", {})
    if not isinstance(workflow_events, dict) or not workflow_events:
        errors.append("project.github_workflow_events must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_events}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_events is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_events contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_name, raw_events in workflow_events.items():
            name = str(raw_name).strip()
            if not name:
                errors.append("project.github_workflow_events keys must be non-empty strings")
            if not isinstance(raw_events, list) or not raw_events:
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must be a non-empty list")
                continue
            events = [str(event).strip() for event in raw_events]
            if any(not event for event in events):
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must only contain non-empty strings")
            if len(events) != len(set(events)):
                errors.append(f"project.github_workflow_events.{name or '<missing>'} must not contain duplicate events")
    workflow_event_types = project.get("github_workflow_event_types", {})
    if not isinstance(workflow_event_types, dict) or not workflow_event_types:
        errors.append("project.github_workflow_event_types must be a non-empty object")
    else:
        for raw_key, raw_types in workflow_event_types.items():
            key = str(raw_key).strip()
            if "." not in key:
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must use workflow.event format")
            if not isinstance(raw_types, list) or not raw_types:
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must be a non-empty list")
                continue
            event_types = [str(event_type).strip() for event_type in raw_types]
            if any(not event_type for event_type in event_types):
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must only contain non-empty strings")
            if len(event_types) != len(set(event_types)):
                errors.append(f"project.github_workflow_event_types.{key or '<missing>'} must not contain duplicate event types")
    workflow_jobs = project.get("github_workflow_jobs", {})
    if not isinstance(workflow_jobs, dict) or not workflow_jobs:
        errors.append("project.github_workflow_jobs must be a non-empty object")
    else:
        configured_workflows = {str(name).strip() for name in workflow_jobs}
        missing_workflows = sorted(expected_workflows - configured_workflows)
        extra_workflows = sorted(configured_workflows - expected_workflows)
        if missing_workflows:
            errors.append(f"project.github_workflow_jobs is missing workflows: {', '.join(missing_workflows)}")
        if extra_workflows:
            errors.append(f"project.github_workflow_jobs contains unknown workflows: {', '.join(extra_workflows)}")
        for raw_workflow, raw_jobs in workflow_jobs.items():
            workflow = str(raw_workflow).strip()
            if not workflow:
                errors.append("project.github_workflow_jobs keys must be non-empty strings")
            if not isinstance(raw_jobs, dict) or not raw_jobs:
                errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'} must be a non-empty object")
                continue
            for raw_job_id, raw_job_name in raw_jobs.items():
                job_id = str(raw_job_id).strip()
                job_name = str(raw_job_name).strip()
                if not job_id:
                    errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'} job ids must be non-empty strings")
                if not job_name:
                    errors.append(f"project.github_workflow_jobs.{workflow or '<missing>'}.{job_id or '<missing>'} must be a non-empty string")
    workflow_job_dependencies = project.get("github_workflow_job_dependencies", {})
    if not isinstance(workflow_job_dependencies, dict) or not workflow_job_dependencies:
        errors.append("project.github_workflow_job_dependencies must be a non-empty object")
    else:
        for raw_key, raw_dependencies in workflow_job_dependencies.items():
            key = str(raw_key).strip()
            workflow, _, job_id = key.partition(".")
            if not workflow or not job_id:
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must use workflow.job format")
            if not isinstance(raw_dependencies, list) or not raw_dependencies:
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must be a non-empty list")
                continue
            dependencies = [str(dependency).strip() for dependency in raw_dependencies]
            if any(not dependency for dependency in dependencies):
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must only contain non-empty strings")
            if len(dependencies) != len(set(dependencies)):
                errors.append(f"project.github_workflow_job_dependencies.{key or '<missing>'} must not contain duplicate jobs")
    sparse_checkout_files = project.get("github_sparse_checkout_files", [])
    if not isinstance(sparse_checkout_files, list) or not sparse_checkout_files:
        errors.append("project.github_sparse_checkout_files must be a non-empty list")
    else:
        paths = [str(path).strip() for path in sparse_checkout_files]
        if any(not path for path in paths):
            errors.append("project.github_sparse_checkout_files must only contain non-empty strings")
        if len(paths) != len(set(paths)):
            errors.append("project.github_sparse_checkout_files must not contain duplicate paths")
        for raw_path in paths:
            path = check_relative_path(raw_path, "project.github_sparse_checkout_files entry", errors)
            if path:
                read(ROOT / path, errors)
    if not isinstance(project.get("github_sparse_checkout_cone_mode"), bool):
        errors.append("project.github_sparse_checkout_cone_mode must be true or false")
    release_notes_fetch_depth = project.get("github_release_notes_fetch_depth")
    if not isinstance(release_notes_fetch_depth, int) or isinstance(release_notes_fetch_depth, bool) or release_notes_fetch_depth < 0:
        errors.append("project.github_release_notes_fetch_depth must be a non-negative integer")
    if not isinstance(project.get("github_release_notes_fetch_tags"), bool):
        errors.append("project.github_release_notes_fetch_tags must be true or false")
    if not isinstance(project.get("github_release_build_fail_fast"), bool):
        errors.append("project.github_release_build_fail_fast must be true or false")
    if not isinstance(project.get("esphome_docker_remove_container"), bool):
        errors.append("project.esphome_docker_remove_container must be true or false")
    release_asset_suffixes = project.get("release_asset_suffixes", [])
    if not isinstance(release_asset_suffixes, list) or not release_asset_suffixes:
        errors.append("project.release_asset_suffixes must be a non-empty list")
    elif any(not isinstance(suffix, str) or not suffix.strip() or not suffix.startswith(".") for suffix in release_asset_suffixes):
        errors.append("project.release_asset_suffixes must only contain non-empty dot-prefixed strings")
    for field in ("release_binary_download_patterns", "release_manifest_download_patterns", "release_uploaded_verify_patterns"):
        patterns = project.get(field, [])
        if not isinstance(patterns, list) or not patterns:
            errors.append(f"project.{field} must be a non-empty list")
        else:
            values = [str(pattern).strip() for pattern in patterns]
            if any(not value for value in values):
                errors.append(f"project.{field} must only contain non-empty strings")
            if len(values) != len(set(values)):
                errors.append(f"project.{field} must not contain duplicate patterns")
    cache_hash_files = project.get("release_esphome_cache_hash_files", [])
    if not isinstance(cache_hash_files, list) or not cache_hash_files:
        errors.append("project.release_esphome_cache_hash_files must be a non-empty list")
    else:
        values = [str(path).strip() for path in cache_hash_files]
        if any(not value for value in values):
            errors.append("project.release_esphome_cache_hash_files must only contain non-empty strings")
        if len(values) != len(set(values)):
            errors.append("project.release_esphome_cache_hash_files must not contain duplicate paths")
    for field in ("release_version_pattern", "stable_release_version_pattern"):
        pattern = str(project.get(field, "")).strip()
        if pattern:
            try:
                re.compile(pattern)
            except re.error as exc:
                errors.append(f"project.{field} must be a valid regular expression: {exc}")
    placeholder_versions = project.get("firmware_placeholder_versions", [])
    local_build_version = str(project.get("firmware_local_build_version", "")).strip()
    if not isinstance(placeholder_versions, list) or not placeholder_versions:
        errors.append("project.firmware_placeholder_versions must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in placeholder_versions):
        errors.append("project.firmware_placeholder_versions must only contain non-empty strings")
    elif "0.0.0" not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include 0.0.0")
    elif default_branch and default_branch not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include project.github_default_branch")
    elif local_build_version and local_build_version not in placeholder_versions:
        errors.append("project.firmware_placeholder_versions must include project.firmware_local_build_version")
    changelog_categories = project.get("release_changelog_categories", [])
    if not isinstance(changelog_categories, list) or not changelog_categories:
        errors.append("project.release_changelog_categories must be a non-empty list")
    else:
        seen_category_titles: set[str] = set()
        for category in changelog_categories:
            if not isinstance(category, dict):
                errors.append("project.release_changelog_categories entries must be objects")
                continue
            title = str(category.get("title", "")).strip()
            if not title:
                errors.append("project.release_changelog_categories entry is missing title")
            elif title in seen_category_titles:
                errors.append(f"Duplicate release changelog category: {title}")
            seen_category_titles.add(title)
            for field in ("paths", "keywords"):
                values = category.get(field, [])
                if not isinstance(values, list) or not values:
                    errors.append(f"project.release_changelog_categories.{title or '<missing>'}.{field} must be a non-empty list")
                elif any(not isinstance(value, str) or not value.strip() for value in values):
                    errors.append(f"project.release_changelog_categories.{title or '<missing>'}.{field} must only contain non-empty strings")
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

    for field in ("support_url", "support_button_image_url"):
        value = str(project.get(field, "")).strip()
        if value and not value.startswith("https://"):
            errors.append(f"project.{field} must be an https URL")
    home_assistant_url = str(project.get("home_assistant_url", "")).strip()
    if home_assistant_url and not home_assistant_url.startswith("https://"):
        errors.append("project.home_assistant_url must be an https URL")
    owner_url = str(project.get("owner_url", "")).strip()
    if owner_url and not owner_url.startswith("https://"):
        errors.append("project.owner_url must be an https URL")
    for field in ("social_image", "usb_flashing_image", "favicon"):
        value = str(project.get(field, "")).strip()
        if value and (value.startswith("/") or ".." in Path(value).parts):
            errors.append(f"project.{field} must be a relative public asset path")
        if value:
            public_asset = ROOT / "docs" / "public" / value
            if not public_asset.is_file():
                errors.append(f"Missing file: {rel(public_asset)}")
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
    manifest_url_length_limit = project.get("firmware_manifest_url_length_limit")
    if not isinstance(manifest_url_length_limit, int) or isinstance(manifest_url_length_limit, bool) or manifest_url_length_limit < 1:
        errors.append("project.firmware_manifest_url_length_limit must be a positive integer")
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
    backup_config_version = project.get("backup_config_version")
    if not isinstance(backup_config_version, int) or isinstance(backup_config_version, bool) or backup_config_version < 1:
        errors.append("project.backup_config_version must be a positive integer")
    backup_import_photo_id_limit = project.get("backup_import_photo_id_limit")
    if (
        not isinstance(backup_import_photo_id_limit, int)
        or isinstance(backup_import_photo_id_limit, bool)
        or backup_import_photo_id_limit < 1
    ):
        errors.append("project.backup_import_photo_id_limit must be a positive integer")
    backup_excluded_values = project.get("backup_excluded_runtime_values", [])
    if not isinstance(backup_excluded_values, list) or not backup_excluded_values:
        errors.append("project.backup_excluded_runtime_values must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_excluded_values}) != len(backup_excluded_values):
        errors.append("project.backup_excluded_runtime_values must not contain duplicate values")
    backup_export_groups = project.get("backup_export_groups", [])
    if not isinstance(backup_export_groups, list) or not backup_export_groups:
        errors.append("project.backup_export_groups must be a non-empty list")
    elif any(not isinstance(value, str) or not value.strip() for value in backup_export_groups):
        errors.append("project.backup_export_groups must only contain non-empty strings")
    elif len({str(value).strip() for value in backup_export_groups}) != len(backup_export_groups):
        errors.append("project.backup_export_groups must not contain duplicate groups")
    backup_export_fields = project.get("backup_export_fields", {})
    if not isinstance(backup_export_fields, dict) or not backup_export_fields:
        errors.append("project.backup_export_fields must be a non-empty object")
    else:
        expected_groups = {str(group).strip() for group in backup_export_groups if str(group).strip()}
        configured_groups = {str(group).strip() for group in backup_export_fields}
        missing_groups = sorted(expected_groups - configured_groups)
        extra_groups = sorted(configured_groups - expected_groups)
        if missing_groups:
            errors.append(f"project.backup_export_fields is missing groups: {', '.join(missing_groups)}")
        if extra_groups:
            errors.append(f"project.backup_export_fields contains unknown groups: {', '.join(extra_groups)}")

        all_fields: set[str] = set()
        field_count = 0
        for raw_group, raw_fields in backup_export_fields.items():
            group = str(raw_group).strip()
            if not group:
                errors.append("project.backup_export_fields keys must be non-empty strings")
            if not isinstance(raw_fields, list) or not raw_fields:
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must be a non-empty list")
                continue
            fields = [str(field).strip() for field in raw_fields]
            if any(not field for field in fields):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must only contain non-empty strings")
            if len(fields) != len(set(fields)):
                errors.append(f"project.backup_export_fields.{group or '<missing>'} must not contain duplicate fields")
            all_fields.update(fields)
            field_count += len(fields)
        if len(all_fields) != field_count:
            errors.append("project.backup_export_fields field names must be unique across groups")
    backup_state_mappings = project.get("backup_field_state_keys", {})
    if not isinstance(backup_state_mappings, dict) or not backup_state_mappings:
        errors.append("project.backup_field_state_keys must be a non-empty object")
    else:
        expected_group_fields = {
            (str(group).strip(), str(field).strip())
            for group, fields in project.get("backup_export_fields", {}).items()
            if isinstance(fields, list)
            for field in fields
            if str(group).strip() and str(field).strip()
        }
        configured_group_fields: set[tuple[str, str]] = set()
        valid_state_keys = (
            {str(setting.get("key", "")).strip() for setting in product.get("settings", [])}
            | set(web_static_entities(product))
            | set(web_manual_entities(product))
        )
        for raw_group, raw_fields in backup_state_mappings.items():
            group = str(raw_group).strip()
            if not group:
                errors.append("project.backup_field_state_keys keys must be non-empty strings")
                continue
            if not isinstance(raw_fields, dict) or not raw_fields:
                errors.append(f"project.backup_field_state_keys.{group} must be a non-empty object")
                continue
            for raw_field, raw_state_keys in raw_fields.items():
                field = str(raw_field).strip()
                if not field:
                    errors.append(f"project.backup_field_state_keys.{group} field keys must be non-empty strings")
                    continue
                configured_group_fields.add((group, field))
                if isinstance(raw_state_keys, list):
                    state_keys = [str(value).strip() for value in raw_state_keys]
                    if not state_keys:
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must list at least one state key")
                    elif any(not value for value in state_keys):
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must only contain non-empty strings")
                    elif len(state_keys) != len(set(state_keys)):
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must not contain duplicate state keys")
                else:
                    state_key = str(raw_state_keys).strip()
                    state_keys = [state_key] if state_key else []
                    if not state_key:
                        errors.append(f"project.backup_field_state_keys.{group}.{field} must be a non-empty string or list")
                for state_key in state_keys:
                    if state_key not in valid_state_keys:
                        errors.append(f"Backup field {group}.{field} maps to unknown state key {state_key}")
        missing_mappings = sorted(expected_group_fields - configured_group_fields)
        extra_mappings = sorted(configured_group_fields - expected_group_fields)
        if missing_mappings:
            errors.append(
                "project.backup_field_state_keys is missing fields: "
                + ", ".join(f"{group}.{field}" for group, field in missing_mappings)
            )
        if extra_mappings:
            errors.append(
                "project.backup_field_state_keys contains unknown fields: "
                + ", ".join(f"{group}.{field}" for group, field in extra_mappings)
            )
    backup_fixture_files = project.get("backup_fixture_files", [])
    if not isinstance(backup_fixture_files, list) or not backup_fixture_files:
        errors.append("project.backup_fixture_files must be a non-empty list")
    elif len({str(fixture_file).strip() for fixture_file in backup_fixture_files}) != len(backup_fixture_files):
        errors.append("project.backup_fixture_files must not contain duplicate files")
    else:
        for fixture_file in backup_fixture_files:
            path = check_relative_path(fixture_file, "project.backup_fixture_files entry", errors)
            if path:
                read(ROOT / path, errors)
    compatibility_fixture_files = project.get("compatibility_fixture_files", {})
    if not isinstance(compatibility_fixture_files, dict) or not compatibility_fixture_files:
        errors.append("project.compatibility_fixture_files must be a non-empty object")
    else:
        accepted = compatibility_fixture_files.get("accepted", [])
        rejected_fields = compatibility_fixture_files.get("rejected_fields", [])
        if not isinstance(accepted, list) or not accepted:
            errors.append("project.compatibility_fixture_files.accepted must be a non-empty list")
        else:
            for fixture_file in accepted:
                path = check_relative_path(fixture_file, "project.compatibility_fixture_files.accepted entry", errors)
                if path:
                    read(ROOT / path, errors)
        if not isinstance(rejected_fields, list) or not rejected_fields:
            errors.append("project.compatibility_fixture_files.rejected_fields must be a non-empty list")
        else:
            for item in rejected_fields:
                if not isinstance(item, dict):
                    errors.append("project.compatibility_fixture_files.rejected_fields entries must be objects")
                    continue
                path = check_relative_path(item.get("path"), "project.compatibility_fixture_files.rejected_fields path", errors)
                if path:
                    read(ROOT / path, errors)
                messages = item.get("messages", [])
                if not isinstance(messages, list) or not messages:
                    errors.append(f"project.compatibility_fixture_files.rejected_fields {path or '<missing>'} messages must be a non-empty list")
                elif any(not isinstance(message, str) or not message.strip() for message in messages):
                    errors.append(f"project.compatibility_fixture_files.rejected_fields {path or '<missing>'} messages must be non-empty strings")
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
    logs_event_source = str(project.get("web_ui_logs_event_source", "")).strip()
    logs_event_name = str(project.get("web_ui_logs_event_name", "")).strip()
    logs_clear_label = str(project.get("web_ui_logs_clear_label", "")).strip()
    if logs_event_source and (not logs_event_source.startswith("/") or any(char.isspace() for char in logs_event_source)):
        errors.append("project.web_ui_logs_event_source must be a root-relative path without whitespace")
    if logs_event_name and not re.match(r"^[A-Za-z][A-Za-z0-9_-]*$", logs_event_name):
        errors.append("project.web_ui_logs_event_name must be a non-empty event name")
    if logs_clear_label and len(logs_clear_label) > 40:
        errors.append("project.web_ui_logs_clear_label must be 40 characters or fewer")
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

    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    if package_name:
        require_contains(firmware_update, f"name: {package_name}", "common/addon/firmware_update.yaml", errors)


def check_npm_package_metadata(product: dict, errors: list[str]) -> None:
    expected_name = str(product["project"].get("npm_package_name", "")).strip()
    expected_license = str(product["project"].get("license_id", "")).strip()
    if not expected_name:
        errors.append("project.npm_package_name is required")
        return

    try:
        package_json = json.loads(read(ROOT / "package.json", errors) or "{}")
        package_lock = json.loads(read(ROOT / "package-lock.json", errors) or "{}")
    except json.JSONDecodeError as exc:
        errors.append(f"Package metadata JSON is invalid: {exc}")
        return

    if package_json.get("name") != expected_name:
        errors.append("package.json name must match project.npm_package_name")
    if expected_license and package_json.get("license") != expected_license:
        errors.append("package.json license must match project.license_id")
    scripts = package_json.get("scripts", {})
    if not isinstance(scripts, dict):
        errors.append("package.json scripts must be an object")
    else:
        if scripts.get("check:backup") != "python3 scripts/check_backup_config.py":
            errors.append("package.json check:backup must run scripts/check_backup_config.py")
        if scripts.get("check:compat") != "python3 scripts/check_compatibility.py":
            errors.append("package.json check:compat must run scripts/check_compatibility.py")
        if scripts.get("check:firmware-fields") != "python3 scripts/check_firmware_fields.py":
            errors.append("package.json check:firmware-fields must run scripts/check_firmware_fields.py")
        if scripts.get("test:web-compat") != "node tests/web_compat_tests.js":
            errors.append("package.json test:web-compat must run tests/web_compat_tests.js")
        if scripts.get("test:web-modules") != "node tests/web_module_tests.js":
            errors.append("package.json test:web-modules must run tests/web_module_tests.js")
        if scripts.get("test:web-smoke") != "node tests/web_smoke_tests.js":
            errors.append("package.json test:web-smoke must run tests/web_smoke_tests.js")
        check_all = str(scripts.get("check:all", ""))
        if "npm run check:backup" not in check_all:
            errors.append("package.json check:all must include check:backup")
        if "npm run check:compat" not in check_all:
            errors.append("package.json check:all must include check:compat")
        if "npm run check:firmware-fields" not in check_all:
            errors.append("package.json check:all must include check:firmware-fields")
        if "npm run test:web-compat" not in check_all:
            errors.append("package.json check:all must include test:web-compat")
        if "npm run test:web-modules" not in check_all:
            errors.append("package.json check:all must include test:web-modules")
        if "npm run test:web-smoke" not in check_all:
            errors.append("package.json check:all must include test:web-smoke")
    if package_lock.get("name") != expected_name:
        errors.append("package-lock.json name must match project.npm_package_name")
    root_package = package_lock.get("packages", {}).get("", {})
    if root_package.get("name") != expected_name:
        errors.append("package-lock.json root package name must match project.npm_package_name")
    if expected_license and root_package.get("license") != expected_license:
        errors.append("package-lock.json root package license must match project.license_id")


def check_license_metadata(product: dict, errors: list[str]) -> None:
    license_id = str(product["project"].get("license_id", "")).strip()
    license_name = str(product["project"].get("license_name", "")).strip()
    if not license_id:
        errors.append("project.license_id is required")
    if not license_name:
        errors.append("project.license_name is required")

    license_text = read(ROOT / "LICENSE", errors)
    readme = read(ROOT / "README.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    if license_name:
        for label, text in (("LICENSE", license_text), ("README.md", readme), ("docs/license.md", license_docs)):
            require_contains(text, license_name, label, errors)
    if license_id:
        require_contains(read(ROOT / "package.json", errors), f'"license": "{license_id}"', "package.json", errors)


def check_immich_api_key_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    mode = str(project.get("immich_api_key_mode", "")).strip()
    privacy_promise = str(project.get("immich_api_key_privacy_promise", "")).strip()
    permissions = project.get("immich_api_key_permissions", [])

    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)

    if mode:
        require_contains(api_key_docs, f"{mode} API key", "docs/api-key.md", errors)
        require_contains(troubleshooting_docs, f"{mode} Immich API key", "docs/troubleshooting.md", errors)
        require_contains(immich_photo_frame_docs, f"{mode.capitalize()} permissions are recommended", "docs/immich-photo-frame.md", errors)
    if privacy_promise:
        require_contains(api_key_docs, privacy_promise, "docs/api-key.md", errors)

    if not isinstance(permissions, list):
        return

    expected_names: list[str] = []
    for permission in permissions:
        if not isinstance(permission, dict):
            continue
        name = str(permission.get("name", "")).strip()
        purpose = str(permission.get("purpose", "")).strip()
        if not name or not purpose:
            continue
        expected_names.append(name)
        require_contains(api_key_docs, f"| `{name}` | {purpose} |", "docs/api-key.md", errors)

    documented_names = re.findall(r"^\| `([^`]+)` \|", api_key_docs, flags=re.MULTILINE)
    if documented_names != expected_names:
        errors.append(
            "docs/api-key.md permission rows must match project.immich_api_key_permissions "
            f"(expected {expected_names}, found {documented_names})"
        )


def check_immich_connection_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    schemes = project.get("immich_server_url_schemes", [])
    targets = project.get("immich_server_url_targets", [])
    examples = project.get("immich_server_url_examples", [])

    install_docs = read(ROOT / "docs" / "install.md", errors)
    api_key_docs = read(ROOT / "docs" / "api-key.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)

    docs_to_check = (
        ("docs/install.md", install_docs),
        ("docs/api-key.md", api_key_docs),
        ("docs/troubleshooting.md", troubleshooting_docs),
    )
    for field_name, values in (
        ("immich_server_url_schemes", schemes),
        ("immich_server_url_targets", targets),
        ("immich_server_url_examples", examples),
    ):
        if not isinstance(values, list):
            continue
        for label, text in docs_to_check:
            for value in values:
                if isinstance(value, str) and value.strip():
                    require_contains(text, value.strip(), f"{label} {field_name}", errors)


def check_home_assistant_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    name = str(project.get("home_assistant_name", "")).strip()
    url = str(project.get("home_assistant_url", "")).strip()
    requirement = str(project.get("home_assistant_requirement", "")).strip()
    platform = str(project.get("home_assistant_integration_platform", "")).strip()
    features = project.get("home_assistant_integration_features", [])
    network_entities = project.get("home_assistant_network_entities", [])
    diagnostic_entities = project.get("home_assistant_diagnostic_entities", [])
    debug_update_interval = str(project.get("device_debug_update_interval", "")).strip()
    wifi_strength_source = str(project.get("network_wifi_strength_source", "")).strip()
    wifi_strength_update_interval = str(project.get("network_wifi_strength_update_interval", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    home_assistant_docs = read(ROOT / "docs" / "home-assistant.md", errors)
    network_yaml = read(ROOT / "common" / "addon" / "network.yaml", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    docs_to_check = (
        ("README.md", readme),
        ("docs/index.md", index_docs),
        ("docs/immich-photo-frame.md", immich_photo_frame_docs),
        ("docs/home-assistant.md", home_assistant_docs),
    )
    if name:
        for label, text in docs_to_check:
            require_contains(text, name, label, errors)
    if requirement:
        require_contains(readme, requirement, "README.md", errors)
        require_contains(home_assistant_docs, requirement, "docs/home-assistant.md", errors)
    if url:
        require_contains(home_assistant_docs, url, "docs/home-assistant.md", errors)
    if platform:
        for label, text in (
            ("README.md", readme),
            ("docs/immich-photo-frame.md", immich_photo_frame_docs),
            ("docs/home-assistant.md", home_assistant_docs),
        ):
            require_contains(text, platform, label, errors)
    if isinstance(features, list):
        for feature in features:
            if not isinstance(feature, str) or not feature.strip():
                continue
            require_contains(home_assistant_docs, feature.strip(), "docs/home-assistant.md", errors)
    if isinstance(network_entities, list):
        for entity in network_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(network_yaml, f'name: "{entity_name}"', "common/addon/network.yaml", errors)
    if isinstance(diagnostic_entities, list):
        for entity in diagnostic_entities:
            if not isinstance(entity, dict):
                continue
            entity_name = str(entity.get("name", "")).strip()
            entity_type = str(entity.get("type", "")).strip()
            entity_description = str(entity.get("description", "")).strip()
            for value in (entity_name, entity_type, entity_description):
                if value:
                    require_contains(home_assistant_docs, value, "docs/home-assistant.md", errors)
            if entity_name:
                require_contains(device_yaml, f'name: "{entity_name}"', device_yaml_path, errors)
    if debug_update_interval:
        require_contains(device_yaml, "debug:", device_yaml_path, errors)
        require_contains(device_yaml, f"update_interval: {debug_update_interval}", device_yaml_path, errors)
    for needle in (
        "text_sensor:",
        "platform: debug",
        "reset_reason:",
        "entity_category: diagnostic",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)
    if wifi_strength_source:
        require_contains(network_yaml, f'name: "{wifi_strength_source}"', "common/addon/network.yaml", errors)
        require_contains(network_yaml, "source_id: wifi_signal_db", "common/addon/network.yaml", errors)
    if wifi_strength_update_interval:
        require_contains(network_yaml, f"update_interval: {wifi_strength_update_interval}", "common/addon/network.yaml", errors)
    for needle in (
        "platform: status",
        "platform: wifi_signal",
        "platform: copy",
        "unit_of_measurement: \"%\"",
        "platform: wifi_info",
        "ip_address:",
        "entity_category: diagnostic",
        "return min(max(2 * (x + 100.0), 0.0), 100.0);",
    ):
        require_contains(network_yaml, needle, "common/addon/network.yaml", errors)
    require_contains(home_assistant_docs, "notify when **Network: Online** goes unavailable", "docs/home-assistant.md", errors)


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
    beta_label = str(project.get("firmware_beta_channel_label", "")).strip()
    manual_check_behavior = str(project.get("firmware_manual_check_behavior", "")).strip()
    beta_check_requirement = str(project.get("firmware_beta_check_requirement", "")).strip()
    custom_manifest_requirement = str(project.get("firmware_custom_manifest_requirement", "")).strip()
    manifest_url_length_limit = project.get("firmware_manifest_url_length_limit")
    frequency_hours = project.get("firmware_update_frequency_hours", {})
    default_urls = default_public_manifest_urls(product)

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    firmware_yaml = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    web_template = read_web_source(errors)
    web_text = read(WEB_APP, errors)

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
    if beta_label:
        require_contains(firmware_docs, beta_label, "docs/firmware-update.md", errors)
        require_contains(web_template, beta_label.capitalize(), rel(WEB_TEMPLATE), errors)
    if manual_check_behavior:
        require_contains(firmware_docs, manual_check_behavior, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, "manual_check_only", "common/addon/firmware_update.yaml", errors)
        require_contains(firmware_yaml, "component.update: firmware_update", "common/addon/firmware_update.yaml", errors)
        require_contains(web_template, 'post(endpoints.firmware_check + "/press")', rel(WEB_TEMPLATE), errors)
    if beta_check_requirement:
        require_contains(firmware_docs, beta_check_requirement, "docs/firmware-update.md", errors)
        require_contains(firmware_yaml, "lambda: 'return id(beta_channel_switch).state;'", "common/addon/firmware_update.yaml", errors)
        require_contains(web_template, "if (!S.beta_channel)", rel(WEB_TEMPLATE), errors)
    if custom_manifest_requirement:
        require_contains(firmware_docs, custom_manifest_requirement, "docs/firmware-update.md", errors)
        require_contains(web_template, custom_manifest_requirement, rel(WEB_TEMPLATE), errors)
        require_contains(firmware_yaml, "is_valid_http_url(url)", "common/addon/firmware_update.yaml", errors)
        require_contains(firmware_yaml, "strip_trailing_slashes", "common/addon/firmware_update.yaml", errors)
    if isinstance(manifest_url_length_limit, int) and not isinstance(manifest_url_length_limit, bool):
        if firmware_yaml.count(f"max_length: {manifest_url_length_limit}") < 2:
            errors.append(
                "common/addon/firmware_update.yaml must use project.firmware_manifest_url_length_limit for both manifest URL text fields"
            )
        require_contains(web_template, f"MAX_FIRMWARE_URL_LENGTH = {manifest_url_length_limit}", rel(WEB_TEMPLATE), errors)
        require_contains(web_text, f"MAX_FIRMWARE_URL_LENGTH = {manifest_url_length_limit}", rel(WEB_APP), errors)
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
        "id(auto_update_switch).state && !id(manual_check_only)",
        "id(beta_channel_switch).state",
        "update_interval: never",
    ):
        require_contains(firmware_yaml, needle, "common/addon/firmware_update.yaml", errors)
    for needle in (
        "Auto updates",
        "Disabled",
        'productSettingOptions("update_frequency")',
        "Check for Update",
        "Install",
        'post(endpoints.update + "/install")',
        'post(endpoints.update_beta + "/install")',
        "Stable Manifest URL",
        "Beta Manifest URL",
    ):
        require_contains(web_template, needle, rel(WEB_TEMPLATE), errors)
    for label, url in default_urls.items():
        require_contains(firmware_docs, url, "docs/firmware-update.md", errors)
        require_contains(backup_docs, url, "docs/backup.md", errors)
        require_contains(firmware_yaml, url, "common/addon/firmware_update.yaml", errors)
        require_contains(web_text, url, rel(WEB_APP), errors)
        require_contains(web_template, f"FIRMWARE_MANIFEST_URLS.{label}", rel(WEB_TEMPLATE), errors)


def check_ota_update_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    platform = str(project.get("ota_update_platform", "")).strip()
    pre_update_action = str(project.get("ota_pre_update_action", "")).strip()
    transition = str(project.get("ota_pre_update_transition", "")).strip()
    delay = str(project.get("ota_pre_update_delay", "")).strip()

    firmware_docs = read(ROOT / "docs" / "firmware-update.md", errors)
    device_yaml_path = "devices/guition-esp32-p4-jc8012p4a1/device/device.yaml"
    device_yaml = read(ROOT / device_yaml_path, errors)

    if pre_update_action:
        require_contains(firmware_docs, pre_update_action, "docs/firmware-update.md", errors)
    if transition:
        require_contains(firmware_docs, transition, "docs/firmware-update.md", errors)
        require_contains(device_yaml, f"transition_length: {transition}", device_yaml_path, errors)
    if delay:
        require_contains(firmware_docs, delay, "docs/firmware-update.md", errors)
        require_contains(device_yaml, f"delay: {delay}", device_yaml_path, errors)
    if platform:
        require_contains(device_yaml, f"platform: {platform}", device_yaml_path, errors)
    for needle in (
        "ota:",
        "on_begin:",
        "light.turn_off:",
        "id: backlight",
    ):
        require_contains(device_yaml, needle, device_yaml_path, errors)


def check_privacy_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    connection_model = str(project.get("privacy_connection_model", "")).strip()
    network_scope = str(project.get("privacy_network_scope", "")).strip()
    no_cloud_service = str(project.get("privacy_no_cloud_service", "")).strip()
    no_extra_account = str(project.get("privacy_no_extra_account", "")).strip()
    no_uploads = str(project.get("privacy_no_uploads", "")).strip()
    no_hosted_service = str(project.get("privacy_no_hosted_service", "")).strip()

    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    immich_photo_frame_docs = read(ROOT / "docs" / "immich-photo-frame.md", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)

    if connection_model:
        require_contains(readme, connection_model, "README.md", errors)
        require_contains(immich_photo_frame_docs, connection_model, "docs/immich-photo-frame.md", errors)
    if network_scope:
        require_contains(readme, network_scope, "README.md", errors)
        require_contains(immich_photo_frame_docs, network_scope, "docs/immich-photo-frame.md", errors)
    if no_cloud_service:
        require_contains(readme, no_cloud_service, "README.md", errors)
    if no_extra_account:
        require_contains(readme, no_extra_account, "README.md", errors)
    if no_uploads:
        require_contains(immich_photo_frame_docs, no_uploads, "docs/immich-photo-frame.md", errors)
    if no_hosted_service:
        require_contains(immich_photo_frame_docs, no_hosted_service, "docs/immich-photo-frame.md", errors)
    for needle in ("No hub, cloud, or extra software required", "self-hosted Immich"):
        require_contains(ai_txt, needle, "docs/public/ai.txt", errors)
    for needle in ("cloud account", "separate bridge service"):
        require_contains(index_docs, needle, "docs/index.md", errors)


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
        'post(endpoints.screen_rotation + "/set"',
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
        "post(endpoints.developer_features_enabled",
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
        if filter_yaml.count(f"max_length: {id_limit}") < 4:
            errors.append("common/addon/immich_filter.yaml must use project.photo_source_id_limit for album/person ID and label fields")

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
        '"on this day" memories',
        "chosen date range",
    ):
        require_contains(readme, needle, "README.md", errors)
    for needle in (
        "Photo Sources",
        "favorites only",
        "specific albums",
        "specific people",
        '"on this day" memories',
        "date range",
    ):
        require_contains(index_docs, needle, "docs/index.md", errors)
    for needle in ("Album IDs", "Album Labels", "Person IDs", "Person Labels"):
        require_contains(backup_docs, needle, "docs/backup.md", errors)

    for needle in (
        "Photos: Source",
        'initial_option: "All Photos"',
        "auto_apply_photo_source",
        "delay: 1s",
        "Photos: Album IDs",
        "Photos: Person IDs",
        "Apply Photo Source",
    ):
        require_contains(filter_yaml, needle, "common/addon/immich_filter.yaml", errors)
    for name in ("Photos: Album IDs", "Photos: Album Labels", "Photos: Person IDs", "Photos: Person Labels"):
        require_firmware_text_entity_shape(filter_yaml, name, "common/addon/immich_filter.yaml", errors)
    for needle in (
        "immich_memory_window_offset",
        "id(immich_memory_window_offset) = -2",
        "id(immich_memory_window_offset) <= 2",
        "build_immich_search_body",
        "pick_one_uuid_from_csv",
        "pick_one_person_id_for_random_search",
    ):
        require_contains(api_yaml, needle, "common/addon/immich_api.yaml", errors)
    require_contains(api_key_docs, "memory.read", "docs/api-key.md", errors)
    for needle in (
        "MAX_PHOTO_ID_FIELD_LENGTH",
        "schedulePhotoSourceApply",
        "Add an album",
        "Add a person",
        "Paste album ID from Immich URL",
        "Paste person ID from Immich URL",
        "Album IDs exceed 255 characters",
        "Person IDs exceed 255 characters",
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
        "productSettingOptions(\"conn_timeout\")",
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


def check_public_manifest_urls(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    if not base_url.startswith("https://"):
        errors.append("project.public_base_url must be an https URL")

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        urls = urls_by_slug.get(slug, {})
        for label, field in (("stable", "public_manifest"), ("beta", "public_beta_manifest")):
            path = str(device.get(field, "")).strip()
            if not path or path.startswith("/") or ".." in Path(path).parts:
                errors.append(f"Device {slug} has invalid {field}: {path}")
            url = urls.get(label, "")
            if not url.startswith(f"{base_url}/"):
                errors.append(f"Device {slug} {field} URL must be under project.public_base_url")

    default_urls = default_public_manifest_urls(product)
    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    backup_docs = read(ROOT / "docs" / "backup.md", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    for label, url in default_urls.items():
        if not url.startswith("https://"):
            errors.append(f"Default {label} firmware manifest URL must be an https URL")
        for filename, text in (
            ("common/addon/firmware_update.yaml", firmware_update),
            ("docs/backup.md", backup_docs),
        ):
            require_contains(text, url, filename, errors)
    require_contains(docs_workflow, "PUBLIC_BASE_URL", ".github/workflows/docs.yml", errors)
    require_contains(docs_workflow, '--base-url "$PUBLIC_BASE_URL"', ".github/workflows/docs.yml", errors)

    urls_by_slug = device_public_manifest_urls(product)
    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        if not package_yaml:
            continue
        package_text = read(ROOT / package_yaml, errors)
        for url in urls_by_slug.get(slug, {}).values():
            require_contains(package_text, url, package_yaml, errors)


def check_public_site_references(product: dict, errors: list[str]) -> None:
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    install_url = public_url("install", product)
    web_app_path = str(product["project"].get("web_server_public_app_path", "")).strip()
    web_app_url = public_url(web_app_path, product) if web_app_path else ""
    project_name = str(product["project"].get("name", "")).strip()
    social_image_alt = str(product["project"].get("social_image_alt", "")).strip()
    usb_flashing_image = str(product["project"].get("usb_flashing_image", "")).strip()
    usb_flashing_image_alt = str(product["project"].get("usb_flashing_image_alt", "")).strip()
    web_installer_required_browsers = product["project"].get("web_installer_required_browsers", [])
    web_installer_required_api = str(product["project"].get("web_installer_required_api", "")).strip()
    web_installer_unsupported_browsers = product["project"].get("web_installer_unsupported_browsers", [])
    repository_url = str(product["project"].get("repository_url", "")).strip().rstrip("/")
    default_branch = str(product["project"].get("github_default_branch", "")).strip()
    support_url = str(product["project"].get("support_url", "")).strip()
    support_button_image_url = str(product["project"].get("support_button_image_url", "")).strip()
    esphome_config_mount = str(product["project"].get("esphome_config_mount", "")).strip()

    robots = read(ROOT / "docs" / "public" / "robots.txt", errors)
    ai_txt = read(ROOT / "docs" / "public" / "ai.txt", errors)
    readme = read(ROOT / "README.md", errors)
    index_docs = read(ROOT / "docs" / "index.md", errors)
    install_docs = read(ROOT / "docs" / "install.md", errors)
    manual_setup = read(ROOT / "docs" / "manual-setup.md", errors)
    license_docs = read(ROOT / "docs" / "license.md", errors)
    roadmap = read(ROOT / "docs" / "roadmap.md", errors)
    troubleshooting_docs = read(ROOT / "docs" / "troubleshooting.md", errors)
    usb_flashing_docs = read(ROOT / "docs" / "usb-flashing.md", errors)
    release_changelog = read(ROOT / "scripts" / "release_changelog.py", errors)

    require_contains(robots, f"Sitemap: {public_url('sitemap.xml', product)}", "docs/public/robots.txt", errors)

    if project_name:
        require_contains(ai_txt, f"name: {project_name}", "docs/public/ai.txt", errors)
        require_contains(ai_txt, f'attribute to "{project_name}"', "docs/public/ai.txt", errors)
    if repository_url:
        require_contains(ai_txt, f"Source and issues: {repository_url}", "docs/public/ai.txt", errors)
        require_contains(manual_setup, f"url: {repository_url}", "docs/manual-setup.md", errors)
        if default_branch:
            require_contains(license_docs, f"({repository_url}/blob/{default_branch}/LICENSE)", "docs/license.md", errors)
        require_contains(roadmap, f"({repository_url}/issues)", "docs/roadmap.md", errors)
        require_contains(release_changelog, 'project_value("repository_url"', "scripts/release_changelog.py", errors)
    require_contains(ai_txt, f"url: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Prefer canonical URLs: {docs_url} and {install_url}", "docs/public/ai.txt", errors)
    require_contains(ai_txt, f"Documentation: {docs_url}", "docs/public/ai.txt", errors)
    require_contains(readme, f"]({install_url})", "README.md installer link", errors)
    require_contains(readme, f"]({docs_url})", "README.md docs link", errors)
    require_contains(readme, base_url, "README.md public base URL", errors)
    for label, text in (("README.md", readme), ("docs/index.md", index_docs)):
        if support_url:
            require_contains(text, support_url, label, errors)
        if support_button_image_url:
            require_contains(text, support_button_image_url, label, errors)
    if social_image_alt:
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, f'alt="{social_image_alt}"', label, errors)
    if usb_flashing_image:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'src="/{usb_flashing_image}"', label, errors)
    if usb_flashing_image_alt:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            require_contains(text, f'alt="{usb_flashing_image_alt}"', label, errors)
    if isinstance(web_installer_required_browsers, list):
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            for browser in web_installer_required_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)
    web_installer_computer_requirement = str(product["project"].get("web_installer_computer_requirement", "")).strip()
    if web_installer_computer_requirement:
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_computer_requirement, label, errors)
    if web_installer_required_api:
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, web_installer_required_api, label, errors)
    if isinstance(web_installer_unsupported_browsers, list):
        for label, text in (
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
        ):
            for browser in web_installer_unsupported_browsers:
                if isinstance(browser, str) and browser.strip():
                    require_contains(text, browser.strip(), label, errors)
    usb_cable_requirement = str(product["project"].get("usb_cable_requirement", "")).strip()
    usb_cable_warning = str(product["project"].get("usb_cable_warning", "")).strip()
    for field_name, value in (
        ("usb_cable_requirement", usb_cable_requirement),
        ("usb_cable_warning", usb_cable_warning),
    ):
        if not value:
            continue
        for label, text in (
            ("README.md", readme),
            ("docs/install.md", install_docs),
            ("docs/usb-flashing.md", usb_flashing_docs),
            ("docs/troubleshooting.md", troubleshooting_docs),
            ("docs/immich-photo-frame.md", read(ROOT / "docs" / "immich-photo-frame.md", errors)),
        ):
            require_contains(text, value, f"{label} {field_name}", errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        esphome_name = str(device.get("esphome_name", "")).strip()
        friendly_name = str(device.get("friendly_name", "")).strip()
        model = str(device.get("model", "")).strip()
        model_code = model.split()[-1] if model else ""
        panel_url = str(device.get("panel_url", "")).strip()
        stand_url = str(device.get("stand_url", "")).strip()
        local_yaml = check_relative_path(device.get("local_yaml"), f"Device {slug} local_yaml", errors)
        package_yaml = check_relative_path(device.get("package_yaml"), f"Device {slug} package_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        for label, text in (
            ("README.md", readme),
            ("docs/index.md", index_docs),
            ("docs/install.md", install_docs),
        ):
            if model_code:
                require_contains(text, model_code, label, errors)
            if panel_url:
                require_contains(text, panel_url, label, errors)
            if stand_url:
                require_contains(text, stand_url, label, errors)
        if esphome_name:
            for label, text in (
                ("docs/install.md", install_docs),
                ("docs/troubleshooting.md", troubleshooting_docs),
                ("docs/usb-flashing.md", usb_flashing_docs),
            ):
                require_contains(text, esphome_name, label, errors)
        if package_yaml:
            require_contains(manual_setup, f"files: [{package_yaml}]", "docs/manual-setup.md", errors)
        if build_yaml:
            if esphome_config_mount:
                require_contains(readme, f"compile {esphome_config_mount}/{build_yaml}", "README.md compile example", errors)
            build_text = read(ROOT / build_yaml, errors)
            if esphome_name:
                require_contains(build_text, f'name: "{esphome_name}"', build_yaml, errors)
            if friendly_name:
                require_contains(build_text, f'friendly_name: "{friendly_name}"', build_yaml, errors)
        if local_yaml and repository_url:
            local_text = read(ROOT / local_yaml, errors)
            if esphome_name:
                require_contains(local_text, f'name: "{esphome_name}"', local_yaml, errors)
            if friendly_name:
                require_contains(local_text, f'friendly_name: "{friendly_name}"', local_yaml, errors)
            require_contains(local_text, f"url: {repository_url}", local_yaml, errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, f'js_url: "{web_app_url}"', device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)


def check_docs_site_config(product: dict, errors: list[str]) -> None:
    config = read(ROOT / "docs" / ".vitepress" / "config.mts", errors)
    project = product["project"]
    project_name = str(project.get("name", "")).strip()
    site_description = str(project.get("site_description", "")).strip()
    ai_description = str(project.get("ai_description", "")).strip()
    social_image = str(project.get("social_image", "")).strip()
    social_image_alt = str(project.get("social_image_alt", "")).strip()
    favicon = str(project.get("favicon", "")).strip()
    base_url = public_base_url(product)
    docs_url = public_url("", product)
    base_path = f"/{base_url.rstrip('/').rsplit('/', 1)[-1]}/"
    repository_url = str(project.get("repository_url", "")).strip().rstrip("/")
    default_branch = str(project.get("github_default_branch", "")).strip()
    owner_name = str(project.get("owner_name", "")).strip()
    owner_url = str(project.get("owner_url", "")).strip()

    if project_name:
        require_contains(config, f"title: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"content: '{project_name}'", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"name: '{project_name}'", "docs/.vitepress/config.mts", errors)
    if site_description:
        require_contains(config, f"description: '{site_description}'", "docs/.vitepress/config.mts", errors)
    if social_image:
        require_contains(config, f"content: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
        require_contains(config, f"image: `${{hostname}}{social_image}`", "docs/.vitepress/config.mts", errors)
    if social_image_alt:
        require_contains(config, f"content: '{social_image_alt}'", "docs/.vitepress/config.mts", errors)
    if favicon:
        require_contains(config, f"href: '{base_path}{favicon}'", "docs/.vitepress/config.mts", errors)
    if ai_description:
        require_contains(read(ROOT / "docs" / "public" / "ai.txt", errors), f"description: {ai_description}", "docs/public/ai.txt", errors)
    require_contains(config, f"const hostname = '{docs_url}'", "docs/.vitepress/config.mts", errors)
    require_contains(config, f"base: '{base_path}'", "docs/.vitepress/config.mts", errors)
    if repository_url:
        require_contains(config, f"link: '{repository_url}'", "docs/.vitepress/config.mts", errors)
        if default_branch:
            require_contains(config, f"pattern: '{repository_url}/edit/{default_branch}/docs/:path'", "docs/.vitepress/config.mts", errors)
    if owner_name:
        require_contains(config, f"name: '{owner_name}'", "docs/.vitepress/config.mts", errors)
    if owner_url:
        require_contains(config, f"url: '{owner_url}'", "docs/.vitepress/config.mts", errors)



def check_generated_asset_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    outputs = [str(value).strip() for value in project.get("generated_asset_outputs", []) if str(value).strip()]
    sources = [str(value).strip() for value in project.get("generated_asset_sources", []) if str(value).strip()]
    placeholders = [str(value).strip() for value in project.get("web_template_placeholders", []) if str(value).strip()]

    generator = read(ROOT / "scripts" / "generate_assets.py", errors)
    web_template = read_web_source(errors)
    package_json = read(ROOT / "package.json", errors)

    expected_outputs = {
        "components/espframe/tz_data_generated.h",
        "common/addon/time.yaml",
        "docs/public/webserver/app.js",
        "docs/public/webserver/style.css",
    }
    expected_sources = {
        "components/espframe/timezones.py",
        "docs/webserver/src/app.template.js",
        "docs/webserver/src/app_shell.js",
        "docs/webserver/src/backup_import.js",
        "docs/webserver/src/compat.js",
        "docs/webserver/src/endpoints.js",
        "docs/webserver/src/live_helpers.js",
        "docs/webserver/src/runtime_state.js",
        "docs/webserver/src/settings_controls.js",
        "docs/webserver/src/startup_wizard.js",
        "docs/webserver/src/style.css",
        "product/espframe.json",
        "scripts/product_config.py",
    }
    missing_outputs = sorted(expected_outputs - set(outputs))
    missing_sources = sorted(expected_sources - set(sources))
    overlapping_paths = sorted(set(outputs).intersection(sources))
    if missing_outputs:
        errors.append("project.generated_asset_outputs is missing paths: " + ", ".join(missing_outputs))
    if missing_sources:
        errors.append("project.generated_asset_sources is missing paths: " + ", ".join(missing_sources))
    if overlapping_paths:
        errors.append("Generated asset paths must not be listed as both sources and outputs: " + ", ".join(overlapping_paths))

    for filename in outputs + sources:
        path = check_relative_path(filename, f"Generated asset path {filename}", errors)
        if path:
            read(ROOT / path, errors)
            path_name = Path(path).name
            if path_name in {"espframe.json", "product_config.py"}:
                require_contains(generator, "load_product", "scripts/generate_assets.py", errors)
            else:
                require_contains(generator, path_name, "scripts/generate_assets.py", errors)
    template_placeholders = set(re.findall(r"__ESPFRAME_[A-Z0-9_]+__", web_template))
    configured_placeholders = set(placeholders)
    missing_placeholders = sorted(template_placeholders - configured_placeholders)
    extra_placeholders = sorted(configured_placeholders - template_placeholders)
    if missing_placeholders:
        errors.append(
            "project.web_template_placeholders is missing template placeholders: "
            + ", ".join(missing_placeholders)
        )
    if extra_placeholders:
        errors.append(
            "project.web_template_placeholders lists placeholders not used by the web template: "
            + ", ".join(extra_placeholders)
        )
    for placeholder in placeholders:
        require_contains(web_template, placeholder, rel(WEB_TEMPLATE), errors)
        require_contains(generator, placeholder, "scripts/generate_assets.py", errors)
    for needle in (
        "python3 scripts/generate_assets.py",
        "python3 scripts/generate_assets.py --check",
        "check:generated",
        "webserver:build",
    ):
        require_contains(package_json, needle, "package.json", errors)
    for needle in (
        "write_or_check",
        "replace_timezone_yaml",
        "web_app_bundle",
        "render_settings_table",
    ):
        require_contains(generator, needle, "scripts/generate_assets.py", errors)


def check_factory_firmware_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    purpose = str(project.get("factory_firmware_purpose", "")).strip()
    secret_policy = str(project.get("factory_firmware_secret_policy", "")).strip()
    network_mode = str(project.get("factory_firmware_network_mode", "")).strip()
    setup_method = str(project.get("factory_firmware_setup_method", "")).strip()
    local_use = str(project.get("factory_firmware_local_use", "")).strip()
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).strip()
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()

    install_docs = read(ROOT / "docs" / "install.md", errors)
    connectivity_yaml = read(ROOT / "common" / "addon" / "connectivity.yaml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if not build_yaml:
            continue
        build_text = read(ROOT / build_yaml, errors)
        for value in (purpose, secret_policy, network_mode, setup_method, local_use):
            if value:
                require_contains(build_text, value, build_yaml, errors)
        if firmware_version_placeholder:
            require_contains(build_text, firmware_version_placeholder, build_yaml, errors)
        if factory_css_include:
            require_contains(build_text, f'css_include: "{factory_css_include}"', build_yaml, errors)
        if factory_js_include:
            require_contains(build_text, f'js_include: "{factory_js_include}"', build_yaml, errors)
        if esphome_config_mount:
            require_contains(
                compile_workflow,
                f"compile {esphome_config_mount}/{build_yaml}",
                ".github/workflows/compile.yml",
                errors,
            )
            require_contains(
                release_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/release.yml",
                errors,
            )

    if network_mode:
        require_contains(install_docs, "hotspot", "docs/install.md", errors)
        require_contains(connectivity_yaml, 'ssid: "${name}"', "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "wifi:", "common/addon/connectivity.yaml", errors)
        require_contains(connectivity_yaml, "ap:", "common/addon/connectivity.yaml", errors)
    if setup_method:
        require_contains(connectivity_yaml, "captive_portal:", "common/addon/connectivity.yaml", errors)
        require_contains(install_docs, setup_method.replace("_", " "), "docs/install.md", errors)


def check_web_server_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    port = project.get("web_server_port")
    version = project.get("web_server_version")
    include_internal = project.get("web_server_include_internal")
    public_app_path = str(project.get("web_server_public_app_path", "")).strip()
    factory_js_url = str(project.get("web_server_factory_js_url", ""))
    factory_css_include = str(project.get("web_server_factory_css_include", "")).strip()
    factory_js_include = str(project.get("web_server_factory_js_include", "")).strip()
    sorting_groups = project.get("web_server_sorting_groups", [])
    group_ids = {
        str(group.get("id", "")).strip()
        for group in sorting_groups
        if isinstance(group, dict) and str(group.get("id", "")).strip()
    }

    if public_app_path and (public_app_path.startswith("/") or ".." in Path(public_app_path).parts):
        errors.append("project.web_server_public_app_path must be a relative public asset path")
    public_app_url = public_url(public_app_path, product) if public_app_path else ""

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(device_text, f"  port: {port}", device_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(device_text, f"  version: {version}", device_yaml, errors)
            if isinstance(include_internal, bool):
                require_contains(device_text, f"  include_internal: {str(include_internal).lower()}", device_yaml, errors)
            if public_app_url:
                require_contains(device_text, f'  js_url: "{public_app_url}"', device_yaml, errors)
            for group in sorting_groups if isinstance(sorting_groups, list) else []:
                if not isinstance(group, dict):
                    continue
                group_id = str(group.get("id", "")).strip()
                name = str(group.get("name", "")).strip()
                weight = group.get("sorting_weight")
                if group_id:
                    require_contains(device_text, f"    - id: {group_id}", device_yaml, errors)
                if name:
                    require_contains(device_text, f'      name: "{name}"', device_yaml, errors)
                if isinstance(weight, int) and not isinstance(weight, bool):
                    require_contains(device_text, f"      sorting_weight: {weight}", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            if isinstance(port, int) and not isinstance(port, bool):
                require_contains(build_text, f"  port: {port}", build_yaml, errors)
            if isinstance(version, int) and not isinstance(version, bool):
                require_contains(build_text, f"  version: {version}", build_yaml, errors)
            require_contains(build_text, f'  js_url: "{factory_js_url}"', build_yaml, errors)
            if factory_css_include:
                require_contains(build_text, f'  css_include: "{factory_css_include}"', build_yaml, errors)
            if factory_js_include:
                require_contains(build_text, f'  js_include: "{factory_js_include}"', build_yaml, errors)

    if group_ids:
        for yaml_path in list((ROOT / "common").rglob("*.yaml")) + list((ROOT / "devices").rglob("*.yaml")):
            text = read(yaml_path, errors)
            for group_id in re.findall(r"sorting_group_id:\s*([A-Za-z0-9_-]+)", text):
                if group_id not in group_ids:
                    errors.append(f"{rel(yaml_path)} references unknown web_server sorting group {group_id}")

    for include_path in (factory_css_include, factory_js_include):
        if not include_path:
            continue
        normalized = include_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_file():
            errors.append(f"project web server include is missing: {normalized}")


def check_external_components_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    component_names = [
        str(value).strip() for value in project.get("external_component_names", []) if str(value).strip()
    ]
    git_source_type = str(project.get("external_component_git_source_type", "")).strip()
    local_source_type = str(project.get("external_component_local_source_type", "")).strip()
    git_path = str(project.get("external_component_git_path", "")).strip()
    local_path = str(project.get("external_component_local_path", "")).strip()
    component_ref = str(project.get("external_component_ref", "")).strip()
    components_inline = f"components: [{', '.join(component_names)}]" if component_names else ""
    repository_url = str(project.get("repository_url", "")).strip()

    for component in component_names:
        component_dir = ROOT / "components" / component
        if not component_dir.is_dir():
            errors.append(f"Missing external component directory: {rel(component_dir)}")
        init_file = component_dir / "__init__.py"
        if not init_file.is_file():
            errors.append(f"Missing external component entrypoint: {rel(init_file)}")

    for device in product["devices"]:
        slug = str(device.get("slug", "")).strip()
        device_yaml = check_relative_path(device.get("device_yaml"), f"Device {slug} device_yaml", errors)
        build_yaml = check_relative_path(device.get("build_yaml"), f"Device {slug} build_yaml", errors)
        if device_yaml:
            device_text = read(ROOT / device_yaml, errors)
            require_contains(device_text, "external_components:", device_yaml, errors)
            if git_source_type:
                require_contains(device_text, f"      type: {git_source_type}", device_yaml, errors)
            if repository_url:
                require_contains(device_text, f'espframe_component_url: "{repository_url}"', device_yaml, errors)
                require_contains(device_text, "      url: ${espframe_component_url}", device_yaml, errors)
            if component_ref:
                require_contains(device_text, f'espframe_component_ref: "{component_ref}"', device_yaml, errors)
                require_contains(device_text, "      ref: ${espframe_component_ref}", device_yaml, errors)
            if git_path:
                require_contains(device_text, f"      path: {git_path}", device_yaml, errors)
            if components_inline:
                require_contains(device_text, f"    {components_inline}", device_yaml, errors)
            require_contains(device_text, "    refresh: 0s", device_yaml, errors)
            require_contains(device_text, "espframe:", device_yaml, errors)
            require_contains(device_text, "  id: espframe_core", device_yaml, errors)
        if build_yaml:
            build_text = read(ROOT / build_yaml, errors)
            require_contains(build_text, "external_components:", build_yaml, errors)
            if local_source_type:
                require_contains(build_text, f"      type: {local_source_type}", build_yaml, errors)
            if local_path:
                require_contains(build_text, f"      path: {local_path}", build_yaml, errors)
            if components_inline:
                require_contains(build_text, f"    {components_inline}", build_yaml, errors)

    if local_path:
        normalized = local_path
        while normalized.startswith("../"):
            normalized = normalized[3:]
        if not (ROOT / normalized).is_dir():
            errors.append(f"project.external_component_local_path is missing: {normalized}")
    if git_path and not (ROOT / git_path).is_dir():
        errors.append(f"project.external_component_git_path is missing: {git_path}")


def main() -> int:
    errors: list[str] = []
    product = load_product()
    check_project_metadata(product, errors)
    check_npm_package_metadata(product, errors)
    check_license_metadata(product, errors)
    check_immich_api_key_metadata(product, errors)
    check_immich_connection_metadata(product, errors)
    check_home_assistant_metadata(product, errors)
    check_device_logging_metadata(product, errors)
    check_firmware_update_metadata(product, errors)
    check_ota_update_metadata(product, errors)
    check_backup_metadata(product, errors)
    check_privacy_metadata(product, errors)
    check_touch_controls_metadata(product, errors)
    check_screen_schedule_metadata(product, errors)
    check_screen_rotation_metadata(product, errors)
    check_developer_features_metadata(product, errors)
    check_screen_tone_metadata(product, errors)
    check_clock_time_metadata(product, errors)
    check_photo_source_metadata(product, errors)
    check_connection_resilience_metadata(product, errors)
    check_setup_flow_metadata(product, errors)
    check_photo_display_metadata(product, errors)
    check_devices(product, errors)
    check_public_manifest_urls(product, errors)
    check_public_site_references(product, errors)
    check_docs_site_config(product, errors)
    check_device_workflow_contract(product, errors)
    check_generated_asset_metadata(product, errors)
    check_factory_firmware_metadata(product, errors)
    check_web_server_metadata(product, errors)
    check_external_components_metadata(product, errors)
    check_esphome_version(product, errors)
    check_node_version(product, errors)
    check_workflows(product, errors)
    check_settings(product, errors)

    if errors:
        for error in errors:
            print(f"product contract error: {error}", file=sys.stderr)
        return 1

    print("product contract validation passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
