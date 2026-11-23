"""Stage verified native preview OTA images and compact board indexes for Pages."""

import argparse
import hashlib
import json
from pathlib import Path
import re
import subprocess
import tempfile


BOARDS = ("v1", "v2", "v3")
VERSION = re.compile(r"v(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)-preview\.(0|[1-9][0-9]*)")


def parse_version(version):
    match = VERSION.fullmatch(version)
    if match and len(version) <= 31:
        parts = tuple(map(int, match.groups()))
        if all(part <= 0xFFFFFFFF for part in parts):
            return parts
    return None


def image_metadata(image, board, version):
    if (not 288 <= len(image) <= 4 * 1024 * 1024 or image[0] != 0xE9 or
            image[12:14] != b"\x12\x00" or image[23] != 1 or
            image[32:36] != b"\x32\x54\xcd\xab" or
            image[48:80].split(b"\0", 1)[0] != version.encode("ascii") or
            image[80:112].split(b"\0", 1)[0] != f"espframe_native_{board}".encode("ascii")):
        raise ValueError(f"{version} does not contain a versioned native {board} OTA image")
    return {
        "version": version,
        "size": len(image),
        "sha256": hashlib.sha256(image).hexdigest(),
        "elf_sha256": image[176:208].hex(),
    }


def published_versions(releases):
    versions = []
    for release in releases:
        tag = release["tagName"]
        if release["isDraft"] or not release["isPrerelease"] or not tag.startswith("native-"):
            continue
        version = tag.removeprefix("native-")
        if parse_version(version) is not None:
            versions.append(version)
    return sorted(set(versions), key=parse_version, reverse=True)[:5]


def stage(output):
    releases = json.loads(subprocess.check_output([
        "gh", "release", "list", "--limit", "100", "--json", "tagName,isDraft,isPrerelease",
    ]))
    indexes = {board: [] for board in BOARDS}
    for version in published_versions(releases):
        with tempfile.TemporaryDirectory() as temp:
            subprocess.run([
                "gh", "release", "download", f"native-{version}", "--dir", temp,
                "--pattern", "v[123]-ota.bin", "--pattern", "SHA256SUMS",
            ], check=True)
            checksums = (Path(temp) / "SHA256SUMS").read_text().splitlines()
            images = {}
            for board in BOARDS:
                filename = f"{board}-ota.bin"
                image = (Path(temp) / filename).read_bytes()
                metadata = image_metadata(image, board, version)
                if f"{metadata['sha256']}  {filename}" not in checksums:
                    raise ValueError(f"{version} checksum mismatch for {filename}")
                images[board] = image, metadata
            destination = output / version
            destination.mkdir(parents=True, exist_ok=True)
            for board, (image, metadata) in images.items():
                (destination / f"{board}-ota.bin").write_bytes(image)
                indexes[board].append(metadata)

    output.mkdir(parents=True, exist_ok=True)
    for board, versions in indexes.items():
        (output / f"{board}.json").write_text(json.dumps(
            {"schema": 1, "board": board, "versions": versions}, separators=(",", ":")
        ) + "\n")


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    stage(parser.parse_args().output)
