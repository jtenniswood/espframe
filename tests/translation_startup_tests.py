#!/usr/bin/env python3
"""Run the actual language callback against controlled startup and scheduler APIs."""
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
text = (ROOT / "common/addon/translations.yaml").read_text()
marker = "        - lambda: |-\n"
assert text.count(marker) == 1
body = text.split(marker, 1)[1]
assert all(not line.strip() or line.startswith("            ") for line in body.splitlines())
body = "\n".join(line[12:] for line in body.splitlines())
labels = set(re.findall(r"id\((\w+)\)", body)) - {"espframe_core", "update_photo_metadata_display"}
with tempfile.TemporaryDirectory(prefix="espframe-translation-startup-") as directory:
    folder = Path(directory)
    fixture = "\n".join(f"lv_obj_t {name}_storage; auto *{name} = &{name}_storage;" for name in sorted(labels))
    fixture += '\nvoid on_language_value(const std::string &x) {\n' + body + '\n}\n'
    (folder / "translation_startup_fixture.inc").write_text(fixture)
    header = (ROOT / "components/espframe/espframe_component.h").read_text()
    method = "  void schedule_language_refresh("
    scheduler = method + header.split(method, 1)[1].split("\n  }", 1)[0] + "\n  }\n"
    (folder / "translation_scheduler_fixture.inc").write_text(scheduler)
    binary = folder / "translation_startup_tests"
    subprocess.run(["g++", "-std=c++17", "-I", str(ROOT), "-I", directory,
                    str(ROOT / "tests/translation_startup_tests.cpp"), "-o", str(binary)], check=True)
    subprocess.run([str(binary)], check=True)
print("Translation restore, setup gate, live changes, and coalescing tests passed")
