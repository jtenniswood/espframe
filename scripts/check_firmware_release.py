#!/usr/bin/env python3
"""Smoke tests for scripts/firmware_release.py."""

from __future__ import annotations

from contextlib import redirect_stderr
from functools import partial
from http.server import SimpleHTTPRequestHandler, ThreadingHTTPServer
import gzip
import io
import json
import os
from pathlib import Path
import subprocess
from tempfile import TemporaryDirectory
from threading import Thread

import firmware_release
from product_contract.workflows import workflow_job_block, workflow_job_step_blocks, workflow_step_run
from script_test_discovery import run_discovered_tests


SLUG = "demo-panel"
VERSION = "v9.8.7"
BETA_VERSION = "v9.8.8-beta.1"
CHIP = "ESP32-S3"
PROJECT_NAME = firmware_release.PROJECT_NAME
REAL_DEVICE = next(iter(firmware_release.DEVICES.values()))
REAL_SLUG = REAL_DEVICE.slug
NESTED_DEVICE = next(
    device for device in firmware_release.DEVICES.values()
    if len(Path(device.public_manifest).parts) > 2
)


class QuietHandler(SimpleHTTPRequestHandler):
    def log_message(self, format: str, *args: object) -> None:
        pass


def write_image(path: Path, *strings: str) -> None:
    payload = bytearray(b"\x00header\x00")
    for item in strings:
        payload.extend(item.encode("ascii"))
        payload.append(0)
    path.write_bytes(bytes(payload))


def project_version_string(version: str) -> str:
    return f"Project {PROJECT_NAME} version {version}"


def release_image_strings(version: str, *extra: str) -> tuple[str, ...]:
    return (
        version,
        project_version_string(version),
        "package_import_url",
        version,
        PROJECT_NAME,
        "project_version",
        *extra,
    )


def write_release_image(path: Path, version: str, *extra: str) -> None:
    write_image(path, *release_image_strings(version, *extra))


def run_ok(args: list[str]) -> None:
    code = firmware_release.main(args)
    assert code == 0, f"{args} exited {code}"


def run_fails(args: list[str]) -> None:
    with redirect_stderr(io.StringIO()):
        code = firmware_release.main(args)
    assert code != 0, f"{args} unexpectedly passed"


def make_release_files(base: Path, slug: str = SLUG, version: str = VERSION) -> tuple[Path, Path, Path]:
    factory = base / f"{slug}.factory.bin"
    ota = base / f"{slug}.ota.bin"
    manifest = base / f"{slug}.manifest.json"
    chip = firmware_release.expected_chip_for_slug(slug) or CHIP
    write_release_image(factory, version)
    write_release_image(ota, version)
    run_ok([
        "manifest",
        "--slug", slug,
        "--chip", chip,
        "--version", version,
        "--factory", str(factory),
        "--ota", str(ota),
        "--out", str(manifest),
    ])
    return manifest, factory, ota


def write_versions_index(base: Path, slug: str, versions: list[str]) -> None:
    entries = []
    for position, version in enumerate(versions):
        if position == 0:
            ota_path = f"{slug}.ota.bin"
            image = base / ota_path
        else:
            ota_path = f"versions/{version}/{slug}.ota.bin"
            image = base / ota_path
            image.parent.mkdir(parents=True, exist_ok=True)
            write_release_image(image, version)
        entries.append({
            "version": version,
            "release_url": firmware_release.RELEASE_URL_BASE + version,
            "ota": {"path": ota_path, "md5": firmware_release.md5sum(image)},
        })
    (base / "versions.json").write_text(json.dumps({"device": slug, "versions": entries}))


def test_valid_files_and_directory() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        assert json.loads(manifest.read_text())["new_install_prompt_erase"] is True
        run_ok([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])
        run_ok(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])


def test_exact_candidate_requires_only_the_requested_release_files() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        slugs = [device.slug for device in firmware_release.DEVICES.values()]
        for slug in slugs:
            make_release_files(base, slug)
        args = ["verify-directory", "--exact", "--version", VERSION, "--dir", str(base), "--slugs", *slugs]
        run_ok(args)
        (base / "unexpected.ota.bin").touch()
        run_fails(args)
        (base / "unexpected.ota.bin").unlink()
        (base / f"{slugs[-1]}.ota.bin").unlink()
        run_fails(args)


def test_candidate_provenance_binds_source_web_assets_and_firmware() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        version = "v0.0.0-candidate.123.1"
        manifest, factory, ota = make_release_files(base, version=version)
        commit = "a" * 40
        web_assets = firmware_release.candidate_web_assets()
        original_factory = factory.read_bytes()
        gzip_assets = b"".join(gzip.compress(content) for content in web_assets.values())
        for image in (factory, ota):
            image.write_bytes(image.read_bytes() + gzip_assets)
        make_manifest = [
            "manifest", "--slug", SLUG, "--chip", CHIP, "--version", version,
            "--factory", str(factory), "--ota", str(ota), "--out", str(manifest),
            "--source-commit", commit,
        ]
        run_ok(make_manifest)
        args = ["verify-directory", "--exact", "--version", version, "--dir", str(base),
                "--slugs", SLUG, "--source-commit"]
        run_ok([*args, commit])
        run_fails([*args, "b" * 40])
        original_manifest = manifest.read_text()
        data = json.loads(original_manifest)
        data["espframe_candidate"]["sha256"]["app.js"] = "0" * 64
        manifest.write_text(json.dumps(data))
        run_fails([*args, commit])
        manifest.write_text(original_manifest)
        factory.write_bytes(factory.read_bytes() + b"changed")
        run_fails([*args, commit])
        # Even freshly hashed firmware must embed both assets from the checkout.
        factory.write_bytes(original_factory + gzip.compress(web_assets["app.js"]))
        run_ok(make_manifest)
        run_fails([*args, commit])


def test_release_promotion_keeps_unverified_assets_private() -> None:
    path = firmware_release.ROOT / ".github/workflows/release.yml"
    errors: list[str] = []
    publish = workflow_job_block(path.read_text(), "publish", str(path), errors)
    assert not errors
    commands = []
    for step in workflow_job_step_blocks(publish):
        command = workflow_step_run(step)
        if command:
            commands.append(" ".join(command.splitlines()) if "run: >-" in step else command)

    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        gh = base / "gh"
        gh.write_text("""#!/bin/bash
set -eu
[ "$1" = release ]
printf '%s\n' "$2" >> calls
case "$2" in
  view) [ "$GH_TEST_MODE" != published ] && echo true || echo false ;;
  upload)
    case "$4" in
      native/*) mkdir -p remote-native; cp native/* remote-native/ ;;
      *) mkdir -p remote; cp firmware/* remote/ ;;
    esac
    ;;
  download)
    destination="${@: -1}"
    if [ "$destination" = published-native ]; then
      cp remote-native/* "$destination/"
      if [ "$GH_TEST_MODE" = corrupt-native ]; then
        printf 'changed without removing image metadata' >> "$destination/v2-legacy-native.bin"
      fi
    else
      cp remote/* "$destination/"
      if [ "$GH_TEST_MODE" = corrupt ]; then
        printf 'changed without removing version records' >> "$destination/$DEVICE_SLUGS.factory.bin"
      fi
    fi
    ;;
  edit) [ "$4" = --draft=false ]; touch made-public ;;
  *) exit 1 ;;
esac
""")
        gh.chmod(0o755)
        git = base / "git"
        git.write_text("""#!/bin/bash
set -eu
[ "$*" = "show HEAD:scripts/native_stable_releases.py" ]
cat scripts/native_stable_releases.py
""")
        git.chmod(0o755)
        for mode in ("valid", "corrupt", "corrupt-native", "missing-native", "published"):
            case = base / mode
            case.mkdir()
            (case / "scripts").symlink_to(firmware_release.ROOT / "scripts", target_is_directory=True)
            (case / "firmware").mkdir()
            (case / "native").mkdir()
            make_release_files(case / "firmware", REAL_SLUG)
            for board in ("v1", "v2", "v3"):
                if mode == "missing-native" and board == "v2":
                    continue
                image = bytearray(288)
                image[0] = 0xE9
                image[12] = 0x12
                image[23] = 1
                image[32:36] = bytes.fromhex("3254cdab")
                image[48:48 + len(VERSION)] = VERSION.encode("ascii")
                project = f"espframe_legacy_{board}".encode("ascii")
                image[80:80 + len(project)] = project
                image[176] = 1
                (case / "native" / f"{board}-legacy-native.bin").write_bytes(image)
            env = {**os.environ, "PATH": f"{base}{os.pathsep}{os.environ['PATH']}",
                   "VERSION": VERSION, "DEVICE_SLUGS": REAL_SLUG, "GH_TEST_MODE": mode,
                   "RUNNER_TEMP": str(case)}
            result = subprocess.run(
                ["bash", "-eo", "pipefail", "-c", "\n".join(commands)], cwd=case, env=env,
                capture_output=True, text=True, timeout=30,
            )
            expected = {
                "valid": ["view", "upload", "upload", "download", "download", "edit"],
                "corrupt": ["view", "upload", "upload", "download"],
                "corrupt-native": ["view", "upload", "upload", "download", "download"],
                "missing-native": ["view", "upload"],
                "published": ["view"],
            }[mode]
            assert (case / "calls").read_text().splitlines() == expected, result.stderr
            assert (result.returncode == 0) == (mode == "valid"), result.stderr
            assert (case / "made-public").exists() == (mode == "valid")


def test_versions_index_accepts_current_plus_four_previous_releases() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        make_release_files(base)
        versions = [VERSION, "v9.8.6", "v9.8.5", "v9.8.4", "v9.8.3"]
        write_versions_index(base, SLUG, versions)
        run_ok(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])


def test_versions_index_rejects_duplicate_malformed_and_missing_assets() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        make_release_files(base)
        write_versions_index(base, SLUG, [VERSION, "v9.8.6"])
        index_path = base / "versions.json"

        index = json.loads(index_path.read_text())
        index["versions"].append(dict(index["versions"][1]))
        index_path.write_text(json.dumps(index))
        run_fails(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])

        write_versions_index(base, SLUG, [VERSION, "v9.8.6"])
        index = json.loads(index_path.read_text())
        index["versions"][1]["version"] = "v9.8"
        index_path.write_text(json.dumps(index))
        run_fails(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])

        write_versions_index(base, SLUG, [VERSION, "v9.8.6"])
        (base / "versions" / "v9.8.6" / f"{SLUG}.ota.bin").unlink()
        run_fails(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])


def test_stage_directory_uses_each_devices_public_paths() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        stable_source = base / "firmware"
        beta_source = stable_source / "beta"
        stable_source.mkdir()
        beta_source.mkdir()
        slugs = [REAL_DEVICE.slug, NESTED_DEVICE.slug]
        for slug in slugs:
            make_release_files(stable_source, slug=slug)
            make_release_files(beta_source, slug=slug, version=BETA_VERSION)

        run_ok([
            "stage-directory",
            "--source", str(stable_source),
            "--dir", str(base),
            "--slugs", *slugs,
        ])
        run_ok([
            "stage-directory",
            "--source", str(beta_source),
            "--dir", str(base),
            "--slugs", *slugs,
            "--beta",
        ])

        stable_manifest = base / NESTED_DEVICE.public_manifest
        beta_manifest = base / NESTED_DEVICE.public_beta_manifest
        assert stable_manifest.is_file()
        assert beta_manifest.is_file()
        assert (stable_manifest.parent / f"{NESTED_DEVICE.slug}.factory.bin").is_file()
        assert (stable_manifest.parent / f"{NESTED_DEVICE.slug}.ota.bin").is_file()
        assert (beta_manifest.parent / f"{NESTED_DEVICE.slug}.factory.bin").is_file()
        assert (beta_manifest.parent / f"{NESTED_DEVICE.slug}.ota.bin").is_file()

        run_ok([
            "verify-directory",
            "--version", VERSION,
            "--dir", str(base / "firmware"),
            "--slugs", *slugs,
        ])


def test_optional_beta_staging_skips_fully_missing_device() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        stable_source = base / "firmware"
        beta_source = base / "firmware" / "beta"
        beta_source.mkdir(parents=True)
        slugs = [REAL_DEVICE.slug, NESTED_DEVICE.slug]
        for slug in slugs:
            make_release_files(stable_source, slug=slug)
        make_release_files(beta_source, slug=REAL_DEVICE.slug, version=BETA_VERSION)

        run_ok([
            "stage-directory",
            "--source", str(stable_source),
            "--dir", str(base),
            "--slugs", *slugs,
        ])
        run_ok([
            "stage-directory",
            "--source", str(beta_source),
            "--dir", str(base),
            "--slugs", *slugs,
            "--beta",
            "--allow-missing",
        ])
        assert (base / REAL_DEVICE.public_beta_manifest).is_file()
        assert not (base / NESTED_DEVICE.public_beta_manifest).exists()
        run_ok([
            "verify-directory",
            "--version", VERSION,
            "--dir", str(stable_source),
            "--slugs", *slugs,
        ])


def test_optional_beta_staging_rejects_partial_device_assets() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        beta_source = base / "firmware" / "beta"
        beta_source.mkdir(parents=True)
        (beta_source / f"{NESTED_DEVICE.slug}.manifest.json").write_text("{}")

        run_fails([
            "stage-directory",
            "--source", str(beta_source),
            "--dir", str(base),
            "--slugs", NESTED_DEVICE.slug,
            "--beta",
            "--allow-missing",
        ])


def test_optional_stable_verification_skips_fully_missing_device() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        make_release_files(base, slug=REAL_DEVICE.slug)

        run_ok([
            "verify-directory",
            "--version", VERSION,
            "--dir", str(base),
            "--slugs", REAL_DEVICE.slug, NESTED_DEVICE.slug,
            "--allow-missing-slugs", NESTED_DEVICE.slug,
        ])


def test_optional_stable_verification_rejects_partial_device_assets() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        (base / f"{NESTED_DEVICE.slug}.manifest.json").write_text("{}")

        run_fails([
            "verify-directory",
            "--version", VERSION,
            "--dir", str(base),
            "--slugs", NESTED_DEVICE.slug,
            "--allow-missing-slugs", NESTED_DEVICE.slug,
        ])


def test_optional_stable_verification_requires_existing_device() -> None:
    with TemporaryDirectory() as tmp:
        run_fails([
            "verify-directory",
            "--version", VERSION,
            "--dir", tmp,
            "--slugs", REAL_DEVICE.slug, NESTED_DEVICE.slug,
            "--allow-missing-slugs", NESTED_DEVICE.slug,
        ])


def test_inject_replaces_factory_placeholder() -> None:
    with TemporaryDirectory() as tmp:
        root = Path(tmp)
        factory_yaml = root / REAL_DEVICE.build_yaml
        factory_yaml.parent.mkdir(parents=True)
        factory_yaml.write_text('substitutions:\n  firmware_version: "0.0.0"\n')
        original_root = firmware_release.ROOT
        try:
            firmware_release.ROOT = root
            run_ok(["inject", "--slug", REAL_SLUG, "--version", VERSION])
            assert f'  firmware_version: "{VERSION}"' in factory_yaml.read_text()
            run_fails(["inject", "--slug", REAL_SLUG, "--version", VERSION])
        finally:
            firmware_release.ROOT = original_root


def test_short_version_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        factory = base / f"{SLUG}.factory.bin"
        ota = base / f"{SLUG}.ota.bin"
        write_release_image(factory, "v1.2")
        write_release_image(ota, "v1.2")
        run_fails([
            "manifest",
            "--slug", SLUG,
            "--chip", CHIP,
            "--version", "v1.2",
            "--factory", str(factory),
            "--ota", str(ota),
            "--out", str(base / f"{SLUG}.manifest.json"),
        ])


def test_placeholder_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        write_release_image(ota, VERSION, project_version_string("dev"))
        run_ok([
            "manifest",
            "--slug", SLUG,
            "--chip", CHIP,
            "--version", VERSION,
            "--factory", str(factory),
            "--ota", str(ota),
            "--out", str(manifest),
        ])
        run_fails([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_unrelated_placeholder_strings_pass() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        write_release_image(ota, VERSION, "Version unknown", "Dev", "0.0.0", "Dev build", "dev")
        run_ok([
            "manifest",
            "--slug", SLUG,
            "--chip", CHIP,
            "--version", VERSION,
            "--factory", str(factory),
            "--ota", str(ota),
            "--out", str(manifest),
        ])
        run_ok([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_project_log_without_api_metadata_passes() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        factory = base / f"{SLUG}.factory.bin"
        ota = base / f"{SLUG}.ota.bin"
        manifest = base / f"{SLUG}.manifest.json"
        write_image(factory, VERSION, project_version_string(VERSION))
        write_image(ota, VERSION, project_version_string(VERSION))
        run_ok([
            "manifest",
            "--slug", SLUG,
            "--chip", CHIP,
            "--version", VERSION,
            "--factory", str(factory),
            "--ota", str(ota),
            "--out", str(manifest),
        ])
        run_ok([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_wrong_manifest_version_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        data = json.loads(manifest.read_text())
        data["version"] = "v0.0.1"
        manifest.write_text(json.dumps(data))
        run_fails([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_wrong_md5_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        write_image(ota, VERSION, "changed-after-manifest")
        run_fails([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_missing_asset_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        make_release_files(base)
        (base / f"{SLUG}.factory.bin").unlink()
        run_fails(["verify-directory", "--version", VERSION, "--dir", str(base), "--slugs", SLUG])


def test_wrong_slug_path_fails() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        manifest, factory, ota = make_release_files(base)
        data = json.loads(manifest.read_text())
        data["builds"][0]["ota"]["path"] = "other-panel.ota.bin"
        manifest.write_text(json.dumps(data))
        run_fails([
            "verify-files",
            "--slug", SLUG,
            "--version", VERSION,
            "--manifest", str(manifest),
            "--factory", str(factory),
            "--ota", str(ota),
        ])


def test_public_pages_verification() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        firmware_dir = base / "firmware"
        firmware_dir.mkdir(parents=True)

        manifest, _, _ = make_release_files(firmware_dir, slug=REAL_SLUG)
        manifest.rename(firmware_dir / firmware_release.public_manifest_name(REAL_SLUG))
        write_versions_index(firmware_dir, REAL_SLUG, [VERSION, "v9.8.6"])

        beta_dir = firmware_dir / "beta"
        beta_dir.mkdir()
        beta_manifest, _, _ = make_release_files(beta_dir, slug=REAL_SLUG, version=BETA_VERSION)
        beta_manifest.rename(beta_dir / firmware_release.public_manifest_name(REAL_SLUG, beta=True))

        run_ok([
            "verify-directory",
            "--version", VERSION,
            "--dir", str(firmware_dir),
            "--slugs", REAL_SLUG,
        ])

        handler = partial(QuietHandler, directory=str(base))
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_ok([
                "verify-pages",
                "--version", VERSION,
                "--base-url", f"http://127.0.0.1:{server.server_port}",
                "--slugs", REAL_SLUG,
            ])
        finally:
            server.shutdown()
            thread.join(timeout=5)


def test_public_pages_verification_allows_unreleased_device() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        firmware_dir = base / "firmware"
        firmware_dir.mkdir(parents=True)

        manifest, _, _ = make_release_files(firmware_dir, slug=REAL_SLUG)
        manifest.rename(firmware_dir / firmware_release.public_manifest_name(REAL_SLUG))
        write_versions_index(firmware_dir, REAL_SLUG, [VERSION])

        handler = partial(QuietHandler, directory=str(base))
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_ok([
                "verify-pages",
                "--version", VERSION,
                "--base-url", f"http://127.0.0.1:{server.server_port}",
                "--slugs", REAL_SLUG, NESTED_DEVICE.slug,
                "--allow-missing-slugs", NESTED_DEVICE.slug,
            ])
        finally:
            server.shutdown()
            thread.join(timeout=5)


def test_public_pages_verification_requires_existing_device() -> None:
    with TemporaryDirectory() as tmp:
        handler = partial(QuietHandler, directory=tmp)
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_fails([
                "verify-pages",
                "--version", VERSION,
                "--base-url", f"http://127.0.0.1:{server.server_port}",
                "--slugs", REAL_SLUG, NESTED_DEVICE.slug,
                "--allow-missing-slugs", NESTED_DEVICE.slug,
            ])
        finally:
            server.shutdown()
            thread.join(timeout=5)


def test_public_pages_verification_rejects_missing_optional_device_binary() -> None:
    with TemporaryDirectory() as tmp:
        base = Path(tmp)
        public_manifest = base / NESTED_DEVICE.public_manifest
        public_manifest.parent.mkdir(parents=True)
        manifest, _, ota = make_release_files(public_manifest.parent, slug=NESTED_DEVICE.slug)
        manifest.rename(public_manifest)
        ota.unlink()

        handler = partial(QuietHandler, directory=str(base))
        server = ThreadingHTTPServer(("127.0.0.1", 0), handler)
        thread = Thread(target=server.serve_forever, daemon=True)
        thread.start()
        try:
            run_fails([
                "verify-pages",
                "--version", VERSION,
                "--base-url", f"http://127.0.0.1:{server.server_port}",
                "--slugs", NESTED_DEVICE.slug,
                "--allow-missing-slugs", NESTED_DEVICE.slug,
            ])
        finally:
            server.shutdown()
            thread.join(timeout=5)


def main() -> int:
    run_discovered_tests(globals())
    print("Firmware release helper tests passed.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
