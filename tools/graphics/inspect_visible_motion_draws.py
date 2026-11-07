"""Map private depth-EQUAL triangle attribution tags to the same-frame draw CSV.

These tags do not execute the original PS: alpha holes, equal-depth overlaps,
non-depth-writing contributors and bias can make them an upper bound. They
are neither production motion validity nor stable cross-frame object identity.
Optional --point X Y selects coordinates in the raw render-resolution buffers.
"""
import argparse
import csv
import json
from pathlib import Path

import numpy as np
from PIL import Image

from inspect_temporal_motion import read_raw


def inspect(directory, path, point=None):
    suffix = path.stem.removeprefix('visible_motion_draw_ids_')
    fmt, encoded = read_raw(path)
    flag_fmt, flags = read_raw(directory / f'visible_motion_draw_validity_{suffix}.raw')
    if fmt != 34 or flag_fmt != 62 or encoded.shape[:2] != flags.shape[:2]:
        raise ValueError('Mismatched RG16F tag / R8_UINT flag footprints')
    encoded = encoded.astype(np.float32)
    if not np.isfinite(encoded).all() or np.any(encoded < 0) or np.any(encoded > 255) or np.any(encoded != np.rint(encoded)):
        raise ValueError('Draw tags must be two exact finite byte values')
    flag = flags[..., 0]
    if np.any((flag != 0) & (flag != 1)):
        raise ValueError('Unexpected diagnostic coverage flag')
    tags = encoded[..., 0].astype(np.uint32) | (encoded[..., 1].astype(np.uint32) << 8)
    if np.any((flag == 0) & (tags != 0)) or np.any((flag != 0) & (tags == 0)):
        raise ValueError('Diagnostic tags and coverage flag disagree')
    with (directory / f'object_motion_draws_{suffix}.csv').open(newline='', encoding='utf-8') as stream:
        rows = list(csv.DictReader(stream))
    if len(rows) > 4096 or any(int(row['draw']) != i for i, row in enumerate(rows)):
        raise ValueError('Draw manifest is not a bounded ordered same-frame replay')
    if tags.size and int(tags.max()) > len(rows):
        raise ValueError('Diagnostic tag has no corresponding captured draw')
    ids, counts = np.unique(tags[flag != 0], return_counts=True)
    visible = []
    for tag, count in zip(ids.tolist(), counts.tolist()):
        record = dict(rows[tag - 1])
        record.update(diagnostic_tag=tag, depth_matched_triangle_pixels=count)
        visible.append(record)
    visible.sort(key=lambda row: (-row['depth_matched_triangle_pixels'], row['diagnostic_tag']))
    # Integer hash gives a stable color per within-capture tag. Black is no tag.
    hashed = tags * np.uint32(0x9e3779b1)
    picture = np.stack(((hashed >> 16) & 255, (hashed >> 8) & 255, hashed & 255), axis=-1).astype(np.uint8)
    picture[flag == 0] = 0
    destination = directory / f'visible_motion_draw_ids_{suffix}.png'
    Image.fromarray(picture).save(destination)
    result = {
        'capture': suffix, 'width': int(tags.shape[1]), 'height': int(tags.shape[0]),
        'depth_matched_triangle_pixels': int(np.count_nonzero(flag)),
        'draws_with_visible_triangle_pixels': len(visible), 'draws': visible,
        'image': destination.name,
        'meaning': 'Private depth-EQUAL triangle replay only; original PS coverage, opacity, actor class and motion identity remain unverified',
    }
    if point is not None:
        x, y = point
        if not (0 <= x < tags.shape[1] and 0 <= y < tags.shape[0]):
            raise ValueError('--point is outside the raw render-resolution buffer')
        tag = int(tags[y, x])
        result['point'] = {'x': x, 'y': y, 'diagnostic_tag': tag,
                           'draw': dict(rows[tag - 1]) if tag else None}
    return result


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--point', type=int, nargs=2, metavar=('X', 'Y'))
    args = parser.parse_args()
    paths = sorted(args.directory.glob('visible_motion_draw_ids_s*.raw'))
    if not paths:
        parser.error('No private visible-draw attribution buffers found')
    report = {'captures': [inspect(args.directory, path, args.point) for path in paths]}
    (args.directory / 'visible_draw_inspection.json').write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
