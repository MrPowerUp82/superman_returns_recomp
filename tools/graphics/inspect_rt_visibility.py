"""Render the native R32_FLOAT visibility readback, not a game screenshot.

White = visible light, black = occluded, magenta = invalid receiver.
Usage: python tools/graphics/inspect_rt_visibility.py capture.raw
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("source", type=Path)
    parser.add_argument("--output", type=Path)
    args = parser.parse_args()
    data = args.source.read_bytes()
    if len(data) < 16:
        raise ValueError("Incomplete readback header")
    width, height, fmt, pitch = struct.unpack_from("<4I", data)
    if fmt != 41 or not (0 < width <= 16384 and 0 < height <= 16384):
        raise ValueError("Expected a bounded R32_FLOAT (DXGI format 41) readback")
    if pitch < width * 4 or 16 + (height - 1) * pitch + width * 4 > len(data):
        raise ValueError("Incomplete or invalid row footprint")
    values = np.ndarray((height, width), dtype="<f4", buffer=data, offset=16, strides=(pitch, 4))
    valid = np.isfinite(values) & (values >= 0) & (values <= 1)
    image = np.full((height, width, 3), (200, 30, 150), dtype=np.uint8)
    gray = np.rint(values[valid] * 255).astype(np.uint8)
    image[valid] = gray[:, None]
    output = args.output or args.source.with_suffix(".png")
    Image.fromarray(image).save(output)
    print(json.dumps({"width": width, "height": height, "valid": int(valid.sum()),
        "occluded": int((valid & (values < 1)).sum()),
        "mean_visibility": float(values[valid].mean()) if valid.any() else None,
        "output": str(output.resolve()),
        "meaning": "Diagnostic ray visibility only; not composed game lighting"}, indent=2))


if __name__ == "__main__":
    main()
