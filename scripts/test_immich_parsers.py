#!/usr/bin/env python3
"""Exercise production USE_JSON parsers with the firmware's pinned ArduinoJson."""
import hashlib
import io
import subprocess
import tarfile
import tempfile
import urllib.request
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
VERSION = "7.4.3"
SHA256 = "a2a8a277fc3cd0404499199bf1468682ea3688f354be0eaf6af8648366911257"
URL = f"https://codeload.github.com/bblanchon/ArduinoJson/tar.gz/refs/tags/v{VERSION}"


def main():
    cache = ROOT / ".esphome/host-tests"
    cache.mkdir(parents=True, exist_ok=True)
    archive = cache / f"ArduinoJson-{VERSION}.tar.gz"
    if not archive.exists():
        with urllib.request.urlopen(URL, timeout=60) as response:
            data = response.read()
        if hashlib.sha256(data).hexdigest() != SHA256:
            raise RuntimeError("ArduinoJson download checksum mismatch")
        archive.write_bytes(data)
    data = archive.read_bytes()
    if hashlib.sha256(data).hexdigest() != SHA256:
        raise RuntimeError(f"ArduinoJson cache checksum mismatch: {archive}")
    with tempfile.TemporaryDirectory(prefix="espframe-parser-tests-") as directory:
        work = Path(directory)
        prefix = f"ArduinoJson-{VERSION}/src/"
        with tarfile.open(fileobj=io.BytesIO(data), mode="r:gz") as source:
            for member in source.getmembers():
                if not member.isfile() or not member.name.startswith(prefix):
                    continue
                relative = Path(member.name[len(prefix):])
                if relative.is_absolute() or ".." in relative.parts:
                    raise RuntimeError("Unsafe ArduinoJson archive member")
                target = work / "include" / relative
                target.parent.mkdir(parents=True, exist_ok=True)
                target.write_bytes(source.extractfile(member).read())
        binary = work / "immich_parser_tests"
        subprocess.run(["g++", "-std=c++17", "-DUSE_JSON", "-I.", "-Itests/stubs",
                        "-I" + str(work / "include"), "tests/immich_parser_tests.cpp",
                        "-o", str(binary)], cwd=ROOT, check=True)
        subprocess.run([str(binary)], cwd=ROOT, check=True)


if __name__ == "__main__":
    main()
