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

from PIL import Image, ImageDraw, ImageFont


def render_preview_from_data(codes, glyphs, bitmap_all, out_path, cols=16, pad=4):
    """Render a monochrome PNG from the raw glyph bitmaps and save to out_path.

    `glyphs` is a list of dicts with keys: bitmapOffset,width,height,xAdvance,xOffset,yOffset
    `bitmap_all` is a flat list of bytes containing packed rows for each glyph.
    """
    # Pillow is required for previews; assume it is available.

    # Determine cell size from max glyph dimensions
    max_w = max(g["width"] for g in glyphs) if glyphs else 0
    max_h = max(g["height"] for g in glyphs) if glyphs else 0
    if max_w == 0 or max_h == 0:
        print("No glyphs to render")
        return 1

    cols = int(cols)
    n = len(codes)
    rows = (n + cols - 1) // cols

    cell_w = max_w + pad * 2
    cell_h = max_h + pad * 2
    img_w = cols * cell_w
    img_h = rows * cell_h

    img = Image.new("RGB", (img_w, img_h), "white")
    draw = ImageDraw.Draw(img)
    pixels = img.load()

    for idx, ch in enumerate(codes):
        if idx >= len(glyphs):
            break
        g = glyphs[idx]
        start = g["bitmapOffset"]
        w = g["width"]
        h = g["height"]
        bpr = bytes_per_row(w)
        r = idx // cols
        c = idx % cols
        base_x = c * cell_w + pad
        base_y = r * cell_h + pad
        # center glyph inside the cell
        offset_x = base_x + (max_w - w) // 2
        offset_y = base_y + (max_h - h) // 2

        # draw pink background sized to this glyph (w x h), centered in cell
        pink = (255, 192, 203)
        bg_x = offset_x
        bg_y = offset_y
        draw.rectangle([bg_x, bg_y, bg_x + w - 1, bg_y + h - 1], fill=pink)

        # draw each pixel from packed bitmap: set black for bits set
        for yy in range(h):
            for xx in range(w):
                byte_idx = start + yy * bpr + (xx // 8)
                if byte_idx >= len(bitmap_all):
                    continue
                byte = bitmap_all[byte_idx]
                bit = 7 - (xx % 8)
                if (byte >> bit) & 1:
                    pixels[offset_x + xx, offset_y + yy] = (0, 0, 0)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    img.save(out_path)
    print(f"Wrote preview image (monochrome): {out_path}")
    return 0


def render_preview_from_ttf(codes, pil_font, size, out_path, cols=16, pad=6):
    """Render an anti-aliased grayscale preview directly from the TTF `pil_font`.

    This produces a nicer preview than the 1-bit `bitmap_all` representation
    by drawing the glyphs with Pillow and preserving gray levels.
    """
    # Render each glyph to a small grayscale image to measure sizes
    glyph_imgs = []
    max_w = 0
    max_h = 0
    for ch in codes:
        s = chr(ch)
        # render glyph onto a temporary canvas and crop to its bbox
        tmp = Image.new("L", (size * 3, size * 3), 0)
        d = ImageDraw.Draw(tmp)
        d.text((0, 0), s, font=pil_font, fill=255)
        bbox = tmp.getbbox()
        if bbox is None:
            w = max(1, size // 2)
            h = max(1, size // 2)
            img = Image.new("L", (w, h), 0)
        else:
            img = tmp.crop(bbox)
            w, h = img.size

        glyph_imgs.append(img)
        max_w = max(max_w, w)
        max_h = max(max_h, h)

    if max_w == 0 or max_h == 0:
        print("No glyphs to render")
        return 1

    cols = int(cols)
    n = len(codes)
    rows = (n + cols - 1) // cols

    cell_w = max_w + pad * 2
    cell_h = max_h + pad * 2
    img_w = cols * cell_w
    img_h = rows * cell_h

    out = Image.new("RGB", (img_w, img_h), "white")

    for idx, gimg in enumerate(glyph_imgs):
        r = idx // cols
        c = idx % cols
        base_x = c * cell_w + pad
        base_y = r * cell_h + pad
        w, h = gimg.size
        offset_x = base_x + (max_w - w) // 2
        offset_y = base_y + (max_h - h) // 2

        # pink background
        pink = (255, 192, 203)
        draw = ImageDraw.Draw(out)
        draw.rectangle([base_x, base_y, base_x + max_w - 1, base_y + max_h - 1], fill=pink)

        # Paste anti-aliased glyph: create a black RGB tile and use the L image as mask
        black = Image.new("RGB", (w, h), (0, 0, 0))
        out.paste(black, (offset_x, offset_y), mask=gimg)

    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    out.save(out_path)
    print(f"Wrote preview image (TTF grayscale): {out_path}")
    return 0


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


def render_glyph_from_ttf(ch, font, size, thickness=0.0):
    """Render a single character `ch` (int) using a PIL ImageFont and
    return width, height, bitmap_bytes, xAdvance, xOffset, yOffset
    xOffset/yOffset are computed relative to a baseline at `ascent` from the font metrics.
    """
    s = chr(ch)
    # create a temporary image big enough
    img = Image.new("L", (size * 3, size * 3), 0)
    draw = ImageDraw.Draw(img)
    # Render the glyph. If a thickness > 0 is requested, prefer Pillow's
    # `stroke_width` argument when available (it expects an integer). Use the
    # rounded integer for stroke_width, but also provide a fallback that
    # approximates fractional thickness by drawing offsets inside a disk of
    # radius `thickness`.
    if thickness and thickness > 0:
        try:
            draw.text(
                (0, 0), s, font=font, fill=255, stroke_width=thickness, stroke_fill=255
            )
        except TypeError:
            draw.text((0, 0), s, font=font, fill=255)
    else:
        draw.text((0, 0), s, font=font, fill=255)

    bbox = img.getbbox()
    try:
        ascent, descent = font.getmetrics()
    except Exception:
        ascent = size
        descent = 0
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
        # try:
        #     xadvance = font.getsize(s)[0]
        # except Exception:
        xadvance = max(1, width)

    # xOffset: distance from cursor to UL corner. We render glyph at (0,0) so
    # left is the horizontal offset of the glyph; use negative left so UL = cursor + xOffset
    xoffset = 0
    # yOffset: want UL such that cursorY is baseline -> UL = baseline + yOffset
    # ascent is distance from baseline to top of font; the glyph top is `upper` relative to draw origin
    yoffset = upper - ascent

    return width, height, bitmap, xadvance, xoffset, yoffset


def format_c_byte_list(byte_list):
    """Format a list of bytes into C-style 0xXX, wrapped at 12 items/line.

    Returns a string where each line is indented by 4 spaces so it can be
    inserted directly into a C array initializer.
    """
    if not byte_list:
        return ""
    parts = [f"0x{b:02X}" for b in byte_list]
    per_line = 12
    lines = [
        "    " + ", ".join(parts[i : i + per_line])
        for i in range(0, len(parts), per_line)
    ]
    return ",\n".join(lines)


def format_c_code_list(code_list):
    """Format a list of character codes into C-style 0xXXXX, wrapped at 12 items/line.

    Returns a string where each line is indented by 4 spaces.
    """
    if not code_list:
        return ""
    # Use variable-width hex formatting to support full Unicode codepoints
    parts = [f"0x{c:X}" for c in code_list]
    per_line = 12
    lines = [
        "    " + ", ".join(parts[i : i + per_line])
        for i in range(0, len(parts), per_line)
    ]
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
        display = chr(ch)
        comment = f"// 0x{ch:X} '{display}'"
        # format chunk bytes
        chunk_c = format_c_byte_list(chunk)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
    bmp_c = ",\n".join(bmp_lines)
    glyph_lines = []
    for idx, g in enumerate(glyphs):
        ch = chars[idx]
        glyph_lines.append(
            f"    {{{g['bitmapOffset']}, 0x{ch:X}, {g['width']}, {g['height']}, {g['xAdvance']}, {g['xOffset']}, {g['yOffset']}}}"
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
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"Wrote {out_path}")
    # return generated data so callers can render a preview using the same bytes
    return glyphs, bitmap_all


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
        display = chr(ch)
        comment = f"// 0x{ch:X} '{display}'"
        chunk_c = format_c_byte_list(chunk)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
    bmp_c = ",\n".join(bmp_lines)

    glyph_lines = []
    for idx, g in enumerate(glyphs):
        ch = chars[idx]
        glyph_lines.append(
            f"    {{{g['bitmapOffset']}, 0x{ch:X}, {g['width']}, {g['height']}, {g['xAdvance']}, {g['xOffset']}, {g['yOffset']}}}"
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
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"Wrote {out_path}")
    return


def main(argv=None):
    p = argparse.ArgumentParser(description="Generate SimpleGFXfont C header files")
    # Allow shorthand positional invocation: name size ttf
    p.add_argument(
        "positional",
        nargs="*",
        help="Optional positional args: name size ttf (in that order).",
    )
    p.add_argument("--out", help="Output header path (default: src/Fonts/<name>.h)")
    p.add_argument("--name", help="Font variable base name (e.g. FreeSans12pt7b)")
    p.add_argument(
        "--chars",
        default="32",
        help=(
            "Either a comma/range list of decimal char codes (eg: 32 or 32,48-57) "
            "or a literal string of characters to include (eg: 'ABCabc0123')."
        ),
    )
    p.add_argument(
        "--chars-file",
        help=(
            "Path to a file containing the literal characters to include. Use this "
            "to avoid shell quoting/escaping issues."
        ),
    )
    p.add_argument(
        "--size",
        type=int,
        help="Font size (px) used to derive glyph width/height",
    )
    p.add_argument("--xoffset", type=int, default=0)
    p.add_argument("--yoffset", type=int, default=0)
    p.add_argument(
        "--ttf", help="Path to a TTF/OTF font file to rasterize glyphs (optional)"
    )
    p.add_argument(
        "--thickness",
        type=float,
        default=0.0,
        help="Stroke thickness to render bolder glyphs (0 = normal). Float allowed; fractional thickness is approximated.",
    )
    p.add_argument(
        "--fill",
        type=int,
        choices=[0, 1],
        default=0,
        help="Fill bitmap: 1 => 0xFF, 0 => 0x00",
    )
    p.add_argument(
        "--preview-output",
        help="Optional PNG path to render an image showing all characters passed to the generator",
    )

    args = p.parse_args(argv)

    # Support shorthand invocation: allow supplying name,size,ttf as positional args
    pos = getattr(args, "positional", []) or []
    if (not args.name) and len(pos) >= 1:
        args.name = pos[0]
    if (args.size is None) and len(pos) >= 2:
        try:
            args.size = int(pos[1])
        except Exception:
            print(f"ERROR: invalid size value: '{pos[1]}'")
            sys.exit(1)
    if (not args.ttf) and len(pos) >= 3:
        args.ttf = pos[2]

    # Require name and size at this point
    if not args.name:
        print(
            "ERROR: font name not specified. Use --name or supply as first positional arg."
        )
        sys.exit(1)
    if args.size is None:
        print(
            "ERROR: font size not specified. Use --size or supply as second positional arg."
        )
        sys.exit(1)

    # Default output paths when not provided: header in src/Fonts and preview in test/output
    if not args.out:
        args.out = os.path.join("src", "Fonts", f"{args.name}.h")
    if not args.preview_output:
        args.preview_output = os.path.join("test", "output", f"{args.name}.png")

    # parse chars spec. Prefer --chars-file when present to avoid shell quoting issues
    codes = []
    # If the user didn't pass a chars-file, prefer the repo default
    # `scripts/chars_input.txt` when it exists (the user's requested default).
    if not args.chars_file:
        default_chars_file = os.path.join(os.path.dirname(__file__), "chars_input.txt")
        if os.path.isfile(default_chars_file) and args.chars == "32":
            args.chars_file = default_chars_file

    if args.chars_file:
        if not os.path.isfile(args.chars_file):
            print(f"ERROR: chars file not found: '{args.chars_file}'")
            sys.exit(1)
        with open(args.chars_file, "r", encoding="utf-8") as cf:
            chars_arg = cf.read()
        # Strip Unicode BOM if present and trailing newlines to avoid accidental
        # inclusion of BOM/newline characters from editors or Set-Content.
        chars_arg = chars_arg.replace("\ufeff", "")
        chars_arg = chars_arg.rstrip("\r\n")
        print(f"Loaded {len(chars_arg)} character(s) from {args.chars_file}")
    else:
        chars_arg = args.chars

    # If the supplied arg contains any non-digit and non-separator characters,
    # treat it as a literal string of characters to include.
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

        ttf_path = args.ttf
        if not os.path.isfile(ttf_path):
            print(f"ERROR: TTF file not found: '{ttf_path}'")
            sys.exit(1)

        try:
            font = ImageFont.truetype(ttf_path, args.size)
        except Exception as e:
            print(f"ERROR: failed to load font '{ttf_path}': {e}")
            sys.exit(1)

        print(
            f"Rendering {len(codes)} glyph(s) from TTF: {ttf_path} (size={args.size})"
        )

        bitmap_all = []
        glyphs = []
        offset = 0
        for i, ch in enumerate(codes):
            w, h, bm, xadv, xoff, yoff = render_glyph_from_ttf(
                ch, font, args.size, args.thickness
            )
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

            # Sanity check: warn if a glyph bitmap is entirely 0x00 or 0xFF
            if bm:
                uniq = set(bm)
                if len(uniq) == 1:
                    val = next(iter(uniq))
                    if val in (0x00, 0xFF):
                        cp = f"0x{ch:X}"
                        print(
                            f"WARNING: glyph {cp} rendered to uniform byte 0x{val:02X}"
                        )

        yadvance = args.size + 2
        write_header_from_data(args.name, args.out, codes, glyphs, bitmap_all, yadvance)
        # optional previews: render both a TTF grayscale preview and a 1-bit
        # preview generated from the packed bitmap data. Both files are written
        # next to the requested path using suffixes `_ttf` and `_bitmap`.
        if args.preview_output:
            base, ext = os.path.splitext(args.preview_output)
            ttf_preview = f"{base}_ttf{ext}"
            bitmap_preview = f"{base}_bitmap{ext}"

            rc = render_preview_from_ttf(codes, font, args.size, ttf_preview)
            if rc != 0:
                sys.exit(rc)

            # Also render the 1-bit preview from the generated bitmap bytes so
            # callers can compare exact on-device rendering appearance.
            rc2 = render_preview_from_data(codes, glyphs, bitmap_all, bitmap_preview)
            if rc2 != 0:
                sys.exit(rc2)
        sys.exit(0)

    # Ensure minimum readable sizes
    width = max(3, size // 2)
    height = max(5, size)
    # xAdvance: typical advance equals width plus a small gap
    xadvance = max(1, width)
    # yAdvance: slightly larger than height to allow line spacing
    yadvance = height + 2

    # Rasterize glyphs using PIL's default font when no TTF is provided.
    try:
        font = ImageFont.load_default()
    except Exception:
        font = None

    if font is not None:
        bitmap_all = []
        glyphs = []
        offset = 0
        for i, ch in enumerate(codes):
            w, h, bm, xadv, xoff, yoff = render_glyph_from_ttf(
                ch, font, args.size, args.thickness
            )
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

        if args.preview_output:
            rc = render_preview_from_data(
                codes, glyphs, bitmap_all, args.preview_output
            )
            if rc != 0:
                sys.exit(rc)
        sys.exit(0)

    # Fallback: generate uniform bitmaps (old behavior)
    glyphs, bitmap_all = generate_header(
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

    # optional preview image showing the same characters (use generated bytes)
    if args.preview_output:
        rc = render_preview_from_data(codes, glyphs, bitmap_all, args.preview_output)
        if rc != 0:
            sys.exit(rc)


if __name__ == "__main__":
    main()
