#!/usr/bin/env python3
"""Generate the localized native status line masks."""

import argparse
from hashlib import sha256
import json
from math import ceil
from pathlib import Path
import re
from struct import pack, unpack_from
import sys

ROOT = Path(__file__).resolve().parents[3]
sys.path.insert(0, str(ROOT / "scripts"))
from asset_generation.translations import load_catalogues

HEIGHT = 36
FONT_HASHES = (
    "5be701a9511117e73603bdab95947feef4f18bd1851dbddc30659370b26c69f1",
    "a5cc5ce90ed4f6393992728fcc63657c4dd83aed119acbd5f4ed223e40c71129",
)


def status_lines():
    labels = ROOT / "firmware/native/main/status_labels.def"
    keys = [re.fullmatch(r"ESPFRAME_STATUS_LABEL\(([a-z0-9_]+)\)", line)[1]
            for line in labels.read_text().splitlines()]
    lines = [(language, words[key]) for language, words in load_catalogues().items() for key in keys]
    assert all(text and "\n" not in text for _, text in lines)
    return lines


def fingerprint(lines):
    return sha256(json.dumps(lines, ensure_ascii=False).encode()).digest()


def encoded_mask(line, language, regular, hebrew):
    from bidi.algorithm import get_display
    from PIL import Image, ImageDraw

    visual = get_display(line, base_dir="R") if language == "he" else line
    glyphs = [(char, hebrew if "\u0590" <= char <= "\u05ff" else regular) for char in visual]
    width = ceil(sum(font.getlength(char) for char, font in glyphs)) + 8
    image = Image.new("L", (width, HEIGHT))
    draw = ImageDraw.Draw(image)
    x = 4
    for char, font in glyphs:
        if bytes(font.getmask(char)) == bytes(font.getmask("\uffff")):
            raise ValueError(f"No glyph for U+{ord(char):04X} in {line!r}")
        draw.text((x, 27), char, font=font, fill=255, anchor="ls")
        x += font.getlength(char)
    left, top, right, bottom = image.getbbox()
    if left == 0 or right == width or top == 0 or bottom == HEIGHT:
        raise ValueError(f"Clipped status mask for {line!r}")
    pixels = image.crop((left, 0, right, HEIGHT)).tobytes()
    mask = bytearray(pack("<H", right - left))
    alpha = -1
    run = 0
    for value in pixels:
        opacity = (value + 8) // 17
        if opacity == alpha and run < 16:
            run += 1
        else:
            if run:
                mask.append((alpha << 4) | (run - 1))
            alpha, run = opacity, 1
    mask.append((alpha << 4) | (run - 1))
    return mask


def check(path, lines):
    data = path.read_bytes()
    if data[:32] != fingerprint(lines):
        raise ValueError("Localized status masks need regeneration")
    offsets = unpack_from(f"<{len(lines) + 1}I", data, 32)
    if offsets[0] != 32 + 4 * len(offsets) or offsets[-1] != len(data):
        raise ValueError("Invalid status mask table")
    for start, end in zip(offsets, offsets[1:]):
        width, = unpack_from("<H", data, start)
        if not (start + 2 < end and 0 < width <= 668 and
                sum((run & 15) + 1 for run in data[start + 2:end]) == HEIGHT * width):
            raise ValueError("Invalid status line dimensions")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fonts", type=Path, nargs=2, metavar=("NOTO", "NOTO_HEBREW"))
    args = parser.parse_args()
    lines = status_lines()
    if args.fonts:
        from PIL import ImageFont

        if tuple(sha256(path.read_bytes()).hexdigest() for path in args.fonts) != FONT_HASHES:
            parser.error("use the recorded Noto Sans faces")
        regular, hebrew = (ImageFont.truetype(str(path), 26, layout_engine=ImageFont.Layout.BASIC)
                           for path in args.fonts)
        offset = 32 + 4 * (len(lines) + 1)
        table = bytearray(fingerprint(lines))
        body = bytearray()
        for language, line in lines:
            table.extend(pack("<I", offset + len(body)))
            body.extend(encoded_mask(line, language, regular, hebrew))
        table.extend(pack("<I", offset + len(body)))
        args.output.write_bytes(table + body)
    check(args.output, lines)


if __name__ == "__main__":
    main()
