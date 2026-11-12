#!/usr/bin/env python3
"""Rebuild the Literata and Atkinson Hyperlegible reader fonts.

Produces resources/fonts/{literata,atkinson}.mfb (the FNTS bundles; the X3
firmware embeds them with the grayscale planes stripped, see
tools/build_assets.py) and the zlib-compressed .bin versions the X4 firmware
embeds and unpacks into its font space.

Both fonts are SIL Open Font License 1.1 (licences next to the bundles).

Inputs (from https://github.com/google/fonts):
  ofl/literata/Literata[opsz,wght].ttf, Literata-Italic[opsz,wght].ttf
  ofl/atkinsonhyperlegible/AtkinsonHyperlegible-{Regular,Bold,Italic,BoldItalic}.ttf

Literata is a variable font; it is cut at optical size 10 (its "caption"
design: sturdier strokes, more open spacing — made for small text, which is
what a 20-32 px e-ink font is) in weights 400 and 700.

Usage:
  python tools/make_extra_fonts.py <google-fonts-checkout> <font_gen binary>

font_gen is tools/font_gen (build it with CMake, or against system
FreeType/HarfBuzz). Same sizes as Bookerly and Alegreya: 20-32 px in steps of
2, default glyph ranges.
"""

import os
import struct
import subprocess
import sys
import tempfile
import zlib

from fontTools.ttLib import TTFont
from fontTools.varLib import instancer

SIZES = ["20", "22", "24", "26", "28", "30", "32"]


def write_bin(mfb_path: str) -> None:
    data = open(mfb_path, "rb").read()
    with open(os.path.splitext(mfb_path)[0] + ".bin", "wb") as f:
        f.write(struct.pack("<I", len(data)))
        f.write(zlib.compress(data, 9))


def main() -> None:
    gfonts, font_gen = sys.argv[1], sys.argv[2]
    out_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "resources", "fonts")
    with tempfile.TemporaryDirectory() as tmp:
        lit = os.path.join(gfonts, "ofl", "literata")
        styles = {}
        for src, ital in (("Literata[opsz,wght].ttf", False), ("Literata-Italic[opsz,wght].ttf", True)):
            for weight in (400, 700):
                font = instancer.instantiateVariableFont(TTFont(os.path.join(lit, src)), {"opsz": 10, "wght": weight})
                key = ("bold-" if weight == 700 else "") + ("italic" if ital else "regular")
                styles[key] = os.path.join(tmp, f"Literata-{key}.ttf")
                font.save(styles[key])
        subprocess.check_call([font_gen, styles["regular"], "--bold", styles["bold-regular"], "--italic",
                               styles["italic"], "--bold-italic", styles["bold-italic"], "--bundle",
                               "--bundle-sizes", *SIZES, "--font-name", "Literata", "--output",
                               os.path.join(out_dir, "literata.mfb")])

    ah = os.path.join(gfonts, "ofl", "atkinsonhyperlegible", "AtkinsonHyperlegible-")
    subprocess.check_call([font_gen, ah + "Regular.ttf", "--bold", ah + "Bold.ttf", "--italic", ah + "Italic.ttf",
                           "--bold-italic", ah + "BoldItalic.ttf", "--bundle", "--bundle-sizes", *SIZES,
                           "--font-name", "Atkinson", "--output", os.path.join(out_dir, "atkinson.mfb")])

    for name in ("literata", "atkinson"):
        write_bin(os.path.join(out_dir, name + ".mfb"))
        print("wrote", name)


if __name__ == "__main__":
    main()
