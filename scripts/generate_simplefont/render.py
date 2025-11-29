"""Rendering helpers: rasterize glyphs via freetype-py and create previews."""

from typing import List, Tuple
import os
import freetype
from PIL import Image, ImageDraw
from .bitmap_utils import bytes_per_row


def render_glyph_from_ttf(
    ch: int, font_path: str, size: int, variations: dict = None
) -> Tuple[int, int, List[int], int, int, int]:
    face = freetype.Face(font_path)
    # Print variable font axes if available
    if hasattr(face, "mm_var") and face.mm_var:
        num_axis = face.mm_var.num_axis
        axes = [face.mm_var.axis[i].tag.decode("ascii") for i in range(num_axis)]
        print(f"Variable font axes: {axes}")
    # Set variable font coordinates if provided
    if variations and hasattr(face, "mm_var") and face.mm_var:
        num_axis = face.mm_var.num_axis
        coords = [0.0] * num_axis
        for i in range(num_axis):
            tag = face.mm_var.axis[i].tag.decode("ascii")
            if tag in variations:
                coords[i] = variations[tag]
        face.set_var_design_coordinates(coords)
    face.set_pixel_sizes(0, size)
    glyph_index = face.get_char_index(ch)
    face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
    bitmap = face.glyph.bitmap
    width = bitmap.width
    height = bitmap.rows
    metrics = face.glyph.metrics
    xoffset = metrics.horiBearingX // 64
    yoffset = -(metrics.horiBearingY // 64)
    xadvance = metrics.horiAdvance // 64

    if width == 0 or height == 0:
        grayscale_pixels = [255] * (size * size // 4)
        width = size // 2
        height = size // 2
        xadvance = size
        yoffset = 0
        return width, height, grayscale_pixels, xadvance, xoffset, yoffset

    # Get grayscale pixels
    buffer = bitmap.buffer
    grayscale_pixels = []
    for y in range(height):
        for x in range(width):
            pixel = 255 - buffer[y * width + x]
            grayscale_pixels.append(pixel)  # No invert, match previous format

    print(
        f"Char: {ch} ({chr(ch)}), width: {width}, height: {height}, xoffset: {xoffset}, yoffset: {yoffset}, xadvance: {xadvance}"
    )

    return width, height, grayscale_pixels, xadvance, xoffset, yoffset


def render_preview_from_data(
    codes: List[int], glyphs: List[dict], bitmap_all: List[int], output_path: str
) -> int:
    glyph_images = []
    offset = 0
    for idx, ch in enumerate(codes):
        g = glyphs[idx]
        w, h = g["width"], g["height"]
        per_glyph_bytes = bytes_per_row(w) * h
        bm = bitmap_all[offset : offset + per_glyph_bytes]
        offset += per_glyph_bytes
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
        PINK = (255, 192, 203)
        img = Image.new("RGB", (w, h), PINK)
        pixels = [PINK if val == 1 else (0, 0, 0) for val in pixel_values_bw]
        img.putdata(pixels)
        glyph_images.append(img)
    num_glyphs = len(glyph_images)
    cols = int(num_glyphs**0.5) + 1
    rows = (num_glyphs + cols - 1) // cols
    max_w = max(img.width for img in glyph_images) if glyph_images else 1
    max_h = max(img.height for img in glyph_images) if glyph_images else 1
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


def render_preview_from_grayscale(
    codes: List[int], glyphs: List[dict], output_path: str
) -> int:
    glyph_images = []
    for idx, ch in enumerate(codes):
        g = glyphs[idx]
        w, h = g["width"], g["height"]
        pixel_values_gray = g["pixel_values"]
        pixels = []
        for val in pixel_values_gray:
            if val == 0:
                pixels.append(255)
            elif val == 1:
                pixels.append(170)
            elif val == 2:
                pixels.append(110)
            elif val == 3:
                pixels.append(50)
            else:
                pixels.append(255)
        img = Image.new("L", (w, h))
        img.putdata(pixels)
        glyph_images.append(img)
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
