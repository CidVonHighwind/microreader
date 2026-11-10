#!/usr/bin/env python3
"""Generate fixtures for the sleep-screen tests.

    python test/fixtures/generate_cover_fixtures.py

Writes (next to this script):
    with_cover.epub        EPUB 2 book whose cover is declared with <meta name="cover">
    with_cover_epub3.epub  EPUB 3 book whose cover is declared with properties="cover-image"
    sleep_landscape.jpg    baseline JPEG, landscape (rotated to fill the portrait screen)
    sleep_portrait.png     PNG, portrait

Needs Pillow.
"""

import io
import os
import zipfile

from PIL import Image, ImageDraw, ImageFont

HERE = os.path.dirname(os.path.abspath(__file__))


def font(size):
    for name in ("DejaVuSans-Bold.ttf", "Arial Bold.ttf", "arialbd.ttf"):
        try:
            return ImageFont.truetype(name, size)
        except OSError:
            pass
    return ImageFont.load_default()


def cover_image(w, h, title, author):
    img = Image.new("L", (w, h), 235)
    d = ImageDraw.Draw(img)
    # Vertical gradient band and a sun, so dithering has something to do.
    for y in range(h // 2, h):
        shade = int(60 + 150 * (y - h // 2) / (h // 2))
        d.line([(0, y), (w, y)], fill=shade)
    r = w // 5
    d.ellipse([w // 2 - r, h // 2 - r - r // 2, w // 2 + r, h // 2 + r - r // 2], fill=40)
    d.rectangle([w // 12, h // 12, w - w // 12, h // 12 + h // 6], fill=255, outline=0, width=4)
    d.text((w // 2, h // 12 + h // 12), title, fill=0, font=font(w // 11), anchor="mm")
    d.text((w // 2, h - h // 10), author, fill=255, font=font(w // 18), anchor="mm")
    return img


def encode(img, fmt):
    out = io.BytesIO()
    if fmt == "JPEG":
        # Colour (3-component, 4:2:0) so the interleaved decode path is exercised.
        img.convert("RGB").save(out, "JPEG", quality=85, progressive=False)
    else:
        img.save(out, "PNG")
    return out.getvalue()


def write_epub(path, title, author, cover_bytes, cover_name, media_type, epub3):
    container = """<?xml version="1.0"?>
<container version="1.0" xmlns="urn:oasis:names:tc:opendocument:xmlns:container">
  <rootfiles><rootfile full-path="OEBPS/content.opf" media-type="application/oebps-package+xml"/></rootfiles>
</container>"""
    cover_meta = "" if epub3 else '<meta name="cover" content="cover-img"/>'
    cover_props = ' properties="cover-image"' if epub3 else ""
    opf = f"""<?xml version="1.0" encoding="utf-8"?>
<package xmlns="http://www.idpf.org/2007/opf" version="{'3.0' if epub3 else '2.0'}" unique-identifier="id">
  <metadata xmlns:dc="http://purl.org/dc/elements/1.1/">
    <dc:title>{title}</dc:title><dc:creator>{author}</dc:creator><dc:language>en</dc:language>
    <dc:identifier id="id">{title}</dc:identifier>{cover_meta}
  </metadata>
  <manifest>
    <item id="cover-img" href="images/{cover_name}" media-type="{media_type}"{cover_props}/>
    <item id="ch1" href="ch1.xhtml" media-type="application/xhtml+xml"/>
    <item id="ch2" href="ch2.xhtml" media-type="application/xhtml+xml"/>
    <item id="ncx" href="toc.ncx" media-type="application/x-dtbncx+xml"/>
  </manifest>
  <spine toc="ncx"><itemref idref="ch1"/><itemref idref="ch2"/></spine>
</package>"""
    para = "<p>" + ("It was a bright cold day in the reading room, and the pages turned slowly. " * 6) + "</p>"
    chapter = lambda n: f"""<?xml version="1.0" encoding="utf-8"?>
<html xmlns="http://www.w3.org/1999/xhtml"><head><title>Chapter {n}</title></head>
<body><h1>Chapter {n}</h1>{para * 12}</body></html>"""
    ncx = f"""<?xml version="1.0" encoding="utf-8"?>
<ncx xmlns="http://www.daisy.org/z3986/2005/ncx/" version="2005-1"><head/><docTitle><text>{title}</text></docTitle>
<navMap>
<navPoint id="n1" playOrder="1"><navLabel><text>Chapter 1</text></navLabel><content src="ch1.xhtml"/></navPoint>
<navPoint id="n2" playOrder="2"><navLabel><text>Chapter 2</text></navLabel><content src="ch2.xhtml"/></navPoint>
</navMap></ncx>"""
    with zipfile.ZipFile(path, "w") as z:
        z.writestr(zipfile.ZipInfo("mimetype"), "application/epub+zip")  # stored, first
        z.writestr("META-INF/container.xml", container, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/content.opf", opf, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/toc.ncx", ncx, zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/ch1.xhtml", chapter(1), zipfile.ZIP_DEFLATED)
        z.writestr("OEBPS/ch2.xhtml", chapter(2), zipfile.ZIP_DEFLATED)
        z.writestr(f"OEBPS/images/{cover_name}", cover_bytes, zipfile.ZIP_STORED)


def main():
    write_epub(os.path.join(HERE, "with_cover.epub"), "The Lighthouse Keeper", "Ada Marlow",
               encode(cover_image(600, 900, "THE LIGHTHOUSE", "Ada Marlow"), "JPEG"), "cover.jpg", "image/jpeg",
               epub3=False)
    write_epub(os.path.join(HERE, "with_cover_epub3.epub"), "Small Hours", "Ben Okafor",
               encode(cover_image(300, 450, "SMALL HOURS", "Ben Okafor"), "PNG"), "front.png", "image/png",
               epub3=True)

    land = Image.new("L", (800, 500), 255)
    d = ImageDraw.Draw(land)
    for x in range(800):
        d.line([(x, 300), (x, 500)], fill=int(40 + 180 * x / 800))
    d.polygon([(0, 300), (250, 120), (480, 300)], fill=70)
    d.polygon([(300, 300), (560, 80), (800, 300)], fill=30)
    d.text((400, 440), "LANDSCAPE", fill=255, font=font(48), anchor="mm")
    land.save(os.path.join(HERE, "sleep_landscape.jpg"), "JPEG", quality=85, progressive=False)

    port = Image.new("L", (300, 500), 250)
    d = ImageDraw.Draw(port)
    for i in range(12):
        d.ellipse([150 - 12 * i, 250 - 12 * i, 150 + 12 * i, 250 + 12 * i], outline=20 * (i % 2) * 6)
    d.text((150, 460), "PORTRAIT", fill=0, font=font(32), anchor="mm")
    port.save(os.path.join(HERE, "sleep_portrait.png"), "PNG")


if __name__ == "__main__":
    main()
