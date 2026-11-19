#!/usr/bin/env python3
"""Create native 4-bit glyph atlases from the Espframe Noto Sans faces."""

import argparse
from hashlib import sha256
from math import ceil
from pathlib import Path
from struct import pack

from PIL import Image, ImageDraw, ImageFont


def caption_atlas(font, glyphs, width, height, origin):
    missing = bytes(font.getmask("\uffff"))
    index = bytearray()
    bitmaps = bytearray()
    for glyph in glyphs:
        if bytes(font.getmask(glyph)) == missing:
            continue
        image = Image.new("L", (width, height))
        ImageDraw.Draw(image).text(origin, glyph, font=font, fill=255, anchor="ls")
        bounds = image.getbbox() or (0, 0, 0, 0)
        left, top, right, bottom = bounds
        ink_width, ink_height = right - left, bottom - top
        index.extend(pack("<HI", ord(glyph), len(bitmaps)))
        bitmaps.extend(bytes((ceil(font.getlength(glyph)), left, top, ink_width, ink_height)))
        if ink_width:
            cropped = Image.new("L", (ink_width + ink_width % 2, ink_height))
            cropped.paste(image.crop(bounds))
            pixels = cropped.tobytes()
            bitmaps.extend(((a + 8) // 17 << 4) | ((b + 8) // 17)
                           for a, b in zip(pixels[::2], pixels[1::2], strict=True))
    return pack("<H", len(index) // 6) + index + bitmaps


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--recovery", action="store_true", help="create the Wi-Fi recovery card atlas")
    mode.add_argument("--captions", action="store_true", help="create the photo caption atlas")
    mode.add_argument("--dates", action="store_true", help="create the photo date atlas")
    args = parser.parse_args()
    regular = "5be701a9511117e73603bdab95947feef4f18bd1851dbddc30659370b26c69f1"
    light = "cfffc3ecf6aaa6a205b16c68070cceca472208a06581e6f131cdd0294913d900"
    recovery = "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5"
    expected = recovery if args.recovery else regular if args.captions or args.dates else light
    if sha256(args.font.read_bytes()).hexdigest() != expected:
        parser.error("use the recorded Noto Sans face for the requested atlas")
    size, width, height, origin = (26, 32, 36, (0, 27)) if args.recovery else (150, 88, 116, (0, 113))
    glyphs = (chr(code) for code in range(32, 127)) if args.recovery else "0123456789:"
    if args.captions or args.dates:
        size, width, height, origin = (36, 64, 64, (1, 39)) if args.captions else (28, 48, 48, (1, 30))
        ranges = [(32, 127), (0x2010, 0x2027)]
        if args.captions:
            ranges[1:1] = [(0xa0, 0x250), (0x370, 0x530), (0x1e00, 0x1efa)]
        glyphs = (chr(code) for start, end in ranges for code in range(start, end))
        font = ImageFont.truetype(str(args.font), size)
        args.output.write_bytes(caption_atlas(font, glyphs, width, height, origin))
        return
    font = ImageFont.truetype(str(args.font), size)
    result = bytearray()
    for glyph in glyphs:
        image = Image.new("L", (width, height))
        ImageDraw.Draw(image).text(origin, glyph, font=font, fill=255, anchor="ls")
        if args.recovery:
            result.append(ceil(font.getlength(glyph)))
        else:
            # Native panel columns are contiguous through the height of a glyph.
            image = image.transpose(Image.Transpose.TRANSPOSE)
        pixels = image.tobytes()
        result.extend(((left + 8) // 17 << 4) | ((right + 8) // 17)
                      for left, right in zip(pixels[::2], pixels[1::2], strict=True))
    args.output.write_bytes(result)


if __name__ == "__main__":
    main()
