from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import (
    ROOT,
    check_relative_path,
    read,
    rel,
    require_contains,
)

def check_project_required_metadata(product: dict, errors: list[str]) -> None:
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


def check_project_url_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
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


def check_project_package_firmware_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    package_name = str(project.get("package_name", "")).strip()
    firmware_update = read(ROOT / "common" / "addon" / "firmware_update.yaml", errors)
    if package_name:
        require_contains(firmware_update, f"name: {package_name}", "common/addon/firmware_update.yaml", errors)
