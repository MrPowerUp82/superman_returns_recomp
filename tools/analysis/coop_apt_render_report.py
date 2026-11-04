"""Verify sampled APT adapter observations inside composed split HUD passes.

This establishes a draw bridge/readback, not stamina identity or packet lifetime.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def analyze(lines):
    passes, packets, composites, errors = {}, {}, {}, []
    started = False
    for line in lines:
        if 'coop: single-instance input preparation;' in line:
            passes, packets, composites, errors = {}, {}, {}, []
            started = True
        if not started:
            continue
        if re.search(r'coop (?:apt|split|status|radar).*failed:|Access violation', line):
            errors.append('runtime verification/guest failure')
        match = re.search(r'coop (apt (?:pass|packet)|split composite): (.*)', line)
        if not match:
            continue
        values = dict(re.findall(r'(\w+)=([^\s]+)', match[2]))
        try:
            frame, generation = int(values['frame']), int(values['generation'])
            if frame <= 0 or not 0 < generation <= 0xFFFFFFFF or generation & 0xC0000000 != 0xC0000000 or not generation & 0x3FFFFFFF:
                raise ValueError('invalid composed frame/generation')
            if match[1] == 'split composite':
                if values['views'] != '2' or frame in composites:
                    raise ValueError('invalid/duplicate composite')
                composites[frame] = generation
                continue
            view = int(values['view'])
            if view not in (0, 1) or values['override'] != 'false':
                raise ValueError('invalid view or unexpected override')
            key = frame, generation, view
            if match[1] == 'apt pass':
                if key in passes: raise ValueError('duplicate HUD pass')
                row = {field: int(values[field]) for field in ('adapters', 'captured', 'nodes', 'node_captured', 'carriers', 'limit')}
                if any(v < 0 for v in row.values()) or row['limit'] != 256:
                    raise ValueError('invalid capture counters/limit')
                if row['captured'] != min(row['adapters'], row['limit']) or row['node_captured'] != min(row['nodes'], row['limit']):
                    raise ValueError('capture counters disagree')
                if row['carriers'] < row['captured']:
                    raise ValueError('captured packet lacks a carrier scope')
                passes[key] = row
            else:
                for field in ('carrier_readable', 'source_matches', 'packet_readable', 'packet_matches', 'readable', 'transform_unchanged'):
                    if values[field] != 'true': raise ValueError(f'{field} is not verified')
                ordinal = int(values['ordinal'])
                if not 1 <= ordinal <= 256 or ordinal in packets.get(key, {}):
                    raise ValueError('invalid/duplicate packet ordinal')
                object_id, method, transform = (int(values[field], 16) for field in ('object', 'method', 'transform'))
                if not all(0 < v <= 0xFFFFFFFF for v in (object_id, method, transform)):
                    raise ValueError('invalid native pointer')
                carrier, packet, matrix = (bytes.fromhex(values[field]) for field in ('carrier_data', 'packet', 'matrix'))
                if len(carrier) != 64 or len(packet) != 16 or len(matrix) != 80:
                    raise ValueError('invalid readback sizes')
                if int.from_bytes(carrier[32:36], 'big') != object_id or int.from_bytes(packet[4:8], 'big') != transform:
                    raise ValueError('native pointer readback disagrees')
                if not all(math.isfinite(v) for v in struct.unpack('>16f', matrix[:64])):
                    raise ValueError('nonfinite transform')
                packets.setdefault(key, {})[ordinal] = {'object': object_id, 'method': method, 'matrix': matrix.hex()}
        except (KeyError, ValueError, OverflowError) as error:
            errors.append(str(error))
    pairs, matched_generations = 0, set()
    if set(packets) - set(passes):
        errors.append('packet records lack a matching HUD pass')
    for key, row in passes.items():
        if composites.get(key[0]) != key[1]:
            errors.append('HUD pass lacks a matching composed frame/generation')
        if set(packets.get(key, {})) != set(range(1, row['captured'] + 1)):
            errors.append('HUD pass missing captured packet records')
    for frame, generation in composites.items():
        keys = [(frame, generation, view) for view in (0, 1)]
        if any(key not in passes for key in keys):
            errors.append('sampled composed frame lacks both HUD passes')
        if all(key in passes and passes[key]['captured'] > 0 for key in keys):
            pairs += 1
            matched_generations.add(generation)
    if pairs < 3:
        errors.append('fewer than three positive APT/composition pairs')
    if set(composites.values()) - matched_generations:
        errors.append('composed generation lacks a positive APT bridge pair')
    return {'passed': not errors, 'scope': 'sampled APT draw bridge/readback; stamina identity and retention not established',
            'passes': len(passes), 'packet_samples': sum(len(rows) for rows in packets.values()),
            'verified_pairs': pairs, 'generations': sorted(matched_generations),
            'consumer_methods': sorted({row['method'] for rows in packets.values() for row in rows.values()}),
            'packet_capped_passes': sum(row['adapters'] > row['limit'] for row in passes.values()),
            'node_capped_passes': sum(row['nodes'] > row['limit'] for row in passes.values()),
            'errors': sorted(set(errors))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    args = parser.parse_args()
    pattern = re.compile(re.escape(args.log.stem) + r'\.(\d+)' + re.escape(args.log.suffix))
    rotations = []
    for path in args.log.parent.iterdir():
        match = pattern.fullmatch(path.name)
        if match: rotations.append((int(match[1]), path))
    paths = [path for _, path in sorted(rotations, reverse=True)] + [args.log]
    def lines():
        for path in paths:
            with path.open(encoding='utf-8', errors='replace') as source:
                yield from source
    result = analyze(lines())
    result['input_files'] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
