#!/usr/bin/env python3
"""Fast tests for package metadata contract helpers."""

from __future__ import annotations

import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

from product_contract.package_metadata import script_includes_step, web_smoke_scenario_names  # noqa: E402
from script_test_discovery import run_discovered_tests  # noqa: E402


def test_script_includes_step_matches_whole_steps() -> None:
    script = " npm run test:web-smoke-cli && npm run test:web-smoke && npm run docs:build "
    assert script_includes_step(script, "npm run test:web-smoke-cli") is True
    assert script_includes_step(script, "npm run test:web-smoke") is True
    assert script_includes_step(script, "npm run docs:build") is True
    assert script_includes_step("npm run test:web-smoke-cli", "npm run test:web-smoke") is False
    assert script_includes_step(script, "npm run missing") is False


def test_web_smoke_scenario_names_reads_registered_scenarios() -> None:
    errors: list[str] = []
    smoke_test = """
const unrelated = [{ name: "outside" }];
const scenarios = [
  { name: "wizard", configured: false },
  { name: "settings-mobile", configured: true },
];
"""
    assert web_smoke_scenario_names(smoke_test, errors) == {"wizard", "settings-mobile"}
    assert errors == []


def test_web_smoke_scenario_names_reports_missing_registry() -> None:
    errors: list[str] = []
    assert web_smoke_scenario_names("const scenarios = [];", errors) == set()
    assert errors == ["tests/web_smoke_tests.js must define const scenarios = [...]"]


def test_web_smoke_scenario_names_reports_duplicates() -> None:
    errors: list[str] = []
    smoke_test = """
const scenarios = [
  { name: "wizard" },
  { name: "wizard" },
];
"""
    assert web_smoke_scenario_names(smoke_test, errors) == {"wizard"}
    assert errors == ["tests/web_smoke_tests.js scenarios must not contain duplicate names"]


def main() -> int:
    run_discovered_tests(globals())
    print("package contract tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
