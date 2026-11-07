"""Compare rigid-only gameplay MVs with the exact frozen NGX camera formula.

This does not classify actors. Inspect the paired presented image and residual
map to associate visible vehicles; static rigid props can also be in this mask.
"""
import argparse
import csv
import json
import struct
from pathlib import Path
import numpy as np
from PIL import Image
from inspect_temporal_motion import read_raw


def camera_motion(depth, inverse, previous, jitter):
    height, width = depth.shape
    yy, xx = np.indices(depth.shape, dtype=np.float64)
    pixels = np.stack((xx + .5 - jitter[0], yy + .5 - jitter[1]), axis=-1)
    ndc = np.stack((pixels[..., 0] / width * 2 - 1,
                    1 - pixels[..., 1] / height * 2, depth, np.ones_like(depth)), axis=-1)
    world = ndc @ inverse.T
    valid = np.isfinite(world).all(axis=-1) & (np.abs(world[..., 3]) > 1e-6)
    homogeneous = np.ones_like(world)
    np.divide(world[..., :3], world[..., 3:4], out=homogeneous[..., :3], where=valid[..., None])
    clip = homogeneous @ previous.T
    valid &= np.isfinite(clip).all(axis=-1) & (clip[..., 3] > 1e-6)
    xy = np.zeros((*depth.shape, 2), dtype=np.float64)
    np.divide(clip[..., :2], clip[..., 3:4], out=xy, where=valid[..., None])
    old_pixels = (xy * np.array([.5, -.5]) + .5) * np.array([width, height])
    return old_pixels - pixels, valid


def inspect(directory, path, depth_format, owner_sources=()):
    suffix = path.stem.removeprefix('motion_camera_reference_')
    data = path.read_bytes()
    if len(data) != 16 + 38 * 4 or struct.unpack_from('<4I', data) != (0x4d564352, 1, 38, 0):
        raise ValueError('Unsupported camera-reference format')
    values = np.frombuffer(data, dtype='<f4', offset=16).astype(np.float64)
    if struct.unpack_from('<I', data, 16 + 34 * 4)[0] != 1:
        raise ValueError('Reference is not from object-motion composition')
    width, height = map(int, values[32:34])
    raw = (directory / f'object_motion_depth_{suffix}.raw').read_bytes()
    w, h, fmt, pitch = struct.unpack_from('<4I', raw)
    if (w, h) != (width, height) or pitch < w * 4 or len(raw) != 16 + pitch * h:
        raise ValueError('Mismatched depth footprint')
    bits = np.ndarray((h, w), dtype='<u4', buffer=raw, offset=16, strides=(pitch, 4))
    depth = (bits & 0xffffff).astype(np.float64) / 16777215 if depth_format == 'd24' else bits.view('<f4')
    camera, valid = camera_motion(depth, values[:16].reshape(4, 4), values[16:32].reshape(4, 4), values[35:37])
    camera_valid = valid.copy()
    _, velocity = read_raw(directory / f'observed_rigid_motion_velocity_{suffix}.raw')
    _, validity = read_raw(directory / f'observed_rigid_motion_validity_{suffix}.raw')
    if velocity.shape != (h, w, 2) or validity.shape != (h, w, 1):
        raise ValueError('Mismatched rigid attribution footprint')
    valid &= (validity[..., 0] != 0) & np.isfinite(velocity).all(axis=-1)
    residual = velocity.astype(np.float64) - camera
    magnitude = np.linalg.norm(residual, axis=-1)
    families = {}
    if owner_sources:
        owner_data = (directory / f'object_pixel_owners_{suffix}.raw').read_bytes()
        ow, oh, ofmt, opitch = struct.unpack_from('<4I', owner_data)
        if (ow, oh, ofmt) != (w, h, 42) or opitch < w * 4 or len(owner_data) != 16 + opitch * h:
            raise ValueError('Mismatched original pixel-owner footprint')
        owners = np.ndarray((h, w), dtype='<u4', buffer=owner_data, offset=16, strides=(opitch, 4))
        _, production = read_raw(directory / f'object_motion_velocity_{suffix}.raw')
        _, production_validity = read_raw(directory / f'object_motion_validity_{suffix}.raw')
        if production.shape != velocity.shape or production_validity.shape != validity.shape:
            raise ValueError('Mismatched production motion footprint')
        production_valid = (production_validity[..., 0] != 0) & np.isfinite(production).all(axis=-1)
        production_residual = np.linalg.norm(production.astype(np.float64) - camera, axis=-1)
        with (directory / f'object_motion_draws_{suffix}.csv').open(newline='', encoding='utf-8') as stream:
            rows = list(csv.DictReader(stream))
        nonzero_owners = [int(row['owner']) for row in rows if int(row['owner'])]
        if len(nonzero_owners) != len(set(nonzero_owners)):
            raise ValueError('Duplicate original pixel-owner ID')
        for source in owner_sources:
            ids = [int(row['owner']) for row in rows if int(row['vs'], 16) == source
                   and int(row['owner']) and int(row['valid']) and int(row['proof']) == 2]
            visible = np.isin(owners & 0x3fffffff, ids)
            selected = visible & ((owners & 0x40000000) == 0) & camera_valid & production_valid
            values_selected = production_residual[selected]
            families[f'{source:016X}'] = {
                'qualified_owner_ids': ids, 'visible_owner_pixels': int(visible.sum()),
                'valid_production_pixels': int(selected.sum()),
                'camera_residual_percentiles_pixels': np.percentile(values_selected, [0, 50, 95, 99, 100]).tolist() if selected.any() else [],
                'residual_above_pixels': {str(t): int((selected & (production_residual > t)).sum()) for t in (.25, .5, 1, 2)},
                'meaning': 'Original-PS ownership, verified-instance proof, production validity and same-frame frozen camera; source class requires separate provenance'}
            mask = np.zeros((h, w, 3), dtype=np.uint8)
            mask[visible] = (100, 0, 100)
            mask[selected] = (0, 120, 0)
            mask[selected & (production_residual > .25)] = (255, 200, 0)
            Image.fromarray(mask).save(directory / f'owner_camera_residual_{source:016X}_{suffix}.png')
    picture = np.full((h, w, 3), (200, 30, 150), dtype=np.uint8)
    rgb = np.stack((np.maximum(residual[..., 0], 0), np.maximum(residual[..., 1], 0),
                    np.maximum(-residual[..., 0], -residual[..., 1]).clip(0)), axis=-1)
    picture[valid] = (rgb[valid].clip(0, 8) * (255 / 8)).astype(np.uint8)
    Image.fromarray(picture).save(directory / f'rigid_camera_residual_{suffix}.png')
    return {'capture': suffix, 'valid_rigid_pixels': int(valid.sum()), 'owner_sources': families,
            'camera_residual_percentiles_pixels': np.percentile(magnitude[valid], [0, 50, 95, 99, 100]).tolist() if valid.any() else [],
            'residual_above_pixels': {str(t): int((valid & (magnitude > t)).sum()) for t in (.25, .5, 1, 2)},
            'meaning': 'Diagnostic rigid-family attribution minus frozen camera reprojection; actor class requires visual/runtime association'}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--depth-format', required=True, choices=['d24', 'd32'])
    parser.add_argument('--owner-vs', action='append', default=[], type=lambda text: int(text, 16),
                        help='Attribute production residuals to original-PS owners of this verified-instance VS (hex, repeatable)')
    args = parser.parse_args()
    paths = sorted(args.directory.glob('motion_camera_reference_s*.bin'))
    if not paths:
        parser.error('No same-frame camera references found')
    report = {'captures': [inspect(args.directory, path, args.depth_format, args.owner_vs) for path in paths]}
    name = 'owner_camera_inspection.json' if args.owner_vs else 'rigid_camera_inspection.json'
    (args.directory / name).write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))


if __name__ == '__main__':
    main()
