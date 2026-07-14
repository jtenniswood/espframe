#!/usr/bin/env python3
"""Fast tests for release-readiness command construction."""

from __future__ import annotations

import contextlib
import io
import sys
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import check_release_ready  # noqa: E402
from script_test_discovery import run_discovered_tests  # noqa: E402


def assert_versioned_compile(command: list[str], config_path: str) -> None:
    compile_index = command.index("compile")
    assert command[compile_index + 1] == config_path
    assert command[compile_index - 3:compile_index] == [
        "-s",
        "firmware_version",
        check_release_ready.TEST_FIRMWARE_VERSION,
    ]


def test_compile_firmware_runs_versioned_factory_and_ota_commands() -> None:
    captured: list[tuple[str, list[str]]] = []
    original_metadata = check_release_ready.github_workflow_metadata
    original_devices = check_release_ready.release_matrix_devices
    original_run = check_release_ready.run

    def fake_run(command: list[str], label: str) -> bool:
        captured.append((label, command))
        return True

    check_release_ready.github_workflow_metadata = lambda: {
        "ESPHOME_DOCKER_IMAGE": "ghcr.io/example/esphome",
        "ESPHOME_VERSION": "1.2.3",
        "ESPHOME_CONFIG_MOUNT": "/config",
        "ESPHOME_DOCKER_REMOVE_FLAG": "--rm",
        "RELEASE_ESPHOME_CACHE_DIR": "builds/.esphome",
    }
    check_release_ready.release_matrix_devices = lambda: [
        {"slug": "test-frame", "yaml": "test-frame", "build_name": "test-frame-build"}
    ]
    check_release_ready.run = fake_run
    try:
        assert check_release_ready.compile_firmware() is True
    finally:
        check_release_ready.github_workflow_metadata = original_metadata
        check_release_ready.release_matrix_devices = original_devices
        check_release_ready.run = original_run

    assert [label for label, _ in captured] == [
        "ESPHome factory compile (test-frame)",
        "Firmware factory budget (test-frame)",
        "ESPHome OTA compile (test-frame)",
        "Firmware OTA budget (test-frame)",
    ]
    assert_versioned_compile(captured[0][1], "/config/builds/test-frame.factory.yaml")
    assert captured[1][1][-4:-2] == ["--profile", "factory"]
    assert captured[1][1][-1].endswith("test-frame-build/firmware.factory.bin")
    assert_versioned_compile(captured[2][1], "/config/builds/test-frame.yaml")
    assert captured[3][1][-4:-2] == ["--profile", "ota"]
    assert captured[3][1][-1].endswith("test-frame-build/firmware.bin")


def test_compile_firmware_rejects_missing_metadata() -> None:
    original_metadata = check_release_ready.github_workflow_metadata
    original_run = check_release_ready.run

    def fail_run(_command: list[str], _label: str) -> bool:
        raise AssertionError("compile should not run with missing metadata")

    check_release_ready.github_workflow_metadata = lambda: {
        "ESPHOME_DOCKER_IMAGE": "",
        "ESPHOME_VERSION": "1.2.3",
        "ESPHOME_CONFIG_MOUNT": "/config",
        "ESPHOME_DOCKER_REMOVE_FLAG": "--rm",
        "RELEASE_ESPHOME_CACHE_DIR": "builds/.esphome",
    }
    check_release_ready.run = fail_run
    try:
        output = io.StringIO()
        with contextlib.redirect_stdout(output):
            assert check_release_ready.compile_firmware() is False
        assert "[FAIL] ESPHome compile metadata" in output.getvalue()
    finally:
        check_release_ready.github_workflow_metadata = original_metadata
        check_release_ready.run = original_run


def main() -> int:
    run_discovered_tests(globals())
    print("release readiness tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
