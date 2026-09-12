#!/usr/bin/env python3
"""Exercise the production RGB565 helper with the vendored libwebp decoder."""
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
import re
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parent.parent
LIB = ROOT / 'components/libwebp-esp32'

def run(command):
    subprocess.run(command, check=True, cwd=ROOT)

with tempfile.TemporaryDirectory(prefix='espframe-webp-test-') as directory:
    build = Path(directory)
    config = build / 'src/webp/config.h'
    config.parent.mkdir(parents=True)
    config.write_text('// Generic host decoder: no platform SIMD or threading.\n')
    sources = re.findall(r'"(src/[^" ]+\.c)"', (LIB / 'CMakeLists.txt').read_text())
    def compile_source(source):
        output = build / (Path(source).stem + '.o')
        run(['gcc', '-std=c99', '-O1', '-ffunction-sections', '-fdata-sections', '-DHAVE_CONFIG_H', '-I'+str(build), '-I'+str(LIB),
             '-c', str(LIB / source), '-o', str(output)])
        return str(output)
    with ThreadPoolExecutor(max_workers=4) as executor:
        objects = list(executor.map(compile_source, sources))
    binary = build / 'tests'
    run(['g++', '-std=c++17', '-O1', '-I'+str(ROOT), '-I'+str(LIB / 'src'),
         str(ROOT / 'tests/webp_decode_tests.cpp'), *objects, '-Wl,--gc-sections', '-lm', '-o', str(binary)])
    run([str(binary)])
