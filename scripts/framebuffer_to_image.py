#!/usr/bin/env python3
"""
Convert framebuffer hex dump to image file.
Reads framebuffer.txt and creates a PNG image.
"""

import re
from PIL import Image

# Display dimensions
WIDTH = 800
HEIGHT = 480
WIDTH_BYTES = WIDTH // 8


def parse_framebuffer(filename):
    """Parse hex dump from framebuffer.txt - handles full serial output"""
    data = []
    in_framebuffer = False
    line_count = 0

    with open(filename, "r", encoding="utf-16", errors="ignore") as f:
        for line in f:
            line = line.strip()

            # Start capturing when we see the framebuffer dump header
            if "=== FRAMEBUFFER DUMP ===" in line:
                in_framebuffer = True
                print("Found framebuffer start marker")
                continue

            # Stop capturing at the end marker
            if "=== END FRAMEBUFFER DUMP ===" in line:
                in_framebuffer = False
                print(f"Found framebuffer end marker, parsed {line_count} lines")
                break

            # Only process lines when inside framebuffer dump
            if in_framebuffer:
                # Look for lines with hex data (format: "00000000: FF FF FF ...")
                # Case-insensitive match for hex digits
                match = re.match(
                    r"^([0-9A-Fa-f]{8}):\s+((?:[0-9A-Fa-f]{2}\s*)+)$", line
                )
                if match:
                    hex_bytes = match.group(2).split()
                    data.extend([int(b, 16) for b in hex_bytes])
                    line_count += 1

    print(f"Total lines with hex data: {line_count}")
    return bytes(data)


def framebuffer_to_image(framebuffer_data, output_filename):
    """Convert framebuffer bytes to PNG image"""
    if len(framebuffer_data) != WIDTH_BYTES * HEIGHT:
        print(
            f"Warning: Expected {WIDTH_BYTES * HEIGHT} bytes, got {len(framebuffer_data)}"
        )

    # Create image (1-bit mode)
    img = Image.new("1", (WIDTH, HEIGHT), 1)
    pixels = img.load()

    # Convert each byte to 8 pixels
    for y in range(HEIGHT):
        for x_byte in range(WIDTH_BYTES):
            if y * WIDTH_BYTES + x_byte >= len(framebuffer_data):
                break

            byte_val = framebuffer_data[y * WIDTH_BYTES + x_byte]

            # Each bit represents one pixel (MSB first)
            for bit in range(8):
                x = x_byte * 8 + bit
                # 1 = white, 0 = black in the framebuffer
                pixel_val = (byte_val >> (7 - bit)) & 1
                pixels[x, y] = pixel_val

    # Save as PNG
    img.save(output_filename)
    print(f"Image saved to: {output_filename}")
    print(f"Dimensions: {WIDTH}x{HEIGHT} pixels")


if __name__ == "__main__":
    import sys

    input_file = "framebuffer.txt"
    output_file = "framebuffer_output.png"

    if len(sys.argv) > 1:
        input_file = sys.argv[1]
    if len(sys.argv) > 2:
        output_file = sys.argv[2]

    print(f"Reading framebuffer from: {input_file}")
    framebuffer = parse_framebuffer(input_file)
    print(f"Parsed {len(framebuffer)} bytes")

    framebuffer_to_image(framebuffer, output_file)
