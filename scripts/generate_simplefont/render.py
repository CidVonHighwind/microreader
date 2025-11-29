"""Rendering helpers: rasterize glyphs via freetype-py and create previews."""

from typing import List, Tuple
import os
import freetype
from PIL import Image
from .bitmap_utils import bytes_per_row


# debug_info builder removed; keep function short and not returning debugging structures


def render_glyph_from_ttf(
    ch: int,
    font_source,
    size: int,
    thickness: float = 0.0,
    variations: dict = None,
) -> Tuple[int, int, List[int], int, int, int]:
    # Determine whether font_source is a path (string) for freetype or a PIL ImageFont.
    face = None
    is_ttf = isinstance(font_source, str)
    if is_ttf:
        face = freetype.Face(font_source)
    else:
        # Non-TTF font (e.g., PIL ImageFont) not currently supported by the freetype
        # For full PIL support, implement separate rasterization logic here.
        try:
            face = freetype.Face(str(font_source))
        except Exception:
            face = None
    # Prefer get_variation_info() to detect variable fonts (some freetype-py builds
    # do not populate face.mm_var, but get_variation_info() works if VF support exists).
    vsi = None
    try:
        vsi = face.get_variation_info() if face is not None else None
    except Exception:
        vsi = None
    # Set pixel size early so default axis rendering is at requested size (only for freetype face)
    if face is not None:
        face.set_pixel_sizes(0, size)
    # Set variable font coordinates if provided.
    # variations: dict mapping axis tag (e.g., 'wght') to axis value in the
    # same units as the font's axis (e.g., 400, 700). freetype-py will convert
    # these floats to the fixed-point representation internally.
    if variations:
        has_vf = vsi is not None and len(vsi.axes) > 0
        # If the face is not variable, ignore variations.
        if not has_vf:
            pass
        else:
            # Use the VariationSpaceInfo wrapper to access properly-scaled axis values
            # Already have variation info in vsi, reuse it
            axes = vsi.axes
            num_axis = len(axes)
            coords = [0.0] * num_axis
            for i in range(num_axis):
                axis = axes[i]
                tag = axis.tag
                # Use provided variation value if available; fall back to axis default
                if tag in variations:
                    coords[i] = float(variations[tag])
                else:
                    coords[i] = float(axis.default)
            # Clamp coords to axis ranges
            for i in range(num_axis):
                coords[i] = max(min(coords[i], axes[i].maximum), axes[i].minimum)
            # Debug: print axis ranges and coords we are setting
            axis_info = [
                {
                    "tag": axes[i].tag,
                    "min": axes[i].minimum,
                    "default": axes[i].default,
                    "max": axes[i].maximum,
                    "coord_to_set": coords[i],
                }
                for i in range(num_axis)
            ]
            # Only print a concise applied coords message when coords actually get applied
        # Use the proper freetype-py method name; use has_vf as the guard
        if has_vf:
            # Apply coords after size is set; only apply if they differ from current coords
            try:
                cur_coords = tuple(face.get_var_design_coords())
            except Exception:
                cur_coords = None
            try:
                if hasattr(face, "set_var_design_coords"):
                    if cur_coords is None or tuple(coords) != cur_coords:
                        face.set_var_design_coords(coords)
                elif hasattr(face, "set_var_design_coordinates"):
                    if cur_coords is None or tuple(coords) != cur_coords:
                        face.set_var_design_coordinates(coords)
            except Exception:
                # Failed to set var coords; ignore silently
                pass
            try:
                applied = tuple(face.get_var_design_coords())
            except Exception:
                applied = None
        # no fallback here - we only apply coords when we have validated axes
    # face.set_pixel_sizes already called earlier and we applied coords above; we only
    # need to render the glyph once, which happens below.
    glyph_index = face.get_char_index(ch)
    face.load_glyph(glyph_index, freetype.FT_LOAD_RENDER)
    bitmap = face.glyph.bitmap
    width = bitmap.width
    height = bitmap.rows
    metrics = face.glyph.metrics
    xoffset = metrics.horiBearingX // 64
    yoffset = -(metrics.horiBearingY // 64)
    xadvance = metrics.horiAdvance // 64

    # If the glyph produced no bitmap (e.g., space), produce a small white box
    # with the correct xadvance taken from the FreeType metrics so the spacing
    # is consistent with the font (don't fall back to 'size' here unless needed).
    if width == 0 or height == 0:
        width = size // 2
        height = size // 2
        grayscale_pixels = [255] * (width * height)
        try:
            xadvance = metrics.horiAdvance // 64
        except Exception:
            xadvance = size
        yoffset = 0
        return width, height, grayscale_pixels, xadvance, xoffset, yoffset

    # Extract the rendered bitmap and convert it to 0-255 grayscale pixels
    buffer = bitmap.buffer
    grayscale_pixels = []
    for y in range(height):
        for x in range(width):
            pixel = 255 - buffer[y * width + x]
            grayscale_pixels.append(pixel)

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
