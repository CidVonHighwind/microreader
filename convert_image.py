#!/usr/bin/env python3
"""
Convert image to byte array for e-ink display
"""
from PIL import Image
import sys


def convert_image_to_bytes(image_path, output_path, width=800, height=480):
    """Convert image to 1-bit bitmap byte array for e-ink display"""
    # Open and convert image
    img = Image.open(image_path)

    # Resize to exact dimensions needed
    img = img.resize((width, height), Image.Resampling.LANCZOS)

    # Rotate 90 degrees counter-clockwise (positive 90)
    img = img.rotate(90, expand=True)

    # Convert to grayscale then to 1-bit (black and white) - inverted
    img = img.convert("L")  # Grayscale
    img = img.point(
        lambda x: 0 if x > 128 else 255, mode="1"
    )  # Threshold to 1-bit (inverted)

    # Get pixel data and final dimensions
    pixels = list(img.getdata())
    final_width, final_height = img.size

    # Convert to byte array (8 pixels per byte, MSB first)
    byte_array = []
    for y in range(final_height):
        for x in range(0, final_width, 8):
            byte_val = 0
            for bit in range(8):
                if x + bit < final_width:
                    pixel_idx = y * final_width + x + bit
                    if pixels[pixel_idx] == 0:  # Black pixel
                        byte_val |= 0x80 >> bit
            byte_array.append(byte_val)

    # Generate C header file
    with open(output_path, "w") as f:
        f.write("#ifndef BEBOP_IMAGE_H\n")
        f.write("#define BEBOP_IMAGE_H\n\n")
        f.write("#include <pgmspace.h>\n\n")
        f.write(f"// Image dimensions after rotation: {final_width}x{final_height}\n")
        f.write(f"#define BEBOP_WIDTH {final_width}\n")
        f.write(f"#define BEBOP_HEIGHT {final_height}\n\n")
        f.write("const unsigned char bebop_image[] PROGMEM = {\n")

        # Write bytes in rows of 16
        for i in range(0, len(byte_array), 16):
            row = byte_array[i : i + 16]
            hex_str = ", ".join(f"0x{b:02X}" for b in row)
            f.write(f"  {hex_str}")
            if i + 16 < len(byte_array):
                f.write(",")
            f.write("\n")

        f.write("};\n\n")
        f.write("#endif // BEBOP_IMAGE_H\n")

    print(f"Converted {image_path} to {output_path}")
    print(f"Original size request: {width}x{height}")
    print(f"Final size after rotation: {final_width}x{final_height}")
    print(f"Byte array size: {len(byte_array)} bytes")


if __name__ == "__main__":
    convert_image_to_bytes("bebop.jpg", "src/bebop_image.h", 150, 200)
