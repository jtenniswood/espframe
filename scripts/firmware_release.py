#!/usr/bin/env python3
"""Firmware release helpers used by CI.

The GitHub release tag is the source of truth for public firmware versions.
This script keeps YAML patching, manifest generation, and asset verification in
one tested place instead of duplicating shell snippets across workflows.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import re
import sys
import time
import urllib.error
import urllib.request
from dataclasses import dataclass
from pathlib import Path
from tempfile import TemporaryDirectory
from urllib.parse import urljoin

ROOT = Path(__file__).resolve().parent.parent
FIRMWARE_VERSION_PLACEHOLDER = '  firmware_version: "0.0.0"'
PLACEHOLDER_STRINGS = {"dev", "0.0.0", "main"}
RELEASE_URL_BASE = "https://github.com/jtenniswood/espframe/releases/tag/"
PROJECT_NAME = "jtenniswood.immich-frame"
RELEASE_VERSION_RE = re.compile(r"^v\d+\.\d+\.\d+(?:-[0-9A-Za-z][0-9A-Za-z.-]*)?$")


@dataclass(frozen=True)
class Device:
    slug: str
    name: str
    chip: str
    build_yaml: str
    public_manifest: str
    public_beta_manifest: str


DEVICES = {
    "immich-frame": Device(
        slug="immich-frame",
        name="Immich Frame",
        chip="ESP32-P4",
        build_yaml="builds/guition-esp32-p4-jc8012p4a1.factory.yaml",
        public_manifest="firmware/manifest.json",
        public_beta_manifest="firmware/beta/manifest.json",
    ),
    "immich-frame-7inch": Device(
        slug="immich-frame-7inch",
        name="Immich Frame 7-inch",
        chip="ESP32-P4",
        build_yaml="builds/guition-esp32-p4-jc1060p470.factory.yaml",
        public_manifest="firmware/manifest-7inch.json",
        public_beta_manifest="firmware/beta/manifest-7inch.json",
    ),
}


class FirmwareReleaseError(RuntimeError):
    pass


def assert_release_version(version: str) -> None:
    if not RELEASE_VERSION_RE.match(version):
        raise FirmwareReleaseError(
            f"{version!r} is not a full release tag like v1.2.3 or v1.2.3-beta.1"
        )
    if version in PLACEHOLDER_STRINGS:
        raise FirmwareReleaseError(f"{version!r} is a placeholder, not a release tag")


def device_for_slug(slug: str) -> Device | None:
    return DEVICES.get(slug)


def display_name_for_slug(slug: str) -> str:
    device = device_for_slug(slug)
    if device:
        return device.name
    return slug.replace("-", " ").title()


def expected_chip_for_slug(slug: str) -> str | None:
    device = device_for_slug(slug)
    return device.chip if device else None


def build_yaml_for_slug(slug: str) -> Path:
    device = device_for_slug(slug)
    if device:
        return ROOT / device.build_yaml
    return ROOT / "builds" / f"{slug}.factory.yaml"


def public_manifest_path(slug: str, beta: bool = False) -> str:
    device = device_for_slug(slug)
    if device:
        return device.public_beta_manifest if beta else device.public_manifest
    return f"firmware/{slug}/{'beta/' if beta else ''}manifest.json"


def public_manifest_name(slug: str, beta: bool = False) -> str:
    return Path(public_manifest_path(slug, beta=beta)).name


def manifest_names(slug: str, beta: bool = False) -> list[str]:
    public_name = public_manifest_name(slug, beta=beta)
    names = [f"{slug}.manifest.json", public_name]
    if slug not in DEVICES or public_name == "manifest.json":
        names.append("manifest.json")
    result: list[str] = []
    for name in names:
        if name not in result:
            result.append(name)
    return result


def md5sum(path: Path) -> str:
    digest = hashlib.md5()
    with path.open("rb") as f:
        for chunk in iter(lambda: f.read(1024 * 1024), b""):
            digest.update(chunk)
    return digest.hexdigest()


def printable_string_list(path: Path, min_length: int = 3) -> list[str]:
    data = path.read_bytes()
    strings: list[str] = []
    current = bytearray()

    def flush() -> None:
        nonlocal current
        if len(current) >= min_length:
            strings.append(current.decode("ascii", errors="ignore"))
        current = bytearray()

    for byte in data:
        if 32 <= byte <= 126:
            current.append(byte)
        else:
            flush()
    flush()
    return strings


def contains_sequence(strings: list[str], sequence: list[str]) -> bool:
    if len(strings) < len(sequence):
        return False
    last_start = len(strings) - len(sequence) + 1
    return any(strings[idx:idx + len(sequence)] == sequence for idx in range(last_start))


def require_file(path: Path, label: str) -> None:
    if not path.is_file():
        raise FirmwareReleaseError(f"{label} not found: {path}")


def assert_binary_version(path: Path, version: str) -> None:
    require_file(path, "firmware image")
    strings = printable_string_list(path)
    string_set = set(strings)
    if version not in string_set:
        raise FirmwareReleaseError(f"{path} does not contain firmware version {version}")

    expected_log_version = f"Project {PROJECT_NAME} version {version}"
    expected_project_metadata = ["package_import_url", version, PROJECT_NAME, "project_version"]
    if expected_log_version not in string_set and not contains_sequence(strings, expected_project_metadata):
        raise FirmwareReleaseError(f"{path} does not contain ESPHome project version {version}")

    for placeholder in PLACEHOLDER_STRINGS:
        placeholder_log_version = f"Project {PROJECT_NAME} version {placeholder}"
        placeholder_project_metadata = ["package_import_url", placeholder, PROJECT_NAME, "project_version"]
        if placeholder_log_version in string_set or contains_sequence(strings, placeholder_project_metadata):
            raise FirmwareReleaseError(f"{path} still contains firmware version placeholder {placeholder}")


def load_manifest(path: Path) -> dict:
    require_file(path, "firmware manifest")
    try:
        return json.loads(path.read_text())
    except json.JSONDecodeError as exc:
        raise FirmwareReleaseError(f"{path} is not valid JSON: {exc}") from exc


def first_build(manifest: dict, manifest_path: Path) -> dict:
    builds = manifest.get("builds")
    if not isinstance(builds, list) or not builds:
        raise FirmwareReleaseError(f"{manifest_path} has no firmware builds")
    if not isinstance(builds[0], dict):
        raise FirmwareReleaseError(f"{manifest_path} first build is not an object")
    return builds[0]


def verify_manifest(
    manifest_path: Path,
    slug: str,
    version: str,
    ota_md5: str,
    require_factory: bool = True,
) -> dict:
    assert_release_version(version)
    manifest = load_manifest(manifest_path)
    actual_version = str(manifest.get("version", "")).strip()
    if actual_version != version:
        raise FirmwareReleaseError(f"{manifest_path} version {actual_version!r} does not match {version!r}")
    if actual_version in PLACEHOLDER_STRINGS:
        raise FirmwareReleaseError(f"{manifest_path} contains placeholder version {actual_version}")
    if manifest.get("home_assistant_domain") != "esphome":
        raise FirmwareReleaseError(f"{manifest_path} home_assistant_domain must be esphome")

    build = first_build(manifest, manifest_path)
    expected_chip = expected_chip_for_slug(slug)
    if expected_chip and build.get("chipFamily") != expected_chip:
        raise FirmwareReleaseError(f"{manifest_path} chipFamily must be {expected_chip}")

    ota = build.get("ota")
    if not isinstance(ota, dict):
        raise FirmwareReleaseError(f"{manifest_path} build has no ota object")

    expected_ota_path = f"{slug}.ota.bin"
    if ota.get("path") != expected_ota_path:
        raise FirmwareReleaseError(f"{manifest_path} ota.path must be {expected_ota_path}")
    if ota.get("md5") != ota_md5:
        raise FirmwareReleaseError(f"{manifest_path} ota.md5 does not match {expected_ota_path}")
    expected_release_url = RELEASE_URL_BASE + version
    if ota.get("release_url") != expected_release_url:
        raise FirmwareReleaseError(f"{manifest_path} release_url must be {expected_release_url}")

    if require_factory:
        expected_factory_path = f"{slug}.factory.bin"
        parts = build.get("parts")
        if not isinstance(parts, list) or not parts:
            raise FirmwareReleaseError(f"{manifest_path} build has no factory parts")
        first_part = parts[0]
        if not isinstance(first_part, dict):
            raise FirmwareReleaseError(f"{manifest_path} first factory part is not an object")
        if first_part.get("path") != expected_factory_path:
            raise FirmwareReleaseError(f"{manifest_path} factory path must be {expected_factory_path}")
        if first_part.get("offset") != 0:
            raise FirmwareReleaseError(f"{manifest_path} factory offset must be 0")

    return build


def verify_files(slug: str, version: str, manifest: Path, factory: Path | None, ota: Path) -> None:
    require_file(manifest, "firmware manifest")
    require_file(ota, "OTA firmware")
    require_factory = factory is not None
    if require_factory:
        require_file(factory, "factory firmware")

    verify_manifest(manifest, slug, version, md5sum(ota), require_factory=require_factory)
    assert_binary_version(ota, version)
    if factory is not None:
        assert_binary_version(factory, version)


def find_first(paths: list[Path]) -> Path | None:
    for path in paths:
        if path.is_file():
            return path
    return None


def locate_release_files(base_dir: Path, slug: str) -> tuple[Path, Path, Path]:
    dirs = [base_dir / slug, base_dir]
    manifests = []
    factories = []
    otas = []
    for directory in dirs:
        manifests.extend(directory / name for name in manifest_names(slug))
        factories.append(directory / f"{slug}.factory.bin")
        otas.append(directory / f"{slug}.ota.bin")

    manifest = find_first(manifests)
    factory = find_first(factories)
    ota = find_first(otas)
    if manifest is None:
        raise FirmwareReleaseError(f"No manifest found for {slug} in {base_dir}")
    if factory is None:
        raise FirmwareReleaseError(f"No factory image found for {slug} in {base_dir}")
    if ota is None:
        raise FirmwareReleaseError(f"No OTA image found for {slug} in {base_dir}")
    return manifest, factory, ota


def locate_beta_files(base_dir: Path, slug: str) -> tuple[Path, Path | None, Path] | None:
    dirs = [base_dir / slug / "beta", base_dir / "beta" / slug, base_dir / "beta"]
    manifests = []
    factories = []
    otas = []
    for directory in dirs:
        manifests.extend(directory / name for name in manifest_names(slug, beta=True))
        factories.append(directory / f"{slug}.factory.bin")
        otas.append(directory / f"{slug}.ota.bin")

    manifest = find_first(manifests)
    if manifest is None:
        return None
    ota = find_first(otas)
    if ota is None:
        raise FirmwareReleaseError(f"Beta manifest exists but no OTA image was found for {slug} in {base_dir}")
    factory = find_first(factories)
    build = first_build(load_manifest(manifest), manifest)
    parts = build.get("parts")
    if isinstance(parts, list) and parts and factory is None:
        raise FirmwareReleaseError(f"Beta manifest exists but no factory image was found for {slug} in {base_dir}")
    return manifest, factory, ota


def manifest_version(path: Path) -> str:
    version = str(load_manifest(path).get("version", "")).strip()
    assert_release_version(version)
    return version


def verify_directory(base_dir: Path, slugs: list[str], version: str) -> None:
    for slug in slugs:
        manifest, factory, ota = locate_release_files(base_dir, slug)
        verify_files(slug, version, manifest, factory, ota)

        beta = locate_beta_files(base_dir, slug)
        if beta is not None:
            beta_manifest, beta_factory, beta_ota = beta
            verify_files(slug, manifest_version(beta_manifest), beta_manifest, beta_factory, beta_ota)


def fetch_url(url: str, timeout: int = 30) -> bytes:
    request = urllib.request.Request(url, headers={"User-Agent": "espframe-firmware-release-check"})
    with urllib.request.urlopen(request, timeout=timeout) as response:
        return response.read()


def download(url: str, path: Path) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_bytes(fetch_url(url))


def public_manifest_url(base_url: str, slug: str, beta: bool = False) -> str:
    path = public_manifest_path(slug, beta=beta)
    return base_url.rstrip("/") + "/" + path


def download_and_verify_public_slug(base_url: str, slug: str, version: str, out_dir: Path, beta: bool = False) -> None:
    manifest_url = public_manifest_url(base_url, slug, beta=beta)
    slug_dir = out_dir / slug / ("beta" if beta else "")
    manifest_path = slug_dir / "manifest.json"
    download(manifest_url, manifest_path)

    expected_version = manifest_version(manifest_path) if beta else version
    build = first_build(load_manifest(manifest_path), manifest_path)
    ota = build.get("ota")
    if not isinstance(ota, dict) or not ota.get("path"):
        raise FirmwareReleaseError(f"{manifest_url} has no OTA path")
    ota_path = slug_dir / f"{slug}.ota.bin"
    download(urljoin(manifest_url, ota["path"]), ota_path)

    factory_path: Path | None = None
    parts = build.get("parts")
    if isinstance(parts, list) and parts and isinstance(parts[0], dict) and parts[0].get("path"):
        factory_path = slug_dir / f"{slug}.factory.bin"
        download(urljoin(manifest_url, parts[0]["path"]), factory_path)
    elif not beta:
        raise FirmwareReleaseError(f"{manifest_url} has no factory path")

    verify_files(slug, expected_version, manifest_path, factory_path, ota_path)


def verify_pages(base_url: str, slugs: list[str], version: str, retries: int, delay: float) -> None:
    last_error: Exception | None = None
    for attempt in range(1, retries + 1):
        try:
            with TemporaryDirectory() as tmp:
                out_dir = Path(tmp)
                for slug in slugs:
                    download_and_verify_public_slug(base_url, slug, version, out_dir, beta=False)
                    try:
                        download_and_verify_public_slug(base_url, slug, version, out_dir, beta=True)
                    except urllib.error.HTTPError as exc:
                        if exc.code != 404:
                            raise
            return
        except Exception as exc:  # noqa: BLE001 - converted to CI-friendly error after retries
            last_error = exc
            if attempt >= retries:
                break
            print(f"Public firmware verification attempt {attempt} failed: {exc}", file=sys.stderr)
            time.sleep(delay)
    raise FirmwareReleaseError(f"Public firmware verification failed after {retries} attempts: {last_error}")


def cmd_inject(args: argparse.Namespace) -> None:
    assert_release_version(args.version)
    path = build_yaml_for_slug(args.slug)
    require_file(path, "factory build YAML")
    text = path.read_text()
    replacement = f'  firmware_version: "{args.version}"'
    if FIRMWARE_VERSION_PLACEHOLDER not in text:
        raise FirmwareReleaseError(f"Expected placeholder not found in {path}")
    path.write_text(text.replace(FIRMWARE_VERSION_PLACEHOLDER, replacement, 1))


def cmd_manifest(args: argparse.Namespace) -> None:
    assert_release_version(args.version)
    factory = Path(args.factory)
    ota = Path(args.ota)
    require_file(factory, "factory firmware")
    require_file(ota, "OTA firmware")
    chip = args.chip or expected_chip_for_slug(args.slug)
    if not chip:
        raise FirmwareReleaseError(f"No chip family provided for {args.slug}")
    data = {
        "name": display_name_for_slug(args.slug),
        "version": args.version,
        "home_assistant_domain": "esphome",
        "builds": [
            {
                "chipFamily": chip,
                "parts": [
                    {"path": f"{args.slug}.factory.bin", "offset": 0},
                ],
                "ota": {
                    "path": f"{args.slug}.ota.bin",
                    "md5": md5sum(ota),
                    "release_url": RELEASE_URL_BASE + args.version,
                },
            },
        ],
    }
    out = Path(args.out)
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_text(json.dumps(data, indent=2) + "\n")


def cmd_verify_files(args: argparse.Namespace) -> None:
    verify_files(args.slug, args.version, Path(args.manifest), Path(args.factory), Path(args.ota))


def cmd_verify_directory(args: argparse.Namespace) -> None:
    verify_directory(Path(args.dir), args.slugs, args.version)


def cmd_verify_pages(args: argparse.Namespace) -> None:
    verify_pages(args.base_url, args.slugs, args.version, args.retries, args.delay)


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description=__doc__)
    sub = parser.add_subparsers(dest="command", required=True)

    inject = sub.add_parser("inject", help="Inject a firmware version into a factory YAML")
    inject.add_argument("--slug", required=True)
    inject.add_argument("--version", required=True)
    inject.set_defaults(func=cmd_inject)

    manifest = sub.add_parser("manifest", help="Generate a firmware manifest")
    manifest.add_argument("--slug", required=True)
    manifest.add_argument("--chip")
    manifest.add_argument("--version", required=True)
    manifest.add_argument("--factory", required=True)
    manifest.add_argument("--ota", required=True)
    manifest.add_argument("--out", required=True)
    manifest.set_defaults(func=cmd_manifest)

    verify_files_cmd = sub.add_parser("verify-files", help="Verify one slug's firmware files")
    verify_files_cmd.add_argument("--slug", required=True)
    verify_files_cmd.add_argument("--version", required=True)
    verify_files_cmd.add_argument("--manifest", required=True)
    verify_files_cmd.add_argument("--factory", required=True)
    verify_files_cmd.add_argument("--ota", required=True)
    verify_files_cmd.set_defaults(func=cmd_verify_files)

    verify_directory_cmd = sub.add_parser("verify-directory", help="Verify firmware files for multiple slugs")
    verify_directory_cmd.add_argument("--version", required=True)
    verify_directory_cmd.add_argument("--dir", required=True)
    verify_directory_cmd.add_argument("--slugs", nargs="+", required=True)
    verify_directory_cmd.set_defaults(func=cmd_verify_directory)

    verify_pages_cmd = sub.add_parser("verify-pages", help="Verify public GitHub Pages firmware")
    verify_pages_cmd.add_argument("--version", required=True)
    verify_pages_cmd.add_argument("--base-url", required=True)
    verify_pages_cmd.add_argument("--slugs", nargs="+", required=True)
    verify_pages_cmd.add_argument("--retries", type=int, default=1)
    verify_pages_cmd.add_argument("--delay", type=float, default=15)
    verify_pages_cmd.set_defaults(func=cmd_verify_pages)

    return parser


def main(argv: list[str] | None = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)
    try:
        args.func(args)
    except FirmwareReleaseError as exc:
        print(f"::error::{exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    sys.exit(main())
