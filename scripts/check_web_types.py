#!/usr/bin/env python3
"""Typecheck every assembled UI source plus its explicitly imported modules."""
import subprocess
import tempfile
from pathlib import Path
from asset_generation.web_bundle import web_app_source

ROOT = Path(__file__).resolve().parents[1]


def typecheck(source_text: str, **options):
    with tempfile.TemporaryDirectory(prefix="espframe-web-types-") as directory:
        source = Path(directory) / "app.ts"
        source.write_text(source_text.replace('from "./', 'from "' + str(ROOT / "docs/webserver/src") + '/'))
        return subprocess.run([str(ROOT / "node_modules/.bin/tsc"), "--noEmit", "--strict", "false",
                                "--target", "ES2018", "--lib", "ES2018,DOM",
                                "--skipLibCheck", str(source)], cwd=ROOT, **options)


if __name__ == "__main__":
    raise SystemExit(typecheck(web_app_source()).returncode)
