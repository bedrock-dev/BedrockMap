#!/usr/bin/env python3
"""Generate a biome color reference chart from biome_color.json."""

import json
import sys
from pathlib import Path

try:
    from PIL import Image, ImageDraw, ImageFont
except ImportError:
    print("Error: Pillow is required. Install with: pip install Pillow")
    sys.exit(1)

SCRIPT_DIR = Path(__file__).resolve().parent
PROJECT_ROOT = SCRIPT_DIR.parent
JSON_PATH = PROJECT_ROOT / "bedrock-level" / "data" / "colors" / "biome_color.json"
OUT_PATH = SCRIPT_DIR / "biome_color_chart.png"

# Layout — two-column table:  #id | name | [rgb] [grass] [leaves] [water]
ROW_HEIGHT = 48           # pixels per row
HEADER_HEIGHT = 52        # header row height
SWATCH_SIZE = 36          # each color swatch
SWATCH_GAP = 6            # gap between swatches
ID_WIDTH = 80             # "#id" column
NAME_WIDTH = 320          # name column
COL_GAP = 48              # gap between left and right columns
PADDING = 24              # padding around the whole image

# BG color for missing fields
MISSING_COLOR = (60, 60, 60)


def parse_color(data: dict, key: str):
    """Return (R, G, B) tuple or MISSING_COLOR if key absent."""
    arr = data.get(key)
    if arr and len(arr) == 3:
        return tuple(arr)
    return MISSING_COLOR


def main():
    with open(JSON_PATH, encoding="utf-8") as f:
        biomes = json.load(f)

    # Sort by id
    items = sorted(biomes.items(), key=lambda kv: kv[1].get("id", 9999))
    n = len(items)

    # Split into two columns
    mid = (n + 1) // 2
    left_items = items[:mid]
    right_items = items[mid:]
    rows = max(len(left_items), len(right_items))

    swatches_w = 4 * SWATCH_SIZE + 3 * SWATCH_GAP
    one_col_w = ID_WIDTH + NAME_WIDTH + swatches_w
    img_w = 2 * one_col_w + COL_GAP + 2 * PADDING
    img_h = HEADER_HEIGHT + rows * ROW_HEIGHT + 2 * PADDING

    img = Image.new("RGB", (img_w, img_h), (30, 30, 30))
    draw = ImageDraw.Draw(img)

    try:
        font = ImageFont.truetype("consola.ttf", 18)
        font_hdr = ImageFont.truetype("consolab.ttf", 14)
    except OSError:
        try:
            font = ImageFont.truetype("cour.ttf", 18)
            font_hdr = font
        except OSError:
            font = ImageFont.load_default()
            font_hdr = font

    swatch_y_off = (ROW_HEIGHT - SWATCH_SIZE) // 2

    def draw_header(col_offset_x):
        hdr_labels = ["#ID", "Name"]
        swatch_lbls = ["DF", "GR", "LV", "WT"]  # Default, Grass, Leaves, Water
        x0 = PADDING + col_offset_x
        hy = PADDING

        draw.rectangle([x0, hy, x0 + one_col_w - 1, hy + HEADER_HEIGHT - 1], fill=(50, 50, 50))

        # #ID
        draw.text((x0 + 4, hy + 16), hdr_labels[0], fill=(255, 200, 60), font=font_hdr)
        # Name
        draw.text((x0 + ID_WIDTH + 4, hy + 16), hdr_labels[1], fill=(255, 200, 60), font=font_hdr)
        # Swatch labels on same baseline as ID/Name
        sx = x0 + ID_WIDTH + NAME_WIDTH
        for j, lbl in enumerate(swatch_lbls):
            sw_x = sx + j * (SWATCH_SIZE + SWATCH_GAP)
            bbox = draw.textbbox((0, 0), lbl, font=font_hdr)
            tw = bbox[2] - bbox[0]
            draw.text((sw_x + (SWATCH_SIZE - tw) // 2, hy + 16), lbl, fill=(255, 200, 60), font=font_hdr)

    draw_header(0)
    draw_header(one_col_w + COL_GAP)

    def draw_row(items, col_offset_x):
        for i, (name, data) in enumerate(items):
            biome_id = data.get("id", "?")
            rgb = parse_color(data, "rgb")
            grass = parse_color(data, "grass")
            leaves = parse_color(data, "leaves")
            water = parse_color(data, "water")

            y0 = PADDING + HEADER_HEIGHT + i * ROW_HEIGHT
            x0 = PADDING + col_offset_x

            # Alternating row bg
            if i % 2 == 0:
                draw.rectangle([x0, y0, x0 + one_col_w - 1, y0 + ROW_HEIGHT - 1], fill=(38, 38, 38))

            # ID
            id_text = f"#{biome_id}"
            draw.text((x0 + 4, y0 + 10), id_text, fill=(180, 180, 180), font=font)

            # Name
            name_display = name
            bbox = draw.textbbox((0, 0), name_display, font=font)
            while bbox[2] - bbox[0] > NAME_WIDTH - 12 and len(name_display) > 1:
                name_display = name_display[:-1]
                bbox = draw.textbbox((0, 0), name_display + "…", font=font)
            if name_display != name:
                name_display += "…"
            draw.text((x0 + ID_WIDTH + 4, y0 + 10), name_display, fill=(220, 220, 220), font=font)

            # Swatches
            sx = x0 + ID_WIDTH + NAME_WIDTH
            sy = y0 + swatch_y_off
            colors = [rgb, grass, leaves, water]
            for j, c in enumerate(colors):
                ox = sx + j * (SWATCH_SIZE + SWATCH_GAP)
                draw.rectangle([ox, sy, ox + SWATCH_SIZE - 1, sy + SWATCH_SIZE - 1], fill=c)

    draw_row(left_items, 0)
    draw_row(right_items, one_col_w + COL_GAP)

    img.save(OUT_PATH)
    print(f"Saved: {OUT_PATH}")
    print(f"Biomes: {n}  |  Rows: {rows}  |  Layout: 2-col table")


if __name__ == "__main__":
    main()