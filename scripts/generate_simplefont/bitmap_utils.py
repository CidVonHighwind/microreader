"""Bitmap packing and C-format helpers for generate_simplefont."""

from typing import List


def bytes_per_row(width: int) -> int:
    return (width + 7) // 8


def gen_bitmap_bytes(width: int, height: int, fill: int) -> List[int]:
    bpr = bytes_per_row(width)
    total = bpr * height
    byte_value = 0xFF if fill else 0x00
    return [byte_value] * total


def format_c_byte_list(byte_list: List[int]) -> str:
    if not byte_list:
        return ""
    parts = [f"0x{b:02X}" for b in byte_list]
    per_line = 12
    lines = [
        "    " + ", ".join(parts[i : i + per_line])
        for i in range(0, len(parts), per_line)
    ]
    return ",\n".join(lines)


def format_c_code_list(code_list: List[int]) -> str:
    if not code_list:
        return ""
    parts = [f"0x{c:X}" for c in code_list]
    per_line = 12
    lines = [
        "    " + ", ".join(parts[i : i + per_line])
        for i in range(0, len(parts), per_line)
    ]
    return ",\n".join(lines)
