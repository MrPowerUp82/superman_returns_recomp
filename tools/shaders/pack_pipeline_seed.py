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
    if magic!=b'CNPS' or version!=1 or size<24 or size>4096 or size%8:
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

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('source',type=Path);parser.add_argument('output',type=Path)
    args=parser.parse_args()
    if args.source.resolve()==args.output.resolve():parser.error('source and output must differ')
    original=args.source.read_bytes()
    result,total,unique=pack_seed(original)
    if not unique:parser.error('no observed pipelines to seed')
    args.output.parent.mkdir(parents=True,exist_ok=True)
    args.output.write_bytes(result)
    report={'source':str(args.source),'output':str(args.output),'records':total,'unique_records':unique,
            'record_size':HEADER.unpack_from(result)[2],'source_sha256':hashlib.sha256(original).hexdigest(),
            'seed_sha256':hashlib.sha256(result).hexdigest(),'bytes':len(result)}
    args.output.with_suffix(args.output.suffix+'.json').write_text(json.dumps(report,indent=2)+'\n')
    print(json.dumps(report,indent=2))

if __name__=='__main__':main()
