"""Prepare an optional first-launch PSO seed from the user's observed cache.

Only serialized pipeline descriptions/hashes are copied; no shader bytecode.
The native loader still checks version and exact sizeof(PsoRecord) at runtime.
"""
import argparse
import hashlib
import json
from pathlib import Path
import struct

HEADER=struct.Struct('<4sIII')

def pack_seed(data:bytes):
    if len(data)<HEADER.size:raise ValueError('truncated pipeline header')
    magic,version,size,reserved=HEADER.unpack_from(data)
    if magic!=b'CNPS' or version not in (1, 2) or reserved or size<24 or size>4096 or size%8:
        raise ValueError('unsupported pipeline header')
    payload=data[HEADER.size:]
    if len(payload)%size:raise ValueError('truncated pipeline record')
    if len(payload)//size>100000:raise ValueError('too many pipeline records')
    seen=set();records=[]
    for offset in range(0,len(payload),size):
        record=payload[offset:offset+size]
        # The renderer key is the first uint64; startup can also find a PSO by
        # full description. Keep the first key and exact remaining bytes.
        identity=record[8:]
        if identity not in seen:seen.add(identity);records.append(record)
    return HEADER.pack(magic,version,size,0)+b''.join(records),len(payload)//size,len(records)


def merge_seeds(sources):
    """Union compatible local captures; never drop distinct descriptions by key."""
    if not sources:
        raise ValueError('at least one source is required')
    validated = [pack_seed(data) for data in sources]
    versions = {HEADER.unpack_from(result)[1] for result, _, _ in validated}
    # v2 appends pixel_variant/pipeline_kind (8 bytes); old records are base PS.
    if versions == {1, 2}:
        promoted = []
        for result, total, unique in validated:
            _, version, size, _ = HEADER.unpack_from(result)
            if version == 1:
                payload = b''.join(result[at:at + size] + bytes(8)
                                   for at in range(HEADER.size, len(result), size))
                result = HEADER.pack(b'CNPS', 2, size + 8, 0) + payload
            promoted.append((result, total, unique))
        validated = promoted
    sizes = {HEADER.unpack_from(result)[2] for result, _, _ in validated}
    if len(sizes) != 1:
        raise ValueError('incompatible pipeline record sizes')
    size = sizes.pop()
    combined = HEADER.pack(b'CNPS', max(versions), size, 0)
    combined += b''.join(result[HEADER.size:] for result, _, _ in validated)
    packed, _, _ = pack_seed(combined)
    return packed, [total for _, total, _ in validated]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    parser.add_argument('--merge', type=Path, action='append', default=[],
                        help='add another observed CNPS capture (repeatable, same ABI)')
    parser.add_argument('--upgrade-v2', action='store_true',
                        help='append base pixel_variant/pipeline_kind to legacy CNPS v1 records')
    args=parser.parse_args()
    paths = [args.source, *args.merge]
    if any(path.resolve() == args.output.resolve() for path in paths):
        parser.error('source and output must differ')
    originals = [path.read_bytes() for path in paths]
    result, counts = merge_seeds(originals)
    if args.upgrade_v2 and HEADER.unpack_from(result)[1] == 1:
        old_size = HEADER.unpack_from(result)[2]
        result = HEADER.pack(b'CNPS', 2, old_size + 8, 0) + b''.join(
            result[at:at + old_size] + bytes(8) for at in range(HEADER.size, len(result), old_size))
    total, unique = sum(counts), (len(result) - HEADER.size) // HEADER.unpack_from(result)[2]
    if not unique:parser.error('no observed pipelines to seed')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(result)
    report={'source':str(args.source),'output':str(args.output),'records':total,'unique_records':unique,
            'record_size':HEADER.unpack_from(result)[2],'source_sha256':hashlib.sha256(originals[0]).hexdigest(),
            'sources': [{'path': str(path), 'sha256': hashlib.sha256(data).hexdigest(), 'records': count}
                        for path, data, count in zip(paths, originals, counts)],
            'seed_sha256':hashlib.sha256(result).hexdigest(),'bytes':len(result)}
    args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
