from __future__ import annotations

import json
import re

from product_contract.common import ROOT, read, require_contains


PACKAGE_SCRIPT_COMMANDS = (
    ("check:backup", "python3 scripts/check_backup_config.py", "scripts/check_backup_config.py"),
    ("check:compat", "python3 scripts/check_compatibility.py", "scripts/check_compatibility.py"),
    ("check:firmware-fields", "python3 scripts/check_firmware_fields.py", "scripts/check_firmware_fields.py"),
    ("check:firmware-release", "python3 scripts/check_firmware_release.py", "scripts/check_firmware_release.py"),
    ("check:release-changelog", "python3 scripts/check_release_changelog.py", "scripts/check_release_changelog.py"),
    ("check:release-ready", "python3 scripts/check_release_ready.py", "scripts/check_release_ready.py"),
    (
        "check:release-ready-with-compile",
        "python3 scripts/check_release_ready.py --compile",
        "scripts/check_release_ready.py --compile",
    ),
    ("changelog:release", "python3 scripts/release_changelog.py", "scripts/release_changelog.py"),
    ("test:package-contract", "python3 tests/package_contract_tests.py", "tests/package_contract_tests.py"),
    ("test:release-ready", "python3 tests/release_ready_tests.py", "tests/release_ready_tests.py"),
    ("test:web-compat", "node tests/web_compat_tests.js", "tests/web_compat_tests.js"),
    ("test:web-modules", "node tests/web_module_tests.js", "tests/web_module_tests.js"),
    ("test:web-smoke", "node tests/web_smoke_tests.js", "tests/web_smoke_tests.js"),
    ("test:web-smoke-cli", "node tests/web_smoke_cli_tests.js", "tests/web_smoke_cli_tests.js"),
    ("test:workflow-contract", "python3 tests/workflow_contract_tests.py", "tests/workflow_contract_tests.py"),
)


PACKAGE_SCRIPT_STEPS = (
    (
        "test:web",
        (
            "npm run test:web-compat",
            "npm run test:web-modules",
            "npm run test:web-smoke-cli",
            "npm run test:web-smoke",
        ),
    ),
    ("test:firmware-logic", ("npm run test:helpers", "npm run test:timezones")),
    (
        "check:fast",
        (
            "npm run check:generated",
            "npm run check:product",
            "npm run check:backup",
            "npm run check:compat",
            "npm run check:firmware-fields",
        ),
    ),
    (
        "check:pr",
        (
            "npm run check:fast",
            "npm run test:web-compat",
            "npm run test:web-modules",
            "npm run test:web-smoke-cli",
            "npm run test:web-smoke",
            "npm run test:package-contract",
            "npm run test:release-ready",
            "npm run test:workflow-contract",
            "npm run test:firmware-logic",
            "npm run docs:build",
        ),
    ),
    (
        "check:release",
        (
            "npm run check:pr",
            "npm run check:firmware-release",
            "npm run check:release-changelog",
        ),
    ),
)


def web_smoke_scenario_names(smoke_test: str, errors: list[str]) -> set[str]:
    if not smoke_test:
        return set()

    match = re.search(r"\bconst\s+scenarios\s*=\s*\[(.*?)\n\s*\];", smoke_test, re.DOTALL)
    if not match:
        errors.append("tests/web_smoke_tests.js must define const scenarios = [...]")
        return set()

    names = re.findall(r'\bname:\s*"([^"]+)"', match.group(1))
    if not names:
        errors.append("tests/web_smoke_tests.js scenarios must list at least one named scenario")
    if len(names) != len(set(names)):
        errors.append("tests/web_smoke_tests.js scenarios must not contain duplicate names")
    return set(names)


def script_includes_step(script: str, expected_step: str) -> bool:
    return expected_step in [step.strip() for step in script.split("&&")]


def check_package_script_commands(scripts: dict[str, object], errors: list[str]) -> None:
    for script_name, expected_command, expected_label in PACKAGE_SCRIPT_COMMANDS:
        if scripts.get(script_name) != expected_command:
            errors.append(f"package.json {script_name} must run {expected_label}")


def check_package_script_steps(scripts: dict[str, object], errors: list[str]) -> None:
    for script_name, expected_steps in PACKAGE_SCRIPT_STEPS:
        script = str(scripts.get(script_name, ""))
        for expected_step in expected_steps:
            if not script_includes_step(script, expected_step):
                expected_label = expected_step.removeprefix("npm run ")
                errors.append(f"package.json {script_name} must include {expected_label}")


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
        check_package_script_commands(scripts, errors)
        check_package_script_steps(scripts, errors)
        if scripts.get("check:all") != "npm run check:release":
            errors.append("package.json check:all must run check:release")
    if package_lock.get("name") != expected_name:
        errors.append("package-lock.json name must match project.npm_package_name")
    root_package = package_lock.get("packages", {}).get("", {})
    if root_package.get("name") != expected_name:
        errors.append("package-lock.json root package name must match project.npm_package_name")
    if expected_license and root_package.get("license") != expected_license:
        errors.append("package-lock.json root package license must match project.license_id")

    smoke_test = read(ROOT / "tests" / "web_smoke_tests.js", errors)
    smoke_scenario_names = web_smoke_scenario_names(smoke_test, errors)
    smoke_scenarios = product["project"].get("web_smoke_required_scenarios", [])
    if isinstance(smoke_scenarios, list):
        for scenario in smoke_scenarios:
            scenario_id = str(scenario).strip()
            if scenario_id and scenario_id not in smoke_scenario_names:
                errors.append(
                    f"project.web_smoke_required_scenarios {scenario_id} must be listed in tests/web_smoke_tests.js scenarios"
                )
    require_contains(smoke_test, "--scenario", "tests/web_smoke_tests.js", errors)
    require_contains(smoke_test, "--list", "tests/web_smoke_tests.js", errors)
    testing_docs = read(ROOT / "docs" / "testing.md", errors)
    require_contains(testing_docs, "npm run test:web-smoke -- --scenario", "docs/testing.md", errors)
    require_contains(testing_docs, "npm run check:release-ready-with-compile", "docs/testing.md", errors)
    require_contains(testing_docs, "npm run check:release", "docs/testing.md", errors)

    release_ready = read(ROOT / "scripts" / "check_release_ready.py", errors)
    require_contains(release_ready, "ESPHome factory compile", "scripts/check_release_ready.py", errors)
    require_contains(release_ready, "ESPHome OTA compile", "scripts/check_release_ready.py", errors)
    require_contains(release_ready, "factory and OTA firmware", "scripts/check_release_ready.py", errors)
    require_contains(release_ready, "TEST_FIRMWARE_VERSION", "scripts/check_release_ready.py", errors)
    release_ready_tests = read(ROOT / "tests" / "release_ready_tests.py", errors)
    require_contains(release_ready_tests, "assert_versioned_compile(captured[0][1]", "tests/release_ready_tests.py", errors)
    require_contains(release_ready_tests, "assert_versioned_compile(captured[1][1]", "tests/release_ready_tests.py", errors)


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
