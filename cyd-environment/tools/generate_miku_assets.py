from pathlib import Path
from PIL import Image

PROJECT = Path(__file__).resolve().parents[1]
SOURCE = PROJECT / "assets" / "miku-spritesheet.webp"
OUTPUT = PROJECT / "include" / "miku_assets.h"
CELL_W, CELL_H = 192, 208
OUT_W, OUT_H = 112, 122

# Keep all interactive/telemetry states, but omit locomotion rows that are not
# used on the stationary CYD screen.
GROUPS = [
    ("idle", 0, 6),
    ("wave", 3, 4),
    ("jump", 4, 5),
    ("failed", 5, 8),
    ("waiting", 6, 6),
    ("working", 7, 6),
    ("review", 8, 6),
    ("look_a", 9, 8),
    ("look_b", 10, 8),
]


def rgb565(r, g, b):
    return ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)


def encode(frame):
    # Transparent pixels use the TFT transparent-key colour. Runs are decoded
    # into one small RAM buffer before a single DMA-friendly pushImage call.
    values = []
    for r, g, b, a in frame.getdata():
        values.append(0xF81F if a < 96 else rgb565(r, g, b))
    out = []
    i = 0
    while i < len(values):
        color = values[i]
        run = 1
        while i + run < len(values) and values[i + run] == color and run < 255:
            run += 1
        out.extend((run, color & 255, color >> 8))
        i += run
    return out


def fit_sprite(source):
    bbox = source.getchannel("A").getbbox()
    sprite = source.crop(bbox) if bbox else source
    sprite.thumbnail((OUT_W - 2, OUT_H - 2), Image.Resampling.NEAREST)
    frame = Image.new("RGBA", (OUT_W, OUT_H))
    frame.alpha_composite(sprite, ((OUT_W - sprite.width) // 2, OUT_H - sprite.height - 1))
    return frame


def main():
    arrays = []
    starts = {}
    index = 0
    atlas = Image.open(SOURCE).convert("RGBA")
    for name, row, count in GROUPS:
        starts[name] = (index, count)
        for col in range(count):
            box = (col * CELL_W, row * CELL_H, (col + 1) * CELL_W, (row + 1) * CELL_H)
            source_frame = atlas.crop(box)
            frame = fit_sprite(source_frame)
            arrays.append((f"miku_frame_{index}", encode(frame)))
            index += 1
    lines = [
        "#pragma once", "#include <Arduino.h>", "",
        f"constexpr uint16_t MIKU_W = {OUT_W};",
        f"constexpr uint16_t MIKU_H = {OUT_H};",
        "constexpr uint16_t MIKU_TRANSPARENT = 0xF81F;", "",
    ]
    for name, data in arrays:
        body = ",".join(str(v) for v in data)
        lines.append(f"const uint8_t {name}[] PROGMEM = {{{body}}};")
    lines.extend(["", "struct MikuFrameRef { const uint8_t* data; uint16_t size; };", "const MikuFrameRef MIKU_FRAMES[] PROGMEM = {"])
    for name, data in arrays:
        lines.append(f"  {{{name}, {len(data)}}},")
    lines.append("};")
    for name, (start, count) in starts.items():
        key = name.upper()
        lines.append(f"constexpr uint8_t MIKU_{key}_START = {start};")
        lines.append(f"constexpr uint8_t MIKU_{key}_COUNT = {count};")
    lines.append("")
    OUTPUT.write_text("\n".join(lines), encoding="utf-8")
    print(f"wrote {OUTPUT} with {index} frames ({OUTPUT.stat().st_size} bytes source)")


if __name__ == "__main__":
    main()
