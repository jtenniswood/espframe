#!/usr/bin/env python3
"""Fast tests for release-readiness command construction."""

from __future__ import annotations

import contextlib
import hashlib
import io
import sys
import subprocess
import tempfile
from unittest.mock import patch
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
sys.path.insert(0, str(ROOT / "scripts"))

import check_release_ready  # noqa: E402
import native_stable_releases  # noqa: E402
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
                    "RAM: [==        ] 24.9% (used 143060 bytes from 573440 bytes)\n"
                    "Flash: [===       ] 30.0% (used 2500000 bytes from 8388608 bytes)\n"
                )
                return True
            command = list(command)
            command[command.index("--binary") + 1] = str(binary)
            result = subprocess.run(command, capture_output=True, text=True)
            assert "ram_static_bytes is 143060, over budget 143000" in result.stderr
            assert result.returncode == 1
            return result.returncode == 0

        with patch.object(check_release_ready, "run", fake_compile):
            assert not check_release_ready.compile_firmware()


def test_compile_firmware_allows_ram_warning_below_hard_ceiling() -> None:
    with tempfile.TemporaryDirectory() as directory:
        binary = Path(directory) / "firmware.bin"
        binary.write_bytes(b"test")

        def fake_compile(command, label, log_path=None):
            if log_path is not None:
                log_path.write_text(
                    "RAM: [==        ] 24.8% (used 142060 bytes from 573440 bytes)\n"
                    "Flash: [===       ] 30.0% (used 2500000 bytes from 8388608 bytes)\n"
                )
                return True
            command = list(command)
            command[command.index("--binary") + 1] = str(binary)
            result = subprocess.run(command, capture_output=True, text=True)
            assert "ram_static_bytes is 142060, over warning threshold 142000" in result.stderr
            assert result.returncode == 0
            return result.returncode == 0

        with patch.object(check_release_ready, "run", fake_compile):
            assert check_release_ready.compile_firmware()


def test_failed_compile_skips_budget_checks_for_stale_binaries() -> None:
    commands = []

    def fail_compile(command, label, log_path=None):
        commands.append(command)
        assert log_path is not None
        return False

    with patch.object(check_release_ready, "run", fail_compile):
        assert not check_release_ready.compile_firmware()
    assert len(commands) == 2 * len(check_release_ready.release_matrix_devices())


def test_stable_native_feed_requires_matching_board_images() -> None:
    with tempfile.TemporaryDirectory() as temp:
        root = Path(temp)
        source = root / "source"
        source.mkdir()
        version = "v1.18.0"
        for board, filename in native_stable_releases.ASSETS.items():
            image = bytearray(288)
            image[0] = 0xE9
            image[12] = 0x12
            image[23] = 1
            image[32:36] = bytes.fromhex("3254cdab")
            image[48:48 + len(version)] = version.encode()
            project = f"espframe_legacy_{board}".encode()
            image[80:80 + len(project)] = project
            image[176] = 1
            (source / filename).write_bytes(image)
        bridges = root / "bridges"
        bridges.mkdir()
        repository = "test-owner/espframe"
        for board, filename in native_stable_releases.ASSETS.items():
            image = (source / filename).read_bytes()
            pins = (f"https://github.com/{repository}/releases/download/{version}/{filename}",
                    hashlib.md5(image).hexdigest(), image[176:208].hex())
            bridge = bridges / f"{native_stable_releases.ESP_HOME_SLUGS[board]}.ota.bin"
            bridge.write_bytes(b"\0".join(pin.encode() for pin in pins) + b"\0")
        native_stable_releases.verify_bridges(bridges, source, repository, version)
        bridge.write_bytes(b"legacy ESPHome image")
        try:
            native_stable_releases.verify_bridges(bridges, source, repository, version)
        except ValueError as exc:
            assert "does not pin" in str(exc)
        else:
            raise AssertionError("A stock ESPHome build was accepted as a migration bridge")
        indexes = {board: [] for board in native_stable_releases.BOARDS}
        output = root / "feed"
        native_stable_releases.stage_release(source, output, version, indexes)
        assert all(entries[0]["version"] == version for entries in indexes.values())
        assert (output / version / "v2-ota.bin").read_bytes() == (source / "v2-legacy-native.bin").read_bytes()
        (source / "v2-legacy-native.bin").write_bytes((source / "v1-legacy-native.bin").read_bytes())
        try:
            native_stable_releases.stage_release(source, root / "invalid", version, indexes)
        except ValueError as exc:
            assert "v2 OTA image" in str(exc)
        else:
            raise AssertionError("A V1 image under the V2 asset name was accepted")
        assert not (root / "invalid").exists()


def main() -> int:
    run_discovered_tests(globals())
    print("release readiness tests passed")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
