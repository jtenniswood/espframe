#!/usr/bin/env python3
"""Fast tests for GitHub workflow contract parsing."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.workflows import (  # noqa: E402
    check_workflow_action_usage,
    check_workflow_event_type_usage,
    check_workflow_events,
    check_workflow_job_dependency_usage,
    check_workflow_job_runner_usage,
    check_workflow_jobs,
    check_workflow_names,
    check_workflow_permissions,
    check_workflow_path_filters,
    normalize_workflow_condition,
    workflow_display_name,
    workflow_event_names,
    workflow_event_path_filters,
    workflow_event_type_filters,
    workflow_job_block,
    workflow_job_condition,
    workflow_job_display_name,
    workflow_job_ids,
    workflow_job_needs,
    workflow_job_runs_on,
    workflow_permissions,
)
from product_contract.project_release_metadata import (  # noqa: E402
    check_release_workflow_actions,
    check_workflow_event_types,
    check_workflow_job_dependencies,
    workflow_event_index,
    workflow_job_index,
)


WORKFLOW = """\
name: Example

jobs:
  direct:
    name: Direct Condition
    if: github.event_name == 'release'
    runs-on: ubuntu-latest

  folded:
    name: Folded Condition
    if: >-
      github.event_name != 'workflow_run' ||
      github.event.workflow_run.conclusion == 'success'
    runs-on: ubuntu-latest

  literal:
    name: Literal Condition
    if: |
      always() &&
      !cancelled()
    runs-on: ubuntu-latest

  unconditional:
    name: No Condition
    runs-on: ubuntu-latest
"""


PATH_WORKFLOW = """\
name: Example

on:
  pull_request:
    paths:
      - "components/**"
      - 'docs/**'
      - scripts/**
  workflow_dispatch:
"""


EVENT_WORKFLOW = """\
name: Example

on:
  pull_request:
    paths:
      - "components/**"
  workflow_dispatch:

permissions:
  contents: read
"""


EVENT_TYPE_WORKFLOW = """\
name: Example

on:
  workflow_run:
    workflows: ["Build Release"]
    types: [completed, requested]
  release:
    types:
      - published
      - prereleased
  workflow_dispatch:
"""


NEEDS_WORKFLOW = """\
name: Example

jobs:
  scalar:
    name: Scalar Dependency
    needs: setup
    runs-on: ubuntu-latest

  inline:
    name: Inline Dependencies
    needs: [build, test]
    runs-on: ubuntu-latest

  block:
    name: Block Dependencies
    needs:
      - build
      - test
    runs-on: ubuntu-latest

  untracked:
    name: Untracked Dependency
    needs: scalar
    runs-on: ubuntu-latest
"""


JOB_NAME_WORKFLOW = """\
name: Example

jobs:
  matching:
    name: "Expected Name"
    runs-on: ubuntu-latest

  shadowed:
    name: Actual Job Name
    runs-on: ubuntu-latest
    steps:
      - name: Expected Step Name
        run: echo ok

  missing-name:
    runs-on: ubuntu-latest
"""


RUNNER_WORKFLOW = """\
name: Example

jobs:
  right:
    name: Expected Runner
    runs-on: ubuntu-latest

  quoted:
    name: Quoted Runner
    runs-on: "ubuntu-latest"

  wrong:
    name: Wrong Runner
    runs-on: macos-latest

  missing:
    name: Missing Runner
"""


PERMISSION_WORKFLOW = """\
name: Example

on:
  workflow_dispatch:

permissions:
  contents: read
  pages: write
  id-token: write

jobs:
  build:
    runs-on: ubuntu-latest
"""


def test_workflow_job_block_finds_exact_job() -> None:
    errors: list[str] = []
    block = workflow_job_block(WORKFLOW, "folded", "example.yml", errors)
    assert errors == []
    assert "Folded Condition" in block
    assert "Literal Condition" not in block


def test_workflow_job_block_reports_missing_job() -> None:
    errors: list[str] = []
    assert workflow_job_block(WORKFLOW, "missing", "example.yml", errors) == ""
    assert errors == ["example.yml is missing job missing"]


def test_workflow_job_ids_reads_top_level_jobs() -> None:
    assert workflow_job_ids(WORKFLOW) == ["direct", "folded", "literal", "unconditional"]
    assert workflow_job_ids("name: Missing Jobs\n") == []


def test_workflow_jobs_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_jobs(
        {
            "compile": {
                "direct": "Direct Condition",
                "missing": "Missing Job",
                "folded": "Wrong Name",
            }
        },
        {"compile": ("example.yml", WORKFLOW)},
        errors,
    )

    assert errors == [
        "example.yml jobs are missing product metadata jobs: missing",
        "example.yml jobs contain jobs missing from product metadata: literal, unconditional",
        "example.yml is missing job missing",
        "example.yml job folded name must be 'Wrong Name', found 'Folded Condition'",
    ]


def test_workflow_job_display_name_reads_job_name() -> None:
    errors: list[str] = []
    assert workflow_job_display_name(
        workflow_job_block(JOB_NAME_WORKFLOW, "matching", "example.yml", errors)
    ) == "Expected Name"
    assert workflow_job_display_name(
        workflow_job_block(JOB_NAME_WORKFLOW, "missing-name", "example.yml", errors)
    ) == ""
    assert errors == []


def test_workflow_jobs_match_job_name_not_step_name() -> None:
    errors: list[str] = []
    check_workflow_jobs(
        {
            "compile": {
                "matching": "Expected Name",
                "shadowed": "Expected Step Name",
                "missing-name": "Missing Name",
            }
        },
        {"compile": ("example.yml", JOB_NAME_WORKFLOW)},
        errors,
    )

    assert errors == [
        "example.yml job shadowed name must be 'Expected Step Name', found 'Actual Job Name'",
        "example.yml job missing-name is missing name",
    ]


def test_workflow_job_needs_reads_supported_forms() -> None:
    errors: list[str] = []
    assert workflow_job_needs(workflow_job_block(NEEDS_WORKFLOW, "scalar", "example.yml", errors)) == ["setup"]
    assert workflow_job_needs(workflow_job_block(NEEDS_WORKFLOW, "inline", "example.yml", errors)) == ["build", "test"]
    assert workflow_job_needs(workflow_job_block(NEEDS_WORKFLOW, "block", "example.yml", errors)) == ["build", "test"]
    assert errors == []


def test_workflow_job_runs_on_reads_job_runner() -> None:
    errors: list[str] = []
    assert workflow_job_runs_on(
        workflow_job_block(RUNNER_WORKFLOW, "right", "example.yml", errors)
    ) == "ubuntu-latest"
    assert workflow_job_runs_on(
        workflow_job_block(RUNNER_WORKFLOW, "quoted", "example.yml", errors)
    ) == "ubuntu-latest"
    assert workflow_job_runs_on(
        workflow_job_block(RUNNER_WORKFLOW, "missing", "example.yml", errors)
    ) == ""
    assert errors == []


def test_workflow_job_runner_usage_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_runner_usage(
        "ubuntu-latest",
        {"compile": ("example.yml", RUNNER_WORKFLOW)},
        errors,
    )

    assert errors == [
        "example.yml job wrong runs-on must be 'ubuntu-latest', found 'macos-latest'",
        "example.yml job missing is missing runs-on",
    ]


def test_workflow_job_dependency_usage_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_dependency_usage(
        {
            "compile.scalar": ["setup"],
            "compile.inline": ["build", "missing"],
        },
        {"compile": ("example.yml", NEEDS_WORKFLOW)},
        errors,
    )

    assert errors == [
        "example.yml job inline needs are missing dependencies: missing",
        "example.yml job inline needs contain dependencies missing from product metadata: test",
        "example.yml job block needs are missing from product metadata: build, test",
        "example.yml job untracked needs are missing from product metadata: scalar",
    ]


def test_workflow_permissions_reads_top_level_permissions() -> None:
    assert workflow_permissions(PERMISSION_WORKFLOW) == {
        "contents": "read",
        "pages": "write",
        "id-token": "write",
    }
    assert workflow_permissions("name: Missing Permissions\n") == {}


def test_workflow_permissions_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_permissions(
        {
            "docs": {
                "contents": "read",
                "pages": "read",
                "actions": "read",
            }
        },
        {"docs": ("docs.yml", PERMISSION_WORKFLOW)},
        errors,
    )

    assert errors == [
        "docs.yml permissions are missing product metadata scopes: actions",
        "docs.yml permissions contain scopes missing from product metadata: id-token",
        "docs.yml permissions.pages must be 'read', found 'write'",
    ]


def test_workflow_display_name_reads_top_level_name() -> None:
    assert workflow_display_name('name: "Compile Check"\n') == "Compile Check"
    assert workflow_display_name("name: Deploy Docs\n") == "Deploy Docs"
    assert workflow_display_name("jobs:\n") == ""


def test_workflow_names_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_names(
        {
            "compile": "Compile Check",
            "docs": "Deploy Docs",
            "release": "Build Release",
        },
        {
            "compile": (".github/workflows/compile.yml", 'name: "Compile Check"\n'),
            "docs": (".github/workflows/docs.yml", "name: Wrong Docs\n"),
            "release": (".github/workflows/release.yml", "on:\n  workflow_dispatch:\n"),
        },
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml name must be 'Deploy Docs', found 'Wrong Docs'",
        ".github/workflows/release.yml is missing top-level workflow name",
    ]


def test_workflow_job_condition_handles_supported_forms() -> None:
    errors: list[str] = []
    assert workflow_job_condition(workflow_job_block(WORKFLOW, "direct", "example.yml", errors)) == (
        "github.event_name == 'release'"
    )
    assert workflow_job_condition(workflow_job_block(WORKFLOW, "folded", "example.yml", errors)) == (
        "github.event_name != 'workflow_run' || github.event.workflow_run.conclusion == 'success'"
    )
    assert workflow_job_condition(workflow_job_block(WORKFLOW, "literal", "example.yml", errors)) == (
        "always() && !cancelled()"
    )
    assert workflow_job_condition(workflow_job_block(WORKFLOW, "unconditional", "example.yml", errors)) == ""
    assert errors == []


def test_normalize_workflow_condition_collapses_whitespace() -> None:
    assert normalize_workflow_condition("  one\n  two\tthree  ") == "one two three"


def test_release_workflow_actions_require_expected_keys() -> None:
    errors: list[str] = []
    check_release_workflow_actions(
        {
            "checkout": "actions/checkout@v7",
            "cahce": "actions/cache@v6",
            "upload_artifact": "actions/upload-artifact@v7",
            "download_artifact": "actions/download-artifact@v8",
            "setup_node": "actions/setup-node@v6",
            "upload_pages_artifact": "actions/upload-pages-artifact@v5",
            "deploy_pages": "actions/deploy-pages@v5",
            "": "actions/cache@v6",
        },
        errors,
    )

    assert errors == [
        "project.release_workflow_actions is missing actions: cache",
        "project.release_workflow_actions contains unknown actions: cahce",
        "project.release_workflow_actions keys must be non-empty strings",
    ]


def test_workflow_action_usage_checks_expected_workflows() -> None:
    errors: list[str] = []
    release_actions = {
        "checkout": "actions/checkout@v7",
        "cache": "actions/cache@v6",
        "upload_artifact": "actions/upload-artifact@v7",
        "download_artifact": "actions/download-artifact@v8",
        "setup_node": "actions/setup-node@v6",
        "upload_pages_artifact": "actions/upload-pages-artifact@v5",
        "deploy_pages": "actions/deploy-pages@v5",
    }
    workflow_texts = {
        ".github/workflows/release.yml": "\n".join(
            [
                "actions/checkout@v7",
                "actions/cache@v6",
                "actions/upload-artifact@v7",
                "actions/download-artifact@v8",
            ]
        ),
        ".github/workflows/docs.yml": "\n".join(
            [
                "actions/checkout@v7",
                "actions/upload-artifact@v7",
                "actions/download-artifact@v8",
                "actions/setup-node@v6",
                "actions/upload-pages-artifact@v5",
            ]
        ),
        ".github/workflows/compile.yml": "\n".join(
            [
                "actions/checkout@v7",
                "actions/cache@v6",
                "actions/upload-artifact@v7",
            ]
        ),
    }

    check_workflow_action_usage(release_actions, workflow_texts, errors)

    assert errors == [
        ".github/workflows/docs.yml is missing 'actions/deploy-pages@v5'",
        ".github/workflows/compile.yml is missing 'actions/setup-node@v6'",
    ]


def test_workflow_event_names_reads_on_block() -> None:
    assert workflow_event_names(EVENT_WORKFLOW) == ["pull_request", "workflow_dispatch"]
    assert workflow_event_names("name: Missing Events\n") == []


def test_workflow_events_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_events(
        {
            "compile": ["pull_request"],
            "docs": ["push", "workflow_run"],
        },
        {
            ".github/workflows/ignored.yml": ("ignored", ""),
            "compile": (".github/workflows/compile.yml", EVENT_WORKFLOW),
            "docs": (
                ".github/workflows/docs.yml",
                """\
name: Docs

on:
  push:
  workflow_dispatch:
""",
            ),
        },
        errors,
    )

    assert errors == [
        ".github/workflows/compile.yml events contain triggers missing from product metadata: workflow_dispatch",
        ".github/workflows/docs.yml events are missing product metadata events: workflow_run",
        ".github/workflows/docs.yml events contain triggers missing from product metadata: workflow_dispatch",
    ]


def test_workflow_event_type_filters_reads_inline_and_block_lists() -> None:
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "workflow_run") == ["completed", "requested"]
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "release") == ["published", "prereleased"]
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "workflow_dispatch") == []
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "missing") == []


def test_workflow_event_type_usage_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_event_type_usage(
        {
            "docs.workflow_run": ["completed", "missing"],
            "release.release": ["published"],
        },
        {
            "docs": (".github/workflows/docs.yml", EVENT_TYPE_WORKFLOW),
            "release": (".github/workflows/release.yml", EVENT_TYPE_WORKFLOW),
        },
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml workflow_run types are missing product metadata types: missing",
        ".github/workflows/docs.yml workflow_run types contain values missing from product metadata: requested",
        ".github/workflows/release.yml release types contain values missing from product metadata: prereleased",
        ".github/workflows/docs.yml release types are missing from product metadata: published, prereleased",
        ".github/workflows/release.yml workflow_run types are missing from product metadata: completed, requested",
    ]


def test_workflow_event_path_filters_reads_quoted_paths() -> None:
    assert workflow_event_path_filters(PATH_WORKFLOW, "pull_request") == [
        "components/**",
        "docs/**",
        "scripts/**",
    ]
    assert workflow_event_path_filters(PATH_WORKFLOW, "workflow_dispatch") == []
    assert workflow_event_path_filters(PATH_WORKFLOW, "missing") == []


def test_workflow_path_filters_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {
        ".github/workflows/compile.yml": """\
on:
  pull_request:
    paths:
      - "components/**"
      - "tests/**"
  workflow_dispatch:
""",
        ".github/workflows/docs.yml": """\
on:
  push:
    branches: [main]
    paths:
      - 'docs/**'
  workflow_dispatch:
""",
    }

    check_workflow_path_filters(
        {
            "compile_pull_request": ["components/**"],
            "docs_push": ["docs/**", "product/**"],
        },
        workflow_texts,
        errors,
    )

    assert errors == [
        ".github/workflows/compile.yml pull_request paths contain filters missing from product metadata: tests/**",
        ".github/workflows/docs.yml push paths are missing product filters: product/**",
    ]


def test_workflow_event_index_normalizes_declared_events() -> None:
    configured_events = workflow_event_index(
        {
            "compile": ["pull_request", " workflow_dispatch ", ""],
            "": ["ignored"],
            "invalid": {"not": "a list"},
        }
    )

    assert configured_events == {"compile.pull_request", "compile.workflow_dispatch"}


def test_workflow_event_types_reject_unknown_events() -> None:
    errors: list[str] = []
    check_workflow_event_types(
        {
            "docs.workflow_run": ["completed"],
            "docs.pull_request": ["opened"],
            "missing": ["completed"],
            "release.release": [],
        },
        {"docs.workflow_run"},
        errors,
    )

    assert errors == [
        "project.github_workflow_event_types.docs.pull_request must point at a known workflow event",
        "project.github_workflow_event_types.missing must use workflow.event format",
        "project.github_workflow_event_types.release.release must point at a known workflow event",
        "project.github_workflow_event_types.release.release must be a non-empty list",
    ]


def test_workflow_job_index_normalizes_declared_jobs() -> None:
    configured_jobs, jobs_by_workflow = workflow_job_index(
        {
            "compile": {
                "validate": "Validate",
                " compile ": "Compile",
            },
            "": {"ignored": "Ignored"},
            "invalid": ["not", "a", "mapping"],
        }
    )

    assert configured_jobs == {"compile.validate", "compile.compile"}
    assert jobs_by_workflow == {"compile": {"validate", "compile"}}


def test_workflow_job_dependencies_reject_unknown_jobs() -> None:
    errors: list[str] = []
    check_workflow_job_dependencies(
        {
            "compile.compile": ["validate", "missing"],
            "compile.unknown": ["validate"],
            "release.publish": ["build-firmware"],
        },
        {"compile.validate", "compile.compile"},
        {"compile": {"validate", "compile"}},
        errors,
    )

    assert errors == [
        "project.github_workflow_job_dependencies.compile.compile references unknown job: missing",
        "project.github_workflow_job_dependencies.compile.unknown must point at a known workflow job",
        "project.github_workflow_job_dependencies.release.publish must point at a known workflow job",
    ]


def main() -> int:
    test_workflow_job_block_finds_exact_job()
    test_workflow_job_block_reports_missing_job()
    test_workflow_job_ids_reads_top_level_jobs()
    test_workflow_jobs_reject_drift_from_product_metadata()
    test_workflow_job_display_name_reads_job_name()
    test_workflow_jobs_match_job_name_not_step_name()
    test_workflow_job_needs_reads_supported_forms()
    test_workflow_job_runs_on_reads_job_runner()
    test_workflow_job_runner_usage_rejects_drift_from_product_metadata()
    test_workflow_job_dependency_usage_rejects_drift_from_product_metadata()
    test_workflow_permissions_reads_top_level_permissions()
    test_workflow_permissions_reject_drift_from_product_metadata()
    test_workflow_display_name_reads_top_level_name()
    test_workflow_names_reject_drift_from_product_metadata()
    test_workflow_job_condition_handles_supported_forms()
    test_normalize_workflow_condition_collapses_whitespace()
    test_release_workflow_actions_require_expected_keys()
    test_workflow_action_usage_checks_expected_workflows()
    test_workflow_event_names_reads_on_block()
    test_workflow_events_reject_drift_from_product_metadata()
    test_workflow_event_type_filters_reads_inline_and_block_lists()
    test_workflow_event_type_usage_rejects_drift_from_product_metadata()
    test_workflow_event_path_filters_reads_quoted_paths()
    test_workflow_path_filters_reject_drift_from_product_metadata()
    test_workflow_event_index_normalizes_declared_events()
    test_workflow_event_types_reject_unknown_events()
    test_workflow_job_index_normalizes_declared_jobs()
    test_workflow_job_dependencies_reject_unknown_jobs()
    print("workflow contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
