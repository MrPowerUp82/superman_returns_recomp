"""Generate a local SRVKLIB v1 library with the production Vulkan compiler.

Contains original game containers and SVR3 results (SPIR-V + reflected metadata).
No game-derived files are distributed. Misses retain the runtime compiler.
"""
import argparse
import concurrent.futures
import json
import shutil
import struct
from pathlib import Path
from compile_vulkan import Toolchain, compile_shader
from runtime_vulkan_shader import ready
from make_preshaders import fnv1a64, parse_header

ROOT = Path(__file__).resolve().parents[2]
MAGIC = b'SRVKLIB\0'
LIBRARY_NAME = 'superman_returns_vulkan.srvk'


def serialize(entries):
    if not entries or len(entries) > 16384:
        raise ValueError('Library needs 1..16384 shaders')
    body = bytearray()
    seen = set()
    for stage, container, result in entries:
        header = parse_header(container)
        if stage not in (0, 1) or not header or header[0] != (stage == 0) or sum(header[1:]) != len(container):
            raise ValueError('Invalid original shader container/stage')
        if (stage, container) in seen:
            raise ValueError('Duplicate original shader')
        seen.add((stage, container))
        if len(result) > 17 * 1024 * 1024:
            raise ValueError('Oversized shader result')
        body += struct.pack('<4I', stage, len(container), len(result), 0)
        body += container + result
    if len(body) + 24 > 256 * 1024 * 1024:
        raise ValueError('Oversized shader library')
    return MAGIC + struct.pack('<IIQ', 1, len(entries), fnv1a64(body)) + body


def compile_entry(raw, cache, tools):
    stage = raw.stem.rsplit('.', 1)[-1]
    try:
        container = raw.read_bytes()
        header = parse_header(container)
        if not header or sum(header[1:]) != len(container) or header[0] != (stage == 'vs'):
            raise ValueError('Invalid container/stage')
        compiled = compile_shader(raw, stage, cache, tools)
        if compiled.status != 'ready':
            raise ValueError(compiled.diagnostic)
        return (0 if stage == 'vs' else 1, container, ready(stage, compiled.binary_path.read_bytes())), None
    except (OSError, ValueError) as error:
        return None, {'container': raw.name, 'diagnostic': str(error)}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--corpus', type=Path, default=ROOT / 'artifacts/shaders/raw')
    parser.add_argument('--cache', type=Path, default=ROOT / 'build/vulkan-m3-runtime')
    parser.add_argument('--out', type=Path, default=ROOT / 'artifacts/shaders' / LIBRARY_NAME)
    parser.add_argument('--emitter', type=Path, default=ROOT / 'build/vulkan-m2/emitter-build/XenosRecompCorpus.exe')
    parser.add_argument('--common', type=Path, default=ROOT / 'build/vulkan-m2/emitter-tree/src/XenosRecomp/shader_common.h')
    parser.add_argument('--dxc', type=Path, default=ROOT / '.tools/dxc/bin/x64/dxc.exe')
    parser.add_argument('--jobs', type=int, choices=range(1, 9), default=4)
    parser.add_argument('--install', type=Path, action='append', default=[])
    args = parser.parse_args()
    raws = sorted(args.corpus.glob('*.bin'))
    if not raws:
        parser.error('No local shader containers; extract your game first')
    tools = Toolchain(args.emitter, args.common, args.dxc)
    entries, failed = [], []
    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        results = pool.map(lambda raw: compile_entry(raw, args.cache, tools), raws)
        for index, (entry, error) in enumerate(results, 1):
            if entry:
                entries.append(entry)
            else:
                failed.append(error)
            if index % 25 == 0 or index == len(raws):
                print(f'Vulkan pre-shaders: {index}/{len(raws)}, {len(failed)} failed', flush=True)
    args.out.parent.mkdir(parents=True, exist_ok=True)
    report = {'schema': 1, 'abi': 'sr-vulkan-buffers-v1', 'total': len(raws), 'ready': len(entries), 'failed': failed}
    args.out.with_suffix('.report.json').write_text(json.dumps(report, indent=2), encoding='utf-8')
    if not entries:
        raise SystemExit('No valid shaders; no library written')
    pending = args.out.with_suffix('.pending')
    pending.write_bytes(serialize(entries))
    pending.replace(args.out)
    for directory in args.install:
        directory.mkdir(parents=True, exist_ok=True)
        shutil.copyfile(args.out, directory / LIBRARY_NAME)
    print(f'{len(entries)} shaders -> {args.out}; runtime compilation covers {len(failed)} failed entries')
    return 0 if not failed else 1


if __name__ == '__main__':
    raise SystemExit(main())
