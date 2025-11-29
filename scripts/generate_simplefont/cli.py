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

# When executed directly as `python cli.py`, the module's __package__ will be
# None which prevents relative imports from resolving. Ensure the repository
# root is on sys.path and use absolute imports from the package so this file
# can be executed both as a module and as a script.
if __package__ is None:
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if repo_root not in sys.path:
        sys.path.insert(0, repo_root)

from fontTools.ttLib import TTFont

from scripts.generate_simplefont.render import (
    render_glyph_from_ttf,
    render_preview_from_data,
    render_preview_from_grayscale,
)
from scripts.generate_simplefont.writer import generate_header, write_header_from_data
from scripts.generate_simplefont.bitmap_utils import (
    bytes_per_row,
    gen_bitmap_bytes,
    format_c_byte_list,
    format_c_code_list,
)


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
        "--var",
        action="append",
        help="Variable font axis settings, e.g., --var wght=700 --var wdth=100",
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
    # `cli.py` lives in `scripts/generate_simplefont/` so go up two levels
    # to reach the repository root.
    repo_root = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", ".."))
    if not args.out:
        args.out = os.path.join(repo_root, "src", "Fonts", f"{args.name}.h")
    if not args.preview_output:
        args.preview_output = os.path.join(
            repo_root, "src", "Fonts", f"{args.name}.png"
        )

    # Check if font is variable and print axes
    if args.ttf and os.path.isfile(args.ttf):
        try:
            font = TTFont(args.ttf)
            if "fvar" in font:
                axes = [axis.axisTag for axis in font["fvar"].axes]
                print(f"Variable font detected. Axes: {axes}")
            else:
                print("Font is not variable.")
        except Exception as e:
            print(f"Error loading font with fonttools: {e}")
    elif args.ttf:
        print(f"TTF file not found: {args.ttf}")

    # parse chars spec. Prefer --chars-file when present to avoid shell quoting issues
    codes = []
    variations = {}
    if args.var:
        for var in args.var:
            if "=" in var:
                axis, value = var.split("=", 1)
                try:
                    variations[axis] = float(value)
                except ValueError:
                    print(f"ERROR: invalid variation value: '{value}'")
                    sys.exit(1)
            else:
                print(f"ERROR: invalid variation format: '{var}' (use axis=value)")
                sys.exit(1)

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
                ch, ttf_path, args.size, args.thickness, variations
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
