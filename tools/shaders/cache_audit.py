"""Audit local pre-shader bytes and route evidence without executing a compiler/GPU.

No generated shader bytes are emitted. A complete corpus and a measured route
are separate claims; a missing/legacy log never becomes a zero-miss result.
"""
import argparse
import hashlib
import json
from pathlib import Path
import re
import struct

from make_preshaders import fnv1a64, parse_header
from pack_pipeline_seed import HEADER, pack_seed

NAME = re.compile(r'([0-9A-Fa-f]{16})\.(vs|ps)\.bin')
PROFILES = ('d3d12', 'portable', 'global')
EVENTS = {
    'pso_runtime_creates': re.compile(r'native: PSO #\d+ created'),
    'pso_runtime_failures': re.compile(r'native: PSO creation failed'),
    'pso_runtime_async': re.compile(r'native: PSO compiling asynchronously'),
    'shader_translation_summaries': re.compile(r'runtime shaders: [1-9]\d* translated'),
    'shader_compile_attempts': re.compile(r'native cache audit: shader_compile '),
    'shader_failures': re.compile(r'runtime shaders: \d+ (?:translated|ready), [1-9]\d* failed'),
    'shader_misses': re.compile(r'(?:pre-shaders?:.*(?:unmatched|unrecognized)|shader not recognized|unrecognized shader)', re.I),
    'unknown_descriptions': re.compile(r'native cache audit: unknown_desc'),
    'precompile_failures': re.compile(r'native cache audit: precompile_failed'),
    'vulkan_shader_misses': re.compile(r'native cache audit: vulkan_shader_miss '),
    'vulkan_unknown_jobs': re.compile(r'native cache audit: vulkan_shader_job_unknown '),
    'vulkan_inflight_jobs': re.compile(r'native cache audit: vulkan_inflight_shader_jobs '),
    'vulkan_opaque_scope': re.compile(r'native cache audit: vulkan_uninstrumented_scope '),
    'vulkan_compiler_invocations': re.compile(r'native cache audit: vulkan_shader_job .*compiler_invocations=[1-9]\d*'),
}


def sha256(data):
    return hashlib.sha256(data).hexdigest()


def inspect_library(data, profile):
    """Return {(stage, SHA256(original)): declared container key or None}.

    SRVK additionally passes independent production reflection verification.
    SRSL is checked strictly enough that truncated slices cannot count as hits.
    """
    if profile not in PROFILES:
        raise ValueError('unsupported profile')
    if not 24 <= len(data) <= 256 * 1024 * 1024:
        raise ValueError('invalid library size')
    expected = b'SRSHLIB\0' if profile == 'd3d12' else b'SRVKLIB\0'
    version, count, checksum = struct.unpack_from('<IIQ', data, 8)
    if data[:8] != expected or not 0 < count <= 16384 or fnv1a64(data[24:]) != checksum:
        raise ValueError('invalid library magic/count/checksum')
    if profile == 'd3d12' and version != 1:
        raise ValueError('unsupported SRSL version')
    if profile != 'd3d12':
        from verify_vulkan_preshaders import verify
        verify(data, profile)
    position, entries, previous = 24, {}, None
    for _ in range(count):
        header_size = 24 if profile == 'd3d12' else 16
        if position + header_size > len(data):
            raise ValueError('truncated library entry header')
        if profile == 'd3d12':
            key, stage, csize, bsize, reserved = struct.unpack_from('<QIIII', data, position)
            order = (key, stage)
            if previous is not None and order <= previous:
                raise ValueError('unsorted/duplicate SRSL key')
            previous = order
        else:
            stage, csize, bsize, reserved = struct.unpack_from('<IIII', data, position)
            key = None
        position += header_size
        if stage > 1 or reserved or not 24 <= csize <= 1024 * 1024 or not 0 < bsize <= 17 * 1024 * 1024:
            raise ValueError('invalid library entry bounds')
        if position + csize + bsize > len(data):
            raise ValueError('truncated library entry data')
        raw = data[position:position + csize]
        binary = data[position + csize:position + csize + bsize]
        header = parse_header(raw)
        if not header or header[0] != (stage == 0) or sum(header[1:]) != csize:
            raise ValueError('invalid library original container/stage')
        if profile == 'd3d12' and not binary.startswith(b'DXBC'):
            raise ValueError('missing DXIL container magic')
        identity = ('vs' if stage == 0 else 'ps', sha256(raw))
        if identity in entries:
            raise ValueError('duplicate original container')
        entries[identity] = key
        position += csize + bsize
    if position != len(data):
        raise ValueError('trailing library bytes')
    return entries


def compare_corpus(expected, entries):
    expected, actual = set(expected), set(entries)
    missing, unexpected = expected - actual, actual - expected
    covered = len(expected & actual)
    return {'total': len(expected), 'covered': covered,
            'coverage_percent': 100.0 * covered / len(expected) if expected else None,
            'complete': bool(expected) and not missing and not unexpected,
            'missing': sorted(missing), 'unexpected': sorted(unexpected)}


def inspect_pipeline_cache(data, corpus):
    packed, total, unique = pack_seed(data)
    _, version, size, _ = HEADER.unpack_from(packed)
    expected_size = 696 if version == 1 else 704
    if size != expected_size:
        raise ValueError('unrecognized native PsoRecord ABI')
    available = {(stage, key) for (stage, _), key in corpus.items()}
    dependencies, variants = set(), {'base': 0, 'rt_shadow': 0, 'coverage': 0, 'rt_coverage': 0}
    kinds = {'guest_raster': 0, 'post_vs_clip': 0}
    for at in range(HEADER.size, len(packed), size):
        _, vs, ps = struct.unpack_from('<3Q', packed, at)
        variant, kind = struct.unpack_from('<II', packed, at + 696) if version == 2 else (0, 0)
        if variant > 3 or kind > 1 or (kind and (variant or ps)):
            raise ValueError('invalid CNPS shader variant')
        dependencies.add(('vs', vs))
        if ps:
            dependencies.add(('ps', ps))
        variants[('base', 'rt_shadow', 'coverage', 'rt_coverage')[variant]] += 1
        kinds[('guest_raster', 'post_vs_clip')[kind]] += 1
    missing = sorted(dependencies - available)
    return {'version': version, 'record_size': size, 'records': total,
            'unique_raw_descriptions': unique, 'pixel_variants': variants, 'pipeline_kinds': kinds,
            'missing_original_shaders': [(stage, f'{key:016X}') for stage, key in missing],
            'note': 'Raw description dedup includes padding. Specialized DXIL readiness is measured separately.'}


def corpus_inventory(raw_dir):
    """Fail closed on malformed originals or filenames whose XXH3 is wrong."""
    import xxhash
    entries = {}
    for path in sorted(raw_dir.glob('*.bin')):
        match = NAME.fullmatch(path.name)
        if not match:
            raise ValueError(f'non-corpus .bin file: {path}')
        raw = path.read_bytes()
        header = parse_header(raw)
        if not header or sum(header[1:]) != len(raw) or header[0] != (match[2] == 'vs'):
            raise ValueError(f'invalid corpus container/stage: {path}')
        if xxhash.xxh3_64_intdigest(raw) != int(match[1], 16):
            raise ValueError(f'corpus filename XXH3 mismatch: {path}')
        identity = (match[2], sha256(raw))
        if identity in entries:
            raise ValueError(f'duplicate corpus container: {path}')
        entries[identity] = int(match[1], 16)
    if not entries:
        raise ValueError(f'empty corpus: {raw_dir}')
    return entries


def event_counts(text):
    # Translation summaries are cumulative and may come from several worker
    # instances. Count messages, never sum them as a shader compile count.
    return {name: len(pattern.findall(text)) for name, pattern in EVENTS.items()}


def audit_route_text(text, name):
    lines = text.splitlines()
    escaped = re.escape(name)
    begins = [i for i, line in enumerate(lines)
              if re.search(r'native cache audit: route_begin name=' + escaped + r'(?:\s|$)', line)]
    ends = [i for i, line in enumerate(lines)
            if re.search(r'native cache audit: route_end name=' + escaped + r'(?:\s|$)', line)]
    reasons, counters, window = [], {}, ''
    if len(begins) != 1 or len(ends) != 1 or ends[0] <= begins[0]:
        reasons.append('missing_route_boundaries')
    else:
        window = '\n'.join(lines[begins[0]:ends[0] + 1])
        counters = {key: int(value) for key, value in re.findall(r'(\w+)=(\d+)(?=\s|$)', lines[ends[0]])}
        required = {'frames', 'draws', 'pso_runtime_creates', 'shader_runtime_compiles', 'shader_misses'}
        if not required <= counters.keys():
            reasons.append('missing_explicit_counters')
        if counters.get('frames', 0) <= 0 or counters.get('draws', 0) <= 0:
            reasons.append('empty_route')
        if counters.get('valid', 1) != 1:
            reasons.append('invalid_route')
        if any(counters.get(key, 0) for key in required - {'frames', 'draws'}):
            reasons.append('runtime_misses_or_compiles')
    events = event_counts(window)
    # Provider snapshots precede the begin marker. An in-flight compiler there
    # prevents proving that the window starts clean even if it ends later.
    if len(begins)==1:
        prefix='\n'.join(lines[max(0,begins[0]-4):begins[0]])
        if EVENTS['vulkan_inflight_jobs'].search(prefix):reasons.append('inflight_compiler_at_route_start')
        if EVENTS['vulkan_opaque_scope'].search(prefix):reasons.append('uninstrumented_external_scope')
    if any(events.values()):
        reasons.append('miss_or_compile_events_in_window')
    return {'name': name, 'counters': counters, 'events_in_window': events,
            'session_events': event_counts(text), 'insufficient_evidence': reasons,
            'zero_runtime_misses_demonstrated': not reasons,
            'scope': 'only this explicitly delimited observed route; no all-game coverage claim'}


def file_evidence(path):
    data = path.read_bytes()
    if not data:
        raise ValueError(f'empty evidence: {path}')
    return {'path': str(path.resolve()), 'bytes': len(data), 'sha256': sha256(data)}


def audit_manifest(path, libraries):
    manifest = json.loads(path.read_text(encoding='utf-8-sig'))
    if manifest.get('schema') != 1 or not isinstance(manifest.get('routes'), list):
        raise ValueError('route manifest requires schema=1 and routes=[]')
    routes, names = [], set()
    for row in manifest['routes']:
        name = row['name']
        if not re.fullmatch(r'[A-Za-z0-9_.-]+', name) or name in names:
            raise ValueError('route names must be unique tokens')
        names.add(name)
        log = path.parent / row['log']
        result = audit_route_text(log.read_text(encoding='utf-8-sig', errors='replace'), name)
        result['log'] = file_evidence(log)
        result['api'] = row.get('api')
        result['profile'] = row.get('profile')
        result['settings'] = row.get('settings', {})
        reasons = result['insufficient_evidence']
        if not row.get('complete'):
            reasons.append('route_not_marked_complete')
        if not row.get('settings'):
            reasons.append('missing_settings')
        profile = row.get('profile')
        library = libraries.get(profile)
        if not library or row.get('library_sha256') != library['sha256']:
            reasons.append('missing_or_mismatched_library_fingerprint')
        if row.get('api') != ('d3d12' if profile == 'd3d12' else 'vulkan'):
            reasons.append('api_profile_mismatch')
        executable = row.get('executable')
        if executable:
            result['executable'] = file_evidence(path.parent / executable)
            if row.get('executable_sha256') != result['executable']['sha256']:
                reasons.append('executable_fingerprint_mismatch')
        else:
            reasons.append('missing_executable_fingerprint')
        result['captures'] = [file_evidence(path.parent / item) for item in row.get('captures', [])]
        if not result['captures']:
            reasons.append('missing_capture_evidence')
        result['zero_runtime_misses_demonstrated'] = not reasons
        routes.append(result)
    return routes


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, required=True, help='directory containing raw originals')
    parser.add_argument('--srsl', type=Path)
    parser.add_argument('--srvk', type=Path)
    parser.add_argument('--srvk-global', type=Path)
    parser.add_argument('--routes', type=Path, help='schema 1 route evidence manifest')
    parser.add_argument('--pso-cache', type=Path, action='append', default=[], help='local CNPS v1/v2 seed/cache')
    parser.add_argument('--out', type=Path, required=True, help='local JSON metadata report')
    args = parser.parse_args()
    report = {'schema': 1, 'libraries': {}, 'pipeline_caches': [], 'routes': [], 'errors': [],
              'acceptance': False, 'all_game_coverage': 'not demonstrated'}
    try:
        corpus = corpus_inventory(args.corpus)
        corpus_metadata = '\n'.join(f'{stage} {key:016X} {digest}'
                                    for (stage, digest), key in sorted(corpus.items()))
        report['corpus'] = {'path': str(args.corpus.resolve()), 'containers': len(corpus),
                            'metadata_sha256': sha256(corpus_metadata.encode('ascii'))}
        for profile, path in [('d3d12', args.srsl), ('portable', args.srvk), ('global', args.srvk_global)]:
            if path is None:
                continue
            evidence = file_evidence(path)
            entries = inspect_library(path.read_bytes(), profile)
            evidence.update(compare_corpus(corpus, entries))
            bad_keys = [identity for identity in set(corpus) & set(entries)
                        if profile == 'd3d12' and corpus[identity] != entries[identity]]
            evidence['mismatched_container_keys'] = sorted(bad_keys)
            evidence['complete'] = evidence['complete'] and not bad_keys
            report['libraries'][profile] = evidence
        for path in args.pso_cache:
            evidence = file_evidence(path)
            evidence.update(inspect_pipeline_cache(path.read_bytes(), corpus))
            report['pipeline_caches'].append(evidence)
        if args.routes:
            report['routes'] = audit_manifest(args.routes, report['libraries'])
        libraries = report['libraries']
        routes = report['routes']
        report['acceptance'] = bool(libraries and routes) and all(
            row['complete'] for row in libraries.values()) and all(
            row['zero_runtime_misses_demonstrated'] for row in routes)
        if not libraries:
            report['errors'].append('no_libraries_selected')
        if not routes:
            report['errors'].append('no_observed_routes')
    except (OSError, ValueError, KeyError, TypeError, ImportError, struct.error) as error:
        report['errors'].append(str(error))
    args.out.parent.mkdir(parents=True, exist_ok=True)
    args.out.write_text(json.dumps(report, indent=2) + '\n', encoding='utf-8')
    print(json.dumps(report, indent=2))
    return 0 if report['acceptance'] else 1


if __name__ == '__main__':
    raise SystemExit(main())
