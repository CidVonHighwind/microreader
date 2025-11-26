#!/usr/bin/env python3
"""
Simple convert image to byte array for e-ink display (no resizing, no dithering)
"""
from PIL import Image
import sys


def convert_image_to_bytes_simple(
    image_path, output_path, array_name, grayscale=False, threshold=128
):
    """Convert image to byte array for e-ink display (no resizing, no dithering)

    Args:
        image_path: Path to input image
        output_path: Path to output header file
        array_name: Name for the C array
        grayscale: If True, output 4-level grayscale (2-bit). If False, output black/white (1-bit). Default: False
        threshold: Threshold for BW conversion (0-255). Default: 128
    """
    # Open and convert image
    img = Image.open(image_path)

    # Handle EXIF orientation (Windows Explorer rotation sets this flag)
    try:
        from PIL import ImageOps

        img = ImageOps.exif_transpose(img)
    except Exception:
        pass  # If no EXIF data, continue with original

    print(f"Image dimensions: {img.width}x{img.height}")

    # Convert to grayscale
    img = img.convert("L")

    # Get pixel data
    pixels = list(img.getdata())
    final_width, final_height = img.size

    # Compute BW
    pixel_values_bw = [
        1 if pixel >= 205 else 0 for pixel in pixels
    ]  # 1 = white, 0 = black

    preview_pixels_bw = [255 if val == 1 else 0 for val in pixel_values_bw]
    preview_img_bw = Image.new("L", (final_width, final_height))
    preview_img_bw.putdata(preview_pixels_bw)

    byte_array = []
    for y in range(final_height):
        for x in range(0, final_width, 8):
            byte_val = 0
            for i in range(8):
                if x + i < final_width:
                    pixel_idx = y * final_width + x + i
                    bit_val = pixel_values_bw[pixel_idx]
                    byte_val |= bit_val << (7 - i)
            byte_array.append(byte_val)

    # Compute grayscale
    pixel_values_gray = []
    for pixel in pixels:
        if pixel >= 205:
            pixel_values_gray.append(0)  # 00 = White
        elif pixel >= 154:
            pixel_values_gray.append(1)  # 01 = Light Gray
        elif pixel >= 103:
            pixel_values_gray.append(2)  # 10 = Gray
        elif pixel >= 52:
            pixel_values_gray.append(3)  # 11 = Dark Gray
        else:
            pixel_values_gray.append(0)  # 00 = White

    preview_pixels_gray = []
    for val in pixel_values_gray:
        if val == 0:
            preview_pixels_gray.append(255)  # White
        elif val == 1:
            preview_pixels_gray.append(170)  # Light Gray
        elif val == 2:
            preview_pixels_gray.append(110)  # Gray
        elif val == 3:
            preview_pixels_gray.append(50)  # Dark Gray
        else:
            preview_pixels_gray.append(255)  # White

    preview_img_gray = Image.new("L", (final_width, final_height))
    preview_img_gray.putdata(preview_pixels_gray)

    lsb_values = [val & 1 for val in pixel_values_gray]
    msb_values = [(val >> 1) & 1 for val in pixel_values_gray]

    lsb_byte_array = []
    for y in range(final_height):
        for x in range(0, final_width, 8):
            byte_val = 0
            for i in range(8):
                if x + i < final_width:
                    pixel_idx = y * final_width + x + i
                    bit_val = lsb_values[pixel_idx]
                    byte_val |= bit_val << (7 - i)
            lsb_byte_array.append(byte_val)

    msb_byte_array = []
    for y in range(final_height):
        for x in range(0, final_width, 8):
            byte_val = 0
            for i in range(8):
                if x + i < final_width:
                    pixel_idx = y * final_width + x + i
                    bit_val = msb_values[pixel_idx]
                    byte_val |= bit_val << (7 - i)
            msb_byte_array.append(byte_val)

    # Generate C header file
    with open(output_path, "w") as f:
        f.write(f"#ifndef {array_name.upper()}_H\n")
        f.write(f"#define {array_name.upper()}_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"// Image dimensions: {final_width}x{final_height}\n")
        f.write(f"// Black/White encoding: 1-bit, 8 pixels per byte\n")
        f.write(f"// Bit values: 0=Black, 1=White\n")
        f.write(
            f"// Grayscale encoding: 2-bit grayscale split into LSB and MSB arrays (1 bit per pixel each)\n"
        )
        f.write(f"// Colors: 00=White, 01=Light Gray, 10=Gray, 11=Dark Gray\n")
        f.write(
            f"// Ranges: 0-51=White, 52-102=Dark Gray, 103-153=Gray, 154-204=Light Gray, 205-255=White\n"
        )
        f.write(f"// LSB array: least significant bit of each pixel\n")
        f.write(f"// MSB array: most significant bit of each pixel\n")
        f.write(f"#define {array_name.upper()}_WIDTH {final_width}\n")
        f.write(f"#define {array_name.upper()}_HEIGHT {final_height}\n\n")

        f.write(f"const unsigned char {array_name}[] PROGMEM = {{\n")
        for i in range(0, len(byte_array), 16):
            row = byte_array[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in row)
            f.write(f"  {hex_str}")
            if i + 16 < len(byte_array):
                f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write(f"const unsigned char {array_name}_lsb[] PROGMEM = {{\n")
        for i in range(0, len(lsb_byte_array), 16):
            row = lsb_byte_array[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in row)
            f.write(f"  {hex_str}")
            if i + 16 < len(lsb_byte_array):
                f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write(f"const unsigned char {array_name}_msb[] PROGMEM = {{\n")
        for i in range(0, len(msb_byte_array), 16):
            row = msb_byte_array[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in row)
            f.write(f"  {hex_str}")
            if i + 16 < len(msb_byte_array):
                f.write(",")
            f.write("\n")
        f.write("};\n\n")

        f.write(f"#endif // {array_name.upper()}_H\n")

    print(f"Converted {image_path} to {output_path}")
    print(f"Image size: {final_width}x{final_height}")
    print(f"BW Encoding: 1-bit black/white")
    print(f"BW Byte array size: {len(byte_array)} bytes")
    print(f"Grayscale Encoding: 2-bit grayscale split into LSB and MSB arrays")
    print(f"LSB array size: {len(lsb_byte_array)} bytes")
    print(f"MSB array size: {len(msb_byte_array)} bytes")

    # Save preview images
    preview_bw_path = output_path.replace(".h", "_bw_preview.png")
    preview_img_bw.save(preview_bw_path)
    print(f"BW Preview saved to: {preview_bw_path}")

    preview_gray_path = output_path.replace(".h", "_gray_preview.png")
    preview_img_gray.save(preview_gray_path)
    print(f"Grayscale Preview saved to: {preview_gray_path}")


if __name__ == "__main__":
    if len(sys.argv) < 4:
        print(
            "Usage: python simple_convert_image.py <image_path> <output_path> <array_name> [--grayscale] [--threshold <value>]"
        )
        sys.exit(1)

    image_path = sys.argv[1]
    output_path = sys.argv[2]
    array_name = sys.argv[3]
    grayscale = "--grayscale" in sys.argv
    threshold = 128
    if "--threshold" in sys.argv:
        idx = sys.argv.index("--threshold")
        if idx + 1 < len(sys.argv):
            threshold = int(sys.argv[idx + 1])

    convert_image_to_bytes_simple(
        image_path, output_path, array_name, grayscale, threshold
    )
