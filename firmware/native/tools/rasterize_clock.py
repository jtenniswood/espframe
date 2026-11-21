#!/usr/bin/env python3
"""Create native 4-bit glyph atlases from the recorded Rubik and Noto Sans faces."""

import argparse
from hashlib import sha256
from math import ceil
from pathlib import Path
from struct import pack, unpack_from


def caption_atlas(font, glyphs, width, height, origin, fallback=None, require_all=False):
    from PIL import Image, ImageDraw

    fonts = [(font, bytes(font.getmask("\uffff")))]
    if fallback:
        fonts.append((fallback, bytes(fallback.getmask("\uffff"))))
    index = bytearray()
    bitmaps = bytearray()
    for glyph in glyphs:
        face = next((face for face, missing in fonts if bytes(face.getmask(glyph)) != missing), None)
        if face is None:
            if require_all:
                raise ValueError(f"No date glyph for U+{ord(glyph):04X}")
            continue
        image = Image.new("L", (width, height))
        ImageDraw.Draw(image).text(origin, glyph, font=face, fill=255, anchor="ls")
        bounds = image.getbbox() or (0, 0, 0, 0)
        left, top, right, bottom = bounds
        ink_width, ink_height = right - left, bottom - top
        index.extend(pack("<HI", ord(glyph), len(bitmaps)))
        bitmaps.extend(bytes((ceil(face.getlength(glyph)), left, top, ink_width, ink_height)))
        if ink_width:
            cropped = Image.new("L", (ink_width + ink_width % 2, ink_height))
            cropped.paste(image.crop(bounds))
            pixels = cropped.tobytes()
            bitmaps.extend(((a + 8) // 17 << 4) | ((b + 8) // 17)
                           for a, b in zip(pixels[::2], pixels[1::2], strict=True))
    return pack("<H", len(index) // 6) + index + bitmaps


def date_glyphs():
    translations = Path(__file__).resolve().parents[3] / "product" / "translations"
    keys = {"today", "day_ago", "days_ago", "month_ago", "months_ago", "year_ago", "years_ago"}
    keys.update(f"month_{month:02}" for month in range(1, 13))
    glyphs = set(chr(code) for code in range(32, 127)) | {"\u2026"}
    for path in translations.glob("strings.*.txt"):
        for line in path.read_text(encoding="utf-8").splitlines():
            key, _, value = line.partition("=")
            if key in keys:
                glyphs.update(value.replace("{count}", ""))
    return sorted(glyphs)


def check_date_atlas(path):
    data = path.read_bytes()
    count, = unpack_from("<H", data)
    codes = [unpack_from("<H", data, 2 + index * 6)[0] for index in range(count)]
    if codes != sorted(set(codes)):
        raise ValueError("Date atlas glyphs must be unique and sorted")
    missing = set(date_glyphs()) - {chr(code) for code in codes}
    if missing:
        raise ValueError(f"Regenerate the date atlas to add glyphs: {''.join(sorted(missing))}")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("font", type=Path, nargs="?")
    parser.add_argument("output", type=Path, nargs="?")
    mode = parser.add_mutually_exclusive_group()
    mode.add_argument("--recovery", action="store_true", help="create the Wi-Fi recovery card atlas")
    mode.add_argument("--captions", action="store_true", help="create the photo caption atlas")
    mode.add_argument("--dates", action="store_true", help="create the photo date atlas")
    mode.add_argument("--check-dates", type=Path, help="check an existing photo date atlas against the catalogue")
    parser.add_argument("--noto-fallback", type=Path, help="recorded Noto Sans Light face for location glyphs missing in Rubik")
    args = parser.parse_args()
    if args.check_dates:
        check_date_atlas(args.check_dates)
        return
    if not args.font or not args.output:
        parser.error("font and output are required to generate an atlas")
    from PIL import Image, ImageDraw, ImageFont

    rubik = "1b3a7437ba2af80e465e773ed60c5036d1ba6ace492d89046dbcf18fb31e4e88"
    light = "cfffc3ecf6aaa6a205b16c68070cceca472208a06581e6f131cdd0294913d900"
    recovery = "f5f552c8c5edb61fe6efb824baf4d4de47b1a8689ab4925ff43f7bd6a4ebece5"
    expected = recovery if args.recovery else rubik if args.captions or args.dates else light
    if sha256(args.font.read_bytes()).hexdigest() != expected:
        parser.error("use the recorded font for the requested atlas")
    if args.captions and (not args.noto_fallback or sha256(args.noto_fallback.read_bytes()).hexdigest() != light):
        parser.error("use the recorded Noto Sans Light face with --noto-fallback for --captions")
    size, width, height, origin = (26, 32, 36, (0, 27)) if args.recovery else (150, 88, 116, (0, 113))
    glyphs = (chr(code) for code in range(32, 127)) if args.recovery else "0123456789:"
    if args.captions or args.dates:
        size, width, height, origin = (25, 48, 48, (1, 28))
        ranges = [(32, 127), (0x2010, 0x2027)]
        if args.captions:
            ranges[1:1] = [(0xa0, 0x250), (0x370, 0x530), (0x1e00, 0x1efa)]
        glyphs = (chr(code) for start, end in ranges for code in range(start, end))
        font = ImageFont.truetype(str(args.font), size)
        font.set_variation_by_axes([300])
        fallback = None
        if args.dates:
            glyphs = date_glyphs()
        else:
            fallback = ImageFont.truetype(str(args.noto_fallback), size)
        args.output.write_bytes(caption_atlas(font, glyphs, width, height, origin, fallback, require_all=args.dates))
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
