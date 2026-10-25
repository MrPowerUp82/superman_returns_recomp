"""Inspect opt-in GPU motion/coverage/color readbacks; no DLSS readiness claim.

Usage: python tools/graphics/inspect_temporal_motion.py <capture-directory>
Velocity PNG: red/right, green/down, blue/left or up; 16 pixels/frame = full scale.
Unverified pixels are magenta. Color captures are unmodified for UNORM formats.
"""
import argparse
import json
import struct
from pathlib import Path

import numpy as np
from PIL import Image


def read_raw(path):
    data = path.read_bytes()
    if len(data) < 16:
        raise ValueError(f"{path}: incomplete header")
    w, h, fmt, pitch = struct.unpack_from('<4I', data)
    formats = {62: ('u1', 1), 34: ('<f2', 2), 28: ('u1', 4),
               87: ('u1', 4), 10: ('<f2', 4)}
    if fmt not in formats or not (0 < w <= 16384 and 0 < h <= 16384):
        raise ValueError(f"{path}: unsupported format or dimensions")
    dtype, channels = formats[fmt]
    size = np.dtype(dtype).itemsize
    row = w * channels * size
    if pitch < row or 16 + (h - 1) * pitch + row > len(data):
        raise ValueError(f"{path}: incomplete row footprint")
    return fmt, np.ndarray((h, w, channels), dtype=dtype, buffer=data, offset=16,
                          strides=(pitch, channels * size, size))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    report = {'meaning': 'Partial static-world motion experiment; objects/deformation unverified', 'captures': []}
    for path in sorted(args.directory.glob('*motion_velocity_*.raw')):
        fmt, velocity = read_raw(path)
        _, coverage = read_raw(path.with_name(path.name.replace('motion_velocity_', 'motion_validity_')))
        if fmt != 34 or coverage.shape[:2] != velocity.shape[:2]:
            raise ValueError('Mismatched motion and validity resources')
        velocity = velocity.astype(np.float32)
        valid = (coverage[..., 0] != 0) & np.isfinite(velocity).all(axis=2)
        magnitude = np.linalg.norm(velocity, axis=2)
        picture = np.full((*valid.shape, 3), (200, 30, 150), dtype=np.uint8)
        rgb = np.stack((np.maximum(velocity[..., 0], 0), np.maximum(velocity[..., 1], 0),
                        np.maximum(-velocity[..., 0], -velocity[..., 1]).clip(0)), axis=2)
        picture[valid] = (rgb[valid].clip(0, 16) * (255 / 16)).astype(np.uint8)
        Image.fromarray(picture).save(path.with_suffix('.png'))
        Image.fromarray((valid * 255).astype(np.uint8)).save(path.with_name(path.stem.replace('velocity', 'validity') + '.png'))
        report['captures'].append({'file': path.name, 'width': valid.shape[1], 'height': valid.shape[0],
            'diagnostic_upper_bound_only': path.name.startswith('candidate_static_only_'),
            'unknown_geometry_omitted': path.name.startswith('candidate_static_only_'),
            'valid_pixels': int(valid.sum()), 'coverage_fraction': float(valid.mean()),
            'moving_pixels_above_0_01': int((valid & (magnitude > .01)).sum()),
            'magnitude_percentiles': np.percentile(magnitude[valid], [0, 50, 95, 100]).tolist() if valid.any() else [],
            'mean_xy': velocity[valid].mean(axis=0).tolist() if valid.any() else []})
    for path in sorted(args.directory.glob('candidate*color*.raw')):
        fmt, values = read_raw(path)
        if fmt == 87:
            rgb = values[..., [2, 1, 0]]
        elif fmt == 28:
            rgb = values[..., :3]
        elif fmt == 10:
            # Explicit inspection mapping for HDR only; not a game presentation.
            v = np.nan_to_num(values[..., :3].astype(np.float32)).clip(0)
            rgb = np.rint((v / (1 + v)) ** (1 / 2.2) * 255).clip(0, 255).astype(np.uint8)
        else:
            raise ValueError('Unexpected candidate color format')
        Image.fromarray(rgb).save(path.with_suffix('.png'))
        report['captures'].append({'file': path.name, 'format': fmt, 'boundary_verified': False})
    destination = args.directory / 'inspection.json'
    destination.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
