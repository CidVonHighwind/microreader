#!/usr/bin/env python3
"""
Convert image to byte array for e-ink display
"""
from PIL import Image
import sys


def convert_image_to_bytes(
    image_path, output_path, array_name, width=None, height=None, grayscale=False
):
    """Convert image to byte array for e-ink display

    Args:
        image_path: Path to input image
        output_path: Path to output header file
        array_name: Name for the C array
        width: Optional target width in pixels (default: use image width)
        height: Optional target height in pixels (default: use image height)
        grayscale: If True, output 4-level grayscale (2-bit). If False, output black/white (1-bit). Default: False
    """
    # Open and convert image
    img = Image.open(image_path)

    # Handle EXIF orientation (Windows Explorer rotation sets this flag)
    try:
        from PIL import ImageOps

        img = ImageOps.exif_transpose(img)
    except Exception:
        pass  # If no EXIF data, continue with original

    print(f"Source image dimensions: {img.width}x{img.height}")

    # Use actual image dimensions if not specified
    if width is None:
        width = img.width
    if height is None:
        height = img.height

    # Only resize if dimensions don't match
    if img.width != width or img.height != height:
        # Calculate aspect-preserving resize
        img_ratio = img.width / img.height
        target_ratio = width / height

        if img_ratio > target_ratio:
            # Image is wider, fit to width
            new_width = width
            new_height = int(width / img_ratio)
        else:
            # Image is taller, fit to height
            new_height = height
            new_width = int(height * img_ratio)

        # Resize maintaining aspect ratio
        img = img.resize((new_width, new_height), Image.Resampling.LANCZOS)

        # Create white background and paste centered
        final_img = Image.new("L", (width, height), 255)  # White background
        x_offset = (width - new_width) // 2
        y_offset = (height - new_height) // 2
        final_img.paste(img, (x_offset, y_offset))
        img = final_img
    else:
        # Use image as-is
        img = img.convert("L")  # Convert to grayscale

    # Apply sharpening to preserve thin lines
    from PIL import ImageFilter, ImageEnhance

    img = img.filter(ImageFilter.SHARPEN)

    # Enhance contrast to help preserve details
    enhancer = ImageEnhance.Contrast(img)
    img = enhancer.enhance(1.5)  # Increase contrast by 50%

    # Get pixel data
    pixels = list(img.getdata())
    final_width, final_height = img.size

    if grayscale:
        # Convert to 4 grayscale levels (2-bit encoding)
        # 0-63: White (00), 64-127: Light Gray (01), 128-191: Dark Gray (10), 192-255: Black (11)
        pixel_values = []
        for pixel in pixels:
            if pixel >= 192:
                pixel_values.append(0)  # 00 = White
            elif pixel >= 128:
                pixel_values.append(1)  # 01 = Light Gray
            elif pixel >= 64:
                pixel_values.append(2)  # 10 = Dark Gray
            else:
                pixel_values.append(3)  # 11 = Black

        # Create preview image showing the 4-level grayscale
        preview_pixels = []
        for val in pixel_values:
            if val == 0:
                preview_pixels.append(255)  # White
            elif val == 1:
                preview_pixels.append(170)  # Light Gray
            elif val == 2:
                preview_pixels.append(85)  # Dark Gray
            else:
                preview_pixels.append(0)  # Black

        preview_img = Image.new("L", (final_width, final_height))
        preview_img.putdata(preview_pixels)

        # Convert to byte array (4 pixels per byte, 2 bits each, MSB first)
        byte_array = []
        for y in range(final_height):
            for x in range(0, final_width, 4):
                byte_val = 0
                for i in range(4):
                    if x + i < final_width:
                        pixel_idx = y * final_width + x + i
                        pixel_val = pixel_values[pixel_idx]
                        byte_val |= pixel_val << (6 - i * 2)  # Shift into position
                byte_array.append(byte_val)
    else:
        # Convert to black/white (1-bit encoding)
        # Use Floyd-Steinberg dithering for better quality
        img_bw = img.convert("1")  # Convert to 1-bit black and white with dithering
        pixels = list(img_bw.getdata())

        # Create preview image
        preview_img = img_bw.convert("L")

        # Convert to byte array (8 pixels per byte, 1 bit each, MSB first)
        byte_array = []
        for y in range(final_height):
            for x in range(0, final_width, 8):
                byte_val = 0
                for i in range(8):
                    if x + i < final_width:
                        pixel_idx = y * final_width + x + i
                        # 0 = black, 255 = white in PIL's 1-bit mode
                        # We want 0 = black, 1 = white for the display
                        bit_val = 1 if pixels[pixel_idx] == 255 else 0
                        byte_val |= bit_val << (7 - i)  # Shift into position
                byte_array.append(byte_val)

    # Generate C header file
    with open(output_path, "w") as f:
        f.write(f"#ifndef {array_name.upper()}_H\n")
        f.write(f"#define {array_name.upper()}_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"// Image dimensions: {final_width}x{final_height}\n")
        if grayscale:
            f.write(f"// Encoding: 2-bit grayscale (4 colors), 4 pixels per byte\n")
            f.write(f"// Colors: 00=White, 01=Light Gray, 10=Dark Gray, 11=Black\n")
        else:
            f.write(f"// Encoding: 1-bit black/white, 8 pixels per byte\n")
            f.write(f"// Bit values: 0=White, 1=Black\n")
        f.write(f"#define {array_name.upper()}_WIDTH {final_width}\n")
        f.write(f"#define {array_name.upper()}_HEIGHT {final_height}\n\n")
        f.write(f"const unsigned char {array_name}[] PROGMEM = {{\n")

        # Write bytes in rows of 16
        for i in range(0, len(byte_array), 16):
            row = byte_array[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in row)
            f.write(f"  {hex_str}")
            if i + 16 < len(byte_array):
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write(f"#endif // {array_name.upper()}_H\n")

    print(f"Converted {image_path} to {output_path}")
    print(f"Original size request: {width}x{height}")
    print(f"Final size after rotation: {final_width}x{final_height}")
    print(
        f"Encoding: {'2-bit grayscale (4 colors)' if grayscale else '1-bit black/white'}"
    )
    print(f"Byte array size: {len(byte_array)} bytes")

    # Save preview image (non-inverted)
    preview_path = output_path.replace(".h", "_preview.png")
    preview_img.save(preview_path)
    print(f"Preview saved to: {preview_path}")


if __name__ == "__main__":
    # Use actual image dimensions
    convert_image_to_bytes("scripts/bebop.jpg", "src/bebop_image.h", "bebop_image")
    convert_image_to_bytes("scripts/bebop_2.jpg", "src/bebop_2.h", "bebop_2")
