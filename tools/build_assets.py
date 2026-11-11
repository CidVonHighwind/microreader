#!/usr/bin/env python3
"""Build assets.bin — a manifest blob of binary resources appended to firmware.bin.

The blob is appended *after* the IDF image (i.e. outside any segment that gets
memory-mapped at boot).  The firmware reads its contents at runtime via
esp_partition_read / esp_partition_mmap from the running app partition.

Format
------
    [magic "ASTS":4][version:4=1][count:4][total_size:4]
    [count × { name[32], offset:4, length:4, crc32:4 }]   # 44 bytes / entry
    [data1][data2]...                                      # each 4-byte aligned

All offsets are relative to the start of the manifest (the "ASTS" magic).
"""

from __future__ import annotations

import os
import struct
import sys
import zlib

# (manifest_name, source_path_relative_to_project_dir)
DEFAULT_ASSETS = [
    ("bookerly.bin", "resources/fonts/bookerly.bin"),
    ("alegreya.bin", "resources/fonts/alegreya.bin"),
    ("sleep_0.mgr", "resources/sleep/sleep_0.mgr"),
    ("sleep_1.mgr", "resources/sleep/sleep_1.mgr"),
    ("sleep_2.mgr", "resources/sleep/sleep_2.mgr"),
]

# The X3 build embeds 792x528 versions of the sleep images
# (made by tools/make_x3_sleep_images.py) and its reader fonts READY TO USE:
# uncompressed font bundles the firmware reads straight from flash. The X3's
# own partition table usually has only a 1 MB "spiffs" partition — too small
# for the 2.4-2.9 MB bundles the X4 unpacks there — so the X3 never unpacks
# them. To keep the firmware small, the bundles leave out the anti-aliasing
# (grayscale) planes, which the X3 doesn't use (see strip_gray_planes).
X3_ASSETS = [
    ("bookerly.fnt", "resources/fonts/bookerly.mfb", "strip_gray"),
    ("alegreya.fnt", "resources/fonts/alegreya.mfb", "strip_gray"),
    ("sleep_0.mgr", "resources/sleep/x3/sleep_0.mgr"),
    ("sleep_1.mgr", "resources/sleep/x3/sleep_1.mgr"),
    ("sleep_2.mgr", "resources/sleep/x3/sleep_2.mgr"),
]


def strip_gray_planes(bundle: bytes) -> bytes:
    """Remove the grayscale (anti-aliasing) planes from an FNTS v2 bundle.

    Each inner MBF4 font stores its two grayscale planes at the very end
    (gray_lsb_offset .. gray_msb_offset .. end); everything the black & white
    renderer needs comes before them. Truncate each font there and clear the
    two offsets so the firmware treats the font as having no grayscale.
    """
    if bundle[:4] != b"FNTS" or bundle[5] != 2:
        raise SystemExit("strip_gray_planes: not an FNTS v2 bundle")
    num = bundle[4]
    sizes = [struct.unpack_from("<I", bundle, 40 + 4 * i)[0] for i in range(num)]
    off = 40 + 4 * num
    fonts = []
    for size in sizes:
        font = bytearray(bundle[off:off + size])
        off += size
        if font[:4] != b"MBF4":
            raise SystemExit("strip_gray_planes: inner font is not MBF4")
        (bitmaps, bold, italic, bold_italic, kerning, gray_lsb, gray_msb) = struct.unpack_from("<7I", font, 20)
        if gray_lsb:
            others = [x for x in (bitmaps, bold, italic, bold_italic, kerning) if x]
            if not (max(others) < gray_lsb < gray_msb <= len(font)):
                raise SystemExit("strip_gray_planes: unexpected font layout")
            font = font[:gray_lsb]
            struct.pack_into("<II", font, 40, 0, 0)  # gray_lsb_offset, gray_msb_offset
        fonts.append(bytes(font))
    out = bytearray(bundle[:40])
    for f in fonts:
        out += struct.pack("<I", len(f))
    for f in fonts:
        out += f
    return bytes(out)


TRANSFORMS = {"strip_gray": strip_gray_planes}

NAME_LEN = 32
ENTRY_SIZE = NAME_LEN + 12  # name + offset + length + crc32 == 44
HEADER_FIXED = 16  # magic + version + count + total_size


def build(project_dir: str, out_path: str, assets=DEFAULT_ASSETS) -> int:
    files = []
    for name, rel, *transform in assets:
        path = os.path.join(project_dir, rel.replace("/", os.sep))
        with open(path, "rb") as f:
            data = f.read()
        if transform:
            data = TRANSFORMS[transform[0]](data)
        if len(name.encode("utf-8")) > NAME_LEN - 1:
            raise SystemExit(f"asset name too long: {name!r}")
        files.append((name, data))

    count = len(files)
    table_size = HEADER_FIXED + count * ENTRY_SIZE
    data_start = (table_size + 3) & ~3  # 4-byte align

    payload = bytearray()
    entries = []
    cursor = data_start
    for name, data in files:
        # align each data blob to 4 bytes
        cursor_aligned = (cursor + 3) & ~3
        if cursor_aligned != cursor:
            payload += b"\x00" * (cursor_aligned - cursor)
            cursor = cursor_aligned
        entries.append((name, cursor, len(data), zlib.crc32(data) & 0xFFFFFFFF))
        payload += data
        cursor += len(data)
    total = cursor

    out = bytearray()
    out += b"ASTS"
    out += struct.pack("<III", 1, count, total)
    for name, offset, length, crc in entries:
        nb = name.encode("utf-8")
        out += nb + b"\x00" * (NAME_LEN - len(nb))
        out += struct.pack("<III", offset, length, crc)
    # pad table out to data_start
    if len(out) < data_start:
        out += b"\x00" * (data_start - len(out))
    out += payload
    assert len(out) == total, f"size mismatch: built {len(out)} vs declared {total}"

    with open(out_path, "wb") as f:
        f.write(out)

    print(f"[assets] {out_path}: {count} entries, {total:,} bytes")
    for name, offset, length, crc in entries:
        print(f"[assets]   {name:<24s} off=0x{offset:08x}  len={length:>9,d}  crc=0x{crc:08x}")
    return total


if __name__ == "__main__":
    project_dir = sys.argv[1] if len(sys.argv) > 1 else "."
    out = sys.argv[2] if len(sys.argv) > 2 else "assets.bin"
    build(project_dir, out)
