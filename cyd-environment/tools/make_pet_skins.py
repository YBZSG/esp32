"""Build the optional TF-card animations and their matching browser portraits."""
import struct
from pathlib import Path

from PIL import Image, ImageDraw

from generate_miku_assets import CELL_W, CELL_H, GROUPS, OUT_H, OUT_W, encode, fit_sprite

ASSETS = Path(__file__).resolve().parents[1] / "assets"
WEB = ASSETS.parent / "web"


def animal(species, action, frame):
    image = Image.new("RGBA", (40, 44))
    d = ImageDraw.Draw(image)
    ink = "#302d38"
    white = "#fff1ce"
    wave = action == "wave" and frame % 4 < 2
    blink = (action == "idle" and frame == 5) or action == "failed"
    lean = (-1 if action == "look_a" else 1) if action.startswith("look") else 0
    if species == "fox":
        fur = "#e88142"
        d.polygon([(20,32),(29,29),(37,21),(38,32),(34,38),(23,40)], fill=fur, outline=ink)
        d.polygon([(33,26),(37,22),(37,30),(35,32),(34,30),(32,31)], fill=white)
        d.ellipse((8,23,28,41), fill=fur, outline=ink)
        d.ellipse((12,27,24,39), fill=white)
        d.polygon([(4,17),(3,2),(13,8),(20,7),(29,2),(29,17),(31,21),(27,25),(16,30),(7,25),(2,21)], fill=fur, outline=ink)
        d.polygon([(5,5),(10,9),(6,11)], fill="#774050")
        d.polygon([(27,5),(23,9),(27,11)], fill="#774050")
        d.polygon([(4,19),(12,21),(16,24),(21,21),(29,19),(26,24),(16,28),(7,24)], fill=white)
        d.rectangle((15+lean,22,17+lean,23), fill=ink)
        d.rectangle((9,39,14,41), fill=ink)
        d.rectangle((22,39,26,41), fill=ink)
        d.line([(9,28),(15,31),(24,28)], fill="#177e78", width=3)
        d.polygon([(19,30),(23,29),(24,35),(21,34)], fill="#177e78")
        eyes = [(10,17),(22,17)]
    elif species == "rabbit":
        fur = "#d9d5e1"
        d.ellipse((7,24,31,41), fill=fur, outline=ink)
        d.ellipse((28,28,35,35), fill="#f6f3e9", outline=ink)
        d.polygon([(8,19),(5,5),(7,1),(10,2),(15,16)], fill=fur, outline=ink)
        d.polygon([(18,16),(21,2),(25,1),(26,4),(23,18)], fill=fur, outline=ink)
        d.line((8,5,11,14), fill="#a79cae", width=2)
        d.line((23,5,21,13), fill="#a79cae", width=2)
        d.ellipse((4,13,29,32), fill=fur, outline=ink)
        d.ellipse((12,22,22,29), fill="#f6f3e9")
        d.rectangle((16+lean,24,18+lean,25), fill="#83686a")
        d.ellipse((6,37,14,42), fill=fur, outline=ink)
        d.ellipse((21,37,29,42), fill=fur, outline=ink)
        d.line((12,31,25,37), fill="#967046", width=2)
        d.polygon([(23,32),(25,34),(28,34),(27,37),(27,39),(23,38),(21,39),(21,36),(19,34),(22,34)], fill="#e3b354", outline="#986c37")
        if wave: d.ellipse((2,27,8,32), fill=fur, outline=ink)
        eyes = [(11,22),(23,22)]
    else:
        fur = "#34475b"
        d.polygon([(8,23),(2,29 if wave else 35),(5,36),(12,32)], fill=fur, outline=ink)
        d.polygon([(27,23),(36,17 if wave else 34),(34,33),(27,32)], fill=fur, outline=ink)
        d.ellipse((7,6,32,41), fill=fur, outline=ink)
        d.polygon([(13,10),(16,4),(18,7),(21,2),(23,8)], fill=fur)
        d.ellipse((10,14,29,38), fill="#f4ecdf")
        d.ellipse((10,15,19,24), fill="#f4ecdf")
        d.ellipse((19,15,28,24), fill="#f4ecdf")
        d.polygon([(17+lean,22),(23+lean,22),(20+lean,25)], fill="#d08037")
        d.rectangle((10,38,17,41), fill="#e5a24a", outline=ink)
        d.rectangle((22,38,29,41), fill="#e5a24a", outline=ink)
        d.line([(8,26),(18,29),(30,26)], fill="#b45054", width=3)
        d.rectangle((23,28,26,34), fill="#bd565b")
        eyes = [(14,19),(24,19)]
    for x, y in eyes:
        x += lean
        d.rectangle((x-1,y,x+1,y if blink else y+2), fill=ink)
    if species == "fox" and wave:
        d.ellipse((3,25,8,30), fill=fur, outline=ink)
    if action in ("working", "review"):
        d.rectangle((7,28,14,36), fill="#6ea88c", outline=ink)
        d.line((9,30,12,30), fill=white)
    bob = 1 if frame % 4 in (1,2) else 0
    canvas = Image.new("RGBA", (OUT_W, OUT_H))
    canvas.alpha_composite(image.resize((100,110), Image.Resampling.NEAREST), (6,10-bob*2))
    return canvas


def write_animation(name, frames):
    payloads = [bytes(encode(frame)) for frame in frames]
    offset = 10 + 6 * len(frames)
    index = bytearray()
    for payload in payloads:
        assert 0 < len(payload) <= 23000
        index.extend(struct.pack("<IH", offset, len(payload)))
        offset += len(payload)
    (ASSETS / (name + ".rle")).write_bytes(
        struct.pack("<4sHHH", b"MKU1", OUT_W, OUT_H, len(frames)) + index + b"".join(payloads))


def main():
    atlas = Image.open(ASSETS / "miku-spritesheet.webp").convert("RGBA")
    portraits = Image.new("RGBA", (OUT_W * 4, OUT_H))
    original = fit_sprite(atlas.crop((0,0,CELL_W,CELL_H)))
    portraits.paste(original, (0,0))
    for slot, name in enumerate(("rabbit", "fox", "penguin"), 1):
        frames = []
        for action, _, count in GROUPS:
            for i in range(count):
                frames.append(animal(name, action, i))
        write_animation(name, frames)
        portraits.paste(frames[0], (slot*OUT_W,0))
    WEB.mkdir(exist_ok=True)
    portraits.save(WEB / "pets.png", optimize=True)


if __name__ == "__main__":
    main()
