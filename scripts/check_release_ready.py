#!/usr/bin/env python3
"""Run the local release-readiness gate and report the result clearly."""

from __future__ import annotations

import argparse
import subprocess
import sys
from pathlib import Path

from product_config import github_workflow_metadata, release_matrix_devices


ROOT = Path(__file__).resolve().parent.parent


def run(command: list[str], label: str) -> bool:
    print(f"[RUN] {label}")
    result = subprocess.run(command)
    if result.returncode != 0:
        print(f"[FAIL] {label}")
        return False
    print(f"[PASS] {label}")
    return True


def git_clean() -> bool:
    result = subprocess.run(
        ["git", "status", "--short"],
        check=False,
        capture_output=True,
        text=True,
    )
    if result.returncode != 0:
        print("[FAIL] Git working tree check")
        return False
    if result.stdout.strip():
        print("[FAIL] Git working tree check")
        print(result.stdout.rstrip())
        return False
    print("[PASS] Git working tree is clean")
    return True


def compile_firmware() -> bool:
    metadata = github_workflow_metadata()
    image = metadata["ESPHOME_DOCKER_IMAGE"]
    version = metadata["ESPHOME_VERSION"]
    mount = metadata["ESPHOME_CONFIG_MOUNT"]
    remove_flag = metadata["ESPHOME_DOCKER_REMOVE_FLAG"]
    if not image or not version or not mount:
        print("[FAIL] ESPHome compile metadata")
        print("Product metadata must define the ESPHome Docker image, version, and config mount.")
        return False

    checks = []
    for device in release_matrix_devices():
        factory_command = ["docker", "run"]
        if remove_flag:
            factory_command.append(remove_flag)
        factory_command.extend(
            [
                "-v",
                f"{ROOT}:{mount}",
                f"{image}:{version}",
                "compile",
                f"{mount}/builds/{device['yaml']}.factory.yaml",
            ]
        )
        checks.append(run(factory_command, f"ESPHome factory compile ({device['slug']})"))

        ota_command = ["docker", "run"]
        if remove_flag:
            ota_command.append(remove_flag)
        ota_command.extend(
            [
                "-v",
                f"{ROOT}:{mount}",
                f"{image}:{version}",
                "-s",
                "firmware_version",
                "v0.0.0",
                "compile",
                f"{mount}/builds/{device['yaml']}.yaml",
            ]
        )
        checks.append(run(ota_command, f"ESPHome OTA compile ({device['slug']})"))
    return all(checks)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument(
        "--compile",
        action="store_true",
        help="Also compile factory and OTA firmware with ESPHome Docker before passing release readiness.",
    )
    return parser


def main(argv: list[str] | None = None) -> int:
    args = build_parser().parse_args(sys.argv[1:] if argv is None else argv)
    print("Espframe release-readiness check")
    if args.compile:
        print("This runs the normal local gate plus ESPHome factory and OTA firmware compile.")
    else:
        print("This runs the normal local gate. Use --compile before firmware releases.")
    passed = run(["npm", "run", "check:all"], "Local validation gate")
    passed = git_clean() and passed
    if args.compile and passed:
        passed = compile_firmware() and passed
        passed = git_clean() and passed
    if passed:
        print("[PASS] Release-readiness checks passed")
        return 0
    print("[FAIL] Release-readiness checks failed")
    return 1


if __name__ == "__main__":
    raise SystemExit(main())
