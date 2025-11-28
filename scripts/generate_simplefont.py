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
import os
import sys

from PIL import Image, ImageDraw, ImageFont, ImageChops


def bytes_per_row(width):
    return (width + 7) // 8


def render_preview_from_data(codes, glyphs, bitmap_all, output_path):
    """Render BW preview from generated bitmap data."""
    glyph_images = []
    offset = 0
    for idx, ch in enumerate(codes):
        g = glyphs[idx]
        w, h = g["width"], g["height"]
        per_glyph_bytes = bytes_per_row(w) * h
        bm = bitmap_all[offset : offset + per_glyph_bytes]
        offset += per_glyph_bytes
        # Unpack to pixel values
        pixel_values_bw = []
        bpr = bytes_per_row(w)
        for y in range(h):
            for x in range(0, w, 8):
                byte_idx = y * bpr + x // 8
                byte_val = bm[byte_idx]
                for i in range(8):
                    if x + i < w:
                        bit = (byte_val >> (7 - i)) & 1
                        pixel_values_bw.append(bit)
        # Create RGB image for the glyph box: pink background, black strokes
        PINK = (255, 192, 203)
        img = Image.new("RGB", (w, h), PINK)
        pixels = [PINK if val == 1 else (0, 0, 0) for val in pixel_values_bw]
        img.putdata(pixels)
        glyph_images.append(img)
    # Arrange in grid
    num_glyphs = len(glyph_images)
    cols = int(num_glyphs**0.5) + 1
    rows = (num_glyphs + cols - 1) // cols
    max_w = max(img.width for img in glyph_images) if glyph_images else 1
    max_h = max(img.height for img in glyph_images) if glyph_images else 1
    # Overall canvas: white RGB so we can paste per-glyph RGB boxes (pink/black)
    WHITE = (255, 255, 255)
    big_img = Image.new("RGB", (cols * max_w, rows * max_h), WHITE)
    for idx, img in enumerate(glyph_images):
        row = idx // cols
        col = idx % cols
        x = col * max_w
        y = row * max_h
        big_img.paste(img, (x, y))
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    big_img.save(output_path)
    print(f"BW Preview saved to: {output_path}")
    return 0


def render_preview_from_grayscale(codes, glyphs, output_path):
    """Render grayscale preview from pixel values."""
    glyph_images = []
    for idx, ch in enumerate(codes):
        g = glyphs[idx]
        w, h = g["width"], g["height"]
        pixel_values_gray = g["pixel_values"]
        # Convert to grayscale colors
        pixels = []
        for val in pixel_values_gray:
            if val == 0:
                pixels.append(255)  # White
            elif val == 1:
                pixels.append(170)  # Light Gray
            elif val == 2:
                pixels.append(110)  # Gray
            elif val == 3:
                pixels.append(50)  # Dark Gray
            else:
                pixels.append(255)
        img = Image.new("L", (w, h))
        img.putdata(pixels)
        glyph_images.append(img)
    # Arrange in grid
    num_glyphs = len(glyph_images)
    cols = int(num_glyphs**0.5) + 1
    rows = (num_glyphs + cols - 1) // cols
    max_w = max(img.width for img in glyph_images) if glyph_images else 1
    max_h = max(img.height for img in glyph_images) if glyph_images else 1
    big_img = Image.new("L", (cols * max_w, rows * max_h), 255)
    for idx, img in enumerate(glyph_images):
        row = idx // cols
        col = idx % cols
        x = col * max_w
        y = row * max_h
        big_img.paste(img, (x, y))
    os.makedirs(os.path.dirname(output_path), exist_ok=True)
    big_img.save(output_path)
    print(f"Grayscale Preview saved to: {output_path}")
    return 0


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
    return width, height, grayscale_pixels, xAdvance, xOffset, yOffset
    grayscale_pixels is a list of 0-255 values for full grayscale.
    xOffset/yOffset are computed relative to a baseline at `ascent` from the font metrics.
    """
    s = chr(ch)
    # create a temporary image big enough
    # Render glyphs in black (0) on a white background (255) which is the
    # desired final appearance. To compute a crop bbox we invert the image
    # and call getbbox() on the inverted image so non-zero pixels correspond
    # to the glyph strokes.
    img = Image.new("L", (size * 4, size * 4), 255)
    draw = ImageDraw.Draw(img)
    try:
        ascent, descent = font.getmetrics()
    except Exception:
        ascent = size
        descent = 0

    # Render the glyph at (0, ascent) so baseline is at ascent from top.
    # Draw glyph strokes in black (0) on the white background.
    if thickness and thickness > 0:
        try:
            draw.text(
                (0, ascent),
                s,
                font=font,
                fill=0,
                stroke_width=thickness,
                stroke_fill=0,
            )
        except TypeError:
            draw.text((0, ascent), s, font=font, fill=0)
    else:
        draw.text((0, ascent), s, font=font, fill=0)

    # Compute bbox on the inverted image so glyph (black=0) becomes non-zero
    # and is detected by getbbox(). This preserves drawing glyphs as black
    # on white while still enabling reliable cropping.
    inv = ImageChops.invert(img)
    bbox = inv.getbbox()
    if bbox is None:
        # empty glyph (e.g., space). Use a blank width heuristic
        width = max(1, size // 2)
        height = max(1, size // 2)
        # For an empty glyph we should return white (background) pixels so
        # downstream code treats the glyph area as empty/white on device.
        grayscale_pixels = [255] * (width * height)
        xadvance = font.getsize(s)[0] if hasattr(font, "getsize") else width
        xoffset = 0
        yoffset = -ascent
        return width, height, grayscale_pixels, xadvance, xoffset, yoffset

    left, upper, right, lower = bbox
    cropped = img.crop(bbox)
    width, height = cropped.size
    pixels = cropped.load()
    grayscale_pixels = []
    for y in range(height):
        for x in range(width):
            v = pixels[x, y]
            grayscale_pixels.append(v)

    # compute xAdvance using font metrics where possible
    try:
        xadvance = font.getsize(s)[0]
    except Exception:
        xadvance = max(1, width)

    # xOffset: distance from cursor to UL corner. Since we render at x=0, left is the offset, so xOffset = -left
    xoffset = 0
    # yOffset: want UL such that cursorY is baseline -> UL = baseline + yOffset
    # ascent is distance from baseline to top of font; the glyph top is `upper` relative to image top, baseline at ascent
    yoffset = upper - ascent

    return width, height, grayscale_pixels, xadvance, xoffset, yoffset


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
    bitmap_lsb_all = []
    bitmap_msb_all = []
    glyphs = []
    offset = 0
    for ch in chars:
        bm = gen_bitmap_bytes(width, height, fill)
        bitmap_all.extend(bm)
        pixel_values = [3 if fill else 0] * (width * height)
        lsb_values = [val & 1 for val in pixel_values]
        msb_values = [(val >> 1) & 1 for val in pixel_values]
        lsb_chunk = []
        for y in range(height):
            for x in range(0, width, 8):
                byte_val = 0
                for i in range(8):
                    if x + i < width:
                        idx = y * width + x + i
                        bit_val = lsb_values[idx]
                        byte_val |= bit_val << (7 - i)
                lsb_chunk.append(byte_val)
        bitmap_lsb_all.extend(lsb_chunk)
        msb_chunk = []
        for y in range(height):
            for x in range(0, width, 8):
                byte_val = 0
                for i in range(8):
                    if x + i < width:
                        idx = y * width + x + i
                        bit_val = msb_values[idx]
                        byte_val |= bit_val << (7 - i)
                msb_chunk.append(byte_val)
        bitmap_msb_all.extend(msb_chunk)
        glyph = {
            "bitmapOffset": offset,
            "width": width,
            "height": height,
            "xAdvance": xadvance,
            "xOffset": xoffset,
            "yOffset": yoffset,
            "pixel_values": pixel_values,
        }
        glyphs.append(glyph)
        offset += len(bm)

    # Prepare content: group bitmap bytes per glyph and add a comment indicating the character
    bmp_lines = []
    bmp_lsb_lines = []
    bmp_msb_lines = []
    for idx, ch in enumerate(chars):
        g = glyphs[idx]
        per_glyph_bytes = bytes_per_row(g["width"]) * g["height"]
        start = g["bitmapOffset"]
        end = start + per_glyph_bytes
        chunk = bitmap_all[start:end]
        chunk_lsb = bitmap_lsb_all[start:end]
        chunk_msb = bitmap_msb_all[start:end]
        # printable character display
        display = chr(ch)
        comment = f"// 0x{ch:X} '{display}'"
        # format chunk bytes
        chunk_c = format_c_byte_list(chunk)
        chunk_lsb_c = format_c_byte_list(chunk_lsb)
        chunk_msb_c = format_c_byte_list(chunk_msb)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
        bmp_lsb_lines.append(f"    {comment}\n{chunk_lsb_c}")
        bmp_msb_lines.append(f"    {comment}\n{chunk_msb_c}")
    bmp_c = ",\n".join(bmp_lines)
    bmp_lsb_c = ",\n".join(bmp_lsb_lines)
    bmp_msb_c = ",\n".join(bmp_msb_lines)
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

const uint8_t {font_name}Bitmaps_lsb[] PROGMEM = {{
{bmp_lsb_c}
}};

const uint8_t {font_name}Bitmaps_msb[] PROGMEM = {{
{bmp_msb_c}
}};

const SimpleGFXglyph {font_name}Glyphs[] PROGMEM = {{
{glyphs_c}
}};

const SimpleGFXfont {font_name} PROGMEM = {{{font_name}Bitmaps, {font_name}Bitmaps_lsb, {font_name}Bitmaps_msb, {font_name}Glyphs,
    {count}, {yadvance}}};
"""

    # ensure output dir exists
    os.makedirs(os.path.dirname(out_path), exist_ok=True)
    with open(out_path, "w", encoding="utf-8", newline="\n") as f:
        f.write(header)
    print(f"Wrote {out_path}")
    # return generated data so callers can render a preview using the same bytes
    return glyphs, bitmap_all, bitmap_lsb_all, bitmap_msb_all


def write_header_from_data(
    font_name,
    out_path,
    chars,
    glyphs,
    bitmap_all,
    bitmap_lsb_all,
    bitmap_msb_all,
    yadvance,
):
    """Write a header when glyphs and bitmap_all are already prepared.

    `glyphs` is a list of dicts with keys: bitmapOffset,width,height,xAdvance,xOffset,yOffset,pixel_values
    """
    # Prepare content: group bitmap bytes per glyph and add a comment indicating the character
    bmp_lines = []
    bmp_lsb_lines = []
    bmp_msb_lines = []
    for idx, ch in enumerate(chars):
        g = glyphs[idx]
        per_glyph_bytes = bytes_per_row(g["width"]) * g["height"]
        start = g["bitmapOffset"]
        end = start + per_glyph_bytes
        chunk = bitmap_all[start:end]
        chunk_lsb = bitmap_lsb_all[start:end]
        chunk_msb = bitmap_msb_all[start:end]
        display = chr(ch)
        comment = f"// 0x{ch:X} '{display}'"
        chunk_c = format_c_byte_list(chunk)
        chunk_lsb_c = format_c_byte_list(chunk_lsb)
        chunk_msb_c = format_c_byte_list(chunk_msb)
        bmp_lines.append(f"    {comment}\n{chunk_c}")
        bmp_lsb_lines.append(f"    {comment}\n{chunk_lsb_c}")
        bmp_msb_lines.append(f"    {comment}\n{chunk_msb_c}")
    bmp_c = ",\n".join(bmp_lines)
    bmp_lsb_c = ",\n".join(bmp_lsb_lines)
    bmp_msb_c = ",\n".join(bmp_msb_lines)

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

const uint8_t {font_name}Bitmaps_lsb[] PROGMEM = {{
{bmp_lsb_c}
}};

const uint8_t {font_name}Bitmaps_msb[] PROGMEM = {{
{bmp_msb_c}
}};

const SimpleGFXglyph {font_name}Glyphs[] PROGMEM = {{
{glyphs_c}
}};

const SimpleGFXfont {font_name} PROGMEM = {{{font_name}Bitmaps, {font_name}Bitmaps_lsb, {font_name}Bitmaps_msb, {font_name}Glyphs,
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

    # Default output paths when not provided: header in src/Fonts (repo root)
    # Compute repo root from this script's directory so defaults are correct
    # regardless of the current working directory that invokes the script.
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), ".."))
    if not args.out:
        args.out = os.path.join(repo_root, "src", "Fonts", f"{args.name}.h")
    if not args.preview_output:
        args.preview_output = os.path.join(
            repo_root, "src", "Fonts", f"{args.name}.png"
        )

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
        bitmap_lsb_all = []
        bitmap_msb_all = []
        glyphs = []
        offset = 0
        for i, ch in enumerate(codes):
            w, h, grayscale_pixels, xadv, xoff, yoff = render_glyph_from_ttf(
                ch, font, args.size, args.thickness
            )
            # Compute grayscale pixel values (0-3)
            pixel_values_gray = []
            for pixel in grayscale_pixels:
                if pixel >= 205:
                    pixel_values_gray.append(0)  # White
                elif pixel >= 154:
                    pixel_values_gray.append(1)  # Light Gray
                elif pixel >= 103:
                    pixel_values_gray.append(2)  # Gray
                elif pixel >= 52:
                    pixel_values_gray.append(3)  # Dark Gray
                else:
                    pixel_values_gray.append(0)  # White
            # Compute BW pixel values (1=white, 0=black)
            pixel_values_bw = [1 if pixel >= 154 else 0 for pixel in grayscale_pixels]
            # Build BW bitmap
            bm = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            pixel_idx = y * w + x + i
                            bit_val = pixel_values_bw[pixel_idx]
                            byte_val |= bit_val << (7 - i)
                    bm.append(byte_val)
            glyph = {
                "bitmapOffset": offset,
                "width": w,
                "height": h,
                "xAdvance": xadv,
                "xOffset": xoff,
                "yOffset": yoff,
                "pixel_values": pixel_values_gray,
            }
            glyphs.append(glyph)
            bitmap_all.extend(bm)
            # compute lsb and msb
            lsb_values = [val & 1 for val in pixel_values_gray]
            msb_values = [(val >> 1) & 1 for val in pixel_values_gray]
            lsb_chunk = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            idx = y * w + x + i
                            bit_val = lsb_values[idx]
                            byte_val |= bit_val << (7 - i)
                    lsb_chunk.append(byte_val)
            bitmap_lsb_all.extend(lsb_chunk)
            msb_chunk = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            idx = y * w + x + i
                            bit_val = msb_values[idx]
                            byte_val |= bit_val << (7 - i)
                    msb_chunk.append(byte_val)
            bitmap_msb_all.extend(msb_chunk)
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
        write_header_from_data(
            args.name,
            args.out,
            codes,
            glyphs,
            bitmap_all,
            bitmap_lsb_all,
            bitmap_msb_all,
            yadvance,
        )
        # optional previews: render both a TTF grayscale preview and a 1-bit
        # preview generated from the packed bitmap data. Both files are written
        # next to the requested path using suffixes `_ttf` and `_bitmap`.
        if args.preview_output:
            base, ext = os.path.splitext(args.preview_output)
            ttf_preview = f"{base}_ttf{ext}"
            bitmap_preview = f"{base}_bitmap{ext}"
            gray_preview = f"{base}_gray{ext}"

            # TTF preview not implemented
            print("TTF preview not implemented")

            # Also render the 1-bit preview from the generated bitmap bytes so
            # callers can compare exact on-device rendering appearance.
            rc2 = render_preview_from_data(codes, glyphs, bitmap_all, bitmap_preview)
            if rc2 != 0:
                sys.exit(rc2)

            # Render the grayscale preview from the generated pixel values
            rc3 = render_preview_from_grayscale(codes, glyphs, gray_preview)
            if rc3 != 0:
                sys.exit(rc3)
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
        bitmap_lsb_all = []
        bitmap_msb_all = []
        glyphs = []
        offset = 0
        for i, ch in enumerate(codes):
            w, h, grayscale_pixels, xadv, xoff, yoff = render_glyph_from_ttf(
                ch, font, args.size, args.thickness
            )
            # Compute grayscale pixel values (0-3)
            pixel_values_gray = []
            for pixel in grayscale_pixels:
                if pixel >= 205:
                    pixel_values_gray.append(0)  # White
                elif pixel >= 154:
                    pixel_values_gray.append(1)  # Light Gray
                elif pixel >= 103:
                    pixel_values_gray.append(2)  # Gray
                elif pixel >= 52:
                    pixel_values_gray.append(3)  # Dark Gray
                else:
                    pixel_values_gray.append(0)  # White
            # Compute BW pixel values (1=white, 0=black)
            pixel_values_bw = [1 if pixel >= 154 else 0 for pixel in grayscale_pixels]
            # Build BW bitmap
            bm = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            pixel_idx = y * w + x + i
                            bit_val = pixel_values_bw[pixel_idx]
                            byte_val |= bit_val << (7 - i)
                    bm.append(byte_val)
            glyph = {
                "bitmapOffset": offset,
                "width": w,
                "height": h,
                "xAdvance": xadv,
                "xOffset": xoff,
                "yOffset": yoff,
                "pixel_values": pixel_values_gray,
            }
            glyphs.append(glyph)
            bitmap_all.extend(bm)
            # compute lsb and msb
            lsb_values = [val & 1 for val in pixel_values_gray]
            msb_values = [(val >> 1) & 1 for val in pixel_values_gray]
            lsb_chunk = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            idx = y * w + x + i
                            bit_val = lsb_values[idx]
                            byte_val |= bit_val << (7 - i)
                    lsb_chunk.append(byte_val)
            bitmap_lsb_all.extend(lsb_chunk)
            msb_chunk = []
            for y in range(h):
                for x in range(0, w, 8):
                    byte_val = 0
                    for i in range(8):
                        if x + i < w:
                            idx = y * w + x + i
                            bit_val = msb_values[idx]
                            byte_val |= bit_val << (7 - i)
                    msb_chunk.append(byte_val)
            bitmap_msb_all.extend(msb_chunk)
            offset += len(bm)

        yadvance = args.size + 2
        write_header_from_data(
            args.name,
            args.out,
            codes,
            glyphs,
            bitmap_all,
            bitmap_lsb_all,
            bitmap_msb_all,
            yadvance,
        )

        if args.preview_output:
            base, ext = os.path.splitext(args.preview_output)
            bitmap_preview = f"{base}_bitmap{ext}"
            gray_preview = f"{base}_gray{ext}"
            rc = render_preview_from_data(codes, glyphs, bitmap_all, bitmap_preview)
            if rc != 0:
                sys.exit(rc)
            rc2 = render_preview_from_grayscale(codes, glyphs, gray_preview)
            if rc2 != 0:
                sys.exit(rc2)
        sys.exit(0)

    # Fallback: generate uniform bitmaps (old behavior)
    glyphs, bitmap_all, bitmap_lsb_all, bitmap_msb_all = generate_header(
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
    # Preview functions not implemented


if __name__ == "__main__":
    main()
