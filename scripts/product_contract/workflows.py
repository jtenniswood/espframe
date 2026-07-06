"""Validate GitHub workflow and release contract metadata."""

from __future__ import annotations

import re
from pathlib import Path

from product_contract.common import ROOT, check_relative_path, read, rel, require_contains
from product_config import public_base_url, release_matrix_devices


WORKFLOW_ACTION_TARGETS = {
    "cache": (".github/workflows/release.yml", ".github/workflows/compile.yml"),
    "checkout": (".github/workflows/release.yml", ".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "deploy_pages": (".github/workflows/docs.yml",),
    "download_artifact": (".github/workflows/release.yml", ".github/workflows/docs.yml"),
    "setup_node": (".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "upload_artifact": (".github/workflows/release.yml", ".github/workflows/docs.yml", ".github/workflows/compile.yml"),
    "upload_pages_artifact": (".github/workflows/docs.yml",),
}


WORKFLOW_PATH_FILTER_TARGETS = {
    "compile_pull_request": (".github/workflows/compile.yml", "pull_request"),
    "docs_push": (".github/workflows/docs.yml", "push"),
}


WORKFLOW_SPARSE_CHECKOUT_TARGETS = {
    "compile": ("metadata",),
    "docs": ("release",),
    "release": ("metadata", "release"),
}


WORKFLOW_FIRMWARE_TIMEOUT_TARGETS = {
    "compile.compile",
    "release.build-firmware",
}


def workflow_event_names(text: str) -> list[str]:
    match = re.search(r"^on:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return []

    events: list[str] = []
    for line in match.group(1).splitlines():
        event_match = re.match(r"^  ([A-Za-z0-9_-]+):", line)
        if event_match:
            events.append(event_match.group(1))
    return events


def check_workflow_events(
    workflow_events: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_events, dict):
        return

    for workflow, (label, text) in workflow_texts.items():
        raw_events = workflow_events.get(workflow)
        if not isinstance(raw_events, list):
            continue
        expected_events = [str(event).strip() for event in raw_events if str(event).strip()]
        actual_events = workflow_event_names(text)
        missing_events = [event for event in expected_events if event not in actual_events]
        extra_events = [event for event in actual_events if event not in expected_events]
        if missing_events:
            errors.append(f"{label} events are missing product metadata events: {', '.join(missing_events)}")
        if extra_events:
            errors.append(f"{label} events contain triggers missing from product metadata: {', '.join(extra_events)}")


def workflow_event_block(text: str, event_name: str) -> str:
    match = re.search(rf"^  {re.escape(event_name)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    return match.group(1) if match else ""


def unquote_workflow_value(value: str) -> str:
    value = value.strip()
    if len(value) >= 2 and value[0] == value[-1] and value[0] in {'"', "'"}:
        return value[1:-1]
    return value


def workflow_inline_list_values(value: str) -> list[str]:
    value = value.strip()
    if not value:
        return []
    if value.startswith("[") and value.endswith("]"):
        return [unquote_workflow_value(item) for item in value[1:-1].split(",") if item.strip()]
    return [unquote_workflow_value(value)]


def workflow_event_list_field_values(text: str, event_name: str, field_name: str) -> list[str]:
    block = workflow_event_block(text, event_name)
    if not block:
        return []

    values: list[str] = []
    in_field = False
    field_prefix = f"    {field_name}:"
    for line in block.splitlines():
        if line.startswith(field_prefix):
            inline_values = workflow_inline_list_values(line.removeprefix(field_prefix))
            if inline_values:
                return inline_values
            in_field = True
            continue
        if not in_field:
            continue
        if line.startswith("      - "):
            values.append(unquote_workflow_value(line.removeprefix("      - ")))
            continue
        if line.strip() and not line.startswith("      "):
            break
    return values


def workflow_event_path_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "paths")


def workflow_event_branch_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "branches")


def workflow_event_type_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "types")


def workflow_event_workflow_filters(text: str, event_name: str) -> list[str]:
    return workflow_event_list_field_values(text, event_name, "workflows")


def check_workflow_event_type_usage(
    workflow_event_types: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_event_types, dict):
        return

    configured_keys = {str(key).strip() for key in workflow_event_types if str(key).strip()}
    for raw_key, raw_types in workflow_event_types.items():
        key = str(raw_key).strip()
        workflow_name, _, event_name = key.partition(".")
        if workflow_name not in workflow_texts or not event_name or not isinstance(raw_types, list):
            continue
        label, text = workflow_texts[workflow_name]
        expected_types = [str(event_type).strip() for event_type in raw_types if str(event_type).strip()]
        actual_types = workflow_event_type_filters(text, event_name)
        missing_types = [event_type for event_type in expected_types if event_type not in actual_types]
        extra_types = [event_type for event_type in actual_types if event_type not in expected_types]
        if missing_types:
            errors.append(f"{label} {event_name} types are missing product metadata types: {', '.join(missing_types)}")
        if extra_types:
            errors.append(f"{label} {event_name} types contain values missing from product metadata: {', '.join(extra_types)}")

    for workflow_name, (label, text) in workflow_texts.items():
        for event_name in workflow_event_names(text):
            key = f"{workflow_name}.{event_name}"
            if key in configured_keys:
                continue
            actual_types = workflow_event_type_filters(text, event_name)
            if actual_types:
                errors.append(f"{label} {event_name} types are missing from product metadata: {', '.join(actual_types)}")


def check_workflow_run_targets(
    expected_workflows: list[str],
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_targets = [workflow.strip() for workflow in expected_workflows if workflow.strip()]
    if not expected_targets:
        return

    actual_targets = workflow_event_workflow_filters(text, "workflow_run")
    missing_targets = [workflow for workflow in expected_targets if workflow not in actual_targets]
    extra_targets = [workflow for workflow in actual_targets if workflow not in expected_targets]
    if missing_targets:
        errors.append(f"{label} workflow_run workflows are missing targets: {', '.join(missing_targets)}")
    if extra_targets:
        errors.append(
            f"{label} workflow_run workflows contain targets missing from product metadata: "
            + ", ".join(extra_targets)
        )


def check_workflow_default_branch(
    expected_branch: str,
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_branch = expected_branch.strip()
    if not expected_branch:
        return

    expected_branches = [expected_branch]
    actual_branches = workflow_event_branch_filters(text, "push")
    missing_branches = [branch for branch in expected_branches if branch not in actual_branches]
    extra_branches = [branch for branch in actual_branches if branch not in expected_branches]
    if missing_branches:
        errors.append(f"{label} push branches are missing default branch: {', '.join(missing_branches)}")
    if extra_branches:
        errors.append(
            f"{label} push branches contain branches missing from product metadata: "
            + ", ".join(extra_branches)
        )


def check_workflow_path_filters(
    workflow_path_filters: object,
    workflow_texts: dict[str, str],
    errors: list[str],
) -> None:
    if not isinstance(workflow_path_filters, dict):
        return

    for filter_set, (label, event_name) in WORKFLOW_PATH_FILTER_TARGETS.items():
        raw_paths = workflow_path_filters.get(filter_set)
        if not isinstance(raw_paths, list):
            continue
        expected_paths = [str(path).strip() for path in raw_paths if str(path).strip()]
        actual_paths = workflow_event_path_filters(workflow_texts.get(label, ""), event_name)
        missing_paths = [path for path in expected_paths if path not in actual_paths]
        extra_paths = [path for path in actual_paths if path not in expected_paths]
        if missing_paths:
            errors.append(f"{label} {event_name} paths are missing product filters: {', '.join(missing_paths)}")
        if extra_paths:
            errors.append(
                f"{label} {event_name} paths contain filters missing from product metadata: {', '.join(extra_paths)}"
            )


def check_workflow_action_usage(
    release_actions: object,
    workflow_texts: dict[str, str],
    errors: list[str],
) -> None:
    if not isinstance(release_actions, dict):
        return

    expected_actions_by_label: dict[str, list[str]] = {}
    for action_key, labels in WORKFLOW_ACTION_TARGETS.items():
        action = release_actions.get(action_key)
        if not isinstance(action, str) or not action.strip():
            continue
        action_name = action.strip()
        for label in labels:
            expected_actions_by_label.setdefault(label, []).append(action_name)

    for label, text in workflow_texts.items():
        expected_actions = expected_actions_by_label.get(label, [])
        if not expected_actions:
            continue
        actual_actions = workflow_action_references(text)
        missing_actions = [action for action in expected_actions if action not in actual_actions]
        extra_actions = [action for action in actual_actions if action not in expected_actions]
        if missing_actions:
            errors.append(f"{label} uses are missing product metadata actions: {', '.join(missing_actions)}")
        if extra_actions:
            errors.append(f"{label} uses contain actions missing from product metadata: {', '.join(extra_actions)}")


def workflow_action_references(text: str) -> list[str]:
    actions: list[str] = []
    for match in re.finditer(r"^\s*(?:-\s*)?uses:\s*(.*?)\s*$", text, re.MULTILINE):
        action = unquote_workflow_value(match.group(1))
        if action and action not in actions:
            actions.append(action)
    return actions


def workflow_top_level_value(text: str, field_name: str) -> str:
    match = re.search(rf"^{re.escape(field_name)}:\s*(.*?)\s*$", text, re.MULTILINE)
    return unquote_workflow_value(match.group(1)) if match else ""


def workflow_display_name(text: str) -> str:
    return workflow_top_level_value(text, "name")


def check_workflow_names(
    workflow_names_metadata: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_names_metadata, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        expected_name = str(workflow_names_metadata.get(workflow_name, "")).strip()
        if not expected_name:
            continue
        actual_name = workflow_display_name(text)
        if not actual_name:
            errors.append(f"{label} is missing top-level workflow name")
        elif actual_name != expected_name:
            errors.append(f"{label} name must be {expected_name!r}, found {actual_name!r}")


def workflow_top_level_mapping(text: str, section_name: str) -> dict[str, str]:
    match = re.search(rf"^{re.escape(section_name)}:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return {}

    values: dict[str, str] = {}
    for line in match.group(1).splitlines():
        item_match = re.match(r"^  ([A-Za-z0-9_-]+):\s*(.*?)\s*$", line)
        if item_match:
            values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
    return values


def workflow_permissions(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "permissions")


def workflow_env(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "env")


def workflow_concurrency(text: str) -> dict[str, str]:
    return workflow_top_level_mapping(text, "concurrency")


def check_workflow_concurrency(
    expected_group: str,
    expected_cancel_in_progress: object,
    label: str,
    text: str,
    errors: list[str],
) -> None:
    expected_group = expected_group.strip()
    expected_cancel = (
        str(expected_cancel_in_progress).lower()
        if isinstance(expected_cancel_in_progress, bool)
        else ""
    )
    if not expected_group and not expected_cancel:
        return

    actual_concurrency = workflow_concurrency(text)
    if not actual_concurrency:
        errors.append(f"{label} is missing top-level concurrency")
        return

    if expected_group:
        actual_group = actual_concurrency.get("group")
        if actual_group is None:
            errors.append(f"{label} concurrency is missing group")
        elif actual_group != expected_group:
            errors.append(
                f"{label} concurrency.group must be {expected_group!r}, "
                f"found {actual_group!r}"
            )
    if expected_cancel:
        actual_cancel = actual_concurrency.get("cancel-in-progress")
        if actual_cancel is None:
            errors.append(f"{label} concurrency is missing cancel-in-progress")
        elif actual_cancel != expected_cancel:
            errors.append(
                f"{label} concurrency.cancel-in-progress must be "
                f"{expected_cancel!r}, found {actual_cancel!r}"
            )


def check_workflow_top_level_env(
    env_name: str,
    expected_value: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    env_name = env_name.strip()
    expected_value = expected_value.strip()
    if not env_name or not expected_value:
        return

    for label, text in workflow_texts.values():
        actual_env = workflow_env(text)
        actual_value = actual_env.get(env_name)
        if actual_value is None:
            errors.append(f"{label} top-level env is missing {env_name}")
        elif actual_value != expected_value:
            errors.append(
                f"{label} top-level env {env_name} must be {expected_value!r}, found {actual_value!r}"
            )


def check_workflow_permissions(
    workflow_permissions_metadata: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_permissions_metadata, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        raw_permissions = workflow_permissions_metadata.get(workflow_name)
        if not isinstance(raw_permissions, dict):
            continue
        expected_permissions = {
            str(scope).strip(): str(access).strip()
            for scope, access in raw_permissions.items()
            if str(scope).strip() and str(access).strip()
        }
        actual_permissions = workflow_permissions(text)
        missing_scopes = [scope for scope in expected_permissions if scope not in actual_permissions]
        extra_scopes = [scope for scope in actual_permissions if scope not in expected_permissions]
        if missing_scopes:
            errors.append(f"{label} permissions are missing product metadata scopes: {', '.join(missing_scopes)}")
        if extra_scopes:
            errors.append(f"{label} permissions contain scopes missing from product metadata: {', '.join(extra_scopes)}")
        for scope, expected_access in expected_permissions.items():
            actual_access = actual_permissions.get(scope)
            if actual_access is not None and actual_access != expected_access:
                errors.append(
                    f"{label} permissions.{scope} must be {expected_access!r}, found {actual_access!r}"
                )


def workflow_job_ids(text: str) -> list[str]:
    match = re.search(r"^jobs:\n(.*?)(?=^[A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        return []
    return re.findall(r"^  ([A-Za-z0-9_-]+):", match.group(1), re.MULTILINE)


def workflow_job_block(text: str, job_id: str, label: str, errors: list[str]) -> str:
    match = re.search(rf"^  {re.escape(job_id)}:\n(.*?)(?=^  [A-Za-z0-9_-]+:|\Z)", text, re.DOTALL | re.MULTILINE)
    if not match:
        errors.append(f"{label} is missing job {job_id}")
        return ""
    return match.group(1)


def workflow_job_field_value(job_block: str, field_name: str) -> str:
    match = re.search(rf"^    {re.escape(field_name)}:\s*(.*?)\s*$", job_block, re.MULTILINE)
    return unquote_workflow_value(match.group(1)) if match else ""


def workflow_job_nested_field_value(job_block: str, section_name: str, field_name: str) -> str:
    match = re.search(
        rf"^    {re.escape(section_name)}:\n(.*?)(?=^    [A-Za-z0-9_-]+:|\Z)",
        job_block,
        re.DOTALL | re.MULTILINE,
    )
    if not match:
        return ""

    field_match = re.search(
        rf"^      {re.escape(field_name)}:\s*(.*?)\s*$",
        match.group(1),
        re.MULTILINE,
    )
    return unquote_workflow_value(field_match.group(1)) if field_match else ""


def workflow_job_display_name(job_block: str) -> str:
    return workflow_job_field_value(job_block, "name")


def check_workflow_jobs(
    workflow_jobs: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_jobs, dict):
        return

    for workflow_name, (label, text) in workflow_texts.items():
        raw_jobs = workflow_jobs.get(workflow_name)
        if not isinstance(raw_jobs, dict):
            continue
        expected_jobs = [str(job_id).strip() for job_id in raw_jobs if str(job_id).strip()]
        actual_jobs = workflow_job_ids(text)
        missing_jobs = [job_id for job_id in expected_jobs if job_id not in actual_jobs]
        extra_jobs = [job_id for job_id in actual_jobs if job_id not in expected_jobs]
        if missing_jobs:
            errors.append(f"{label} jobs are missing product metadata jobs: {', '.join(missing_jobs)}")
        if extra_jobs:
            errors.append(f"{label} jobs contain jobs missing from product metadata: {', '.join(extra_jobs)}")

        for raw_job_id, raw_job_name in raw_jobs.items():
            job_id = str(raw_job_id).strip()
            job_name = str(raw_job_name).strip()
            if not job_id or not job_name:
                continue
            job_block = workflow_job_block(text, job_id, label, errors)
            if job_block:
                actual_name = workflow_job_display_name(job_block)
                if not actual_name:
                    errors.append(f"{label} job {job_id} is missing name")
                elif actual_name != job_name:
                    errors.append(f"{label} job {job_id} name must be {job_name!r}, found {actual_name!r}")


def workflow_job_runs_on(job_block: str) -> str:
    return workflow_job_field_value(job_block, "runs-on")


def workflow_job_timeout_minutes(job_block: str) -> str:
    return workflow_job_field_value(job_block, "timeout-minutes")


def workflow_job_strategy_fail_fast(job_block: str) -> str:
    return workflow_job_nested_field_value(job_block, "strategy", "fail-fast")


def workflow_job_mapping(job_block: str, section_name: str) -> dict[str, str]:
    values: dict[str, str] = {}
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith(f"    {section_name}:"):
            continue
        for continuation in lines[index + 1:]:
            if continuation.strip() and not continuation.startswith("      "):
                break
            item_match = re.match(r"^      ([A-Za-z_][A-Za-z0-9_-]*):\s*(.*?)\s*$", continuation)
            if item_match:
                values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
        return values
    return values


def workflow_job_outputs(job_block: str) -> dict[str, str]:
    return workflow_job_mapping(job_block, "outputs")


def check_workflow_job_outputs(
    target: str,
    expected_outputs: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_outputs = {
        str(name).strip(): str(value).strip()
        for name, value in expected_outputs.items()
        if str(name).strip() and str(value).strip()
    }
    workflow_name, _, job_id = target.partition(".")
    if not expected_outputs or workflow_name not in workflow_texts or not job_id:
        return

    label, text = workflow_texts[workflow_name]
    job_block = workflow_job_block(text, job_id, label, errors)
    if not job_block:
        return

    actual_outputs = workflow_job_outputs(job_block)
    for name, expected_value in expected_outputs.items():
        actual_value = actual_outputs.get(name)
        if actual_value is None:
            errors.append(f"{label} job {job_id} outputs is missing {name}")
        elif actual_value != expected_value:
            errors.append(
                f"{label} job {job_id} outputs.{name} must be {expected_value!r}, "
                f"found {actual_value!r}"
            )


def workflow_job_step_blocks(job_block: str) -> list[str]:
    blocks: list[str] = []
    current: list[str] = []
    in_steps = False
    for line in job_block.splitlines():
        if line.startswith("    steps:"):
            in_steps = True
            continue
        if in_steps and re.match(r"^    [A-Za-z0-9_-]+:", line):
            break
        if not in_steps:
            continue
        if line.startswith("      - "):
            if current:
                blocks.append("\n".join(current))
            current = [line]
        elif current:
            current.append(line)
    if current:
        blocks.append("\n".join(current))
    return blocks


def workflow_step_display_name(step_block: str) -> str:
    for line in step_block.splitlines():
        match = re.match(r"^      - name:\s*(.*?)\s*$", line)
        if not match:
            match = re.match(r"^        name:\s*(.*?)\s*$", line)
        if match:
            return unquote_workflow_value(match.group(1))
    return ""


def workflow_step_uses(step_block: str) -> str:
    for line in step_block.splitlines():
        match = re.match(r"^      - uses:\s*(.*?)\s*$", line)
        if not match:
            match = re.match(r"^        uses:\s*(.*?)\s*$", line)
        if match:
            return unquote_workflow_value(match.group(1))
    return ""


def workflow_step_mapping(step_block: str, section_name: str) -> dict[str, str]:
    values: dict[str, str] = {}
    lines = step_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith(f"        {section_name}:"):
            continue
        for continuation in lines[index + 1:]:
            if continuation.strip() and not continuation.startswith("          "):
                break
            item_match = re.match(r"^          ([A-Za-z_][A-Za-z0-9_-]*):\s*(.*?)\s*$", continuation)
            if item_match:
                values[item_match.group(1)] = unquote_workflow_value(item_match.group(2))
        return values
    return values


def workflow_step_env(step_block: str) -> dict[str, str]:
    return workflow_step_mapping(step_block, "env")


def workflow_step_with(step_block: str) -> dict[str, str]:
    return workflow_step_mapping(step_block, "with")


def workflow_step_run(step_block: str) -> str:
    lines = step_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("        run:"):
            continue
        command = line.removeprefix("        run:").strip()
        if command in {">", ">-", "|", "|-"}:
            run_lines: list[str] = []
            for continuation in lines[index + 1:]:
                if continuation.startswith("          "):
                    run_lines.append(continuation.removeprefix("          "))
                    continue
                if continuation.strip():
                    break
            return "\n".join(run_lines)
        return unquote_workflow_value(command)
    return ""


def workflow_step_uses_gh_cli(step_block: str) -> bool:
    return re.search(r"(^|[\s;&|])gh\s+", workflow_step_run(step_block)) is not None


def check_workflow_action_step_inputs(
    target: str,
    action_ref: str,
    match_input: str,
    match_value: str,
    expected_inputs: dict[str, str],
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    action_ref = action_ref.strip()
    match_input = match_input.strip()
    match_value = match_value.strip()
    expected_inputs = {
        str(name).strip(): str(value).strip()
        for name, value in expected_inputs.items()
        if str(name).strip() and str(value).strip()
    }
    workflow_name, _, job_id = target.partition(".")
    if (
        not action_ref
        or not match_input
        or not match_value
        or not expected_inputs
        or workflow_name not in workflow_texts
        or not job_id
    ):
        return

    label, text = workflow_texts[workflow_name]
    job_block = workflow_job_block(text, job_id, label, errors)
    if not job_block:
        return

    matching_step = ""
    for step_block in workflow_job_step_blocks(job_block):
        if workflow_step_uses(step_block) != action_ref:
            continue
        step_inputs = workflow_step_with(step_block)
        if step_inputs.get(match_input) == match_value:
            matching_step = step_block
            break

    if not matching_step:
        errors.append(
            f"{label} job {job_id} is missing {action_ref} step with "
            f"{match_input} {match_value!r}"
        )
        return

    step_name = workflow_step_display_name(matching_step) or "<unnamed>"
    actual_inputs = workflow_step_with(matching_step)
    for name, expected_value in expected_inputs.items():
        actual_value = actual_inputs.get(name)
        if actual_value is None:
            errors.append(f"{label} job {job_id} step {step_name!r} with is missing {name}")
        elif actual_value != expected_value:
            errors.append(
                f"{label} job {job_id} step {step_name!r} with.{name} "
                f"must be {expected_value!r}, found {actual_value!r}"
            )


def check_workflow_job_run_command(
    target: str,
    expected_run: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_run = expected_run.strip()
    workflow_name, _, job_id = target.partition(".")
    if not expected_run or workflow_name not in workflow_texts or not job_id:
        return

    label, text = workflow_texts[workflow_name]
    job_block = workflow_job_block(text, job_id, label, errors)
    if not job_block:
        return

    for step_block in workflow_job_step_blocks(job_block):
        if workflow_step_run(step_block).strip() == expected_run:
            return
    errors.append(f"{label} job {job_id} is missing run command {expected_run!r}")


def check_workflow_gh_cli_env(
    github_cli_env: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(github_cli_env, dict):
        return

    expected_env = {
        str(raw_name).strip(): str(raw_value).strip()
        for raw_name, raw_value in github_cli_env.items()
        if str(raw_name).strip() and str(raw_value).strip()
    }
    if not expected_env:
        return

    for label, text in workflow_texts.values():
        for job_id in workflow_job_ids(text):
            job_block = workflow_job_block(text, job_id, label, errors)
            if not job_block:
                continue
            for step_block in workflow_job_step_blocks(job_block):
                if not workflow_step_uses_gh_cli(step_block):
                    continue
                step_name = workflow_step_display_name(step_block) or "<unnamed>"
                actual_env = workflow_step_env(step_block)
                for name, expected_value in expected_env.items():
                    actual_value = actual_env.get(name)
                    if actual_value is None:
                        errors.append(f"{label} job {job_id} step {step_name!r} env is missing {name}")
                    elif actual_value != expected_value:
                        errors.append(
                            f"{label} job {job_id} step {step_name!r} env.{name} "
                            f"must be {expected_value!r}, found {actual_value!r}"
                        )


def check_workflow_release_build_fail_fast(
    expected_fail_fast: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(expected_fail_fast, bool):
        return
    if "release" not in workflow_texts:
        return

    expected_fail_fast_value = str(expected_fail_fast).lower()
    label, text = workflow_texts["release"]
    job_block = workflow_job_block(text, "build-firmware", label, errors)
    if not job_block:
        return
    actual_fail_fast = workflow_job_strategy_fail_fast(job_block)
    if not actual_fail_fast:
        errors.append(f"{label} job build-firmware strategy is missing fail-fast")
    elif actual_fail_fast != expected_fail_fast_value:
        errors.append(
            f"{label} job build-firmware strategy.fail-fast must be {expected_fail_fast_value!r}, "
            f"found {actual_fail_fast!r}"
        )


def check_workflow_job_timeout_usage(
    expected_timeout: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(expected_timeout, int) or isinstance(expected_timeout, bool):
        return
    expected_timeout_value = str(expected_timeout)

    for target in sorted(WORKFLOW_FIRMWARE_TIMEOUT_TARGETS):
        workflow_name, _, job_id = target.partition(".")
        if workflow_name not in workflow_texts or not job_id:
            continue
        label, text = workflow_texts[workflow_name]
        job_block = workflow_job_block(text, job_id, label, errors)
        if not job_block:
            continue
        actual_timeout = workflow_job_timeout_minutes(job_block)
        if not actual_timeout:
            errors.append(f"{label} job {job_id} is missing timeout-minutes")
        elif actual_timeout != expected_timeout_value:
            errors.append(
                f"{label} job {job_id} timeout-minutes must be {expected_timeout_value!r}, "
                f"found {actual_timeout!r}"
            )


def check_workflow_job_runner_usage(
    expected_runner: str,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_runner = expected_runner.strip()
    if not expected_runner:
        return

    for label, text in workflow_texts.values():
        for job_id in workflow_job_ids(text):
            job_block = workflow_job_block(text, job_id, label, errors)
            if not job_block:
                continue
            actual_runner = workflow_job_runs_on(job_block)
            if not actual_runner:
                errors.append(f"{label} job {job_id} is missing runs-on")
            elif actual_runner != expected_runner:
                errors.append(
                    f"{label} job {job_id} runs-on must be {expected_runner!r}, found {actual_runner!r}"
                )


def workflow_job_needs(job_block: str) -> list[str]:
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("    needs:"):
            continue
        inline_needs = workflow_inline_list_values(line.removeprefix("    needs:"))
        if inline_needs:
            return inline_needs
        dependencies: list[str] = []
        for continuation in lines[index + 1:]:
            if continuation.startswith("      - "):
                dependencies.append(unquote_workflow_value(continuation.removeprefix("      - ")))
                continue
            if continuation.strip() and not continuation.startswith("      "):
                break
        return dependencies
    return []


def check_workflow_job_dependency_usage(
    workflow_job_dependencies: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    if not isinstance(workflow_job_dependencies, dict):
        return

    configured_keys = {str(key).strip() for key in workflow_job_dependencies if str(key).strip()}
    for raw_key, raw_dependencies in workflow_job_dependencies.items():
        key = str(raw_key).strip()
        workflow_name, _, job_id = key.partition(".")
        if workflow_name not in workflow_texts or not job_id or not isinstance(raw_dependencies, list):
            continue
        label, text = workflow_texts[workflow_name]
        expected_dependencies = [str(dependency).strip() for dependency in raw_dependencies if str(dependency).strip()]
        job_block = workflow_job_block(text, job_id, label, errors)
        if not job_block:
            continue
        actual_dependencies = workflow_job_needs(job_block)
        missing_dependencies = [dependency for dependency in expected_dependencies if dependency not in actual_dependencies]
        extra_dependencies = [dependency for dependency in actual_dependencies if dependency not in expected_dependencies]
        if missing_dependencies:
            errors.append(f"{label} job {job_id} needs are missing dependencies: {', '.join(missing_dependencies)}")
        if extra_dependencies:
            errors.append(
                f"{label} job {job_id} needs contain dependencies missing from product metadata: {', '.join(extra_dependencies)}"
            )

    for workflow_name, (label, text) in workflow_texts.items():
        for job_id in workflow_job_ids(text):
            key = f"{workflow_name}.{job_id}"
            if key in configured_keys:
                continue
            job_block = workflow_job_block(text, job_id, label, errors)
            actual_dependencies = workflow_job_needs(job_block)
            if actual_dependencies:
                errors.append(f"{label} job {job_id} needs are missing from product metadata: {', '.join(actual_dependencies)}")


def workflow_sparse_checkout_entries(text: str) -> list[tuple[list[str], str]]:
    entries: list[tuple[list[str], str]] = []
    lines = text.splitlines()
    for index, line in enumerate(lines):
        match = re.match(r"^(\s*)sparse-checkout:\s*\|\s*$", line)
        if not match:
            continue
        indent = match.group(1)
        value_prefix = f"{indent}  "
        paths: list[str] = []
        next_index = index + 1
        while next_index < len(lines):
            continuation = lines[next_index]
            if continuation.startswith(value_prefix):
                paths.append(unquote_workflow_value(continuation.removeprefix(value_prefix)))
                next_index += 1
                continue
            if continuation.strip():
                break
            next_index += 1
        cone_mode = ""
        if next_index < len(lines):
            cone_mode_prefix = f"{indent}sparse-checkout-cone-mode:"
            if lines[next_index].startswith(cone_mode_prefix):
                cone_mode = unquote_workflow_value(lines[next_index].removeprefix(cone_mode_prefix))
        entries.append((paths, cone_mode))
    return entries


def workflow_sparse_checkout_blocks(text: str) -> list[list[str]]:
    blocks: list[list[str]] = []
    for paths, _ in workflow_sparse_checkout_entries(text):
        blocks.append(paths)
    return blocks


def check_workflow_sparse_checkout_usage(
    release_checkout_files: list[str],
    metadata_checkout_files: list[str],
    expected_cone_mode: object,
    workflow_texts: dict[str, tuple[str, str]],
    errors: list[str],
) -> None:
    expected_checkout_groups = {
        "metadata": [path.strip() for path in metadata_checkout_files if path.strip()],
        "release": [path.strip() for path in release_checkout_files if path.strip()],
    }
    if not expected_checkout_groups["metadata"] or not expected_checkout_groups["release"]:
        return
    expected_cone_mode_value = (
        str(expected_cone_mode).lower() if isinstance(expected_cone_mode, bool) else ""
    )

    for workflow_name, (label, text) in workflow_texts.items():
        expected_groups = set(WORKFLOW_SPARSE_CHECKOUT_TARGETS.get(workflow_name, ()))
        if not expected_groups:
            continue

        matched_groups: list[str] = []
        for block, cone_mode in workflow_sparse_checkout_entries(text):
            matching_groups = [name for name, paths in expected_checkout_groups.items() if block == paths]
            if matching_groups:
                matched_group = matching_groups[0]
                matched_groups.append(matched_group)
            else:
                matched_group = "unknown"
                errors.append(f"{label} sparse-checkout block must match product metadata, found: {', '.join(block)}")
            if expected_cone_mode_value and cone_mode != expected_cone_mode_value:
                found = cone_mode or "<missing>"
                errors.append(
                    f"{label} sparse-checkout block {matched_group} cone mode must be "
                    f"{expected_cone_mode_value!r}, found {found!r}"
                )

        for expected_group in sorted(expected_groups):
            if expected_group not in matched_groups:
                errors.append(
                    f"{label} is missing {expected_group} sparse-checkout block: "
                    + ", ".join(expected_checkout_groups[expected_group])
                )
        for matched_group in matched_groups:
            if matched_group not in expected_groups:
                errors.append(f"{label} sparse-checkout block {matched_group} is not expected")


def normalize_workflow_condition(condition: str) -> str:
    return re.sub(r"\s+", " ", condition).strip()


def workflow_job_condition(job_block: str) -> str:
    lines = job_block.splitlines()
    for index, line in enumerate(lines):
        if not line.startswith("    if:"):
            continue
        condition = line.removeprefix("    if:").strip()
        if condition in {">", ">-", "|", "|-"}:
            folded_lines = []
            for continuation in lines[index + 1:]:
                if not continuation.startswith("      "):
                    break
                folded_lines.append(continuation.strip())
            return normalize_workflow_condition(" ".join(folded_lines))
        return normalize_workflow_condition(condition)
    return ""


def check_device_workflow_contract(product: dict, errors: list[str]) -> None:
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    project = product["project"]
    release_actions = project.get("release_workflow_actions", {})
    artifact_prefix = str(project.get("release_artifact_prefix", "")).strip()
    release_build_output_dir = str(project.get("release_build_output_dir", "")).strip()
    release_publish_dir = str(project.get("release_publish_dir", "")).strip()
    release_uploaded_verify_dir = str(project.get("release_uploaded_verify_dir", "")).strip()
    release_source_factory_binary = str(project.get("release_source_factory_binary", "")).strip()
    release_source_ota_binary = str(project.get("release_source_ota_binary", "")).strip()
    release_esphome_cache_dir = str(project.get("release_esphome_cache_dir", "")).strip()
    release_esphome_cache_key_prefix = str(project.get("release_esphome_cache_key_prefix", "")).strip()
    release_esphome_cache_hash_files = [
        str(path).strip() for path in project.get("release_esphome_cache_hash_files", []) if str(path).strip()
    ]
    asset_suffixes = [str(value).strip() for value in project.get("release_asset_suffixes", []) if str(value).strip()]
    binary_download_patterns = [
        str(value).strip() for value in project.get("release_binary_download_patterns", []) if str(value).strip()
    ]
    manifest_download_patterns = [
        str(value).strip() for value in project.get("release_manifest_download_patterns", []) if str(value).strip()
    ]
    uploaded_verify_patterns = [
        str(value).strip() for value in project.get("release_uploaded_verify_patterns", []) if str(value).strip()
    ]
    release_download_clobber = project.get("github_release_download_clobber")
    release_upload_clobber = project.get("github_release_upload_clobber")
    release_version_pattern = str(project.get("release_version_pattern", "")).strip()
    stable_release_version_pattern = str(project.get("stable_release_version_pattern", "")).strip()
    firmware_version_placeholder = str(project.get("firmware_version_placeholder_line", "")).rstrip("\n")
    local_build_version = str(project.get("firmware_local_build_version", "")).strip()
    placeholder_versions = [str(value).strip() for value in project.get("firmware_placeholder_versions", []) if str(value).strip()]
    changelog_categories = project.get("release_changelog_categories", [])
    changelog_fallback = str(project.get("release_changelog_fallback_category", "")).strip()
    docs_dist_artifact_name = str(project.get("docs_dist_artifact_name", "")).strip()
    docs_firmware_artifact_name = str(project.get("docs_firmware_artifact_name", "")).strip()
    compile_firmware_artifact_prefix = str(project.get("compile_firmware_artifact_prefix", "")).strip()
    compile_firmware_output_dir = str(project.get("compile_firmware_output_dir", "")).strip()
    compile_firmware_version_prefix = str(project.get("compile_firmware_version_prefix", "")).strip()
    checkout_action = (
        str(release_actions.get("checkout", "")).strip() if isinstance(release_actions, dict) else ""
    )
    upload_artifact_action = (
        str(release_actions.get("upload_artifact", "")).strip() if isinstance(release_actions, dict) else ""
    )
    download_artifact_action = (
        str(release_actions.get("download_artifact", "")).strip() if isinstance(release_actions, dict) else ""
    )
    upload_pages_artifact_action = (
        str(release_actions.get("upload_pages_artifact", "")).strip() if isinstance(release_actions, dict) else ""
    )
    docs_dist_output_path = str(project.get("docs_dist_output_path", "")).strip()
    docs_deploy_path = str(project.get("docs_deploy_path", "")).strip()
    pages_environment = str(project.get("github_pages_environment", "")).strip()
    pages_concurrency_group = str(project.get("github_pages_concurrency_group", "")).strip()
    pages_cancel_in_progress = project.get("github_pages_cancel_in_progress")
    docs_workflow_success_conclusion = str(project.get("github_docs_workflow_run_success_conclusion", "")).strip()
    release_notes_fetch_depth = project.get("github_release_notes_fetch_depth")
    release_notes_fetch_tags = project.get("github_release_notes_fetch_tags")
    release_notes_version_ref = str(project.get("github_release_notes_version_ref", "")).strip()
    release_build_version_ref = str(project.get("github_release_build_version_ref", "")).strip()
    release_build_ref = str(project.get("github_release_build_ref", "")).strip()
    release_build_fail_fast = project.get("github_release_build_fail_fast")
    release_notes_output = str(project.get("github_release_notes_output", "")).strip()
    sparse_checkout_files = [
        str(path).strip() for path in project.get("github_sparse_checkout_files", []) if str(path).strip()
    ]
    metadata_sparse_checkout_files = [
        str(path).strip() for path in project.get("github_metadata_sparse_checkout_files", []) if str(path).strip()
    ]
    sparse_checkout_cone_mode = project.get("github_sparse_checkout_cone_mode")
    docs_verify_retries = project.get("docs_firmware_verify_retries")
    docs_verify_delay = project.get("docs_firmware_verify_delay_seconds")
    docs_release_meta_step_id = str(project.get("github_docs_release_meta_step_id", "")).strip()
    docs_release_tag_env = str(project.get("github_docs_release_tag_env", "")).strip()
    docs_release_tag_output = str(project.get("github_docs_release_tag_output", "")).strip()
    docs_prerelease_tag_env = str(project.get("github_docs_prerelease_tag_env", "")).strip()
    pages_deployment_step_id = str(project.get("github_pages_deployment_step_id", "")).strip()
    pages_url_output = str(project.get("github_pages_url_output", "")).strip()
    prerelease_lookup_limit = project.get("github_prerelease_lookup_limit")
    github_cli_env = project.get("github_cli_env", {})
    firmware_compile_timeout = project.get("firmware_compile_timeout_minutes")
    esphome_config_mount = str(project.get("esphome_config_mount", "")).strip()
    slugs = [str(device.get("slug", "")).strip() for device in product["devices"]]
    expected_slugs = " ".join(slugs)
    workflow_texts = {
        "compile": (".github/workflows/compile.yml", compile_workflow),
        "docs": (".github/workflows/docs.yml", docs_workflow),
        "release": (".github/workflows/release.yml", release_workflow),
    }
    product_output_names = [
        "release_matrix",
        "esphome_docker_image",
        "esphome_version",
        "esphome_config_mount",
        "esphome_docker_remove_flag",
        "release_esphome_cache_dir",
        "release_esphome_cache_key_prefix",
    ]
    product_metadata_outputs = {
        name: f"${{{{ steps.product.outputs.{name} }}}}"
        for name in product_output_names
    }
    check_workflow_job_outputs(
        "compile.firmware-metadata",
        product_metadata_outputs,
        workflow_texts,
        errors,
    )
    release_metadata_outputs = {
        **product_metadata_outputs,
        "device_slugs": "${{ steps.product.outputs.device_slugs }}",
    }
    check_workflow_job_outputs(
        "release.release-metadata",
        release_metadata_outputs,
        workflow_texts,
        errors,
    )
    release_notes_checkout_inputs: dict[str, str] = {}
    if isinstance(release_notes_fetch_depth, int) and not isinstance(release_notes_fetch_depth, bool):
        release_notes_checkout_inputs["fetch-depth"] = str(release_notes_fetch_depth)
    if isinstance(release_notes_fetch_tags, bool):
        release_notes_checkout_inputs["fetch-tags"] = str(release_notes_fetch_tags).lower()
    if release_notes_checkout_inputs:
        match_input, match_value = next(iter(release_notes_checkout_inputs.items()))
        check_workflow_action_step_inputs(
            "release.release-notes",
            checkout_action,
            match_input,
            match_value,
            release_notes_checkout_inputs,
            workflow_texts,
            errors,
        )
    if release_build_ref:
        for target in ("release.release-metadata", "release.build-firmware"):
            check_workflow_action_step_inputs(
                target,
                checkout_action,
                "ref",
                release_build_ref,
                {"ref": release_build_ref},
                workflow_texts,
                errors,
            )
    if release_notes_version_ref:
        require_contains(release_workflow, f"VERSION: {release_notes_version_ref}", ".github/workflows/release.yml", errors)
    if release_build_version_ref:
        require_contains(release_workflow, f"VERSION: {release_build_version_ref}", ".github/workflows/release.yml", errors)
    if release_notes_output:
        for needle in (
            f'--output "{release_notes_output}"',
            f'--notes-file "{release_notes_output}"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    for label, text in (
        (".github/workflows/release.yml", release_workflow),
        (".github/workflows/docs.yml", docs_workflow),
    ):
        if label == ".github/workflows/release.yml":
            require_contains(text, "DEVICE_SLUGS: ${{ needs.release-metadata.outputs.device_slugs }}", label, errors)
        else:
            require_contains(text, "python3 scripts/product_config.py github-env >> \"$GITHUB_ENV\"", label, errors)
            require_contains(text, "$DEVICE_SLUGS", label, errors)
        if f"DEVICE_SLUGS: {expected_slugs}" in text:
            errors.append(f"{label} must read DEVICE_SLUGS from product metadata, not a literal device list")
    check_workflow_gh_cli_env(
        github_cli_env,
        {
            "docs": (".github/workflows/docs.yml", docs_workflow),
            "release": (".github/workflows/release.yml", release_workflow),
        },
        errors,
    )
    check_workflow_sparse_checkout_usage(
        sparse_checkout_files,
        metadata_sparse_checkout_files,
        sparse_checkout_cone_mode,
        {
            "compile": (".github/workflows/compile.yml", compile_workflow),
            "docs": (".github/workflows/docs.yml", docs_workflow),
            "release": (".github/workflows/release.yml", release_workflow),
        },
        errors,
    )
    check_workflow_action_usage(
        release_actions,
        {
            ".github/workflows/release.yml": release_workflow,
            ".github/workflows/docs.yml": docs_workflow,
            ".github/workflows/compile.yml": compile_workflow,
        },
        errors,
    )
    if artifact_prefix:
        release_artifact_name = f"{artifact_prefix}${{{{ matrix.slug }}}}"
        release_artifact_upload_inputs = {"name": release_artifact_name}
        if release_build_output_dir:
            release_artifact_upload_inputs["path"] = f"{release_build_output_dir}/"
        check_workflow_action_step_inputs(
            "release.build-firmware",
            upload_artifact_action,
            "name",
            release_artifact_name,
            release_artifact_upload_inputs,
            {"release": (".github/workflows/release.yml", release_workflow)},
            errors,
        )

        release_artifact_download_inputs = {
            "pattern": f"{artifact_prefix}*",
            "merge-multiple": "true",
        }
        if release_publish_dir:
            release_artifact_download_inputs["path"] = release_publish_dir
        check_workflow_action_step_inputs(
            "release.publish",
            download_artifact_action,
            "pattern",
            f"{artifact_prefix}*",
            release_artifact_download_inputs,
            {"release": (".github/workflows/release.yml", release_workflow)},
            errors,
        )
    if release_build_output_dir:
        for needle in (
            f"mkdir -p {release_build_output_dir}",
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.factory.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.ota.bin"',
            f'"{release_build_output_dir}/${{{{ matrix.slug }}}}.manifest.json"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_factory_binary:
        for needle in (
            f'"${{BUILD_DIR}}/{release_source_factory_binary}"',
            f"factory binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_source_ota_binary:
        for needle in (
            '-s firmware_version "${VERSION}"',
            'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
            f'"${{BUILD_DIR}}/{release_source_ota_binary}"',
            f"OTA binary not found",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_publish_dir:
        for needle in (
            f"--dir {release_publish_dir}",
            f"{release_publish_dir}/* --clobber",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_uploaded_verify_dir:
        for needle in (
            f"mkdir -p {release_uploaded_verify_dir}",
            f"--dir {release_uploaded_verify_dir}",
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_dir:
        for needle in (
            "path: ${{ needs.release-metadata.outputs.release_esphome_cache_dir }}",
            'if [ -d "${RELEASE_ESPHOME_CACHE_DIR}" ]; then',
            'sudo chown -R "$USER:$USER" "${RELEASE_ESPHOME_CACHE_DIR}"',
            'chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"',
            'BUILD_DIR="${RELEASE_ESPHOME_CACHE_DIR}/build/${{ matrix.build_name }}/.pioenvs/${{ matrix.build_name }}"',
        ):
            require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)
    if release_esphome_cache_key_prefix:
        require_contains(
            release_workflow,
            "restore-keys: |",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            release_workflow,
            "${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-",
            ".github/workflows/release.yml",
            errors,
        )
    if release_esphome_cache_hash_files:
        hash_files = "', '".join(release_esphome_cache_hash_files)
        require_contains(
            release_workflow,
            f"hashFiles('{hash_files}')",
            ".github/workflows/release.yml",
            errors,
        )
        require_contains(
            compile_workflow,
            f"hashFiles('{hash_files}')",
            ".github/workflows/compile.yml",
            errors,
        )
    if compile_firmware_artifact_prefix:
        compile_artifact_name = f"{compile_firmware_artifact_prefix}${{{{ matrix.slug }}}}"
        compile_artifact_upload_inputs = {"name": compile_artifact_name}
        if compile_firmware_output_dir:
            compile_artifact_upload_inputs["path"] = f"{compile_firmware_output_dir}/"
        check_workflow_action_step_inputs(
            "compile.compile",
            upload_artifact_action,
            "name",
            compile_artifact_name,
            compile_artifact_upload_inputs,
            {"compile": (".github/workflows/compile.yml", compile_workflow)},
            errors,
        )
    if compile_firmware_output_dir:
        for needle in (
            f"mkdir -p {compile_firmware_output_dir}",
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.factory.bin"',
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.ota.bin"',
            f'"{compile_firmware_output_dir}/${{{{ matrix.slug }}}}.version.txt"',
        ):
            require_contains(compile_workflow, needle, ".github/workflows/compile.yml", errors)
    if compile_firmware_version_prefix:
        require_contains(
            compile_workflow,
            f'TEST_VERSION="{compile_firmware_version_prefix}-${{GITHUB_RUN_ID}}-${{GITHUB_RUN_ATTEMPT}}"',
            ".github/workflows/compile.yml",
            errors,
        )
        require_contains(compile_workflow, '-s firmware_version "${TEST_VERSION}"', ".github/workflows/compile.yml", errors)
    for pattern in binary_download_patterns:
        require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in manifest_download_patterns:
        if pattern == "manifest.json":
            require_contains(docs_workflow, 'basename "$DEFAULT_PUBLIC_MANIFEST"', ".github/workflows/docs.yml", errors)
            require_contains(docs_workflow, 'basename "$DEFAULT_PUBLIC_BETA_MANIFEST"', ".github/workflows/docs.yml", errors)
        else:
            require_contains(docs_workflow, f'--pattern "{pattern}"', ".github/workflows/docs.yml", errors)
    for pattern in uploaded_verify_patterns:
        require_contains(release_workflow, f'--pattern "{pattern}"', ".github/workflows/release.yml", errors)
    if release_download_clobber is True:
        require_contains(docs_workflow, "--clobber", ".github/workflows/docs.yml", errors)
    if release_upload_clobber is True:
        require_contains(release_workflow, f"{release_publish_dir}/* --clobber", ".github/workflows/release.yml", errors)
    for suffix in asset_suffixes:
        require_contains(release_workflow, suffix, ".github/workflows/release.yml", errors)
        if suffix == ".manifest.json":
            require_contains(docs_workflow, suffix, ".github/workflows/docs.yml", errors)
        require_contains(read(ROOT / "scripts" / "firmware_release.py", errors), suffix, "scripts/firmware_release.py", errors)
    firmware_release_script = read(ROOT / "scripts" / "firmware_release.py", errors)
    release_changelog_script = read(ROOT / "scripts" / "release_changelog.py", errors)
    if release_version_pattern:
        require_contains(firmware_release_script, "release_version_pattern", "scripts/firmware_release.py", errors)
    if stable_release_version_pattern:
        require_contains(release_changelog_script, "stable_release_version_pattern", "scripts/release_changelog.py", errors)
    if isinstance(changelog_categories, list) and changelog_categories:
        require_contains(release_changelog_script, "release_changelog_categories", "scripts/release_changelog.py", errors)
    if changelog_fallback:
        require_contains(release_changelog_script, "release_changelog_fallback_category", "scripts/release_changelog.py", errors)
    if firmware_version_placeholder:
        require_contains(firmware_release_script, "firmware_version_placeholder_line", "scripts/firmware_release.py", errors)
        for device in product["devices"]:
            build_yaml = check_relative_path(device.get("build_yaml"), f"Device {device.get('slug', '<missing>')} build_yaml", errors)
            if build_yaml:
                require_contains(read(ROOT / build_yaml, errors), firmware_version_placeholder, build_yaml, errors)
    if placeholder_versions:
        require_contains(firmware_release_script, "firmware_placeholder_versions", "scripts/firmware_release.py", errors)
    if local_build_version:
        require_contains(
            read(ROOT / "common" / "addon" / "firmware_update.yaml", errors),
            f'firmware_version: "{local_build_version}"',
            "common/addon/firmware_update.yaml",
            errors,
        )
    docs_workflow_texts = {"docs": (".github/workflows/docs.yml", docs_workflow)}
    if docs_dist_artifact_name:
        docs_dist_upload_inputs = {"name": docs_dist_artifact_name}
        if docs_dist_output_path:
            docs_dist_upload_inputs["path"] = docs_dist_output_path
        check_workflow_action_step_inputs(
            "docs.build-docs",
            upload_artifact_action,
            "name",
            docs_dist_artifact_name,
            docs_dist_upload_inputs,
            docs_workflow_texts,
            errors,
        )
    if docs_firmware_artifact_name:
        if f"mkdir -p {docs_firmware_artifact_name}" not in docs_workflow:
            require_contains(docs_workflow, 'mkdir -p "$STABLE_MANIFEST_DIR"', ".github/workflows/docs.yml", errors)
        check_workflow_action_step_inputs(
            "docs.download-firmware",
            upload_artifact_action,
            "name",
            docs_firmware_artifact_name,
            {
                "name": docs_firmware_artifact_name,
                "path": f"{docs_firmware_artifact_name}/",
            },
            docs_workflow_texts,
            errors,
        )
        if docs_deploy_path:
            check_workflow_action_step_inputs(
                "docs.deploy-docs",
                download_artifact_action,
                "name",
                docs_firmware_artifact_name,
                {
                    "name": docs_firmware_artifact_name,
                    "path": f"{docs_deploy_path}/{docs_firmware_artifact_name}",
                },
                docs_workflow_texts,
                errors,
            )
            require_contains(
                docs_workflow,
                f"rm -rf {docs_deploy_path}/{docs_firmware_artifact_name}",
                ".github/workflows/docs.yml",
                errors,
            )
    if docs_deploy_path:
        if docs_dist_artifact_name:
            check_workflow_action_step_inputs(
                "docs.deploy-docs",
                download_artifact_action,
                "name",
                docs_dist_artifact_name,
                {
                    "name": docs_dist_artifact_name,
                    "path": docs_deploy_path,
                },
                docs_workflow_texts,
                errors,
            )
        check_workflow_action_step_inputs(
            "docs.deploy-docs",
            upload_pages_artifact_action,
            "path",
            docs_deploy_path,
            {"path": docs_deploy_path},
            docs_workflow_texts,
            errors,
        )
    if pages_environment:
        require_contains(docs_workflow, "environment:", ".github/workflows/docs.yml", errors)
        require_contains(docs_workflow, f"name: {pages_environment}", ".github/workflows/docs.yml", errors)
    if pages_deployment_step_id and pages_url_output:
        require_contains(docs_workflow, f"id: {pages_deployment_step_id}", ".github/workflows/docs.yml", errors)
        require_contains(
            docs_workflow,
            f"url: ${{{{ steps.{pages_deployment_step_id}.outputs.{pages_url_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    check_workflow_concurrency(
        pages_concurrency_group,
        pages_cancel_in_progress,
        ".github/workflows/docs.yml",
        docs_workflow,
        errors,
    )
    if docs_workflow_success_conclusion:
        require_contains(
            docs_workflow,
            "github.event_name != 'workflow_run' ||",
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"github.event.workflow_run.conclusion == '{docs_workflow_success_conclusion}'",
            ".github/workflows/docs.yml",
            errors,
        )
    if isinstance(docs_verify_retries, int) and not isinstance(docs_verify_retries, bool):
        require_contains(docs_workflow, f"--retries {docs_verify_retries}", ".github/workflows/docs.yml", errors)
    if isinstance(docs_verify_delay, int) and not isinstance(docs_verify_delay, bool):
        require_contains(docs_workflow, f"--delay {docs_verify_delay}", ".github/workflows/docs.yml", errors)
    if docs_release_tag_env:
        release_tag_ref = f"${docs_release_tag_env}"
        for needle in (
            f"{docs_release_tag_env}=$(gh release view --json tagName -q .tagName)",
            f'echo "{docs_release_tag_env}=${{{docs_release_tag_env}}}" >> "$GITHUB_ENV"',
            f'gh release download "{release_tag_ref}"',
            f'--version "{release_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    if docs_release_tag_env and docs_release_tag_output:
        if docs_release_meta_step_id:
            require_contains(docs_workflow, f"id: {docs_release_meta_step_id}", ".github/workflows/docs.yml", errors)
            require_contains(
                docs_workflow,
                f"{docs_release_tag_output}: ${{{{ steps.{docs_release_meta_step_id}.outputs.{docs_release_tag_output} }}}}",
                ".github/workflows/docs.yml",
                errors,
            )
        require_contains(
            docs_workflow,
            f'echo "{docs_release_tag_output}=${{{docs_release_tag_env}}}" >> "$GITHUB_OUTPUT"',
            ".github/workflows/docs.yml",
            errors,
        )
        require_contains(
            docs_workflow,
            f"${{{{ needs.download-firmware.outputs.{docs_release_tag_output} }}}}",
            ".github/workflows/docs.yml",
            errors,
        )
    if docs_prerelease_tag_env:
        prerelease_tag_ref = f"${docs_prerelease_tag_env}"
        for needle in (
            f"{docs_prerelease_tag_env}=$(gh release list",
            f'if [ -n "{prerelease_tag_ref}" ]; then',
            f'gh release download "{prerelease_tag_ref}"',
        ):
            require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)
    check_workflow_job_timeout_usage(
        firmware_compile_timeout,
        {
            "compile": (".github/workflows/compile.yml", compile_workflow),
            "release": (".github/workflows/release.yml", release_workflow),
        },
        errors,
    )
    check_workflow_release_build_fail_fast(
        release_build_fail_fast,
        {"release": (".github/workflows/release.yml", release_workflow)},
        errors,
    )
    docs_release_lookup_needles = [
        "gh release view --json tagName",
        "python3 scripts/firmware_release.py verify-directory",
        "python3 scripts/firmware_release.py verify-pages",
        '--base-url "$PUBLIC_BASE_URL"',
    ]
    if isinstance(prerelease_lookup_limit, int) and not isinstance(prerelease_lookup_limit, bool):
        docs_release_lookup_needles.append(f"gh release list --limit {prerelease_lookup_limit} --json tagName,isPrerelease")
    for needle in docs_release_lookup_needles:
        require_contains(docs_workflow, needle, ".github/workflows/docs.yml", errors)

    try:
        release_devices = release_matrix_devices(product)
    except RuntimeError as exc:
        errors.append(str(exc))
        return

    devices_by_slug = {str(device.get("slug", "")).strip(): device for device in product["devices"]}
    require_contains(
        release_workflow,
        "release_matrix: ${{ steps.product.outputs.release_matrix }}",
        ".github/workflows/release.yml",
        errors,
    )
    require_contains(
        release_workflow,
        "matrix: ${{ fromJson(needs.release-metadata.outputs.release_matrix) }}",
        ".github/workflows/release.yml",
        errors,
    )
    for release_device in release_devices:
        slug = release_device["slug"]
        build_yaml = str(devices_by_slug.get(slug, {}).get("build_yaml", "")).strip()
        local_yaml = str(devices_by_slug.get(slug, {}).get("local_yaml", "")).strip()
        device_dir = str(Path(local_yaml).parent) if local_yaml else ""
        if esphome_config_mount:
            require_contains(
                release_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/release.yml",
                errors,
            )
            require_contains(
                compile_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
                ".github/workflows/compile.yml",
                errors,
            )
            require_contains(
                compile_workflow,
                'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
                ".github/workflows/compile.yml",
                errors,
            )
        if device_dir:
            require_contains(
                compile_workflow,
                f'"{device_dir}/**"',
                ".github/workflows/compile.yml",
                errors,
            )
        public_manifest_dirs = []
        for field in ("public_manifest", "public_beta_manifest"):
            public_manifest = str(devices_by_slug.get(slug, {}).get(field, "")).strip()
            if public_manifest:
                public_manifest_dirs.append(Path(public_manifest).parent.as_posix())
        for prefix in dict.fromkeys(public_manifest_dirs):
            env_name = "DEFAULT_PUBLIC_BETA_MANIFEST" if prefix.endswith("/beta") else "DEFAULT_PUBLIC_MANIFEST"
            dir_name = "BETA_MANIFEST_DIR" if prefix.endswith("/beta") else "STABLE_MANIFEST_DIR"
            require_contains(
                docs_workflow,
                f'{dir_name}=$(dirname "${env_name}")',
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f'if [ -f "${{{dir_name}}}/${{DEFAULT_DEVICE_SLUG}}.manifest.json" ]; then',
                ".github/workflows/docs.yml",
                errors,
            )
            require_contains(
                docs_workflow,
                f'cp "${{{dir_name}}}/${{DEFAULT_DEVICE_SLUG}}.manifest.json" "${env_name}"',
                ".github/workflows/docs.yml",
                errors,
            )


def check_esphome_version(product: dict, errors: list[str]) -> None:
    project = product["project"]
    version = str(project.get("esphome_version", "")).strip()
    docker_image = str(project.get("esphome_docker_image", "")).strip().rstrip(":")
    config_mount = str(project.get("esphome_config_mount", "")).strip()
    remove_container = project.get("esphome_docker_remove_container")
    if not version:
        errors.append("project.esphome_version is required")
        return

    required_refs = [
        ROOT / "README.md",
        ROOT / "docs" / "install.md",
        ROOT / "docs" / "manual-setup.md",
    ]
    for path in required_refs:
        text = read(path, errors)
        require_contains(text, version, rel(path), errors)

    readme = read(ROOT / "README.md", errors)
    if docker_image:
        require_contains(readme, f"{docker_image}:{version}", "README.md", errors)

    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(
        compile_workflow,
        'python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"',
        ".github/workflows/compile.yml",
        errors,
    )
    require_contains(
        compile_workflow,
        '"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"',
        ".github/workflows/compile.yml",
        errors,
    )

    if config_mount:
        require_contains(compile_workflow, '-v "${PWD}:${ESPHOME_CONFIG_MOUNT}"', ".github/workflows/compile.yml", errors)
        require_contains(readme, f'-v "${{PWD}}:{config_mount}"', "README.md", errors)
    if remove_container is True:
        require_contains(compile_workflow, "docker run ${ESPHOME_DOCKER_REMOVE_FLAG}", ".github/workflows/compile.yml", errors)
        require_contains(readme, "docker run --rm", "README.md", errors)

    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    for needle in (
        '"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"',
        '-v "${PWD}:${ESPHOME_CONFIG_MOUNT}"',
        "docker run ${ESPHOME_DOCKER_REMOVE_FLAG}",
    ):
        require_contains(release_workflow, needle, ".github/workflows/release.yml", errors)


def check_workflows(product: dict, errors: list[str]) -> None:
    project = product["project"]
    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    require_contains(compile_workflow, '"product/**"', ".github/workflows/compile.yml", errors)

    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    release_workflow = read(ROOT / ".github" / "workflows" / "release.yml", errors)
    default_branch = str(project.get("github_default_branch", "")).strip()
    check_workflow_default_branch(default_branch, ".github/workflows/docs.yml", docs_workflow, errors)
    workflow_path_filters = project.get("github_workflow_path_filters", {})
    check_workflow_path_filters(
        workflow_path_filters,
        {
            ".github/workflows/compile.yml": compile_workflow,
            ".github/workflows/docs.yml": docs_workflow,
        },
        errors,
    )
    workflow_texts = {
        "compile": (".github/workflows/compile.yml", compile_workflow),
        "docs": (".github/workflows/docs.yml", docs_workflow),
        "release": (".github/workflows/release.yml", release_workflow),
    }
    workflow_events = project.get("github_workflow_events", {})
    check_workflow_events(workflow_events, workflow_texts, errors)
    node24_env = str(project.get("github_actions_node24_env", "")).strip()
    if str(project.get("node_version", "")).strip() == "24":
        check_workflow_top_level_env(node24_env, "true", workflow_texts, errors)
    workflow_event_types = project.get("github_workflow_event_types", {})
    check_workflow_event_type_usage(workflow_event_types, workflow_texts, errors)
    workflow_jobs = project.get("github_workflow_jobs", {})
    check_workflow_jobs(workflow_jobs, workflow_texts, errors)
    actions_runner = str(project.get("github_actions_runner", "")).strip()
    check_workflow_job_runner_usage(actions_runner, workflow_texts, errors)
    workflow_job_dependencies = project.get("github_workflow_job_dependencies", {})
    check_workflow_job_dependency_usage(workflow_job_dependencies, workflow_texts, errors)
    workflow_job_conditions = project.get("github_workflow_job_conditions", {})
    if isinstance(workflow_job_conditions, dict):
        for key, raw_condition in workflow_job_conditions.items():
            workflow_name, _, job_id = str(key).strip().partition(".")
            if workflow_name not in workflow_texts or not job_id:
                continue
            label, text = workflow_texts[workflow_name]
            job_block = workflow_job_block(text, job_id, label, errors)
            if not job_block:
                continue
            expected_condition = normalize_workflow_condition(str(raw_condition)) if raw_condition is not None else ""
            actual_condition = workflow_job_condition(job_block)
            if expected_condition and actual_condition != expected_condition:
                errors.append(
                    f"{label} job {job_id} if condition must be {expected_condition!r}, found {actual_condition!r}"
                )
            elif not expected_condition and actual_condition:
                errors.append(f"{label} job {job_id} must not define an if condition")
    workflow_permissions = project.get("github_workflow_permissions", {})
    workflow_names = project.get("github_workflow_names", {})
    if isinstance(workflow_names, dict):
        check_workflow_names(workflow_names, workflow_texts, errors)
        release_workflow_name = str(workflow_names.get("release", "")).strip()
        if release_workflow_name:
            check_workflow_run_targets(
                [release_workflow_name],
                ".github/workflows/docs.yml",
                docs_workflow,
                errors,
            )
    check_workflow_permissions(workflow_permissions, workflow_texts, errors)
    for label, text in (
        (".github/workflows/docs.yml", docs_workflow),
        (".github/workflows/release.yml", release_workflow),
    ):
        require_contains(text, "scripts/product_config.py", label, errors)
        require_contains(text, "product/espframe.json", label, errors)

    pull_request_template_path = check_relative_path(
        project.get("github_pull_request_template_path"),
        "project.github_pull_request_template_path",
        errors,
    )
    if pull_request_template_path:
        label = pull_request_template_path
        template = read(ROOT / pull_request_template_path, errors)
        for heading in ("## Automated checks", "## Device testing", "## Notes for reviewers"):
            require_contains(template, heading, label, errors)
        local_check_command = str(project.get("local_check_command", "")).strip()
        if local_check_command:
            require_contains(template, f"`{local_check_command}`", label, errors)
        compile_workflow_name = str(workflow_names.get("compile", "")).strip() if isinstance(workflow_names, dict) else ""
        if compile_workflow_name:
            require_contains(template, compile_workflow_name, label, errors)
        compile_artifact_prefix = str(project.get("compile_firmware_artifact_prefix", "")).strip()
        if compile_artifact_prefix:
            require_contains(template, compile_artifact_prefix, label, errors)
        for needle in ("workflow run/artifact", "Firmware artifact", "Device tested", "Result/notes"):
            require_contains(template, needle, label, errors)
        device_testing_options = project.get("github_pull_request_device_testing_options", [])
        if isinstance(device_testing_options, list):
            for option in device_testing_options:
                option_text = str(option).strip()
                if option_text:
                    require_contains(template, f"- [ ] {option_text}", label, errors)


def check_node_version(product: dict, errors: list[str]) -> None:
    project = product["project"]
    version = str(project.get("node_version", "")).strip()
    package_cache = str(project.get("node_package_cache", "")).strip()
    install_command = str(project.get("node_install_command", "")).strip()
    local_check_command = str(project.get("local_check_command", "")).strip()
    docs_build_command = str(project.get("docs_build_command", "")).strip()
    node24_env = str(project.get("github_actions_node24_env", "")).strip()
    release_actions = project.get("release_workflow_actions", {})
    setup_node_action = (
        str(release_actions.get("setup_node", "")).strip() if isinstance(release_actions, dict) else ""
    )
    if not version:
        errors.append("project.node_version is required")
        return
    if not re.match(r"^\d+$", version):
        errors.append("project.node_version must be a major version number")
    if version == "24" and not node24_env:
        errors.append("project.github_actions_node24_env is required when project.node_version is 24")

    compile_workflow = read(ROOT / ".github" / "workflows" / "compile.yml", errors)
    docs_workflow = read(ROOT / ".github" / "workflows" / "docs.yml", errors)
    workflow_texts = {
        "compile": (".github/workflows/compile.yml", compile_workflow),
        "docs": (".github/workflows/docs.yml", docs_workflow),
    }
    setup_node_inputs = {"node-version": version}
    if package_cache:
        setup_node_inputs["cache"] = package_cache
    for target in ("compile.validate", "docs.build-docs"):
        check_workflow_action_step_inputs(
            target,
            setup_node_action,
            "node-version",
            version,
            setup_node_inputs,
            workflow_texts,
            errors,
        )
    if install_command:
        for target in ("compile.validate", "docs.build-docs"):
            check_workflow_job_run_command(target, install_command, workflow_texts, errors)
    if local_check_command:
        check_workflow_job_run_command("compile.validate", local_check_command, workflow_texts, errors)
    if docs_build_command:
        check_workflow_job_run_command("docs.build-docs", docs_build_command, workflow_texts, errors)
