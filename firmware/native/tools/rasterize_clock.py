#!/usr/bin/env python3
"""Create the native 4-bit glyph atlases from the matching Noto Sans Regular font."""

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
    args = parser.parse_args()
    if sha256(args.font.read_bytes()).hexdigest() != "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5":
        parser.error("use the recorded Noto Sans Regular 2.015 font")
    size, width, height, origin = (26, 32, 36, (0, 27)) if args.recovery else (55, 36, 56, (2, 48))
    glyphs = (chr(code) for code in range(32, 127)) if args.recovery else "0123456789:"
    if args.captions:
        size, width, height, origin = 23, 32, 32, (1, 24)
        ranges = ((32, 127), (0xa0, 0x250), (0x370, 0x530), (0x1e00, 0x1efa), (0x2010, 0x2027))
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
        pixels = image.tobytes()
        result.extend(((left + 8) // 17 << 4) | ((right + 8) // 17)
                      for left, right in zip(pixels[::2], pixels[1::2], strict=True))
    args.output.write_bytes(result)


if __name__ == "__main__":
    main()
