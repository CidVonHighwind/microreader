from dataclasses import dataclass
from typing import List


@dataclass
class Glyph:
    bitmapOffset: int
    width: int
    height: int
    xAdvance: int
    xOffset: int
    yOffset: int
    pixel_values: List[int]
