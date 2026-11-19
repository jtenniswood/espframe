#!/usr/bin/env python3
"""Create the 4-bit clock glyph atlas from the matching Noto Sans Regular font."""

import argparse
from hashlib import sha256
from pathlib import Path

from PIL import Image, ImageDraw, ImageFont


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path)
    parser.add_argument("output", type=Path)
    args = parser.parse_args()
    if sha256(args.font.read_bytes()).hexdigest() != "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5":
        parser.error("use the recorded Noto Sans Regular 2.015 font")
    font = ImageFont.truetype(str(args.font), 55)
    result = bytearray()
    for glyph in "0123456789:":
        image = Image.new("L", (36, 56))
        ImageDraw.Draw(image).text((2, 48), glyph, font=font, fill=255, anchor="ls")
        pixels = image.tobytes()
        result.extend(((left + 8) // 17 << 4) | ((right + 8) // 17)
                      for left, right in zip(pixels[::2], pixels[1::2], strict=True))
    args.output.write_bytes(result)


if __name__ == "__main__":
    main()
