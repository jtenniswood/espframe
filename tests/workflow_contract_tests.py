#!/usr/bin/env python3
"""Fast tests for GitHub workflow contract parsing."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.workflows import (  # noqa: E402
    normalize_workflow_condition,
    workflow_job_block,
    workflow_job_condition,
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


def main() -> int:
    test_workflow_job_block_finds_exact_job()
    test_workflow_job_block_reports_missing_job()
    test_workflow_job_condition_handles_supported_forms()
    test_normalize_workflow_condition_collapses_whitespace()
    print("workflow contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
