#!/usr/bin/env python3
"""Fast tests for GitHub workflow contract parsing."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.workflows import (  # noqa: E402
    append_list_drift_errors,
    append_mapping_value_errors,
    check_workflow_action_step_inputs,
    check_workflow_action_usage,
    check_workflow_event_type_usage,
    check_workflow_events,
    check_workflow_concurrency,
    check_workflow_gh_cli_env,
    check_workflow_job_dependency_usage,
    check_workflow_job_env,
    check_workflow_job_environment,
    check_workflow_named_step_contains,
    check_workflow_named_step_env,
    check_workflow_named_step_id,
    check_workflow_named_step_run_contains,
    check_workflow_named_step_uses,
    check_workflow_named_step_with,
    check_workflow_job_outputs,
    check_workflow_job_runner_usage,
    check_workflow_job_run_command,
    check_workflow_job_strategy_matrix,
    check_workflow_job_timeout_usage,
    check_workflow_jobs,
    check_workflow_names,
    check_workflow_permissions,
    check_workflow_path_filters,
    check_workflow_default_branch,
    check_workflow_run_targets,
    check_workflow_release_build_fail_fast,
    check_workflow_sparse_checkout_usage,
    check_workflow_top_level_env,
    normalize_workflow_condition,
    workflow_display_name,
    workflow_action_references,
    workflow_concurrency,
    workflow_env,
    workflow_event_branch_filters,
    workflow_event_names,
    workflow_event_path_filters,
    workflow_event_type_filters,
    workflow_event_workflow_filters,
    workflow_job_block,
    workflow_job_condition,
    workflow_job_display_name,
    workflow_job_env,
    workflow_job_ids,
    workflow_job_needs,
    workflow_job_outputs,
    workflow_job_step_blocks,
    workflow_job_strategy_fail_fast,
    workflow_job_strategy_matrix,
    workflow_job_runs_on,
    workflow_job_timeout_minutes,
    workflow_permissions,
    workflow_sparse_checkout_blocks,
    workflow_sparse_checkout_entries,
    workflow_step_display_name,
    workflow_step_env,
    workflow_step_run,
    workflow_step_uses,
    workflow_step_uses_gh_cli,
    workflow_step_with,
)
from product_contract.project_release_metadata import (  # noqa: E402
    check_release_workflow_actions,
    check_workflow_event_types,
    check_workflow_job_dependencies,
    workflow_event_index,
    workflow_job_index,
)
from script_test_discovery import discovered_test_functions, run_discovered_tests  # noqa: E402


def test_append_list_drift_errors_reports_ordered_missing_and_extra_values() -> None:
    errors: list[str] = []

    append_list_drift_errors(
        ["build", "test", "release"],
        ["test", "build", "deploy"],
        "workflow is missing expected values",
        "workflow contains unexpected values",
        errors,
    )

    assert errors == [
        "workflow is missing expected values: release",
        "workflow contains unexpected values: deploy",
    ]


def test_append_mapping_value_errors_reports_missing_and_changed_values() -> None:
    errors: list[str] = []

    append_mapping_value_errors(
        {"GH_TOKEN": "${{ github.token }}", "GH_REPO": "${{ github.repository }}"},
        {"GH_TOKEN": "${{ secrets.BAD_TOKEN }}"},
        "release.yml job release step 'Publish' env",
        "release.yml job release step 'Publish' env",
        errors,
    )

    assert errors == [
        (
            "release.yml job release step 'Publish' env.GH_TOKEN must be "
            "'${{ github.token }}', found '${{ secrets.BAD_TOKEN }}'"
        ),
        "release.yml job release step 'Publish' env is missing GH_REPO",
    ]


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


BRANCH_WORKFLOW = """\
name: Example

on:
  push:
    branches:
      - main
      - "release"
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


WORKFLOW_RUN_TARGET_WORKFLOW = """\
name: Example

on:
  workflow_run:
    workflows:
      - Build Release
      - "Compile Check"
    types: [completed]
  workflow_dispatch:
"""


SPARSE_CHECKOUT_WORKFLOW = """\
name: Example

jobs:
  metadata:
    steps:
      - uses: actions/checkout@v7
        with:
          sparse-checkout: |
            scripts/product_config.py
            product/espframe.json
          sparse-checkout-cone-mode: false

  release:
    steps:
      - uses: actions/checkout@v7
        with:
          sparse-checkout: |
            scripts/firmware_release.py
            scripts/product_config.py
            product/espframe.json
          sparse-checkout-cone-mode: false
"""


SPARSE_CHECKOUT_CONE_DRIFT_WORKFLOW = """\
name: Example

jobs:
  metadata:
    steps:
      - uses: actions/checkout@v7
        with:
          sparse-checkout: |
            scripts/product_config.py
            product/espframe.json
          sparse-checkout-cone-mode: true

  release:
    steps:
      - uses: actions/checkout@v7
        with:
          sparse-checkout: |
            scripts/firmware_release.py
            scripts/product_config.py
            product/espframe.json
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


TIMEOUT_WORKFLOW = """\
name: Example

jobs:
  compile:
    name: Compile Firmware
    timeout-minutes: 30
    runs-on: ubuntu-latest

  build-firmware:
    name: Build Firmware
    timeout-minutes: 45
    runs-on: ubuntu-latest

  missing-timeout:
    name: Missing Timeout
    runs-on: ubuntu-latest
"""


STRATEGY_WORKFLOW = """\
name: Example

jobs:
  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    strategy:
      fail-fast: false
      matrix: ${{ fromJson(needs.release-metadata.outputs.release_matrix) }}

  missing-strategy:
    name: Missing Strategy
    runs-on: ubuntu-latest

  missing-fail-fast:
    name: Missing Fail Fast
    runs-on: ubuntu-latest
    strategy:
      matrix: []
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


ENV_WORKFLOW = """\
name: Example

env:
  FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: true
  QUOTED_FLAG: "yes"

jobs:
  build:
    runs-on: ubuntu-latest
"""


CONCURRENCY_WORKFLOW = """\
name: Example

concurrency:
  group: pages
  cancel-in-progress: false

jobs:
  build:
    runs-on: ubuntu-latest
"""


GH_CLI_WORKFLOW = """\
name: Example

jobs:
  release:
    name: Release
    runs-on: ubuntu-latest
    steps:
      - name: Update release notes
        env:
          GH_TOKEN: ${{ github.token }}
          GH_REPO: ${{ github.repository }}
        run: gh release edit "$VERSION"

      - name: Missing repo
        env:
          GH_TOKEN: ${{ github.token }}
        run: |
          echo start
          gh release upload "$VERSION" firmware/*

      - name: Wrong token
        env:
          GH_TOKEN: ${{ secrets.BAD_TOKEN }}
          GH_REPO: ${{ github.repository }}
        run: >-
          gh release download "$VERSION"

      - name: Local shell
        run: echo done
"""


ACTION_INPUT_WORKFLOW = """\
name: Example

jobs:
  release-notes:
    name: Update Release Notes
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
        with:
          fetch-depth: 0
          fetch-tags: true

  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - uses: actions/checkout@v7
        with:
          ref: ${{ github.event.release.tag_name || github.ref }}

  build-docs:
    name: Build Docs
    runs-on: ubuntu-latest
    steps:
      - name: Upload docs artifact
        uses: actions/upload-artifact@v7
        with:
          name: docs-dist
          path: docs/.vitepress/dist

  deploy-docs:
    name: Deploy Docs
    runs-on: ubuntu-latest
    steps:
      - name: Download docs artifact
        uses: actions/download-artifact@v8
        with:
          name: docs-dist
          path: dist

      - uses: actions/upload-pages-artifact@v5
        with:
          path: dist

  publish:
    name: Publish
    runs-on: ubuntu-latest
    steps:
      - name: Download all firmware artifacts
        uses: actions/download-artifact@v8
        with:
          pattern: firmware-*
          merge-multiple: true
          path: firmware
"""


STEP_ENV_WORKFLOW = """\
name: Example

jobs:
  release-notes:
    name: Update Release Notes
    runs-on: ubuntu-latest
    steps:
      - name: Build detailed changelog
        env:
          VERSION: ${{ github.event.release.tag_name }}
        run: python3 scripts/release_changelog.py "$VERSION" --output "$RUNNER_TEMP/release-notes.md"

      - name: Update GitHub release notes
        env:
          GH_TOKEN: ${{ github.token }}
          GH_REPO: ${{ github.repository }}
          VERSION: ${{ github.event.release.tag_name }}
        run: gh release edit "$VERSION" --notes-file "$RUNNER_TEMP/wrong-notes.md"

  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile firmware
        env:
          VERSION: ${{ github.event.release.tag_name || github.ref_name }}
        run: echo compile
"""


RELEASE_BUILD_RUN_WORKFLOW = """\
name: Example

jobs:
  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile firmware
        run: |
          mkdir -p output
          if [ ! -f "${BUILD_DIR}/wrong.factory.bin" ]; then
            echo "::error::Firmware compilation failed - factory binary not found"
            exit 1
          fi
          sudo cp "${BUILD_DIR}/wrong.factory.bin" "output/${{ matrix.slug }}.factory.bin"

          docker run ${ESPHOME_DOCKER_REMOVE_FLAG} \
            -s firmware_version "${VERSION}" \
            compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"

      - name: Collect firmware files and generate manifest
        run: |
          mkdir -p output
          if [ ! -f "${BUILD_DIR}/firmware.bin" ]; then
            echo "::error::Firmware compilation failed - OTA binary not found"
            exit 1
          fi
          sudo cp "${BUILD_DIR}/firmware.bin" "output/${{ matrix.slug }}.ota.bin"

          python3 scripts/firmware_release.py manifest \
            --version "${VERSION}" \
            --ota "output/${{ matrix.slug }}.ota.bin" \
            --out "output/${{ matrix.slug }}.manifest.json"
"""


RELEASE_PUBLISH_RUN_WORKFLOW = """\
name: Example

jobs:
  publish:
    name: Publish to Release
    runs-on: ubuntu-latest
    steps:
      - name: Verify assembled release assets
        run: >-
          python3 scripts/firmware_release.py verify-directory
          --version "${VERSION}"
          --dir firmware
          --slugs $DEVICE_SLUGS

      - name: Upload firmware to release
        run: gh release upload "${VERSION}" wrong/* --clobber

      - name: Verify uploaded release assets
        run: |
          mkdir -p published
          gh release download "${VERSION}" \
            --pattern "*.manifest.json" \
            --pattern "*.ota.bin" \
            --dir published
          python3 scripts/firmware_release.py verify-directory \
            --version "${VERSION}" \
            --dir published \
            --slugs $DEVICE_SLUGS
"""


CACHE_WORKFLOW = """\
name: Example

jobs:
  compile:
    name: Compile Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Cache ESPHome build
        uses: actions/cache@v6
        with:
          path: ${{ needs.firmware-metadata.outputs.release_esphome_cache_dir }}
          key: ${{ needs.firmware-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-${{ hashFiles('builds/*.yaml', 'common/**') }}
          restore-keys: |
            ${{ needs.firmware-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-

      - name: Fix ESPHome cache permissions
        run: |
          if [ -d "${RELEASE_ESPHOME_CACHE_DIR}" ]; then
            sudo chown -R "$USER:$USER" "${RELEASE_ESPHOME_CACHE_DIR}"
            chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"
          fi

  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Cache ESPHome build
        uses: actions/cache@v5
        with:
          path: ${{ needs.release-metadata.outputs.wrong_cache_dir }}
          key: ${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-${{ hashFiles('builds/*.yaml', 'common/**') }}
          restore-keys: |
            ${{ needs.release-metadata.outputs.wrong_cache_prefix }}-${{ matrix.slug }}-

      - name: Fix ESPHome cache permissions
        run: |
          if [ -d "${RELEASE_ESPHOME_CACHE_DIR}" ]; then
            sudo chown -R "$USER:$USER" "${RELEASE_ESPHOME_CACHE_DIR}"
          fi
"""


COMPILE_ARTIFACT_RUN_WORKFLOW = """\
name: Example

jobs:
  compile:
    name: Compile Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile test firmware artifacts
        run: |
          TEST_VERSION="pr-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"
          mkdir -p output

          docker run ${ESPHOME_DOCKER_REMOVE_FLAG} \
            -s firmware_version "${TEST_VERSION}" \
            compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"

          sudo cp "${BUILD_DIR}/firmware.factory.bin" "output/${{ matrix.slug }}.factory.bin"
          sudo cp "${BUILD_DIR}/firmware.bin" "wrong/${{ matrix.slug }}.ota.bin"

          {
            echo "version=${TEST_VERSION}"
            echo "source_ref=${GITHUB_REF_NAME}"
          } > "output/${{ matrix.slug }}.build.txt"
"""


COMPILE_COMMAND_RUN_WORKFLOW = """\
name: Example

jobs:
  compile:
    name: Compile Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Unrelated compile note
        run: |
          echo 'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"'

      - name: Compile test firmware artifacts
        run: |
          docker run ${ESPHOME_DOCKER_REMOVE_FLAG} \
            compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"

  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile firmware
        run: |
          docker run ${ESPHOME_DOCKER_REMOVE_FLAG} \
            compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"
"""


ESPHOME_DOCKER_RUN_WORKFLOW = """\
name: Example

jobs:
  firmware-metadata:
    name: Read Firmware Build Metadata
    runs-on: ubuntu-latest
    steps:
      - name: Read product metadata
        run: python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"

  compile:
    name: Compile Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile test firmware artifacts
        run: |
          docker run ${ESPHOME_DOCKER_REMOVE_FLAG}
          -v "${PWD}:${ESPHOME_CONFIG_MOUNT}"

  release-metadata:
    name: Read Product Release Metadata
    runs-on: ubuntu-latest
    steps:
      - name: Read product metadata
        run: python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"

  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Compile firmware
        run: |
          docker run ${ESPHOME_DOCKER_REMOVE_FLAG}
          -v "${PWD}:${ESPHOME_CONFIG_MOUNT}"
          "${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"
"""


DOCS_DOWNLOAD_RUN_WORKFLOW = """\
name: Example

jobs:
  download-firmware:
    name: Download Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Download firmware from latest release
        run: |
          gh release download "$RELEASE_TAG" \
            --pattern "*.factory.bin" \
            --pattern "*.ota.bin" \
            --clobber
          for SLUG in $DEVICE_SLUGS; do
            gh release download "$RELEASE_TAG" \
              --pattern "${SLUG}.manifest.json" \
              --clobber
          done
          gh release download "$RELEASE_TAG" \
            --pattern "$(basename "$DEFAULT_PUBLIC_MANIFEST")" \
            --clobber

      - name: Download firmware from latest pre-release
        run: |
          gh release download "$BETA_TAG" \
            --pattern "*.ota.bin"
          for SLUG in $DEVICE_SLUGS; do
            gh release download "$BETA_TAG" \
              --pattern "${SLUG}.manifest.json"
          done
          gh release download "$BETA_TAG" \
            --pattern "$(basename "$DEFAULT_PUBLIC_MANIFEST")"
"""


DOCS_MANIFEST_DIR_WORKFLOW = """\
name: Example

jobs:
  download-firmware:
    name: Download Firmware
    runs-on: ubuntu-latest
    steps:
      - name: Download firmware from latest release
        run: |
          STABLE_MANIFEST_DIR=$(dirname "$DEFAULT_PUBLIC_MANIFEST")
          mkdir -p "$STABLE_MANIFEST_DIR"

      - name: Download firmware from latest pre-release
        run: |
          BETA_MANIFEST_DIR=$(dirname "$DEFAULT_PUBLIC_BETA_MANIFEST")
"""


DOCS_RELEASE_METADATA_WORKFLOW = """\
name: Example

jobs:
  download-firmware:
    name: Download Firmware
    runs-on: ubuntu-latest
    outputs:
      release_tag: ${{ steps.wrong-meta.outputs.release_tag }}
    steps:
      - name: Set release metadata
        id: release-meta
        run: |
          RELEASE_TAG=$(gh release view --json tagName -q .tagName)
          echo "RELEASE_TAG=${RELEASE_TAG}" >> "$GITHUB_ENV"
          echo "wrong_release_tag=${RELEASE_TAG}" >> "$GITHUB_OUTPUT"

      - name: Download firmware from latest release
        run: gh release download "$RELEASE_TAG"

      - name: Download firmware from latest pre-release
        run: |
          BETA_TAG=$(gh release list --limit 20 --json tagName,isPrerelease)
          if [ -n "$BETA_TAG" ]; then
            echo skip
          fi

      - name: Verify firmware assets
        run: python3 scripts/firmware_release.py verify-directory --version "$WRONG_TAG"

  deploy-docs:
    name: Deploy Docs
    runs-on: ubuntu-latest
    steps:
      - name: Verify public firmware
        run: >-
          python3 scripts/firmware_release.py verify-pages
          --version "${{ needs.download-firmware.outputs.wrong_tag }}"
          --retries 5
"""


DOCS_DEPLOY_WORKFLOW = """\
name: Example

jobs:
  deploy-docs:
    name: Deploy Docs
    runs-on: ubuntu-latest
    environment:
      name: github-pages
      url: ${{ steps.wrong-deployment.outputs.page_url }}
    steps:
      - name: Deploy to GitHub Pages
        id: wrong-deployment
        uses: actions/deploy-pages@v4
"""


RUN_COMMAND_WORKFLOW = """\
name: Example

jobs:
  validate:
    name: Validate
    runs-on: ubuntu-latest
    steps:
      - name: Install dependencies
        run: npm ci

      - name: Run PR checks
        run: npm run check:pr

  build-docs:
    name: Build Docs
    runs-on: ubuntu-latest
    steps:
      - name: Build docs
        run: npm run docs:build
"""


OUTPUT_WORKFLOW = """\
name: Example

jobs:
  metadata:
    name: Read Metadata
    runs-on: ubuntu-latest
    outputs:
      release_matrix: ${{ steps.product.outputs.release_matrix }}
      esphome_version: ${{ steps.product.outputs.esphome_version }}
      device_slugs: ${{ steps.product.outputs.device_slugs }}
    steps:
      - name: Read product metadata
        id: product
        run: python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"

  missing:
    name: Missing Outputs
    runs-on: ubuntu-latest
"""


JOB_ENV_WORKFLOW = """\
name: Example

jobs:
  compile:
    name: Compile Firmware
    runs-on: ubuntu-latest
    env:
      ESPHOME_DOCKER_IMAGE: ${{ needs.firmware-metadata.outputs.esphome_docker_image }}
      ESPHOME_VERSION: ${{ needs.firmware-metadata.outputs.esphome_version }}
      RELEASE_ESPHOME_CACHE_DIR: ${{ needs.firmware-metadata.outputs.release_esphome_cache_dir }}
    steps:
      - run: echo compile

  missing:
    name: Missing Env
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


def test_workflow_job_timeout_minutes_reads_job_timeout() -> None:
    errors: list[str] = []
    assert workflow_job_timeout_minutes(
        workflow_job_block(TIMEOUT_WORKFLOW, "compile", "example.yml", errors)
    ) == "30"
    assert workflow_job_timeout_minutes(
        workflow_job_block(TIMEOUT_WORKFLOW, "missing-timeout", "example.yml", errors)
    ) == ""
    assert errors == []


def test_workflow_job_timeout_usage_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_timeout_usage(
        30,
        {
            "compile": ("compile.yml", TIMEOUT_WORKFLOW),
            "release": (
                "release.yml",
                """\
jobs:
  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
""",
            ),
        },
        errors,
    )

    assert errors == [
        "release.yml job build-firmware is missing timeout-minutes",
    ]

    errors = []
    check_workflow_job_timeout_usage(
        30,
        {
            "compile": ("compile.yml", TIMEOUT_WORKFLOW),
            "release": ("release.yml", TIMEOUT_WORKFLOW),
        },
        errors,
    )

    assert errors == [
        "release.yml job build-firmware timeout-minutes must be '30', found '45'",
    ]


def test_workflow_job_strategy_fail_fast_reads_strategy_value() -> None:
    errors: list[str] = []
    assert workflow_job_strategy_fail_fast(
        workflow_job_block(STRATEGY_WORKFLOW, "build-firmware", "example.yml", errors)
    ) == "false"
    assert workflow_job_strategy_matrix(
        workflow_job_block(STRATEGY_WORKFLOW, "build-firmware", "example.yml", errors)
    ) == "${{ fromJson(needs.release-metadata.outputs.release_matrix) }}"
    assert workflow_job_strategy_fail_fast(
        workflow_job_block(STRATEGY_WORKFLOW, "missing-strategy", "example.yml", errors)
    ) == ""
    assert workflow_job_strategy_fail_fast(
        workflow_job_block(STRATEGY_WORKFLOW, "missing-fail-fast", "example.yml", errors)
    ) == ""
    assert errors == []


def test_workflow_release_build_fail_fast_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_release_build_fail_fast(
        True,
        {"release": ("release.yml", STRATEGY_WORKFLOW)},
        errors,
    )

    assert errors == [
        "release.yml job build-firmware strategy.fail-fast must be 'true', found 'false'",
    ]

    errors = []
    check_workflow_release_build_fail_fast(
        False,
        {
            "release": (
                "release.yml",
                """\
jobs:
  build-firmware:
    name: Build Firmware
    runs-on: ubuntu-latest
""",
            )
        },
        errors,
    )

    assert errors == [
        "release.yml job build-firmware strategy is missing fail-fast",
    ]


def test_workflow_job_strategy_matrix_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_strategy_matrix(
        "release.build-firmware",
        "${{ fromJson(needs.firmware-metadata.outputs.release_matrix) }}",
        {"release": ("release.yml", STRATEGY_WORKFLOW)},
        errors,
    )

    assert errors == [
        (
            "release.yml job build-firmware strategy.matrix must be "
            "'${{ fromJson(needs.firmware-metadata.outputs.release_matrix) }}', found "
            "'${{ fromJson(needs.release-metadata.outputs.release_matrix) }}'"
        ),
    ]

    errors = []
    check_workflow_job_strategy_matrix(
        "release.missing-strategy",
        "${{ fromJson(needs.release-metadata.outputs.release_matrix) }}",
        {"release": ("release.yml", STRATEGY_WORKFLOW)},
        errors,
    )

    assert errors == [
        "release.yml job missing-strategy strategy is missing matrix",
    ]


def test_workflow_job_step_blocks_read_step_metadata() -> None:
    errors: list[str] = []
    job_block = workflow_job_block(GH_CLI_WORKFLOW, "release", "example.yml", errors)
    step_blocks = workflow_job_step_blocks(job_block)

    assert [workflow_step_display_name(block) for block in step_blocks] == [
        "Update release notes",
        "Missing repo",
        "Wrong token",
        "Local shell",
    ]
    assert workflow_step_env(step_blocks[0]) == {
        "GH_TOKEN": "${{ github.token }}",
        "GH_REPO": "${{ github.repository }}",
    }
    assert workflow_step_run(step_blocks[1]) == (
        'echo start\ngh release upload "$VERSION" firmware/*'
    )
    assert workflow_step_uses_gh_cli(step_blocks[0]) is True
    assert workflow_step_uses_gh_cli(step_blocks[3]) is False
    assert errors == []


def test_workflow_gh_cli_env_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_gh_cli_env(
        {
            "GH_TOKEN": "${{ github.token }}",
            "GH_REPO": "${{ github.repository }}",
        },
        {"release": ("release.yml", GH_CLI_WORKFLOW)},
        errors,
    )

    assert errors == [
        "release.yml job release step 'Missing repo' env is missing GH_REPO",
        (
            "release.yml job release step 'Wrong token' env.GH_TOKEN "
            "must be '${{ github.token }}', found '${{ secrets.BAD_TOKEN }}'"
        ),
    ]


def test_workflow_step_uses_and_with_read_action_inputs() -> None:
    errors: list[str] = []
    release_notes_block = workflow_job_block(ACTION_INPUT_WORKFLOW, "release-notes", "docs.yml", errors)
    build_firmware_block = workflow_job_block(ACTION_INPUT_WORKFLOW, "build-firmware", "docs.yml", errors)
    build_block = workflow_job_block(ACTION_INPUT_WORKFLOW, "build-docs", "docs.yml", errors)
    deploy_block = workflow_job_block(ACTION_INPUT_WORKFLOW, "deploy-docs", "docs.yml", errors)
    release_notes_steps = workflow_job_step_blocks(release_notes_block)
    build_firmware_steps = workflow_job_step_blocks(build_firmware_block)
    build_steps = workflow_job_step_blocks(build_block)
    deploy_steps = workflow_job_step_blocks(deploy_block)

    assert workflow_step_with(release_notes_steps[0]) == {
        "fetch-depth": "0",
        "fetch-tags": "true",
    }
    assert workflow_step_with(build_firmware_steps[0]) == {
        "ref": "${{ github.event.release.tag_name || github.ref }}",
    }
    assert workflow_step_uses(build_steps[0]) == "actions/upload-artifact@v7"
    assert workflow_step_with(build_steps[0]) == {
        "name": "docs-dist",
        "path": "docs/.vitepress/dist",
    }
    assert workflow_step_uses(deploy_steps[1]) == "actions/upload-pages-artifact@v5"
    assert workflow_step_with(deploy_steps[1]) == {"path": "dist"}
    publish_block = workflow_job_block(ACTION_INPUT_WORKFLOW, "publish", "docs.yml", errors)
    publish_steps = workflow_job_step_blocks(publish_block)
    assert workflow_step_with(publish_steps[0]) == {
        "pattern": "firmware-*",
        "merge-multiple": "true",
        "path": "firmware",
    }
    assert errors == []


def test_workflow_action_step_inputs_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {"docs": ("docs.yml", ACTION_INPUT_WORKFLOW)}

    check_workflow_action_step_inputs(
        "docs.build-docs",
        "actions/upload-artifact@v7",
        "name",
        "docs-dist",
        {
            "name": "docs-dist",
            "path": "wrong-dist",
        },
        workflow_texts,
        errors,
    )
    check_workflow_action_step_inputs(
        "docs.deploy-docs",
        "actions/download-artifact@v8",
        "name",
        "firmware",
        {
            "name": "firmware",
            "path": "dist/firmware",
        },
        workflow_texts,
        errors,
    )
    check_workflow_action_step_inputs(
        "docs.publish",
        "actions/download-artifact@v8",
        "pattern",
        "firmware-*",
        {
            "pattern": "firmware-*",
            "merge-multiple": "false",
            "path": "firmware",
        },
        workflow_texts,
        errors,
    )
    check_workflow_action_step_inputs(
        "docs.release-notes",
        "actions/checkout@v7",
        "fetch-depth",
        "0",
        {
            "fetch-depth": "0",
            "fetch-tags": "false",
        },
        workflow_texts,
        errors,
    )
    check_workflow_action_step_inputs(
        "docs.build-firmware",
        "actions/checkout@v7",
        "ref",
        "${{ github.ref }}",
        {"ref": "${{ github.ref }}"},
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "docs.yml job build-docs step 'Upload docs artifact' with.path "
            "must be 'wrong-dist', found 'docs/.vitepress/dist'"
        ),
        "docs.yml job deploy-docs is missing actions/download-artifact@v8 step with name 'firmware'",
        (
            "docs.yml job publish step 'Download all firmware artifacts' with.merge-multiple "
            "must be 'false', found 'true'"
        ),
        (
            "docs.yml job release-notes step '<unnamed>' with.fetch-tags "
            "must be 'false', found 'true'"
        ),
        (
            "docs.yml job build-firmware is missing actions/checkout@v7 step "
            "with ref '${{ github.ref }}'"
        ),
    ]


def test_workflow_named_step_with_reports_missing_input() -> None:
    errors: list[str] = []

    check_workflow_named_step_with(
        "docs.build-docs",
        "Upload docs artifact",
        {"retention-days": "7"},
        {"docs": ("docs.yml", ACTION_INPUT_WORKFLOW)},
        errors,
    )

    assert errors == [
        "docs.yml job build-docs step 'Upload docs artifact' with is missing retention-days",
    ]


def test_workflow_named_step_uses_reports_missing_uses() -> None:
    errors: list[str] = []

    check_workflow_named_step_uses(
        "release.release-notes",
        "Build detailed changelog",
        "actions/checkout@v7",
        {"release": ("release.yml", STEP_ENV_WORKFLOW)},
        errors,
    )

    assert errors == [
        "release.yml job release-notes step 'Build detailed changelog' is missing uses",
    ]


def test_workflow_named_step_env_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {"release": ("release.yml", STEP_ENV_WORKFLOW)}

    check_workflow_named_step_env(
        "release.release-notes",
        "Build detailed changelog",
        {"VERSION": "${{ github.event.release.tag_name }}"},
        workflow_texts,
        errors,
    )
    check_workflow_named_step_env(
        "release.release-notes",
        "Update GitHub release notes",
        {"VERSION": "${{ github.ref_name }}"},
        workflow_texts,
        errors,
    )
    check_workflow_named_step_env(
        "release.build-firmware",
        "Collect firmware files and generate manifest",
        {"VERSION": "${{ github.event.release.tag_name || github.ref_name }}"},
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "release.yml job release-notes step 'Update GitHub release notes' env.VERSION "
            "must be '${{ github.ref_name }}', found '${{ github.event.release.tag_name }}'"
        ),
        "release.yml job build-firmware is missing step 'Collect firmware files and generate manifest'",
    ]


def test_workflow_named_step_run_contains_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {"release": ("release.yml", STEP_ENV_WORKFLOW)}

    check_workflow_named_step_run_contains(
        "release.release-notes",
        "Build detailed changelog",
        ['--output "$RUNNER_TEMP/release-notes.md"'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.release-notes",
        "Update GitHub release notes",
        ['--notes-file "$RUNNER_TEMP/release-notes.md"'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.build-firmware",
        "Collect firmware files and generate manifest",
        ['--version "${VERSION}"'],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "release.yml job release-notes step 'Update GitHub release notes' run is missing "
            '\'--notes-file "$RUNNER_TEMP/release-notes.md"\''
        ),
        "release.yml job build-firmware is missing step 'Collect firmware files and generate manifest'",
    ]


def test_workflow_named_step_run_contains_reports_missing_run() -> None:
    errors: list[str] = []

    check_workflow_named_step_run_contains(
        "docs.build-docs",
        "Upload docs artifact",
        ["npm run docs:build"],
        {"docs": ("docs.yml", ACTION_INPUT_WORKFLOW)},
        errors,
    )

    assert errors == [
        "docs.yml job build-docs step 'Upload docs artifact' is missing run",
    ]


def test_workflow_named_step_run_contains_checks_firmware_build_steps() -> None:
    errors: list[str] = []
    workflow_texts = {"release": ("release.yml", RELEASE_BUILD_RUN_WORKFLOW)}

    check_workflow_named_step_run_contains(
        "release.build-firmware",
        "Compile firmware",
        [
            '"${BUILD_DIR}/firmware.factory.bin"',
            '"output/${{ matrix.slug }}.factory.bin"',
            '-s firmware_version "${VERSION}"',
        ],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.build-firmware",
        "Collect firmware files and generate manifest",
        [
            '"${BUILD_DIR}/firmware.bin"',
            '"output/${{ matrix.slug }}.manifest.json"',
            '--factory "output/${{ matrix.slug }}.factory.bin"',
        ],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "release.yml job build-firmware step 'Compile firmware' run is missing "
            "'\"${BUILD_DIR}/firmware.factory.bin\"'"
        ),
        (
            "release.yml job build-firmware step 'Collect firmware files and generate manifest' "
            "run is missing '--factory \"output/${{ matrix.slug }}.factory.bin\"'"
        ),
    ]


def test_workflow_named_step_run_contains_checks_publish_steps() -> None:
    errors: list[str] = []
    workflow_texts = {"release": ("release.yml", RELEASE_PUBLISH_RUN_WORKFLOW)}

    check_workflow_named_step_run_contains(
        "release.publish",
        "Verify assembled release assets",
        ["--dir firmware"],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.publish",
        "Upload firmware to release",
        ["firmware/*", "firmware/* --clobber"],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.publish",
        "Verify uploaded release assets",
        [
            "mkdir -p published",
            '--pattern "*.manifest.json"',
            '--pattern "*.factory.bin"',
            '--pattern "*.ota.bin"',
            "--dir published",
        ],
        workflow_texts,
        errors,
    )

    assert errors == [
        "release.yml job publish step 'Upload firmware to release' run is missing 'firmware/*'",
        "release.yml job publish step 'Upload firmware to release' run is missing 'firmware/* --clobber'",
        (
            "release.yml job publish step 'Verify uploaded release assets' run is missing "
            '\'--pattern "*.factory.bin"\''
        ),
    ]


def test_workflow_named_step_helpers_check_cache_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {"compile": ("compile.yml", CACHE_WORKFLOW), "release": ("release.yml", CACHE_WORKFLOW)}
    cache_key = (
        "${{ needs.firmware-metadata.outputs.release_esphome_cache_key_prefix }}-"
        "${{ matrix.slug }}-${{ hashFiles('builds/*.yaml', 'common/**') }}"
    )
    release_cache_key = (
        "${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-"
        "${{ matrix.slug }}-${{ hashFiles('builds/*.yaml', 'common/**') }}"
    )

    check_workflow_named_step_uses(
        "compile.compile",
        "Cache ESPHome build",
        "actions/cache@v6",
        workflow_texts,
        errors,
    )
    check_workflow_named_step_with(
        "compile.compile",
        "Cache ESPHome build",
        {
            "path": "${{ needs.firmware-metadata.outputs.release_esphome_cache_dir }}",
            "key": cache_key,
        },
        workflow_texts,
        errors,
    )
    check_workflow_named_step_contains(
        "compile.compile",
        "Cache ESPHome build",
        [
            (
                "restore-keys: |\n"
                "            ${{ needs.firmware-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-"
            ),
        ],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "compile.compile",
        "Fix ESPHome cache permissions",
        ['chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_uses(
        "release.build-firmware",
        "Cache ESPHome build",
        "actions/cache@v6",
        workflow_texts,
        errors,
    )
    check_workflow_named_step_with(
        "release.build-firmware",
        "Cache ESPHome build",
        {
            "path": "${{ needs.release-metadata.outputs.release_esphome_cache_dir }}",
            "key": release_cache_key,
        },
        workflow_texts,
        errors,
    )
    check_workflow_named_step_contains(
        "release.build-firmware",
        "Cache ESPHome build",
        [
            (
                "restore-keys: |\n"
                "            ${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-"
            ),
        ],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "release.build-firmware",
        "Fix ESPHome cache permissions",
        ['chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"'],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "release.yml job build-firmware step 'Cache ESPHome build' uses must be "
            "'actions/cache@v6', found 'actions/cache@v5'"
        ),
        (
            "release.yml job build-firmware step 'Cache ESPHome build' with.path must be "
            "'${{ needs.release-metadata.outputs.release_esphome_cache_dir }}', found "
            "'${{ needs.release-metadata.outputs.wrong_cache_dir }}'"
        ),
        (
            "release.yml job build-firmware step 'Cache ESPHome build' is missing "
            "'restore-keys: |\\n            "
            "${{ needs.release-metadata.outputs.release_esphome_cache_key_prefix }}-${{ matrix.slug }}-'"
        ),
        (
            "release.yml job build-firmware step 'Fix ESPHome cache permissions' run is missing "
            '\'chmod -R u+rwX "${RELEASE_ESPHOME_CACHE_DIR}"\''
        ),
    ]


def test_workflow_named_step_run_contains_checks_compile_artifact_step() -> None:
    errors: list[str] = []
    workflow_texts = {"compile": ("compile.yml", COMPILE_ARTIFACT_RUN_WORKFLOW)}

    check_workflow_named_step_run_contains(
        "compile.compile",
        "Compile test firmware artifacts",
        [
            'TEST_VERSION="pr-${GITHUB_RUN_ID}-${GITHUB_RUN_ATTEMPT}"',
            '-s firmware_version "${TEST_VERSION}"',
            "mkdir -p output",
            '"output/${{ matrix.slug }}.factory.bin"',
            '"output/${{ matrix.slug }}.ota.bin"',
            '"output/${{ matrix.slug }}.version.txt"',
        ],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "compile.yml job compile step 'Compile test firmware artifacts' run is missing "
            "'\"output/${{ matrix.slug }}.ota.bin\"'"
        ),
        (
            "compile.yml job compile step 'Compile test firmware artifacts' run is missing "
            "'\"output/${{ matrix.slug }}.version.txt\"'"
        ),
    ]


def test_workflow_named_step_run_contains_checks_compile_commands() -> None:
    errors: list[str] = []
    workflow_texts = {
        "compile": ("compile.yml", COMPILE_COMMAND_RUN_WORKFLOW),
        "release": ("release.yml", COMPILE_COMMAND_RUN_WORKFLOW),
    }

    check_workflow_named_step_run_contains(
        "release.build-firmware",
        "Compile firmware",
        ['compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "compile.compile",
        "Compile test firmware artifacts",
        [
            'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.factory.yaml"',
            'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"',
        ],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "compile.yml job compile step 'Compile test firmware artifacts' run is missing "
            '\'compile "${ESPHOME_CONFIG_MOUNT}/builds/${{ matrix.yaml }}.yaml"\''
        ),
    ]


def test_workflow_named_step_run_contains_checks_esphome_docker_invocation() -> None:
    errors: list[str] = []
    workflow_texts = {
        "compile": ("compile.yml", ESPHOME_DOCKER_RUN_WORKFLOW),
        "release": ("release.yml", ESPHOME_DOCKER_RUN_WORKFLOW),
    }

    for target in ("compile.firmware-metadata", "release.release-metadata"):
        check_workflow_named_step_run_contains(
            target,
            "Read product metadata",
            ['python3 scripts/product_config.py github-output >> "$GITHUB_OUTPUT"'],
            workflow_texts,
            errors,
        )
    for target, step_name in (
        ("compile.compile", "Compile test firmware artifacts"),
        ("release.build-firmware", "Compile firmware"),
    ):
        check_workflow_named_step_run_contains(
            target,
            step_name,
            [
                '"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}"',
                '-v "${PWD}:${ESPHOME_CONFIG_MOUNT}"',
                "docker run ${ESPHOME_DOCKER_REMOVE_FLAG}",
            ],
            workflow_texts,
            errors,
        )

    assert errors == [
        (
            "compile.yml job compile step 'Compile test firmware artifacts' run is missing "
            "'\"${ESPHOME_DOCKER_IMAGE}:${ESPHOME_VERSION}\"'"
        ),
    ]


def test_workflow_named_step_run_contains_checks_docs_download_steps() -> None:
    errors: list[str] = []
    workflow_texts = {"docs": ("docs.yml", DOCS_DOWNLOAD_RUN_WORKFLOW)}

    for step_name, manifest_env in (
        ("Download firmware from latest release", "DEFAULT_PUBLIC_MANIFEST"),
        ("Download firmware from latest pre-release", "DEFAULT_PUBLIC_BETA_MANIFEST"),
    ):
        check_workflow_named_step_run_contains(
            "docs.download-firmware",
            step_name,
            [
                '--pattern "*.factory.bin"',
                '--pattern "*.ota.bin"',
                '--pattern "${SLUG}.manifest.json"',
                f'basename "${manifest_env}"',
                "--clobber",
            ],
            workflow_texts,
            errors,
        )

    assert errors == [
        (
            "docs.yml job download-firmware step 'Download firmware from latest pre-release' "
            "run is missing '--pattern \"*.factory.bin\"'"
        ),
        (
            "docs.yml job download-firmware step 'Download firmware from latest pre-release' "
            "run is missing 'basename \"$DEFAULT_PUBLIC_BETA_MANIFEST\"'"
        ),
        (
            "docs.yml job download-firmware step 'Download firmware from latest pre-release' "
            "run is missing '--clobber'"
        ),
    ]


def test_workflow_named_step_run_contains_checks_docs_manifest_directories() -> None:
    errors: list[str] = []
    workflow_texts = {"docs": ("docs.yml", DOCS_MANIFEST_DIR_WORKFLOW)}

    for step_name, dir_name in (
        ("Download firmware from latest release", "STABLE_MANIFEST_DIR"),
        ("Download firmware from latest pre-release", "BETA_MANIFEST_DIR"),
    ):
        check_workflow_named_step_run_contains(
            "docs.download-firmware",
            step_name,
            [f'mkdir -p "${dir_name}"'],
            workflow_texts,
            errors,
        )

    assert errors == [
        (
            "docs.yml job download-firmware step 'Download firmware from latest pre-release' "
            'run is missing \'mkdir -p "$BETA_MANIFEST_DIR"\''
        ),
    ]


def test_workflow_named_step_helpers_check_docs_release_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {"docs": ("docs.yml", DOCS_RELEASE_METADATA_WORKFLOW)}

    check_workflow_named_step_contains(
        "docs.download-firmware",
        "Set release metadata",
        ["id: release-meta"],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "docs.download-firmware",
        "Set release metadata",
        [
            "RELEASE_TAG=$(gh release view --json tagName -q .tagName)",
            'echo "RELEASE_TAG=${RELEASE_TAG}" >> "$GITHUB_ENV"',
            'echo "release_tag=${RELEASE_TAG}" >> "$GITHUB_OUTPUT"',
        ],
        workflow_texts,
        errors,
    )
    check_workflow_job_outputs(
        "docs.download-firmware",
        {"release_tag": "${{ steps.release-meta.outputs.release_tag }}"},
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "docs.download-firmware",
        "Download firmware from latest release",
        ['$RELEASE_TAG'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "docs.download-firmware",
        "Verify firmware assets",
        ['$RELEASE_TAG'],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "docs.download-firmware",
        "Download firmware from latest pre-release",
        [
            "BETA_TAG=$(gh release list",
            "gh release list --limit 20 --json tagName,isPrerelease",
            'if [ -n "$BETA_TAG" ]; then',
            'gh release download "$BETA_TAG"',
        ],
        workflow_texts,
        errors,
    )
    check_workflow_named_step_run_contains(
        "docs.deploy-docs",
        "Verify public firmware",
        [
            "python3 scripts/firmware_release.py verify-pages",
            "${{ needs.download-firmware.outputs.release_tag }}",
            '--base-url "$PUBLIC_BASE_URL"',
            "--retries 10",
            "--delay 15",
        ],
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "docs.yml job download-firmware step 'Set release metadata' run is missing "
            "'echo \"release_tag=${RELEASE_TAG}\" >> \"$GITHUB_OUTPUT\"'"
        ),
        (
            "docs.yml job download-firmware outputs.release_tag must be "
            "'${{ steps.release-meta.outputs.release_tag }}', found "
            "'${{ steps.wrong-meta.outputs.release_tag }}'"
        ),
        (
            "docs.yml job download-firmware step 'Verify firmware assets' run is missing "
            "'$RELEASE_TAG'"
        ),
        (
            "docs.yml job download-firmware step 'Download firmware from latest pre-release' "
            "run is missing 'gh release download \"$BETA_TAG\"'"
        ),
        (
            "docs.yml job deploy-docs step 'Verify public firmware' run is missing "
            "'${{ needs.download-firmware.outputs.release_tag }}'"
        ),
        (
            "docs.yml job deploy-docs step 'Verify public firmware' run is missing "
            '\'--base-url "$PUBLIC_BASE_URL"\''
        ),
        "docs.yml job deploy-docs step 'Verify public firmware' run is missing '--retries 10'",
        "docs.yml job deploy-docs step 'Verify public firmware' run is missing '--delay 15'",
    ]


def test_workflow_job_environment_and_named_step_id_reject_pages_deploy_drift() -> None:
    errors: list[str] = []
    workflow_texts = {"docs": ("docs.yml", DOCS_DEPLOY_WORKFLOW)}

    check_workflow_job_environment(
        "docs.deploy-docs",
        {
            "name": "github-pages",
            "url": "${{ steps.deployment.outputs.page_url }}",
        },
        workflow_texts,
        errors,
    )
    check_workflow_named_step_id(
        "docs.deploy-docs",
        "Deploy to GitHub Pages",
        "deployment",
        workflow_texts,
        errors,
    )
    check_workflow_named_step_uses(
        "docs.deploy-docs",
        "Deploy to GitHub Pages",
        "actions/deploy-pages@v5",
        workflow_texts,
        errors,
    )

    assert errors == [
        (
            "docs.yml job deploy-docs environment.url must be "
            "'${{ steps.deployment.outputs.page_url }}', found "
            "'${{ steps.wrong-deployment.outputs.page_url }}'"
        ),
        "docs.yml job deploy-docs step 'Deploy to GitHub Pages' id must be 'deployment', found 'wrong-deployment'",
        (
            "docs.yml job deploy-docs step 'Deploy to GitHub Pages' uses must be "
            "'actions/deploy-pages@v5', found 'actions/deploy-pages@v4'"
        ),
    ]


def test_workflow_job_environment_reports_missing_environment() -> None:
    errors: list[str] = []

    check_workflow_job_environment(
        "release.missing",
        {"name": "github-pages"},
        {"release": ("release.yml", OUTPUT_WORKFLOW)},
        errors,
    )

    assert errors == ["release.yml job missing is missing environment"]


def test_workflow_job_run_command_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    workflow_texts = {
        "compile": ("compile.yml", RUN_COMMAND_WORKFLOW),
        "docs": ("docs.yml", RUN_COMMAND_WORKFLOW),
    }

    check_workflow_job_run_command("compile.validate", "npm ci", workflow_texts, errors)
    check_workflow_job_run_command("docs.build-docs", "npm run docs:build", workflow_texts, errors)
    check_workflow_job_run_command("compile.validate", "npm run docs:build", workflow_texts, errors)

    assert errors == [
        "compile.yml job validate is missing run command 'npm run docs:build'",
    ]


def test_workflow_job_outputs_reads_job_outputs() -> None:
    errors: list[str] = []
    assert workflow_job_outputs(workflow_job_block(OUTPUT_WORKFLOW, "metadata", "example.yml", errors)) == {
        "release_matrix": "${{ steps.product.outputs.release_matrix }}",
        "esphome_version": "${{ steps.product.outputs.esphome_version }}",
        "device_slugs": "${{ steps.product.outputs.device_slugs }}",
    }
    assert workflow_job_outputs(workflow_job_block(OUTPUT_WORKFLOW, "missing", "example.yml", errors)) == {}
    assert errors == []


def test_workflow_job_outputs_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_outputs(
        "release.metadata",
        {
            "release_matrix": "${{ steps.product.outputs.release_matrix }}",
            "esphome_version": "${{ steps.product.outputs.wrong_version }}",
            "release_esphome_cache_dir": "${{ steps.product.outputs.release_esphome_cache_dir }}",
        },
        {"release": ("release.yml", OUTPUT_WORKFLOW)},
        errors,
    )

    assert errors == [
        (
            "release.yml job metadata outputs.esphome_version must be "
            "'${{ steps.product.outputs.wrong_version }}', found "
            "'${{ steps.product.outputs.esphome_version }}'"
        ),
        "release.yml job metadata outputs is missing release_esphome_cache_dir",
    ]


def test_workflow_job_env_reads_job_env() -> None:
    errors: list[str] = []
    assert workflow_job_env(workflow_job_block(JOB_ENV_WORKFLOW, "compile", "example.yml", errors)) == {
        "ESPHOME_DOCKER_IMAGE": "${{ needs.firmware-metadata.outputs.esphome_docker_image }}",
        "ESPHOME_VERSION": "${{ needs.firmware-metadata.outputs.esphome_version }}",
        "RELEASE_ESPHOME_CACHE_DIR": "${{ needs.firmware-metadata.outputs.release_esphome_cache_dir }}",
    }
    assert workflow_job_env(workflow_job_block(JOB_ENV_WORKFLOW, "missing", "example.yml", errors)) == {}
    assert errors == []


def test_workflow_job_env_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_job_env(
        "compile.compile",
        {
            "ESPHOME_DOCKER_IMAGE": "${{ needs.firmware-metadata.outputs.esphome_docker_image }}",
            "ESPHOME_VERSION": "${{ needs.release-metadata.outputs.esphome_version }}",
            "ESPHOME_CONFIG_MOUNT": "${{ needs.firmware-metadata.outputs.esphome_config_mount }}",
        },
        {"compile": ("compile.yml", JOB_ENV_WORKFLOW)},
        errors,
    )

    assert errors == [
        (
            "compile.yml job compile env.ESPHOME_VERSION must be "
            "'${{ needs.release-metadata.outputs.esphome_version }}', found "
            "'${{ needs.firmware-metadata.outputs.esphome_version }}'"
        ),
        "compile.yml job compile env is missing ESPHOME_CONFIG_MOUNT",
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


def test_workflow_concurrency_reads_top_level_concurrency() -> None:
    assert workflow_concurrency(CONCURRENCY_WORKFLOW) == {
        "group": "pages",
        "cancel-in-progress": "false",
    }
    assert workflow_concurrency("name: Missing Concurrency\n") == {}


def test_workflow_concurrency_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_concurrency(
        "docs",
        True,
        ".github/workflows/docs.yml",
        CONCURRENCY_WORKFLOW,
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml concurrency.group must be 'docs', found 'pages'",
        ".github/workflows/docs.yml concurrency.cancel-in-progress must be "
        "'true', found 'false'",
    ]

    errors = []
    check_workflow_concurrency(
        "pages",
        False,
        ".github/workflows/docs.yml",
        "name: Docs\njobs:\n",
        errors,
    )

    assert errors == [".github/workflows/docs.yml is missing top-level concurrency"]


def test_workflow_env_reads_top_level_env() -> None:
    assert workflow_env(ENV_WORKFLOW) == {
        "FORCE_JAVASCRIPT_ACTIONS_TO_NODE24": "true",
        "QUOTED_FLAG": "yes",
    }
    assert workflow_env("name: Missing Env\n") == {}


def test_workflow_top_level_env_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_top_level_env(
        "FORCE_JAVASCRIPT_ACTIONS_TO_NODE24",
        "true",
        {
            "compile": (".github/workflows/compile.yml", ENV_WORKFLOW),
            "docs": (
                ".github/workflows/docs.yml",
                "name: Docs\nenv:\n  FORCE_JAVASCRIPT_ACTIONS_TO_NODE24: false\n",
            ),
            "release": (".github/workflows/release.yml", "name: Release\njobs:\n"),
        },
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml top-level env FORCE_JAVASCRIPT_ACTIONS_TO_NODE24 must be 'true', found 'false'",
        ".github/workflows/release.yml top-level env is missing FORCE_JAVASCRIPT_ACTIONS_TO_NODE24",
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
                "      - uses: actions/checkout@v7",
                "        uses: actions/cache@v6",
                "        uses: actions/upload-artifact@v7",
                "        uses: actions/download-artifact@v8",
            ]
        ),
        ".github/workflows/docs.yml": "\n".join(
            [
                "      - uses: actions/checkout@v7",
                "        uses: actions/upload-artifact@v7",
                "        uses: actions/download-artifact@v8",
                "      - uses: actions/setup-node@v6",
                "        uses: actions/upload-pages-artifact@v5",
                "        uses: actions/cache@v6",
            ]
        ),
        ".github/workflows/compile.yml": "\n".join(
            [
                "      - uses: actions/checkout@v7",
                "        uses: actions/cache@v6",
                "        uses: actions/upload-artifact@v7",
            ]
        ),
    }

    check_workflow_action_usage(release_actions, workflow_texts, errors)

    assert errors == [
        ".github/workflows/docs.yml uses are missing product metadata actions: actions/deploy-pages@v5",
        ".github/workflows/docs.yml uses contain actions missing from product metadata: actions/cache@v6",
        ".github/workflows/compile.yml uses are missing product metadata actions: actions/setup-node@v6",
    ]


def test_workflow_action_references_reads_uses_lines() -> None:
    assert workflow_action_references(
        """\
steps:
  - uses: actions/checkout@v7
  - uses: "actions/setup-node@v6"
  - uses: actions/checkout@v7
  - run: npm ci
"""
    ) == ["actions/checkout@v7", "actions/setup-node@v6"]


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


def test_workflow_event_branch_filters_reads_inline_and_block_lists() -> None:
    assert workflow_event_branch_filters("on:\n  push:\n    branches: [main]\n", "push") == ["main"]
    assert workflow_event_branch_filters(BRANCH_WORKFLOW, "push") == ["main", "release"]
    assert workflow_event_branch_filters(BRANCH_WORKFLOW, "workflow_dispatch") == []
    assert workflow_event_branch_filters(BRANCH_WORKFLOW, "missing") == []


def test_workflow_default_branch_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_default_branch(
        "develop",
        ".github/workflows/docs.yml",
        BRANCH_WORKFLOW,
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml push branches are missing default branch: develop",
        (
            ".github/workflows/docs.yml push branches contain branches missing from product metadata: "
            "main, release"
        ),
    ]


def test_workflow_event_type_filters_reads_inline_and_block_lists() -> None:
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "workflow_run") == ["completed", "requested"]
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "release") == ["published", "prereleased"]
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "workflow_dispatch") == []
    assert workflow_event_type_filters(EVENT_TYPE_WORKFLOW, "missing") == []


def test_workflow_event_workflow_filters_reads_inline_and_block_lists() -> None:
    assert workflow_event_workflow_filters(EVENT_TYPE_WORKFLOW, "workflow_run") == ["Build Release"]
    assert workflow_event_workflow_filters(WORKFLOW_RUN_TARGET_WORKFLOW, "workflow_run") == [
        "Build Release",
        "Compile Check",
    ]
    assert workflow_event_workflow_filters(WORKFLOW_RUN_TARGET_WORKFLOW, "workflow_dispatch") == []
    assert workflow_event_workflow_filters(WORKFLOW_RUN_TARGET_WORKFLOW, "missing") == []


def test_workflow_run_targets_reject_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_run_targets(
        ["Build Release", "Missing Workflow"],
        ".github/workflows/docs.yml",
        WORKFLOW_RUN_TARGET_WORKFLOW,
        errors,
    )

    assert errors == [
        ".github/workflows/docs.yml workflow_run workflows are missing targets: Missing Workflow",
        (
            ".github/workflows/docs.yml workflow_run workflows contain targets missing from product metadata: "
            "Compile Check"
        ),
    ]


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


def test_workflow_sparse_checkout_blocks_reads_checkout_paths() -> None:
    assert workflow_sparse_checkout_blocks(SPARSE_CHECKOUT_WORKFLOW) == [
        ["scripts/product_config.py", "product/espframe.json"],
        ["scripts/firmware_release.py", "scripts/product_config.py", "product/espframe.json"],
    ]
    assert workflow_sparse_checkout_blocks("name: Missing Sparse Checkout\n") == []


def test_workflow_sparse_checkout_entries_read_cone_mode() -> None:
    assert workflow_sparse_checkout_entries(SPARSE_CHECKOUT_WORKFLOW) == [
        (["scripts/product_config.py", "product/espframe.json"], "false"),
        (["scripts/firmware_release.py", "scripts/product_config.py", "product/espframe.json"], "false"),
    ]
    assert workflow_sparse_checkout_entries("name: Missing Sparse Checkout\n") == []


def test_workflow_sparse_checkout_usage_rejects_drift_from_product_metadata() -> None:
    errors: list[str] = []
    check_workflow_sparse_checkout_usage(
        ["scripts/firmware_release.py", "scripts/product_config.py", "product/espframe.json"],
        ["scripts/product_config.py", "product/espframe.json"],
        False,
        {
            "compile": (".github/workflows/compile.yml", "name: Compile\n"),
            "docs": (".github/workflows/docs.yml", SPARSE_CHECKOUT_WORKFLOW),
            "release": (".github/workflows/release.yml", SPARSE_CHECKOUT_WORKFLOW),
        },
        errors,
    )

    assert errors == [
        (
            ".github/workflows/compile.yml is missing metadata sparse-checkout block: "
            "scripts/product_config.py, product/espframe.json"
        ),
        ".github/workflows/docs.yml sparse-checkout block metadata is not expected",
    ]


def test_workflow_sparse_checkout_usage_rejects_cone_mode_drift() -> None:
    errors: list[str] = []
    check_workflow_sparse_checkout_usage(
        ["scripts/firmware_release.py", "scripts/product_config.py", "product/espframe.json"],
        ["scripts/product_config.py", "product/espframe.json"],
        False,
        {"release": (".github/workflows/release.yml", SPARSE_CHECKOUT_CONE_DRIFT_WORKFLOW)},
        errors,
    )

    assert errors == [
        ".github/workflows/release.yml sparse-checkout block metadata cone mode must be 'false', found 'true'",
        ".github/workflows/release.yml sparse-checkout block release cone mode must be 'false', found '<missing>'",
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


def test_discovered_test_functions_discovers_named_callables() -> None:
    calls: list[str] = []

    def selected() -> None:
        calls.append("selected")

    def also_selected() -> None:
        calls.append("also_selected")

    discovered = discovered_test_functions(
        {
            "helper": selected,
            "test_selected": selected,
            "test_value": "not callable",
            "test_also_selected": also_selected,
        }
    )

    assert [name for name, _ in discovered] == ["test_selected", "test_also_selected"]
    for _, test_func in discovered:
        test_func()
    assert calls == ["selected", "also_selected"]


def main() -> int:
    run_discovered_tests(globals())
    print("workflow contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
