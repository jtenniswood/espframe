#!/usr/bin/env python3
"""Create the native 4-bit glyph atlases from the matching Noto Sans Regular font."""

import argparse
from hashlib import sha256
from math import ceil
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--recovery", action="store_true", help="create the Wi-Fi recovery card atlas")
    args = parser.parse_args()
    if sha256(args.font.read_bytes()).hexdigest() != "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5":
        parser.error("use the recorded Noto Sans Regular 2.015 font")
    size, width, height, origin = (26, 32, 36, (0, 27)) if args.recovery else (55, 36, 56, (2, 48))
    glyphs = (chr(code) for code in range(32, 127)) if args.recovery else "0123456789:"
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
