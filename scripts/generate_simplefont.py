#!/usr/bin/env python3
"""
Simple font header generator for SimpleGFXfont (used by TextRenderer)

This script now provides a simplified interface: pass the output path, a
font base name, a numeric `--size` and a character range via `--chars`.
The generator computes glyph width/height and advances from `--size` so
you don't need to supply per-glyph dimensions.

Usage examples:
    # generate a single-space filled glyph header (size 8)
    python scripts/generate_simplefont.py --out src/Fonts/GeneratedSpace.h --name FreeSans12pt7b --size 8 --chars 32

The script writes a C++ header that matches the `SimpleGFXfont`/`SimpleGFXglyph`
structures in `src/text_renderer/SimpleFont.h`.
"""

import argparse
import math
import os
import textwrap
import sys

try:
    from PIL import Image, ImageDraw, ImageFont

    PIL_AVAILABLE = True
except Exception:
    PIL_AVAILABLE = False


def bytes_per_row(width):
    return (width + 7) // 8


def gen_bitmap_bytes(width, height, fill):
    """Return a list of bytes for a glyph of size width x height.

    fill: 0 => all zeros, 1 => all ones (0xFF)
    Layout: row-major, each row uses `bytes_per_row(width)` bytes.
    """
    bpr = bytes_per_row(width)
    total = bpr * height
    byte_value = 0xFF if fill else 0x00
    return [byte_value] * total


def render_glyph_from_ttf(ch, font, size):
    """Render a single character `ch` (int) using a PIL ImageFont and
    return width, height, bitmap_bytes, xAdvance, xOffset, yOffset
    xOffset/yOffset are computed relative to a baseline at `ascent` from the font metrics.
    """
    s = chr(ch)
    # create a temporary image big enough
    img = Image.new("L", (size * 3, size * 3), 0)
    draw = ImageDraw.Draw(img)
    draw.text((0, 0), s, font=font, fill=255)
    bbox = img.getbbox()
    ascent, descent = font.getmetrics()
    if bbox is None:
        # empty glyph (e.g., space). Use a blank width heuristic
        width = max(1, size // 2)
        height = max(1, size // 2)
        bitmap = gen_bitmap_bytes(width, height, 0)
        xadvance = font.getsize(s)[0] if hasattr(font, "getsize") else width
        xoffset = 0
        yoffset = -ascent
        return width, height, bitmap, xadvance, xoffset, yoffset

    left, upper, right, lower = bbox
    cropped = img.crop(bbox)
    width, height = cropped.size
    pixels = cropped.load()
    bpr = bytes_per_row(width)
    bitmap = []
    for y in range(height):
        row_bytes = [0] * bpr
        for x in range(width):
            v = pixels[x, y]
            if v > 128:
                byte_idx = x // 8
                bit = 7 - (x % 8)
                row_bytes[byte_idx] |= 1 << bit
        bitmap.extend(row_bytes)

    # compute xAdvance using font metrics where possible
    try:
        xadvance = font.getsize(s)[0]
    except Exception:
        xadvance = max(1, width + 1)

    # xOffset: distance from cursor to UL corner. We render glyph at (0,0) so
    # left is the horizontal offset of the glyph; use negative left so UL = cursor + xOffset
    xoffset = -left
    # yOffset: want UL such that cursorY is baseline -> UL = baseline + yOffset
    # ascent is distance from baseline to top of font; the glyph top is `upper` relative to draw origin
    yoffset = upper - ascent

    return width, height, bitmap, xadvance, xoffset, yoffset


def format_c_byte_list(byte_list):
    # format as 0xFF, 0x00, ... wrapped at 12 items per line
    parts = [f"0x{b:02X}" for b in byte_list]
    lines = []
    per_line = 12
    for i in range(0, len(parts), per_line):
        lines.append("    " + ", ".join(parts[i : i + per_line]))
    return ",\n".join(lines)


def format_c_code_list(code_list):
    # format as 0xXXXX, 0x00FF, ... wrapped at 12 items per line
    parts = [f"0x{c:04X}" for c in code_list]
    lines = []
    per_line = 12
    for i in range(0, len(parts), per_line):
        lines.append("    " + ", ".join(parts[i : i + per_line]))
    return ",\n".join(lines)


def generate_header(
    font_name,
    out_path,
    chars,
    width,
    height,
    xadvance,
    yadvance,
    xoffset,
    yoffset,
    fill,
):
    # chars: list of integer char codes (e.g., [32])
    bitmap_all = []
    glyphs = []
    offset = 0
    for ch in chars:
        bm = gen_bitmap_bytes(width, height, fill)
        bitmap_all.extend(bm)
        glyph = {
            "bitmapOffset": offset,
            "width": width,
            "height": height,
            "xAdvance": xadvance,
            "xOffset": xoffset,
            "yOffset": yoffset,
        }
        glyphs.append(glyph)
        offset += len(bm)

    # Prepare content: group bitmap bytes per glyph and add a comment indicating the character
    bmp_lines = []
    for idx, ch in enumerate(chars):
        g = glyphs[idx]
        per_glyph_bytes = bytes_per_row(g["width"]) * g["height"]
        start = g["bitmapOffset"]
        end = start + per_glyph_bytes
        chunk = bitmap_all[start:end]
        # printable character display
        if 32 <= ch <= 126:
            display = chr(ch)
            comment = f"// 0x{ch:02X} '{display}'"
        else:
            comment = f"// 0x{ch:02X}"
        # format chunk bytes
        chunk_c = format_c_byte_list(chunk)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
    bmp_c = ",\n".join(bmp_lines)
    glyph_lines = []
    for idx, g in enumerate(glyphs):
        ch = chars[idx]
        glyph_lines.append(
            f"    {{{g['bitmapOffset']}, 0x{ch:04X}, {g['width']}, {g['height']}, {g['xAdvance']}, {g['xOffset']}, {g['yOffset']}}}"
        )
    glyphs_c = ",\n".join(glyph_lines)

    first = min(chars)
    last = max(chars)
    codes_c = format_c_code_list(chars)
    count = len(chars)

    header = f"""#pragma once
#include "../text_renderer/SimpleFont.h"

// Generated by scripts/generate_simplefont.py
// Font: {font_name}

const uint8_t {font_name}Bitmaps[] PROGMEM = {{
{bmp_c}
}};

const SimpleGFXglyph {font_name}Glyphs[] PROGMEM = {{
{glyphs_c}
}};

const SimpleGFXfont {font_name} PROGMEM = {{(const uint8_t*){font_name}Bitmaps, (const SimpleGFXglyph*){font_name}Glyphs,
    {count}, {yadvance}}};
"""

    # ensure output dir exists
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", newline="\n") as f:
        f.write(header)
    print(f"Wrote {out_path}")


def write_header_from_data(font_name, out_path, chars, glyphs, bitmap_all, yadvance):
    """Write a header when glyphs and bitmap_all are already prepared.

    `glyphs` is a list of dicts with keys: bitmapOffset,width,height,xAdvance,xOffset,yOffset
    """
    # Prepare content: group bitmap bytes per glyph and add a comment indicating the character
    bmp_lines = []
    for idx, ch in enumerate(chars):
        g = glyphs[idx]
        per_glyph_bytes = bytes_per_row(g["width"]) * g["height"]
        start = g["bitmapOffset"]
        end = start + per_glyph_bytes
        chunk = bitmap_all[start:end]
        if 32 <= ch <= 126:
            display = chr(ch)
            comment = f"// 0x{ch:02X} '{display}'"
        else:
            comment = f"// 0x{ch:02X}"
        chunk_c = format_c_byte_list(chunk)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
    bmp_c = ",\n".join(bmp_lines)

    glyph_lines = []
    for idx, g in enumerate(glyphs):
        ch = chars[idx]
        glyph_lines.append(
            f"    {{{g['bitmapOffset']}, 0x{ch:04X}, {g['width']}, {g['height']}, {g['xAdvance']}, {g['xOffset']}, {g['yOffset']}}}"
        )
    glyphs_c = ",\n".join(glyph_lines)

    first = min(chars)
    last = max(chars)
    codes_c = format_c_code_list(chars)
    count = len(chars)

    header = f"""#pragma once
#include "../text_renderer/SimpleFont.h"

// Generated by scripts/generate_simplefont.py
// Font: {font_name}

const uint8_t {font_name}Bitmaps[] PROGMEM = {{
{bmp_c}
}};

const SimpleGFXglyph {font_name}Glyphs[] PROGMEM = {{
{glyphs_c}
}};

const SimpleGFXfont {font_name} PROGMEM = {{(const uint8_t*){font_name}Bitmaps, (const SimpleGFXglyph*){font_name}Glyphs,
    {count}, {yadvance}}};
"""

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", newline="\n") as f:
        f.write(header)
    print(f"Wrote {out_path}")


if __name__ == "__main__":
    p = argparse.ArgumentParser(description="Generate SimpleGFXfont C header files")
    p.add_argument("--out", required=True, help="Output header path")
    p.add_argument(
        "--name", required=True, help="Font variable base name (e.g. FreeSans12pt7b)"
    )
    p.add_argument(
        "--chars",
        default="32",
        help="Either a comma/range list of decimal char codes (eg: 32 or 32,48-57) or a literal string of characters to include (eg: 'ABCabc0123').",
    )
    p.add_argument(
        "--size",
        type=int,
        required=True,
        help="Font size (px) used to derive glyph width/height",
    )
    p.add_argument("--xoffset", type=int, default=0)
    p.add_argument("--yoffset", type=int, default=0)
    p.add_argument(
        "--ttf", help="Path to a TTF/OTF font file to rasterize glyphs (optional)"
    )
    p.add_argument(
        "--fill",
        type=int,
        choices=[0, 1],
        default=1,
        help="Fill bitmap: 1 => 0xFF, 0 => 0x00",
    )

    args = p.parse_args()

    # parse chars spec
    codes = []
    chars_arg = args.chars
    # If the supplied arg contains any non-digit and non-separator characters,
    # treat it as a literal string of characters to include. This lets callers
    # pass an explicit set like "ABCabc0123Ω漢".
    if any((not ch.isdigit()) and (ch not in ",-") for ch in chars_arg):
        for ch in chars_arg:
            codes.append(ord(ch))
    else:
        for part in chars_arg.split(","):
            part = part.strip()
            if not part:
                continue
            if "-" in part:
                a, b = part.split("-", 1)
                codes.extend(list(range(int(a), int(b) + 1)))
            else:
                codes.append(int(part))

    # derive dimensions from size: heuristics — width is half the size, height=size
    size = args.size
    # If a TTF was supplied, render each glyph using Pillow
    if args.ttf:
        if not PIL_AVAILABLE:
            print(
                "ERROR: Pillow (PIL) is required to render from TTF. Install with `pip install pillow`."
            )
            sys.exit(1)
        try:
            font = ImageFont.truetype(args.ttf, args.size)
        except Exception as e:
            print(f"ERROR: failed to load font '{args.ttf}': {e}")
            sys.exit(1)

        bitmap_all = []
        glyphs = []
        offset = 0
        for ch in codes:
            w, h, bm, xadv, xoff, yoff = render_glyph_from_ttf(ch, font, args.size)
            glyph = {
                "bitmapOffset": offset,
                "width": w,
                "height": h,
                "xAdvance": xadv,
                "xOffset": xoff,
                "yOffset": yoff,
            }
            glyphs.append(glyph)
            bitmap_all.extend(bm)
            offset += len(bm)

        yadvance = args.size + 2
        write_header_from_data(args.name, args.out, codes, glyphs, bitmap_all, yadvance)
        sys.exit(0)

    # Ensure minimum readable sizes
    width = max(3, size // 2)
    height = max(5, size)
    # xAdvance: typical advance equals width plus a small gap
    xadvance = max(1, width)
    # yAdvance: slightly larger than height to allow line spacing
    yadvance = height + 2

    generate_header(
        args.name,
        args.out,
        codes,
        width,
        height,
        xadvance,
        yadvance,
        args.xoffset,
        args.yoffset,
        args.fill,
    )
