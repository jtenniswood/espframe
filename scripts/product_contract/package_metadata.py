from __future__ import annotations

import json
import re

from product_contract.common import ROOT, read, require_contains


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
        if scripts.get("check:release-ready") != "python3 scripts/check_release_ready.py":
            errors.append("package.json check:release-ready must run scripts/check_release_ready.py")
        if scripts.get("check:release-ready-with-compile") != "python3 scripts/check_release_ready.py --compile":
            errors.append("package.json check:release-ready-with-compile must run scripts/check_release_ready.py --compile")
        if scripts.get("test:web-compat") != "node tests/web_compat_tests.js":
            errors.append("package.json test:web-compat must run tests/web_compat_tests.js")
        if scripts.get("test:web-modules") != "node tests/web_module_tests.js":
            errors.append("package.json test:web-modules must run tests/web_module_tests.js")
        if scripts.get("test:web-smoke-cli") != "node tests/web_smoke_cli_tests.js":
            errors.append("package.json test:web-smoke-cli must run tests/web_smoke_cli_tests.js")
        if scripts.get("test:web-smoke") != "node tests/web_smoke_tests.js":
            errors.append("package.json test:web-smoke must run tests/web_smoke_tests.js")
        test_web = str(scripts.get("test:web", ""))
        if not script_includes_step(test_web, "npm run test:web-compat"):
            errors.append("package.json test:web must include test:web-compat")
        if not script_includes_step(test_web, "npm run test:web-modules"):
            errors.append("package.json test:web must include test:web-modules")
        if not script_includes_step(test_web, "npm run test:web-smoke-cli"):
            errors.append("package.json test:web must include test:web-smoke-cli")
        if not script_includes_step(test_web, "npm run test:web-smoke"):
            errors.append("package.json test:web must include test:web-smoke")
        firmware_logic = str(scripts.get("test:firmware-logic", ""))
        if not script_includes_step(firmware_logic, "npm run test:helpers"):
            errors.append("package.json test:firmware-logic must include test:helpers")
        if not script_includes_step(firmware_logic, "npm run test:timezones"):
            errors.append("package.json test:firmware-logic must include test:timezones")
        check_fast = str(scripts.get("check:fast", ""))
        if not script_includes_step(check_fast, "npm run check:generated"):
            errors.append("package.json check:fast must include check:generated")
        if not script_includes_step(check_fast, "npm run check:product"):
            errors.append("package.json check:fast must include check:product")
        if not script_includes_step(check_fast, "npm run check:backup"):
            errors.append("package.json check:fast must include check:backup")
        if not script_includes_step(check_fast, "npm run check:compat"):
            errors.append("package.json check:fast must include check:compat")
        if not script_includes_step(check_fast, "npm run check:firmware-fields"):
            errors.append("package.json check:fast must include check:firmware-fields")
        check_pr = str(scripts.get("check:pr", ""))
        if not script_includes_step(check_pr, "npm run check:fast"):
            errors.append("package.json check:pr must include check:fast")
        if not script_includes_step(check_pr, "npm run test:web-compat"):
            errors.append("package.json check:pr must include test:web-compat")
        if not script_includes_step(check_pr, "npm run test:web-modules"):
            errors.append("package.json check:pr must include test:web-modules")
        if not script_includes_step(check_pr, "npm run test:web-smoke-cli"):
            errors.append("package.json check:pr must include test:web-smoke-cli")
        if not script_includes_step(check_pr, "npm run test:web-smoke"):
            errors.append("package.json check:pr must include test:web-smoke")
        if not script_includes_step(check_pr, "npm run test:firmware-logic"):
            errors.append("package.json check:pr must include test:firmware-logic")
        if not script_includes_step(check_pr, "npm run docs:build"):
            errors.append("package.json check:pr must include docs:build")
        check_release = str(scripts.get("check:release", ""))
        if not script_includes_step(check_release, "npm run check:pr"):
            errors.append("package.json check:release must include check:pr")
        if not script_includes_step(check_release, "npm run check:firmware-release"):
            errors.append("package.json check:release must include check:firmware-release")
        if not script_includes_step(check_release, "npm run check:release-changelog"):
            errors.append("package.json check:release must include check:release-changelog")
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
    if release_ready.count('"firmware_version"') < 2:
        errors.append("scripts/check_release_ready.py must set firmware_version for factory and OTA compile checks")


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
