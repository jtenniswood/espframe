"""Embed the captive page and shared webserver styles in both encodings."""
from __future__ import annotations

import gzip
import subprocess

from .paths import ROOT


def captive_portal_header() -> str:
    template = (ROOT / "components/captive_portal/portal.html").read_text()
    source = template.replace("/* __ESPFRAME_WEB_STYLE__ */",
                              (ROOT / "docs/webserver/src/style.css").read_text()).encode()
    gzip_data = gzip.compress(source, compresslevel=9, mtime=0)
    # Python 3.11/3.12 may emit the host OS byte; keep generation identical to
    # later Python versions and across the developer machine and CI.
    gzip_data = gzip_data[:9] + b"\xff" + gzip_data[10:]
    brotli_data = subprocess.run(
        ["node", "-e", "const chunks=[]; process.stdin.on('data', b => chunks.push(b)); "
         "process.stdin.on('end', () => process.stdout.write(require('zlib').brotliCompressSync(Buffer.concat(chunks))));"],
        input=source, capture_output=True, check=True,
    ).stdout

    def array(data: bytes) -> str:
        return "\n".join("    " + ", ".join(f"0x{byte:02x}" for byte in data[i:i + 16]) + ","
                         for i in range(0, len(data), 16))

    return ("// ESPFRAME: generated from components/captive_portal/portal.html; run `npm run generate` to update.\n"
            "// Upstream page: esphome/esphome 2026.9.1; see README.md and PORTAL_LICENSE.\n"
            "#pragma once\n#include \"esphome/core/hal.h\"\n"
            "namespace esphome::captive_portal {\n#ifdef USE_CAPTIVE_PORTAL_GZIP\n"
            "constexpr uint8_t INDEX_GZ[] PROGMEM = {\n" + array(gzip_data) + "\n};\n#else\n"
            "constexpr uint8_t INDEX_GZ[] PROGMEM = {\n" + array(brotli_data) + "\n};\n#endif\n}\n")
