#!/usr/bin/env python3
"""
SSD1677 LUT decoder / pretty printer

Takes a 112-byte LUT in SSD1677 format:
- 5 x 10 bytes  : VS blocks (LUT0..LUT4)
- 10 x 5 bytes  : TP/RP blocks (groups 0..9)
- 5 bytes       : frame-rate / timing
- 5 bytes       : voltage settings (VGH, VSH1, VSH2, VSL, VCOM)
- 2 bytes       : reserved

and prints a human-readable breakdown.

The VS bytes use this mapping (2 bits each):
  00 -> VSS
  01 -> VSH1
  10 -> VSL
  11 -> VSH2
"""

from typing import List

# --------------------------------------------------------------------
# CONFIG: paste your LUT here (from the C array)
# --------------------------------------------------------------------

# disable formatting for this section
# fmt: off
LUT_4G: List[int] = [
    # VS L0 (white) - 10 bytes
    0x80, 0x48, 0x4A, 0x22, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    # VS L1 (light grey) - 10 bytes
    0x0A, 0x48, 0x68, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    # VS L2 (dark grey) - 10 bytes
    0x88, 0x48, 0x60, 0x08, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    # VS L3 (black) - 10 bytes
    0xA8, 0x48, 0x45, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,
    # VS L4 (VCOM) - 10 bytes
    0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00,

    # Groups 0..9: TP[nA], TP[nB], TP[nC], TP[nD], RP[n]
    0x07, 0x1E, 0x1C, 0x02, 0x00,  # group 0
    0x05, 0x01, 0x05, 0x01, 0x02,  # group 1
    0x08, 0x01, 0x01, 0x04, 0x04,  # group 2
    0x00, 0x02, 0x01, 0x02, 0x02,  # group 3
    0x00, 0x00, 0x00, 0x00, 0x00,  # group 4
    0x00, 0x00, 0x00, 0x00, 0x00,  # group 5
    0x00, 0x00, 0x00, 0x00, 0x00,  # group 6
    0x00, 0x00, 0x00, 0x00, 0x00,  # group 7
    0x00, 0x00, 0x00, 0x00, 0x00,  # group 8
    0x00, 0x00, 0x00, 0x00, 0x01,  # group 9

    # Frame rate bytes (5)
    0x22, 0x22, 0x22, 0x22, 0x22,

    # Voltages: VGH, VSH1, VSH2, VSL, VCOM
    0x17, 0x41, 0xA8, 0x32, 0x30,

    # Reserved
    0x00, 0x00,
]
# fmt: on

# --------------------------------------------------------------------
# Decoder helpers
# --------------------------------------------------------------------

VS_MAP = {
    0b00: "VSS",
    0b01: "VSH1",
    0b10: "VSL",
    0b11: "VSH2",
}

LUT_LEVEL_NAMES = {
    0: "L0 (white)",
    1: "L1 (light gray)",
    2: "L2 (dark gray)",
    3: "L3 (black)",
    4: "L4 (VCOM)",
}


def decode_vs_byte(byte: int) -> List[str]:
    """
    Decode one VS byte into 4 voltage labels (2 bits each, MSB first).
    """
    phases = []
    for shift in (6, 4, 2, 0):
        code = (byte >> shift) & 0b11
        phases.append(VS_MAP.get(code, f"?{code:02b}"))
    return phases


def print_vs_blocks(lut: List[int]) -> None:
    """
    Print VS (voltage selection) blocks: 5 levels x 10 bytes.
    """
    print("=== VS BLOCKS (voltage selection per LUT level) ===")
    print("2-bit code: 00=VSS, 01=VSH1, 10=VSL, 11=VSH2\n")

    # 5 levels (L0..L4), 10 bytes each
    for level in range(5):
        start = level * 10
        end = start + 10
        level_bytes = lut[start:end]
        level_name = LUT_LEVEL_NAMES.get(level, f"L{level}")

        print(f"[{level_name}] bytes {start}..{end-1}")
        for i, b in enumerate(level_bytes):
            idx = start + i
            phases = decode_vs_byte(b)
            print(
                f"  byte[{idx:3d}] = 0x{b:02X} -> "
                f"A:{phases[0]:>4}  B:{phases[1]:>4}  C:{phases[2]:>4}  D:{phases[3]:>4}"
            )
        print()


def print_tp_rp_blocks(lut: List[int]) -> None:
    """
    Print TP/RP groups: 10 groups x 5 bytes.
    Each group: TP[nA], TP[nB], TP[nC], TP[nD], RP[n]
    Note: RP[n] is 'repeat_count - 1' (0 -> repeat once, 1 -> repeat twice, etc).
    """
    print("=== TP / RP BLOCKS (timing per group) ===\n")

    base = 5 * 10  # skip VS (50 bytes)
    for group in range(10):
        off = base + group * 5
        tp_a, tp_b, tp_c, tp_d, rp = lut[off : off + 5]
        repeat_count = rp + 1  # SSD1677 convention

        print(f"Group {group}: bytes {off}..{off+4}")
        print(
            f"  TP[A]={tp_a:3d} frames, "
            f"TP[B]={tp_b:3d} frames, "
            f"TP[C]={tp_c:3d} frames, "
            f"TP[D]={tp_d:3d} frames"
        )
        print(f"  RP[{group}] = 0x{rp:02X} -> repeats {repeat_count} time(s)")
        if tp_a == tp_b == tp_c == tp_d == 0:
            print("  (all TP=0 -> this group is effectively disabled)")
        print()


def print_frame_rate_and_voltages(lut: List[int]) -> None:
    """
    Print frame rate bytes and voltage register bytes.
    """
    vs_bytes = 5 * 10
    tp_rp_bytes = 10 * 5

    frame_base = vs_bytes + tp_rp_bytes  # index 100
    volt_base = frame_base + 5  # index 105
    reserved_base = volt_base + 5  # index 110

    frame_bytes = lut[frame_base : frame_base + 5]
    volt_bytes = lut[volt_base : volt_base + 5]
    reserved_bytes = lut[reserved_base : reserved_base + 2]

    print("=== FRAME RATE BYTES ===")
    for i, b in enumerate(frame_bytes):
        print(f"  frame[{frame_base + i}] = 0x{b:02X}")
    print("  (exact meaning depends on datasheet timing tables)\n")

    print("=== VOLTAGE REGISTER BYTES ===")
    labels = ["VGH", "VSH1", "VSH2", "VSL", "VCOM"]
    for i, (name, b) in enumerate(zip(labels, volt_bytes)):
        print(f"  {name:4s} (byte {volt_base + i}) = 0x{b:02X}")
    print("  (use datasheet tables to convert these to actual volts)\n")

    print("=== RESERVED BYTES ===")
    for i, b in enumerate(reserved_bytes):
        print(f"  reserved[{reserved_base + i}] = 0x{b:02X}")
    print()


def decode_lut(lut: List[int]) -> None:
    """
    High-level: decode and pretty-print everything.
    """
    if len(lut) != 112:
        raise ValueError(f"Expected 112 bytes, got {len(lut)}")

    print("===================================================")
    print("SSD1677 LUT DECODER")
    print("===================================================\n")

    print_vs_blocks(lut)
    print_tp_rp_blocks(lut)
    print_frame_rate_and_voltages(lut)


if __name__ == "__main__":
    decode_lut(LUT_4G)
