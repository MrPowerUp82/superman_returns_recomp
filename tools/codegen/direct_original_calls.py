"""Bounded experiment: direct calls only to equal-address integer aliases."""
from collections import Counter
import argparse
import hashlib
import json
from pathlib import Path
import re
import shutil
import struct

DEFINITION = re.compile(r'^DEFINE_REX_FUNC\((\w+)\)', re.M)
SYMBOL = re.compile(r'^\s*[0-9a-fA-F]{4}:[0-9a-fA-F]{8}\s+(\S+)\s+([0-9a-fA-F]{16})\s', re.M)
FLOAT_STATE = re.compile(r'ctx\.(?:fpscr|[fv][0-9]+)\b|\b(?:float|double|f32|f64|PPCVRegister)\b|_mm_\w+_(?:ps|pd)\b')
CALL = re.compile(r'''(?P<skip>//[^\r\n]*|/\*.*?\*/|"(?:\\.|[^"\\])*"|'(?:\\.|[^'\\])*')|(?<![\w:.])(?P<target>\w+)(?P<args>\(\s*ctx\s*,\s*base\s*\))''', re.S)


def safe_targets(map_text, generated_text):
    symbols = {}
    ambiguous = set()
    for name, address in SYMBOL.findall(map_text):
        address = int(address, 16)
        if name in symbols and symbols[name] != address:
            ambiguous.add(name)
        symbols[name] = address
    definitions = list(DEFINITION.finditer(generated_text))
    targets = set()
    callees = {}
    for i, definition in enumerate(definitions):
        name = definition[1]
        if not re.fullmatch(r'sub_[0-9A-Fa-f]{8}|__(?:save|rest)\w+', name):
            continue
        original = '__imp__' + name
        if name in ambiguous or original in ambiguous:
            continue
        if name not in symbols or original not in symbols or symbols[name] != symbols[original]:
            continue
        end = definitions[i + 1].start() if i + 1 < len(definitions) else len(generated_text)
        body = generated_text[definition.start():end]
        if FLOAT_STATE.search(body) or re.search(r'REX_CALL_INDIRECT|REX_MM_(?:LOAD|STORE)|SEH_|(?:setjmp|longjmp)', body):
            continue
        callees[name] = {match['target'] for match in CALL.finditer(body) if match['target']}
        targets.add(name)
    # Keep a closed integer call graph. A wrapper around floating-point,
    # unknown imports or an indirect call is deliberately not a candidate.
    while True:
        rejected = {name for name in targets if not callees[name].issubset(targets)}
        if not rejected:
            break
        targets -= rejected
    return targets


def _replace(text, mapping):
    counts = Counter()
    def call(match):
        name = match['target']
        if match['skip'] is not None or name not in mapping:
            return match[0]
        counts[name] += 1
        return mapping[name] + match['args']
    return CALL.sub(call, text), counts


def rewrite(text, targets):
    return _replace(text, {name: '__imp__' + name for name in targets})


def validate_backup_location(generated, backup):
    if backup.resolve().is_relative_to(generated.resolve()):
        raise ValueError('Backup must be outside the generated source tree')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--generated', type=Path, required=True)
    parser.add_argument('--map', type=Path, required=True)
    parser.add_argument('--exe', type=Path, required=True)
    parser.add_argument('--report', type=Path, required=True)
    parser.add_argument('--backup', type=Path)
    parser.add_argument('--apply', action='store_true')
    args = parser.parse_args()
    image = args.exe.read_bytes()
    pe = struct.unpack_from('<I', image, 0x3c)[0]
    if image[:2] != b'MZ' or image[pe:pe+4] != b'PE\0\0':
        raise ValueError('Expected a PE executable')
    timestamp = struct.unpack_from('<I', image, pe+8)[0]
    map_text = args.map.read_text()
    stamp = re.search(r'Timestamp is ([0-9a-fA-F]+)', map_text)
    if not stamp or int(stamp[1], 16) != timestamp:
        raise ValueError('Executable and MAP timestamps do not match')
    files = {p: p.read_bytes().decode('utf-8') for p in sorted(args.generated.glob('*.cpp'))}
    targets = safe_targets(map_text, '\n'.join(files.values()))
    # An already-direct call must not be changed by the inverse byte audit.
    for source in files.values():
        for match in CALL.finditer(source):
            name = match['target']
            if name and name.startswith('__imp__'):
                targets.discard(name[7:])
    changed = {}
    counts = Counter()
    for path, source in files.items():
        result, replacements = rewrite(source, targets)
        restored, _ = _replace(result, {'__imp__'+name: name for name in targets})
        if restored != source:
            raise ValueError(f'Inverse source audit failed: {path}')
        if result != source:
            changed[path] = result
            counts.update(replacements)
    report = {'exe_sha256': hashlib.sha256(image).hexdigest(),
              'map_sha256': hashlib.sha256(args.map.read_bytes()).hexdigest(),
              'integer_equal_address_targets': len(targets), 'changed_files': len(changed),
              'changed_calls': sum(counts.values()), 'targets': sorted(targets),
              'counts': dict(counts), 'applied': args.apply, 'inverse_audit': 'pass'}
    if args.apply:
        if not args.backup or args.backup.exists():
            raise ValueError('Apply requires a fresh backup directory')
        validate_backup_location(args.generated, args.backup)
        if not changed:
            raise ValueError('No eligible static calls found')
        # Preserve sources and the exact paired artifacts before changing files.
        shutil.copytree(args.generated, args.backup / 'generated')
        shutil.copy2(args.exe, args.backup / args.exe.name)
        shutil.copy2(args.map, args.backup / args.map.name)
        for path, source in changed.items():
            path.write_bytes(source.encode('utf-8'))
    args.report.write_text(json.dumps(report, indent=2), encoding='utf-8')
    print(f"Eligible integer targets={len(targets)} files={len(changed)} static calls={sum(counts.values())}; inverse audit pass; apply={args.apply}")


if __name__ == '__main__':
    main()
