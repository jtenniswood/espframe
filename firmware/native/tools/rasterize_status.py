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

COMPACT_MASK = 0x8000
GLYPH_SEQUENCE = 0x4000
GLYPH_BEARING = 4
SETUP_SIZES = {
    "wifi_setup_title": (46, 60),
    "wifi_setup_installer": (30, 42),
    "wifi_setup_hotspot": (30, 42),
    "wifi_setup_browser": (30, 42),
    "immich_setup": (46, 60),
    "open_browser": (30, 42),
    "configure_immich_first": (30, 42),
    "configure_immich_second": (30, 42),
}
ICON_FONT_HASH = "61e8aba5a4e981fe22cf7c8e8bcdbea00476e75c62c37f01bf7ee33361d68428"
FONT_HASHES = (
    "5be701a9511117e73603bdab95947feef4f18bd1851dbddc30659370b26c69f1",
    "a5cc5ce90ed4f6393992728fcc63657c4dd83aed119acbd5f4ed223e40c71129",
)


def status_lines():
    labels = ROOT / "firmware/native/main/status_labels.def"
    keys = [re.fullmatch(r"ESPFRAME_STATUS_LABEL\(([a-z0-9_]+)\)", line)[1]
            for line in labels.read_text().splitlines()]
    recovery_header = ROOT / "firmware/native/main/wifi_recovery.h"
    recovery = recovery_header.read_text()
    url = re.search(r'recovery_url\[\] = "([^"]+)"', recovery)[1]
    lines = []
    for language, words in load_catalogues().items():
        installer, hotspot, browser = words["wifi_instructions"].splitlines()
        instructions = words["configure_immich"].splitlines()
        if len(instructions) == 1 and len(instructions[0]) > 43:
            words_to_wrap = instructions[0].split()
            split = min(range(1, len(words_to_wrap)), key=lambda i: abs(
                len(" ".join(words_to_wrap[:i])) - len(instructions[0]) / 2))
            instructions = [" ".join(words_to_wrap[:split]), " ".join(words_to_wrap[split:])]
        setup = {
            "configure_immich_first": instructions[0],
            "configure_immich_second": instructions[1] if len(instructions) > 1 else "",
            "wifi_setup_title": words["wifi_setup"],
            "wifi_setup_installer": installer,
            "wifi_setup_hotspot": hotspot,
            "wifi_setup_browser": browser.replace("{hotspot}", "").replace("{address}", url),
        }
        for key in keys:
            size, height = SETUP_SIZES.get(key, (26, 36))
            lines.append((language, (setup if key in setup else words)[key], size, height, key in SETUP_SIZES))
    icons = ROOT / "devices/guition-esp32-p4-jc8012p4a1/assets/icons.yaml"
    icon_source = icons.read_text()
    for name in ("wifi", "cog"):
        codepoint = re.search(rf'icon_{name}: "\\U([0-9A-Fa-f]{{8}})"', icon_source)[1]
        lines.append(("", chr(int(codepoint, 16)), 64, 64, True))
    address_characters = re.search(r'setup_address_characters\[\] = "([^"]+)"', recovery)[1]
    lines.extend(("glyph", char, 46, 60, True) for char in address_characters)
    ssid_characters = re.search(r'recovery_ssid_characters\[\] = "([^"]+)"', recovery)[1]
    size, height = SETUP_SIZES["wifi_setup_hotspot"]
    body_characters = set("".join(text for _, text, font_size, _, _ in lines if font_size == size))
    glyphs = ssid_characters + "".join(sorted(body_characters - set(ssid_characters)))
    if len(glyphs) > 256:
        raise ValueError("Setup glyph indices must fit in one byte")
    lines.extend(("glyph", char, size, height, True) for char in glyphs)
    assert all("\n" not in text for _, text, _, _, _ in lines)
    return lines


def fingerprint(lines):
    return sha256(json.dumps(lines, ensure_ascii=False).encode()).digest()


def visible_text(line, language):
    if language == "he":
        from bidi.algorithm import get_display

        return get_display(line, base_dir="R")
    return line


def setup_sequence(language, size):
    return size == SETUP_SIZES["wifi_setup_hotspot"][0] and language != "glyph"


def encoded_mask(line, language, fonts, icon, size, height, compact):
    from PIL import Image, ImageDraw

    if language:
        regular, hebrew = fonts[size]
        glyphs = [(char, hebrew if "\u0590" <= char <= "\u05ff" else regular) for char in line]
        baseline = size + 1
    else:
        glyphs = [(line, icon)]
        _, top, _, bottom = icon.getbbox(line, anchor="ls")
        baseline = (height - top - bottom) // 2
    padding = 12
    width = ceil(sum(font.getlength(char) for char, font in glyphs)) + 2 * padding
    image = Image.new("L", (width, height))
    draw = ImageDraw.Draw(image)
    x = padding
    for char, font in glyphs:
        if bytes(font.getmask(char)) == bytes(font.getmask("\uffff")):
            raise ValueError(f"No glyph for U+{ord(char):04X} in {line!r}")
        draw.text((x, baseline), char, font=font, fill=255, anchor="ls")
        x += font.getlength(char)
    bounds = image.getbbox()
    if language == "glyph":
        # Include the full advance and both bearings so the glyph can be
        # composed at the same positions as the original full-line raster.
        left, right = padding - GLYPH_BEARING, int(x) + GLYPH_BEARING
        if bounds and (bounds[0] < left or bounds[2] > right or bounds[1] == 0 or bounds[3] == height):
            raise ValueError(f"Setup glyph exceeds its bounds for {line!r}")
    else:
        left, top, right, bottom = bounds
        if left == 0 or right == width or top == 0 or bottom == height:
            raise ValueError(f"Clipped status mask for {line!r}")
    pixels = image.crop((left, 0, right, height)).tobytes()
    # Setup has several long sentences per language. Give these masks longer
    # runs at two bits of opacity so they fit the OTA partition.
    mask = bytearray(pack("<HB", (right - left) | (COMPACT_MASK if compact else 0), height))
    alpha_step = 85 if compact else 17
    run_bits = 6 if compact else 4
    alpha = -1
    run = 0
    for value in pixels:
        opacity = (value + alpha_step // 2) // alpha_step
        if opacity == alpha and run < 1 << run_bits:
            run += 1
        else:
            if run:
                mask.append((alpha << run_bits) | (run - 1))
            alpha, run = opacity, 1
    mask.append((alpha << run_bits) | (run - 1))
    return mask


def check(path, lines):
    data = path.read_bytes()
    if data[:32] != fingerprint(lines):
        raise ValueError("Localized status masks need regeneration")
    offsets = unpack_from(f"<{len(lines) + 1}I", data, 32)
    if offsets[0] != 32 + 4 * len(offsets) or offsets[-1] != len(data):
        raise ValueError("Invalid status mask table")
    glyphs = [(unpack_from("<H", data, start)[0] & ~COMPACT_MASK) - 2 * GLYPH_BEARING
              for (language, _, size, _, _), start in zip(lines, offsets) if language == "glyph" and size == 30]
    for (language, _, size, height, compact), start, end in zip(lines, offsets, offsets[1:]):
        header, recorded_height = unpack_from("<HB", data, start)
        width = header & ~(COMPACT_MASK | GLYPH_SEQUENCE)
        sequence = setup_sequence(language, size)
        contents = data[start + 3:end]
        max_width = 668 if height == 36 else 700
        if not (bool(header & COMPACT_MASK) == compact and bool(header & GLYPH_SEQUENCE) == sequence and
                start + 3 <= end and 0 <= width <= max_width and height == recorded_height):
            raise ValueError("Invalid status line dimensions")
        if sequence:
            if any(index >= len(glyphs) for index in contents) or sum(glyphs[index] for index in contents) != width:
                raise ValueError("Invalid setup glyph sequence")
        else:
            run_mask = 63 if compact else 15
            if width == 0 or sum((run & run_mask) + 1 for run in contents) != height * width:
                raise ValueError("Invalid status mask dimensions")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("output", type=Path)
    parser.add_argument("--fonts", type=Path, nargs=2, metavar=("NOTO", "NOTO_HEBREW"))
    parser.add_argument("--icon-font", type=Path)
    args = parser.parse_args()
    if bool(args.fonts) != bool(args.icon_font):
        parser.error("pass --fonts and --icon-font together to generate the masks")
    lines = status_lines()
    if args.fonts:
        from PIL import ImageFont

        if tuple(sha256(path.read_bytes()).hexdigest() for path in args.fonts) != FONT_HASHES:
            parser.error("use the recorded Noto Sans faces")
        if sha256(args.icon_font.read_bytes()).hexdigest() != ICON_FONT_HASH:
            parser.error("use the recorded Material Design Icons face")
        fonts = {size: tuple(ImageFont.truetype(str(path), size, layout_engine=ImageFont.Layout.BASIC)
                             for path in args.fonts) for size in (26, 30, 46)}
        icon = ImageFont.truetype(str(args.icon_font), 64, layout_engine=ImageFont.Layout.BASIC)
        glyph_masks = {(size, line): encoded_mask(line, language, fonts, icon, size, height, compact)
                       for language, line, size, height, compact in lines if language == "glyph"}
        body_glyphs = [char for size, char in glyph_masks if size == 30]
        glyph_index = {char: index for index, char in enumerate(body_glyphs)}
        offset = 32 + 4 * (len(lines) + 1)
        table = bytearray(fingerprint(lines))
        body = bytearray()
        for language, line, size, height, compact in lines:
            table.extend(pack("<I", offset + len(body)))
            visual = visible_text(line, language)
            if setup_sequence(language, size):
                width = sum((unpack_from("<H", glyph_masks[size, char])[0] & ~COMPACT_MASK) - 2 * GLYPH_BEARING
                            for char in visual)
                body.extend(pack("<HB", width | COMPACT_MASK | GLYPH_SEQUENCE, height))
                body.extend(bytes(glyph_index[char] for char in visual))
            elif language == "glyph":
                body.extend(glyph_masks[size, line])
            else:
                body.extend(encoded_mask(visual, language, fonts, icon, size, height, compact))
        table.extend(pack("<I", offset + len(body)))
        args.output.write_bytes(table + body)
    check(args.output, lines)


if __name__ == "__main__":
    main()
