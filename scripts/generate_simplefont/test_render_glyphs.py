from scripts.generate_simplefont.render import render_glyph_from_ttf
import os

fonts = [
    os.path.join("data", f)
    for f in os.listdir("data")
    if f.lower().endswith((".ttf", ".otf"))
]
ALL_OK = True
for font in fonts:
    full = os.path.abspath(font)
    for ch in [ord("A"), ord(" "), 0x2014, ord("é")]:
        try:
            width, height, pixels, xadv, xoff, yoff = render_glyph_from_ttf(
                ch, full, 24
            )
            if len(pixels) != width * height:
                print(
                    f"MISMATCH: {font} char {hex(ch)} pixel len {len(pixels)} vs expected {width*height}"
                )
                ALL_OK = False
        except Exception as e:
            print(f"EXCEPTION: {font} char {hex(ch)} -> {type(e).__name__}: {e}")
            ALL_OK = False

if not ALL_OK:
    raise SystemExit(1)
print("All font renders successful")
