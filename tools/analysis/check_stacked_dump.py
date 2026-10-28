"""Verify native per-Z raw exports; report data, without judging shadow fidelity.

Example: python tools/analysis/check_stacked_dump.py logs/capture --prefix cap_ --depth 4
Legacy single-image files require --legacy-2d instead of --depth.
DXGI 37 is R16G16_SNORM; DXGI 34 is R16G16_FLOAT (IEEE half).
"""
from __future__ import annotations

import argparse
import hashlib
import json
import math
from pathlib import Path
import re
import struct


FORMATS = {37: ('R16G16_SNORM', '<hh'), 34: ('R16G16_FLOAT', '<ee')}


def _distribution(values: list[float], channel: str) -> dict:
    finite = [value for value in values if math.isfinite(value)]
    return {
        'channel': channel, 'samples': len(values), 'finite': len(finite),
        'nan': sum(math.isnan(value) for value in values),
        'positive_infinity': values.count(math.inf),
        'negative_infinity': values.count(-math.inf),
        'zero': finite.count(0.0),
        'negative': sum(value < 0 for value in finite),
        'positive': sum(value > 0 for value in finite),
        'min': min(finite) if finite else None,
        'max': max(finite) if finite else None,
        'mean': math.fsum(finite) / len(finite) if finite else None,
    }


def check_dump(directory: Path, prefix: str, *, depth: int | None = None,
               base: str = '1E820000', legacy_2d: bool = False) -> dict:
    """Validate one exact resource identity and its complete explicitly sized set."""
    if legacy_2d and depth is not None:
        raise ValueError('choose depth or legacy 2D, not both')
    if not legacy_2d and (depth is None or depth < 1):
        raise ValueError('positive explicit depth is required for per-Z dumps')
    try:
        base_value = int(base, 16)
    except (ValueError, TypeError):
        raise ValueError('base must be a hexadecimal 32-bit resolve address') from None
    if not 0 <= base_value <= 0xFFFFFFFF:
        raise ValueError('base must be a hexadecimal 32-bit resolve address')
    base = f'{base_value:08X}'
    start = f'{prefix}resolve_{base}_'
    pattern = re.compile(re.escape(start) +
        r'f(?P<format>\d+)_(?P<width>\d+)x(?P<height>\d+)'
        r'(?P<identity>_id[0-9A-Fa-f]+)?(?:_z(?P<z>\d+))?\.raw', re.IGNORECASE)
    files = []
    identities = set()
    for path in sorted(Path(directory).iterdir()):
        if not path.is_file() or not path.name.lower().startswith(start.lower()) or path.suffix.lower() != '.raw':
            continue
        match = pattern.fullmatch(path.name)
        if not match:
            raise ValueError(f'invalid matching dump filename: {path.name}')
        identities.add((match['format'], match['width'], match['height'],
                        (match['identity'] or '').lower()))
        if legacy_2d != (match['z'] is None):
            raise ValueError('2D and per-Z filenames require separate explicit modes')
        files.append((path, match))
    if not files:
        raise ValueError(f'no matching dumps for prefix {prefix!r}, base {base}')
    if len(identities) != 1:
        raise ValueError('multiple resource identities match; select a narrower capture prefix')
    wanted_depth = 1 if legacy_2d else depth
    indexed = {}
    for path, match in files:
        z = 0 if legacy_2d else int(match['z'])
        if z in indexed:
            raise ValueError(f'duplicate slice {z}')
        indexed[z] = (path, match)
    missing = sorted(set(range(wanted_depth)) - indexed.keys())
    unexpected = sorted(indexed.keys() - set(range(wanted_depth)))
    if missing or unexpected:
        raise ValueError(f'incomplete slice set: missing {missing}, unexpected {unexpected}')
    slices, groups = [], {}
    common_header = None
    for z, (path, match) in sorted(indexed.items()):
        raw = path.read_bytes()
        if len(raw) < 16:
            raise ValueError(f'truncated header: {path.name}')
        width, height, fmt, pitch = struct.unpack_from('<4I', raw)
        if (width, height, fmt) != tuple(int(match[key]) for key in ('width', 'height', 'format')):
            raise ValueError(f'header does not match filename: {path.name}')
        if fmt not in FORMATS:
            raise ValueError(f'unsupported DXGI format {fmt}; supported: 34, 37')
        if not width or not height or pitch < width * 4:
            raise ValueError(f'invalid header dimensions or row pitch: {path.name}')
        header = (width, height, fmt, pitch)
        if common_header is not None and header != common_header:
            raise ValueError(f'inconsistent slice header: {path.name}')
        common_header = header
        if len(raw) != 16 + height * pitch:
            raise ValueError(f'payload size does not match header: {path.name}')
        pixels = b''.join(raw[16 + row * pitch:16 + row * pitch + width * 4]
                          for row in range(height))
        pixel_hash = hashlib.sha256(pixels).hexdigest()
        groups.setdefault(pixel_hash, []).append(z)
        channels = [[], []]
        for pair in struct.iter_unpack(FORMATS[fmt][1], pixels):
            for channel, value in enumerate(pair):
                channels[channel].append(max(-1.0, value / 32767.0) if fmt == 37 else value)
        slices.append({
            'z': z, 'file': path.name, 'bytes': len(raw),
            'sha256': hashlib.sha256(raw).hexdigest(), 'pixel_sha256': pixel_hash,
            'channels': [_distribution(values, name)
                         for values, name in zip(channels, ('R', 'G'))],
        })
    width, height, fmt, pitch = common_header
    return {
        'schema': 1, 'base': base, 'prefix': prefix,
        'mode': '2d' if legacy_2d else 'stacked', 'depth': wanted_depth,
        'header': {'width': width, 'height': height, 'dxgi_format': fmt,
                   'format_name': FORMATS[fmt][0], 'row_pitch': pitch},
        'slices': slices, 'distinct_pixel_slices': len(groups),
        'all_slices_distinct': len(groups) == wanted_depth,
        'identical_slice_groups': [group for group in groups.values() if len(group) > 1],
        'note': 'Raw numeric values before SRV swizzle; pixel hashes exclude row padding. '
                'Distinctness does not establish shadow fidelity.',
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory', type=Path)
    parser.add_argument('--prefix', required=True, help='exact export filename prefix (can be empty)')
    parser.add_argument('--base', default='1E820000', help='hexadecimal resolve base')
    mode = parser.add_mutually_exclusive_group(required=True)
    mode.add_argument('--depth', type=int, help='expected full per-Z slice count, e.g. 4')
    mode.add_argument('--legacy-2d', action='store_true', help='read one unsuffixed 2D file')
    args = parser.parse_args()
    try:
        report = check_dump(args.directory, args.prefix, depth=args.depth,
                            base=args.base, legacy_2d=args.legacy_2d)
    except (ValueError, OSError) as error:
        parser.exit(1, f'error: {error}\n')
    print(json.dumps(report, indent=2, allow_nan=False))
    return 0


if __name__ == '__main__':
    raise SystemExit(main())
