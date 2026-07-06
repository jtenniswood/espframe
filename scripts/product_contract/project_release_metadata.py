from __future__ import annotations

import re

from product_contract.common import ROOT, check_relative_path, read


def workflow_event_index(workflow_events: object) -> set[str]:
    if not isinstance(workflow_events, dict):
        return set()

    configured_workflow_events: set[str] = set()
    for raw_workflow, raw_events in workflow_events.items():
        workflow = str(raw_workflow).strip()
        if not workflow or not isinstance(raw_events, list):
            continue
        events = {str(raw_event).strip() for raw_event in raw_events if str(raw_event).strip()}
        configured_workflow_events.update(f"{workflow}.{event}" for event in events)
    return configured_workflow_events


def check_workflow_event_types(
    workflow_event_types: object,
    configured_workflow_events: set[str],
    errors: list[str],
) -> None:
    if not isinstance(workflow_event_types, dict) or not workflow_event_types:
        errors.append("project.github_workflow_event_types must be a non-empty object")
        return

    for raw_key, raw_types in workflow_event_types.items():
        key = str(raw_key).strip()
        label = f"project.github_workflow_event_types.{key or '<missing>'}"
        workflow, _, event_name = key.partition(".")
        if not workflow or not event_name:
            errors.append(f"{label} must use workflow.event format")
        elif key not in configured_workflow_events:
            errors.append(f"{label} must point at a known workflow event")
        if not isinstance(raw_types, list) or not raw_types:
            errors.append(f"{label} must be a non-empty list")
            continue
        event_types = [str(event_type).strip() for event_type in raw_types]
        if any(not event_type for event_type in event_types):
            errors.append(f"{label} must only contain non-empty strings")
        if len(event_types) != len(set(event_types)):
            errors.append(f"{label} must not contain duplicate event types")


def workflow_job_index(workflow_jobs: object) -> tuple[set[str], dict[str, set[str]]]:
    if not isinstance(workflow_jobs, dict):
        return set(), {}

    configured_workflow_jobs: set[str] = set()
    jobs_by_workflow: dict[str, set[str]] = {}
    for raw_workflow, raw_jobs in workflow_jobs.items():
        workflow = str(raw_workflow).strip()
        if not workflow or not isinstance(raw_jobs, dict):
            continue
        job_ids = {str(raw_job_id).strip() for raw_job_id in raw_jobs if str(raw_job_id).strip()}
        if not job_ids:
            continue
        configured_workflow_jobs.update(f"{workflow}.{job_id}" for job_id in job_ids)
        jobs_by_workflow[workflow] = job_ids
    return configured_workflow_jobs, jobs_by_workflow


def check_workflow_job_dependencies(
    workflow_job_dependencies: object,
    configured_workflow_jobs: set[str],
    jobs_by_workflow: dict[str, set[str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_job_dependencies, dict) or not workflow_job_dependencies:
        errors.append("project.github_workflow_job_dependencies must be a non-empty object")
        return

    for raw_key, raw_dependencies in workflow_job_dependencies.items():
        key = str(raw_key).strip()
        label = f"project.github_workflow_job_dependencies.{key or '<missing>'}"
        workflow, _, job_id = key.partition(".")
        if not workflow or not job_id:
            errors.append(f"{label} must use workflow.job format")
        elif key not in configured_workflow_jobs:
            errors.append(f"{label} must point at a known workflow job")
        if not isinstance(raw_dependencies, list) or not raw_dependencies:
            errors.append(f"{label} must be a non-empty list")
            continue
        dependencies = [str(dependency).strip() for dependency in raw_dependencies]
        if any(not dependency for dependency in dependencies):
            errors.append(f"{label} must only contain non-empty strings")
        if len(dependencies) != len(set(dependencies)):
            errors.append(f"{label} must not contain duplicate jobs")

        known_jobs = jobs_by_workflow.get(workflow)
        if known_jobs is None:
            continue
        unknown_dependencies = sorted({dependency for dependency in dependencies if dependency} - known_jobs)
        for dependency in unknown_dependencies:
            errors.append(f"{label} references unknown job: {dependency}")


def check_project_release_metadata(product: dict, errors: list[str]) -> None:
    project = product["project"]
    default_branch = str(project.get("github_default_branch", "")).strip()
    for field in ("compile_firmware_artifact_prefix", "compile_firmware_output_dir", "compile_firmware_version_prefix"):
        if not str(project.get(field, "")).strip():
            errors.append(f"project.{field} is required")
    pull_request_template = check_relative_path(
        project.get("github_pull_request_template_path"),
        "project.github_pull_request_template_path",
        errors,
    )
    if pull_request_template:
        read(ROOT / pull_request_template, errors)
    device_testing_options = project.get("github_pull_request_device_testing_options", [])
    if not isinstance(device_testing_options, list) or not device_testing_options:
        errors.append("project.github_pull_request_device_testing_options must be a non-empty list")
    else:
        options = [str(option).strip() for option in device_testing_options]
        if any(not option for option in options):
            errors.append("project.github_pull_request_device_testing_options must only contain non-empty strings")
        if len(options) != len(set(options)):
            errors.append("project.github_pull_request_device_testing_options must not contain duplicate options")
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
    configured_workflow_events = workflow_event_index(workflow_events)
    workflow_event_types = project.get("github_workflow_event_types", {})
    check_workflow_event_types(workflow_event_types, configured_workflow_events, errors)
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
    configured_workflow_jobs, jobs_by_workflow = workflow_job_index(workflow_jobs)
    workflow_job_dependencies = project.get("github_workflow_job_dependencies", {})
    check_workflow_job_dependencies(workflow_job_dependencies, configured_workflow_jobs, jobs_by_workflow, errors)
    workflow_job_conditions = project.get("github_workflow_job_conditions", {})
    if not isinstance(workflow_job_conditions, dict) or not workflow_job_conditions:
        errors.append("project.github_workflow_job_conditions must be a non-empty object")
    else:
        configured_condition_jobs = {str(key).strip() for key in workflow_job_conditions}
        missing_condition_jobs = sorted(configured_workflow_jobs - configured_condition_jobs)
        extra_condition_jobs = sorted(configured_condition_jobs - configured_workflow_jobs)
        if missing_condition_jobs:
            errors.append(
                "project.github_workflow_job_conditions is missing jobs: " + ", ".join(missing_condition_jobs)
            )
        if extra_condition_jobs:
            errors.append(
                "project.github_workflow_job_conditions contains unknown jobs: " + ", ".join(extra_condition_jobs)
            )
        for raw_key, raw_condition in workflow_job_conditions.items():
            key = str(raw_key).strip()
            workflow, _, job_id = key.partition(".")
            if not workflow or not job_id:
                errors.append(f"project.github_workflow_job_conditions.{key or '<missing>'} must use workflow.job format")
            if raw_condition is not None and (not isinstance(raw_condition, str) or not raw_condition.strip()):
                errors.append(f"project.github_workflow_job_conditions.{key or '<missing>'} must be a non-empty string or null")
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
