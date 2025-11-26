#!/usr/bin/env python3
"""Quantize an image to 5 gray levels and save as PNG, plus two additional variants.

Usage examples:
  python scripts/quantize_to_5_gray.py "font test.png" out.png

Optional flags:
  --levels L1,L2,...  Comma-separated gray levels (0-255). Default: 255,192,128,64,0

Requires: Pillow (`pip install pillow`)
"""
import argparse
import os
from PIL import Image

DEFAULT_LEVELS = [255, 192, 128, 64, 0]


def parse_palette(s: str):
    parts = [p.strip() for p in s.split(",") if p.strip()]
    vals = []
    for p in parts:
        try:
            v = int(p)
        except ValueError:
            raise argparse.ArgumentTypeError(f"Invalid palette value: {p}")
        if not (0 <= v <= 255):
            raise argparse.ArgumentTypeError(f"Palette values must be 0-255: {p}")
        vals.append(v)
    if len(vals) < 2:
        raise argparse.ArgumentTypeError("Palette must contain at least 2 levels")
    return vals


def quantize_image(img: Image.Image, levels):
    # Ensure grayscale
    if img.mode in ("RGBA", "LA") or (img.mode == "P" and "transparency" in img.info):
        bg = Image.new("RGB", img.size, (255, 255, 255))
        bg.paste(img, mask=img.split()[-1])
        img = bg.convert("L")
    else:
        img = img.convert("L")

    # Create lookup table mapping 0..255 -> nearest level
    n = len(levels)
    # If levels are not sorted, sort them to have monotonic mapping
    sorted_levels = sorted(levels)

    lut = [0] * 256
    for v in range(256):
        # Map v to index using rounding across the 0..255 range
        idx = int(round((v * (n - 1)) / 255.0))
        # Clamp and lookup
        if idx < 0:
            idx = 0
        elif idx >= n:
            idx = n - 1
        lut[v] = sorted_levels[idx]

    # Preserve exact black and white: if the original pixel is 0 or 255,
    # keep it exactly 0 or 255 regardless of the palette mapping.
    lut[0] = 0
    lut[255] = 255

    return img.point(lut)


def main():
    p = argparse.ArgumentParser(
        description="Quantize an image to 5 gray levels and save as PNG, plus two additional variants"
    )
    p.add_argument("input", help="Input image path")
    p.add_argument("output", help="Output PNG path")
    p.add_argument(
        "--levels",
        type=parse_palette,
        default=DEFAULT_LEVELS,
        help="Comma-separated gray levels (0-255). Default: 255,192,128,64,0",
    )
    args = p.parse_args()

    if not os.path.exists(args.input):
        p.error(f"Input file not found: {args.input}")

    try:
        img = Image.open(args.input)
    except Exception as e:
        p.error(f"Unable to open input image: {e}")

    out_img = quantize_image(img, args.levels)

    out_dir = os.path.dirname(args.output)
    if out_dir and not os.path.exists(out_dir):
        os.makedirs(out_dir, exist_ok=True)

    try:
        out_img.save(args.output, format="PNG")
    except Exception as e:
        p.error(f"Unable to save output image: {e}")

    print(f"Saved quantized image to: {args.output}")

    # Create additional outputs
    base, ext = os.path.splitext(args.output)
    bw_output = base + "_bw.png"
    no_black_output = base + "_no_black.png"

    # First additional: black for all except white
    img_bw = out_img.point(lambda p: 255 if p == 255 else 0)
    try:
        img_bw.save(bw_output, format="PNG")
    except Exception as e:
        p.error(f"Unable to save BW image: {e}")
    print(f"Saved BW image to: {bw_output}")

    # Second additional: white and grays, black to white
    img_no_black = out_img.point(lambda p: 255 if p == 0 else p)
    try:
        img_no_black.save(no_black_output, format="PNG")
    except Exception as e:
        p.error(f"Unable to save no-black image: {e}")
    print(f"Saved no-black image to: {no_black_output}")


if __name__ == "__main__":
    main()
