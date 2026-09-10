"""Convert the BedrockMap SVG into a multi-size Windows ICO with alpha preserved.

Usage:
    python scripts/svg_to_ico.py [source.svg] [output.ico]

Small sizes are written as classic BMP entries, large ones as embedded PNG
(Vista+). Both carry a real alpha channel plus a matching 1-bpp AND mask, which
is what stops rounded corners from gaining a white fringe.
"""

from __future__ import annotations

import struct
import sys
from io import BytesIO
from pathlib import Path

import resvg_py
from PIL import Image

REPO_ROOT = Path(__file__).resolve().parents[1]
SOURCE = REPO_ROOT / (sys.argv[1] if len(sys.argv) > 1 else "BedrockMap.svg")
DESTINATION = REPO_ROOT / (sys.argv[2] if len(sys.argv) > 2 else "BedrockMap.ico")

BMP_SIZES = (16, 24, 32, 48, 64)
PNG_SIZES = (128, 256)


def render(size: int) -> Image.Image:
    data = resvg_py.svg_to_bytes(
        svg_string=SOURCE.read_text(encoding="utf-8"), width=size, height=size
    )
    return Image.open(BytesIO(data)).convert("RGBA")


def bmp_entry(image: Image.Image) -> bytes:
    """BITMAPINFOHEADER + bottom-up BGRA rows + the AND mask."""
    width, height = image.size
    pixels = image.load()

    xor = bytearray()
    for y in range(height - 1, -1, -1):
        for x in range(width):
            r, g, b, a = pixels[x, y]
            xor += bytes((b, g, r, a))

    row_bytes = ((width + 31) // 32) * 4
    mask = bytearray()
    for y in range(height - 1, -1, -1):
        row = bytearray(row_bytes)
        for x in range(width):
            if pixels[x, y][3] == 0:
                row[x // 8] |= 0x80 >> (x % 8)
        mask += row

    header = struct.pack(
        "<IiiHHIIiiII", 40, width, height * 2, 1, 32, 0, len(xor) + len(mask), 0, 0, 0, 0
    )
    return header + bytes(xor) + bytes(mask)


def png_entry(image: Image.Image) -> bytes:
    buffer = BytesIO()
    image.save(buffer, format="PNG", optimize=True)
    return buffer.getvalue()


def main() -> None:
    entries: list[tuple[int, bytes]] = []
    for size in BMP_SIZES:
        entries.append((size, bmp_entry(render(size))))
    for size in PNG_SIZES:
        entries.append((size, png_entry(render(size))))

    offset = 6 + 16 * len(entries)
    directory = b""
    payload = b""
    for size, blob in entries:
        dimension = 0 if size == 256 else size  # 0 encodes 256 in the directory
        directory += struct.pack(
            "<BBBBHHII", dimension, dimension, 0, 0, 1, 32, len(blob), offset
        )
        payload += blob
        offset += len(blob)

    DESTINATION.write_bytes(
        struct.pack("<HHH", 0, 1, len(entries)) + directory + payload
    )
    print(f"Wrote {DESTINATION.name}: {len(entries)} sizes, {DESTINATION.stat().st_size} bytes")


if __name__ == "__main__":
    main()
