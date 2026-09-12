#!/usr/bin/env python3
"""Fast tests for release-readiness command construction."""

from __future__ import annotations

import contextlib
import io
import sys
import subprocess
import tempfile
from unittest.mock import patch
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

    def fake_run(command: list[str], label: str, log_path: Path | None = None) -> bool:
        if "compile" in command:
            assert log_path is not None
            log_path.write_text("RAM: used 137060 bytes from 571392 bytes\n")
        else:
            assert "--compile-log" in command
            assert "137060" in Path(command[command.index("--compile-log") + 1]).read_text()
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
    assert captured[1][1][-1].endswith("build/test-frame-build/build/firmware.factory.bin")
    assert_versioned_compile(captured[2][1], "/config/builds/test-frame.yaml")
    assert captured[3][1][-4:-2] == ["--profile", "ota"]
    assert captured[3][1][-1].endswith("build/test-frame-build/build/firmware.ota.bin")


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


def test_run_captures_stderr_and_preserves_failure() -> None:
    with tempfile.TemporaryDirectory() as directory:
        log = Path(directory) / "compile.log"
        with contextlib.redirect_stdout(io.StringIO()):
            assert not check_release_ready.run(
                [sys.executable, "-c", "import sys; print('size report', file=sys.stderr); sys.exit(7)"],
                "failed compile", log,
            )
        assert "size report" in log.read_text()


def test_compile_firmware_rejects_ram_over_budget() -> None:
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "firmware.bin"
        binary.write_bytes(b"test")

        def fake_compile(command, label, log_path=None):
            if log_path is not None:
                log_path.write_text(
                    "RAM: [==        ] 23.9% (used 137060 bytes from 573440 bytes)\n"
                    "Flash: [===       ] 30.0% (used 2500000 bytes from 8388608 bytes)\n"
                )
                return True
            command = list(command)
            command[command.index("--binary") + 1] = str(binary)
            result = subprocess.run(command, capture_output=True, text=True)
            assert "ram_used_bytes is 137060, over budget 137000" in result.stderr
            assert result.returncode == 1
            return result.returncode == 0

        with patch.object(check_release_ready, "run", fake_compile):
            assert not check_release_ready.compile_firmware()


def test_failed_compile_skips_budget_checks_for_stale_binaries() -> None:
    commands = []

    def fail_compile(command, label, log_path=None):
        commands.append(command)
        assert log_path is not None
        return False

    with patch.object(check_release_ready, "run", fail_compile):
        assert not check_release_ready.compile_firmware()
    assert len(commands) == 2 * len(check_release_ready.release_matrix_devices())


def main() -> int:
    run_discovered_tests(globals())
    print("release readiness tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
