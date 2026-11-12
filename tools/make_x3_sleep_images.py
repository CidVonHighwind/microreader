#!/usr/bin/env python3
"""Regenerate the X3-sized embedded sleep images (resources/sleep/x3/*.mgr).

    python tools/make_x3_sleep_images.py

Takes the X4 source artwork (resources/sleep/sleep_0.png, sleep_1.png,
sleep_2.jpg), fits each onto the X3's 528x792 portrait screen, rotates it into
the panel's 792x528 physical orientation and writes MGR2 (2 bits per pixel,
4 gray levels) with the same ordered dithering BmpSleepConverter uses.
Needs Pillow: pip install pillow
"""

import os
import struct

from PIL import Image

ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))
SOURCES = ["sleep_0.png", "sleep_1.png", "sleep_2.jpg"]

PHYS_W, PHYS_H = 792, 528  # panel scan order
LOG_W, LOG_H = 528, 792    # what the reader shows

BAYER = [[0, 8, 2, 10], [12, 4, 14, 6], [3, 11, 1, 9], [15, 7, 13, 5]]


def quantize(gray, x, y):
    adj = gray + BAYER[y & 3][x & 3] * 4 - 30
    adj = max(0, min(255, adj))
    return 3 - min(3, adj >> 6)  # 0=white .. 3=black


def main():
    out_dir = os.path.join(ROOT, "resources", "sleep", "x3")
    os.makedirs(out_dir, exist_ok=True)
    for i, name in enumerate(SOURCES):
        im = Image.open(os.path.join(ROOT, "resources", "sleep", name)).convert("L")
        scale = min(LOG_W / im.width, LOG_H / im.height)
        fit = im.resize((max(1, round(im.width * scale)), max(1, round(im.height * scale))), Image.LANCZOS)
        canvas = Image.new("L", (LOG_W, LOG_H), 255)
        canvas.paste(fit, ((LOG_W - fit.width) // 2, (LOG_H - fit.height) // 2))

        # Logical portrait -> physical: phys[py][px] = logical[x = 527-py][y = px]
        cl = canvas.load()
        stride = (PHYS_W + 3) // 4
        out = bytearray(b"MGR2" + struct.pack("<HH", PHYS_W, PHYS_H))
        for py in range(PHYS_H):
            xl = PHYS_H - 1 - py
            row = bytearray(stride)
            for px in range(PHYS_W):
                state = quantize(cl[xl, px], px, py)
                row[px // 4] |= state << (6 - (px % 4) * 2)
            out += row
        dst = os.path.join(out_dir, f"sleep_{i}.mgr")
        with open(dst, "wb") as f:
            f.write(bytes(out))
        print(f"[make_x3_sleep_images] {os.path.relpath(dst, ROOT)} ({len(out):,} bytes)")


if __name__ == "__main__":
    main()
