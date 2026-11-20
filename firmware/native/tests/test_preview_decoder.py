#!/usr/bin/env python3
"""Run orientation and color checks against the same decoders used by firmware."""

from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
import sys
from tempfile import TemporaryDirectory


ROOT = Path(__file__).resolve().parents[3]
NATIVE = ROOT / "firmware/native"
LIBRARIES = [ROOT / "components" / name for name in ("libjpeg-turbo-esp32", "libwebp-esp32")]


def run(*args):
    subprocess.run(args, check=True)


with TemporaryDirectory(prefix="espframe-native-decode-") as work:
    build = Path(work)
    config = build / "src/webp/config.h"
    config.parent.mkdir(parents=True)
    config.write_text("// Generic host decoder.\n")
    inputs = []
    for library in LIBRARIES:
        sources = re.findall(r'"(src/[^" ]+\.c)"', (library / "CMakeLists.txt").read_text())
        inputs.extend((library, library / source) for source in sources)

    def compile_source(entry):
        library, source = entry
        obj = build / (source.stem + ".o")
        byte_order = ["-DWEBP_SWAP_16BIT_CSP=1"] if library == LIBRARIES[1] else []
        run("cc", "-O1", "-DNDEBUG", "-DHAVE_CONFIG_H", "-ffunction-sections", "-fdata-sections",
            *byte_order, "-I", build, "-I", library, "-I", library / "src", "-c", source, "-o", obj)
        return obj

    with ThreadPoolExecutor(max_workers=4) as pool:
        objects = list(pool.map(compile_source, inputs))
    binary = build / "preview-test"
    discard_unused = "-Wl,-dead_strip" if sys.platform == "darwin" else "-Wl,--gc-sections"
    run("c++", "-std=c++20", "-O1", "-Wall", "-Wextra", "-Werror",
        "-I", NATIVE / "main", "-I", NATIVE / "tests/stubs",
        "-I", ROOT / "components/remote_image",
        "-I", LIBRARIES[0] / "src", "-I", LIBRARIES[1] / "src",
        NATIVE / "main/preview_decoder.cpp", NATIVE / "main/p4_jpeg_decoder.cpp",
        NATIVE / "tests/preview_decoder_test.cpp", *objects, discard_unused,
        "-lm", "-o", binary)
    fixtures = NATIVE / "tests/fixtures"
    run(binary, fixtures / "quadrants.jpg", fixtures / "quadrants-progressive.jpg", fixtures / "quadrants.webp",
        fixtures / "portrait-primary.jpg", fixtures / "portrait-companion.webp",
        fixtures / "quadrants-odd-progressive.jpg", ROOT / "tests/fixtures/webp/landscape-lossy.webp",
        fixtures / "portrait-scaled-progressive.jpg", fixtures / "portrait-scaled-sequential.jpg")
    print("Native JPEG and WebP preview tests passed")
