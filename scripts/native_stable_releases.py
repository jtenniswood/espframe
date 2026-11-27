"""Validate stable legacy-layout native assets and stage their board-specific feeds."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile

BOARDS = ("v1", "v2", "v3")

VERSION = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)")
ASSETS = {board: f"{board}-legacy-native.bin" for board in BOARDS}
ESP_HOME_SLUGS = {"v1": "immich-frame", "v2": "immich-frame-v2", "v3": "immich-frame-v3"}
MAX_IMAGE_BYTES = 4 * 1024 * 1024


def stable_version(version):
    match = VERSION.fullmatch(version)
    if match and len(version) <= 31:
        numbers = tuple(map(int, match.groups()))
        if all(number <= 0xFFFFFFFF for number in numbers):
            return numbers
    return None


def image_metadata(image, board, version):
    if (not stable_version(version) or board not in ASSETS or
            not 288 <= len(image) <= MAX_IMAGE_BYTES or image[0] != 0xE9 or
            image[12:14] != b"\x12\x00" or image[23] != 1 or
            image[32:36] != b"\x32\x54\xcd\xab" or
            image[48:80].split(b"\0", 1)[0] != version.encode("ascii") or
            image[80:112].split(b"\0", 1)[0] != f"espframe_legacy_{board}".encode("ascii") or
            not any(image[176:208])):
        raise ValueError(f"{version} does not contain a stable legacy-layout {board} OTA image")
    return {
        "version": version,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "elf_sha256": image[176:208].hex(),
    }


def verify(directory, version):
    if set(ASSETS.values()) != {asset.name for asset in directory.iterdir()}:
        raise ValueError(f"{version} needs a complete set of legacy native images")
    for board, filename in ASSETS.items():
        image_metadata((directory / filename).read_bytes(), board, version)


def verify_bridges(bridges, native, repository, version):
    verify(native, version)
    for board, filename in ASSETS.items():
        image = (native / filename).read_bytes()
        bridge = (bridges / f"{ESP_HOME_SLUGS[board]}.ota.bin").read_bytes()
        url = f"https://github.com/{repository}/releases/download/{version}/{filename}"
        pins = (url, hashlib.md5(image).hexdigest(), image[176:208].hex())
        if not all(pin.encode("ascii") + b"\0" in bridge for pin in pins):
            raise ValueError(f"{version} {board} ESPHome bridge does not pin the matching native image")


def stage_release(source, output, version, indexes):
    verify(source, version)
    destination = output / version
    destination.mkdir(parents=True, exist_ok=True)
    for board, filename in ASSETS.items():
        image = (source / filename).read_bytes()
        (destination / f"{board}-ota.bin").write_bytes(image)
        indexes[board].append(image_metadata(image, board, version))


def stage(output):
    releases = json.loads(subprocess.check_output([
        "gh", "api", "repos/{owner}/{repo}/releases?per_page=100",
    ]))
    versions = [
        release["tag_name"] for release in releases
        if not release["draft"] and not release["prerelease"] and stable_version(release["tag_name"])
        and set(ASSETS.values()).intersection(asset["name"] for asset in release["assets"])
    ]
    indexes = {board: [] for board in BOARDS}
    for version in sorted(set(versions), key=stable_version, reverse=True)[:5]:
        with tempfile.TemporaryDirectory() as temp:
            subprocess.run([
                "gh", "release", "download", version, "--dir", temp, "--pattern", "v[123]-legacy-native.bin",
            ], check=True)
            stage_release(Path(temp), output, version, indexes)
    if indexes["v1"]:
        for board, entries in indexes.items():
            (output / f"{board}.json").write_text(json.dumps(
                {"schema": 1, "board": board, "versions": entries}, separators=(",", ":")
            ) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    subparsers = parser.add_subparsers(dest="command", required=True)
    pins = subparsers.add_parser("pins")
    pins.add_argument("image", type=Path)
    pins.add_argument("board", choices=BOARDS)
    pins.add_argument("version")
    check = subparsers.add_parser("verify")
    check.add_argument("directory", type=Path)
    check.add_argument("version")
    bridges = subparsers.add_parser("verify-bridges")
    bridges.add_argument("bridges", type=Path)
    bridges.add_argument("native", type=Path)
    bridges.add_argument("repository")
    bridges.add_argument("version")
    feed = subparsers.add_parser("stage")
    feed.add_argument("directory", type=Path)
    args = parser.parse_args()
    if args.command == "pins":
        image = args.image.read_bytes()
        metadata = image_metadata(image, args.board, args.version)
        print(hashlib.md5(image).hexdigest(), metadata["elf_sha256"])
    elif args.command == "verify":
        verify(args.directory, args.version)
    elif args.command == "verify-bridges":
        verify_bridges(args.bridges, args.native, args.repository, args.version)
    else:
        stage(args.directory)
