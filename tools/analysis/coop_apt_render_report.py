"""Verify sampled APT adapter observations inside composed split HUD passes.

Optional preview evidence verifies private P2 positions while retaining original
effect frames and shield colors; it does not establish packet lifetime.
"""
import argparse
import json
import math
from pathlib import Path
import re
import struct


def ancestry_role(value):
    path = [tuple(map(int, node.split(':'))) for node in value.split(';')]
    if not 1 <= len(path) <= 64 or any(len(n) != 7 for n in path):
        raise ValueError('invalid ancestry path')
    if any(not 0 < n[0] <= 0xFFFFFFFF or not 0 <= n[1] <= 0xFFFFFFFF or
           not 0 <= n[2] <= 0xFFFFFFFF or not 0 <= n[3] <= 0xFFFFFFFF or
           not 0 <= n[4] <= 65535 or n[5] not in (0, 1, 2, 3) or n[6] not in (0, 1) for n in path):
        raise ValueError('invalid ancestry node fields')
    role = 0
    for i in range(len(path) - 3):
        main, stamina, meter, child = path[i:i + 4]
        if main[5:] != (1, 1) or stamina[5:] != (2, 1) or stamina[4] != 172 or meter[5:] != (3, 1) or meter[4] != 1:
            continue
        if len({main[2], stamina[2], meter[2]}) != 3:
            raise ValueError('ancestry movie roots alias')
        if not all(n[1] and n[2] for n in (main, stamina, meter)):
            raise ValueError('selected ancestry lacks source/movie roots')
        if any(n[5] != 3 or n[2] != meter[2] for n in path[i + 3:]):
            break
        role = {(13, 24): 1, (26, 30): 2, (21, 34): 3, (27, 39): 4, (40, 43): 5}.get((child[3], child[4]), 0)
        break
    return role, path


def f32(value):
    return struct.unpack('>f', struct.pack('>f', value))[0]


def validate_preview(values):
    role = int(values['role'])
    if role not in (2, 3, 4, 5) or values['unchanged'] != 'true' or values['override'] != 'true':
        raise ValueError('preview is not an unchanged moving-role override')
    target, current, maximum, scaled = (int(values[f]) for f in ('target', 'current', 'maximum', 'scaled'))
    if not 0 < target <= 0xFFFFFFFF or target == 255 or maximum != 10000 or not 0 <= current <= 10000:
        raise ValueError('preview requires a valid target and 10000-point capacity')
    if scaled != int(f32(f32(current / 10000) * 10000)):
        raise ValueError('preview scaled value is not canonical C++ float32')
    width, start, notch, desired, local = (float(values[f]) for f in ('width', 'start', 'notch', 'desired', 'local'))
    if not all(math.isfinite(v) for v in (width, start, notch, desired, local)) or not 16 < width <= 4096:
        raise ValueError('preview geometry is nonfinite or unsupported')
    width, start, notch, local = map(f32, (width, start, notch, local))
    percent = min(1., max(0., f32((scaled - 500) / 9500)))
    delta = f32(-width * f32(1 - percent))
    expected = {2: lambda: f32(f32(f32(start + width) - 8) + delta),
                3: lambda: f32(f32(start + 8) + delta),
                4: lambda: f32(start + delta),
                5: lambda: f32(notch + f32(width * percent))}[role]()
    if abs(desired - expected) > .001 or abs(desired - local) > 8192:
        raise ValueError('preview desired position contradicts role math')
    basis, original, matrix, private = (bytes.fromhex(values[f]) for f in
                                        ('basis', 'original_packet', 'original_matrix', 'private'))
    if tuple(map(len, (basis, original, matrix, private))) != (64, 16, 80, 96):
        raise ValueError('invalid preview readback sizes')
    basis_values = struct.unpack('>16f', basis)
    matrix_values = struct.unpack('>16f', matrix[:64])
    private_values = struct.unpack('>16f', private[16:80])
    if not all(math.isfinite(v) for v in (*basis_values, *matrix_values, *private_values)):
        raise ValueError('nonfinite preview transform/basis')
    scratch = int(values['scratch'], 16)
    transform = int.from_bytes(original[4:8], 'big')
    if not 0 < scratch <= 0x100000000 - 96 or not 0 < transform <= 0x100000000 - 80:
        raise ValueError('preview pointer range exceeds guest memory')
    if scratch < transform + 80 and transform < scratch + 96:
        raise ValueError('preview private storage aliases original matrix')
    expected_packet = original[:4] + (scratch + 16).to_bytes(4, 'big') + original[8:]
    if private[:16] != expected_packet:
        raise ValueError('preview private packet changed beyond matrix pointer')
    if private[16:64] != matrix[:48] or private[80:] != matrix[64:]:
        raise ValueError('preview private matrix changed beyond translation (color/effect bytes)')
    shift = f32(f32(desired) - local)
    for i in range(4):
        expected_translation = f32(matrix_values[12 + i] + f32(shift * basis_values[i]))
        if not math.isfinite(expected_translation) or abs(private_values[12 + i] - expected_translation) > .001:
            raise ValueError('preview private translation contradicts GenericMeter basis')
    return {'role': role, 'target': target, 'current': current, 'scaled': scaled,
            'desired': desired, 'packet': original.hex(), 'matrix': matrix.hex(), 'scratch': scratch}


def analyze(lines, required_ancestry=False, required_preview=False):
    required_ancestry = required_ancestry or required_preview
    passes, packets, composites, errors = {}, {}, {}, []
    scopes = {}
    previews, preview_passes = {}, {}
    started = False
    for line in lines:
        if 'coop: single-instance input preparation;' in line:
            passes, packets, composites, errors = {}, {}, {}, []
            scopes = {}
            previews, preview_passes = {}, {}
            started = True
        if not started:
            continue
        if re.search(r'coop (?:apt|split|status|radar).*failed:|Access violation', line):
            errors.append('runtime verification/guest failure')
        match = re.search(r'coop (apt (?:pass|packet|scopepass|preview|previewpass)|split composite): (.*)', line)
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
            override = values['override']
            if view not in (0, 1) or override not in ('false', 'true'):
                raise ValueError('invalid view or unexpected override')
            if override == 'true' and not (required_preview and
                    (match[1] == 'apt previewpass' or
                     (view == 1 and match[1] in ('apt preview', 'apt packet') and int(values['role']) in (2, 3, 4, 5)))):
                raise ValueError('invalid view or unexpected override')
            key = frame, generation, view
            if match[1] in ('apt preview', 'apt previewpass') and not required_preview:
                raise ValueError('unexpected preview evidence without --require-preview')
            if match[1] == 'apt previewpass':
                if key in preview_passes: raise ValueError('duplicate preview pass')
                row = {f: int(values[f]) for f in ('applied', 'skipped')}
                if min(row.values()) < 0 or override != str(row['applied'] != 0).lower():
                    raise ValueError('preview pass counters/override disagree')
                if view == 0 and any(row.values()): raise ValueError('P1 preview counters must be zero')
                preview_passes[key] = row
            elif match[1] == 'apt preview':
                if view != 1: raise ValueError('P1 preview is forbidden')
                ordinal = int(values['ordinal'])
                if not 1 <= ordinal <= 256 or ordinal in previews.get(key, {}):
                    raise ValueError('invalid/duplicate preview ordinal')
                previews.setdefault(key, {})[ordinal] = validate_preview(values)
            elif match[1] == 'apt scopepass':
                if key in scopes: raise ValueError('duplicate ancestry pass')
                if values['valid'] != 'true' or values['balanced'] != 'true':
                    raise ValueError('unbalanced or invalid native ancestry scope')
                row = {field: int(values[field]) for field in ('pushes', 'pops', 'selected', 'mask', 'right', 'body', 'left', 'notch')}
                if min(row.values()) < 0 or row['pushes'] != row['pops'] or row['selected'] != sum(row[f] for f in ('mask', 'right', 'body', 'left', 'notch')):
                    raise ValueError('ancestry counters disagree')
                scopes[key] = row
            elif match[1] == 'apt pass':
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
                role = None
                if required_ancestry or 'role' in values:
                    if values['scope_valid'] != 'true': raise ValueError('invalid packet ancestry scope')
                    expected, path = ancestry_role(values['path'])
                    role = int(values['role'])
                    if role != expected: raise ValueError('packet ancestry contradicts selected role')
                    if role and (path[-1][1] != int(values['carrier'], 16) or path[-1][2] != int.from_bytes(carrier[4:8], 'big')):
                        raise ValueError('selected ancestry leaf does not own packet carrier/movie')
                packets.setdefault(key, {})[ordinal] = {'object': object_id, 'method': method, 'matrix': matrix.hex(),
                                                       'packet': packet.hex(), 'role': role, 'override': override == 'true',
                                                       'manager': int(values['manager'], 16) if 'manager' in values else None}
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
    selected_pairs, selected_generations = 0, set()
    if required_ancestry:
        if set(scopes) != set(passes): errors.append('ancestry scopes do not match HUD passes')
        for key, scope in scopes.items():
            rows = list(packets.get(key, {}).values())
            if key not in passes: continue
            if scope['pushes'] != passes[key]['nodes']:
                errors.append('ancestry push count contradicts native node count')
            selected = sum(bool(row['role']) for row in rows)
            if passes[key]['adapters'] <= 256 and selected != scope['selected']:
                errors.append('selected packet count contradicts ancestry summary')
            for role, field in enumerate(('mask', 'right', 'body', 'left', 'notch'), 1):
                if passes[key]['adapters'] <= 256 and sum(row['role'] == role for row in rows) != scope[field]:
                    errors.append('selected role count contradicts ancestry summary')
        for frame, generation in composites.items():
            keys = [(frame, generation, view) for view in (0, 1)]
            if all({row['role'] for row in packets.get(key, {}).values()} >= {1, 2, 3, 4, 5} for key in keys):
                selected_pairs += 1; selected_generations.add(generation)
        if selected_pairs < 3: errors.append('fewer than three complete stamina ancestry/composition pairs')
        if set(composites.values()) - selected_generations:
            errors.append('composed generation lacks complete stamina ancestry roles')
    preview_pairs, preview_generations = 0, set()
    if required_preview:
        if set(preview_passes) != set(passes): errors.append('preview summaries do not match HUD passes')
        for key, rows in previews.items():
            if key not in passes: errors.append('preview records lack a matching HUD pass')
            for ordinal, preview in rows.items():
                outer = packets.get(key, {}).get(ordinal)
                if outer is None:
                    errors.append('orphan preview lacks matching outer packet')
                    continue
                if not outer['override'] or any(outer[f] != preview[f] for f in ('role', 'packet', 'matrix')):
                    errors.append('preview/outer packet override, role or original readbacks disagree')
                if outer['manager'] is not None:
                    address = outer['manager'] + 84
                    if not 0 < outer['manager'] or address + 16 > 0x100000000:
                        errors.append('original packet pointer exceeds guest memory')
                    if preview['scratch'] < address + 16 and address < preview['scratch'] + 96:
                        errors.append('preview private storage aliases original packet')
        for key, rows in packets.items():
            for ordinal, row in rows.items():
                if row['override'] and ordinal not in previews.get(key, {}):
                    errors.append('outer packet override lacks matching preview')
        for key, row in preview_passes.items():
            count = len(previews.get(key, {}))
            if key in passes and (row['applied'] < count or
                    (passes[key]['adapters'] <= 256 and row['applied'] != count)):
                errors.append('preview applied count contradicts captured records')
        for frame, generation in composites.items():
            if (not previews.get((frame, generation, 0)) and
                    {row['role'] for row in previews.get((frame, generation, 1), {}).values()} >= {2, 3, 4, 5}):
                preview_pairs += 1
                preview_generations.add(generation)
        if preview_pairs < 3: errors.append('fewer than three complete P2 preview/composition pairs')
        if set(composites.values()) - preview_generations:
            errors.append('composed generation lacks a positive P2 preview pair')
    return {'passed': not errors, 'scope': (
                'sampled private P2 stamina positions only; original effect frames and shield colors retained; packet lifetime not established'
                if required_preview else 'sampled APT draw bridge/readback; stamina identity and retention not established'),
            'passes': len(passes), 'packet_samples': sum(len(rows) for rows in packets.values()),
            'verified_pairs': pairs, 'generations': sorted(matched_generations),
            'consumer_methods': sorted({row['method'] for rows in packets.values() for row in rows.values()}),
            'packet_capped_passes': sum(row['adapters'] > row['limit'] for row in passes.values()),
            'node_capped_passes': sum(row['nodes'] > row['limit'] for row in passes.values()),
            'ancestry': {'scope_samples': len(scopes), 'selected_pairs': selected_pairs,
                         'generations': sorted(selected_generations)},
            'preview': {'required': required_preview, 'scope_samples': len(preview_passes),
                        'packet_samples': sum(len(rows) for rows in previews.values()),
                        'verified_pairs': preview_pairs, 'generations': sorted(preview_generations),
                        'applied': sum(row['applied'] for row in preview_passes.values()),
                        'skipped': sum(row['skipped'] for row in preview_passes.values()),
                        'roles': {str(role): sum(row['role'] == role for rows in previews.values() for row in rows.values())
                                  for role in (2, 3, 4, 5)},
                        'targets': sorted({row['target'] for rows in previews.values() for row in rows.values()}),
                        'fully_captured_passes': sum(passes[key]['adapters'] <= 256 for key in preview_passes if key in passes)},
            'errors': sorted(set(errors))}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--require-ancestry', action='store_true')
    parser.add_argument('--require-preview', action='store_true', help='require private P2 previews (implies ancestry)')
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
    result = analyze(lines(), required_ancestry=args.require_ancestry, required_preview=args.require_preview)
    result['input_files'] = [str(path) for path in paths]
    args.output.parent.mkdir(parents=True, exist_ok=True)
    args.output.write_text(json.dumps(result, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(result, indent=2))
    return 0 if result['passed'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
