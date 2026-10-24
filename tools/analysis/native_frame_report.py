"""Join a complete native one-frame trace with shader reflection, without inferring render roles."""
from __future__ import annotations

import argparse
from collections import Counter
import json
from pathlib import Path
import re

BEGIN = re.compile(r'trace begin frame (\d+)(?: after_guest_swap=(\d+))?')
DRAW = re.compile(r'trace draw p(-?\d+) #(\d+) prim (\d+) vs ([0-9a-fA-F]{16}) ps ([0-9a-fA-F]{16})')
CANDIDATE = re.compile(r'view|proj|world|eye|shadow', re.I)
ROTATION_GAP = '# native_frame_report: rotation gap'


def read_log(path: Path) -> str:
    """spdlog rotates name.log -> name.1.log; larger numbers are older."""
    pattern = re.compile(re.escape(path.stem) + r'\.(\d+)' + re.escape(path.suffix) + r'$')
    parts = []
    for candidate in path.parent.glob(path.stem + '.*' + path.suffix):
        match = pattern.fullmatch(candidate.name)
        if match and candidate.is_file():
            parts.append((int(match[1]), candidate))
    chunks = []
    previous = None
    for number, part in sorted(parts, reverse=True) + [(0, path)]:
        if previous is not None and previous - number > 1:
            chunks.append(ROTATION_GAP)
        chunks.append(part.read_text(encoding='utf-8-sig', errors='replace'))
        previous = number
    return '\n'.join(chunks)


def analyze(log_text: str, catalog: dict, frame: int | None = None) -> dict:
    frames = {}
    active = None
    for line in log_text.splitlines():
        if line == ROTATION_GAP:
            if active is not None:
                raise ValueError('rotation gap inside trace frame')
            continue
        begin = BEGIN.search(line)
        if begin:
            number = int(begin[1])
            if active is not None or number in frames:
                raise ValueError('Nested or repeated trace frame')
            active = {'frame': number, 'after_guest_swap': int(begin[2]) if begin[2] else None, 'events': []}
        elif 'trace end of frame' in line and active is not None:
            frames[active['frame']] = active
            active = None
        elif active is not None:
            pos = line.find('trace ')
            if pos < 0:
                continue
            text = line[pos:]
            draw = DRAW.search(text)
            event = {'text': text, 'kind': 'detail'}
            if draw:
                event.update(kind='draw', pass_id=int(draw[1]), index=int(draw[2]),
                             primitive=int(draw[3]), vs=draw[4].upper(), ps=draw[5].upper())
            elif 'bind RT ' in text:
                event['kind'] = 'bind'
            elif 'resolve flags ' in text:
                event['kind'] = 'resolve'
            elif 'clear ' in text:
                event['kind'] = 'clear'
            elif 'BeginTiling' in text or 'EndTiling' in text:
                event['kind'] = 'tiling'
            active['events'].append(event)
    if active is not None:
        raise ValueError('Incomplete trace frame')
    if not frames:
        raise ValueError('No complete native trace frame')
    if frame is None:
        if len(frames) != 1:
            raise ValueError('Multiple frames: choose --frame')
        frame = next(iter(frames))
    if frame not in frames:
        raise ValueError(f'Frame {frame} not found')
    result = frames[frame]
    draws = [e for e in result['events'] if e['kind'] == 'draw']
    pairs = Counter((d['vs'], d['ps']) for d in draws)
    usage = Counter((stage, d[stage]) for d in draws for stage in ('vs', 'ps'))
    # Deliberately not keyed by ucode hashes: runtime native draws use container IDs.
    by_shader = {(s['type'], s['container_hash'].upper()): s for s in catalog['shaders']}
    shaders = []
    for (stage, key), count in sorted(usage.items()):
        source = by_shader.get((stage, key))
        constants = source.get('reflection', {}).get('constants', []) if source else []
        shaders.append({'stage': stage, 'hash': key, 'draws': count, 'known': source is not None,
                        'candidate_constants': [c for c in constants if CANDIDATE.search(c['name'])]})
    result.update(draws=len(draws), shader_pairs=[{'vs': v, 'ps': p, 'draws': n} for (v, p), n in pairs.most_common()],
                  shaders=shaders,
                  unknown_shaders=[{'stage': s['stage'], 'hash': s['hash'], 'draws': s['draws']}
                                   for s in shaders if not s['known']],
                  roles_confirmed=False,
                  note='Reflection names are candidates; no render role or camera contract has been confirmed.')
    return result


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--log', type=Path, required=True)
    parser.add_argument('--catalog', type=Path, required=True)
    parser.add_argument('--output', type=Path, required=True)
    parser.add_argument('--frame', type=int)
    args = parser.parse_args()
    try:
        result = analyze(read_log(args.log),
                         json.loads(args.catalog.read_text(encoding='utf-8-sig')), args.frame)
    except (ValueError, KeyError) as error:
        parser.error(str(error))
    args.output.write_text(json.dumps(result, indent=2, ensure_ascii=False) + '\n', encoding='utf-8')
    print(f"frame={result['frame']} draws={result['draws']} pairs={len(result['shader_pairs'])} "
          f"unknown_shaders={len(result['unknown_shaders'])} roles_confirmed=false")


if __name__ == '__main__':
    main()
