"""Pack the RLE frames from the original header into a TF-card asset.

The firmware format is intentionally simple: MKU1, width, height, frame count,
then (absolute offset, byte length) for every frame, followed by the original
three-byte RLE streams.
"""
from pathlib import Path
import re
import struct

root = Path(__file__).resolve().parents[1]
source = (root / "include" / "miku_assets.h").read_text(encoding="utf-8")
frames = []
for index, body in sorted(
    ((int(n), body) for n, body in re.findall(
        r"miku_frame_(\d+)\[\].*?=\s*\{(.*?)\};", source, re.S
    )),
    key=lambda item: item[0],
):
    values = [int(value) for value in re.findall(r"\d+", body)]
    if len(values) > 0xFFFF:
        raise ValueError(f"frame {index} is too large")
    frames.append(bytes(values))

if len(frames) != 57:
    raise ValueError(f"expected 57 frames, found {len(frames)}")

header_size = 10 + len(frames) * 6
offset = header_size
index = bytearray()
payload = bytearray()
for frame in frames:
    index += struct.pack("<IH", offset, len(frame))
    payload += frame
    offset += len(frame)

asset = struct.pack("<4sHHH", b"MKU1", 112, 122, len(frames)) + index + payload
out = root / "assets" / "miku.rle"
out.write_bytes(asset)
print(f"wrote {out} ({len(asset)} bytes, {len(frames)} frames)")
