"""Attribute opt-in gameplay motion pixels to original PS owner IDs.

Requires object_motion_draws_sN.csv, object_pixel_owners_sN.raw and the motion
validity/velocity readbacks from the same live capture. Counts visible pixels;
matching a draw alone is not reported as valid motion coverage.
"""
import argparse
import csv
import json
import struct
from pathlib import Path

import numpy as np
from inspect_temporal_motion import read_raw


def inspect(directory, manifest):
    suffix = manifest.stem.removeprefix('object_motion_draws_')
    owner_path = directory / f'object_pixel_owners_{suffix}.raw'
    data = owner_path.read_bytes()
    if len(data) < 16:
        raise ValueError('Incomplete pixel-owner header')
    w, h, fmt, pitch = struct.unpack_from('<4I', data)
    if fmt != 42 or not 0 < w <= 16384 or not 0 < h <= 16384 or pitch < w * 4 or len(data) != 16 + pitch * h:
        raise ValueError('Unsupported or incomplete R32_UINT pixel-owner readback')
    owners = np.ndarray((h, w), dtype='<u4', buffer=data, offset=16, strides=(pitch, 4))
    _, validity = read_raw(directory / f'object_motion_validity_{suffix}.raw')
    _, velocity = read_raw(directory / f'object_motion_velocity_{suffix}.raw')
    if validity.shape[:2] != owners.shape or velocity.shape != (h, w, 2):
        raise ValueError('Mismatched same-frame readback dimensions')
    ids = owners & 0x3fffffff
    mixed = (owners & 0x40000000) != 0
    valid = (validity[..., 0] != 0) & np.isfinite(velocity).all(axis=2)
    magnitude = np.linalg.norm(velocity.astype(np.float32), axis=2)
    families = {}
    seen = set()
    with manifest.open(newline='', encoding='utf-8') as stream:
        for row in csv.DictReader(stream):
            owner = int(row['owner'])
            if not owner:
                continue
            if owner in seen:
                raise ValueError('Duplicate original pixel-owner ID')
            seen.add(owner)
            region = ids == owner
            visible = int(region.sum())
            pixels = region & valid
            family = families.setdefault(row['vs'].upper().zfill(16), {
                'draws': 0, 'matched_draws': 0, 'visible_owner_pixels': 0,
                'mixed_alpha_pixels': 0, 'valid_motion_pixels': 0, 'moving_pixels_above_0_01': 0})
            family['draws'] += 1
            family['matched_draws'] += int(row['valid']) != 0
            family['visible_owner_pixels'] += visible
            family['mixed_alpha_pixels'] += int((region & mixed).sum())
            family['valid_motion_pixels'] += int(pixels.sum())
            family['moving_pixels_above_0_01'] += int((pixels & (magnitude > .01)).sum())
    return {'capture': suffix, 'width': w, 'height': h, 'families': families,
            'meaning': 'Visible original-PS ownership only; shader family does not establish vehicle class or absence of temporal ghosting'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    args = parser.parse_args()
    manifests = sorted(args.directory.glob('object_motion_draws_s*.csv'))
    if not manifests:
        parser.error('No same-frame motion draw manifests found')
    report = {'captures': [inspect(args.directory, path) for path in manifests]}
    target = args.directory / 'owner_inspection.json'
    target.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
