"""Compare bounded, frozen constant snapshots for the same candidate mesh.

Usage: python tools/graphics/inspect_motion_inputs.py <capture-directory>
This reports evidence; equal mesh/buffer keys never prove a moving actor's ID.
"""
import argparse
import json
import math
import struct
from pathlib import Path


def read(path):
    data=path.read_bytes()
    if len(data)!=16+4096+636*4:
        raise ValueError(f'{path}: invalid snapshot length')
    magic,version,first,last=struct.unpack_from('<4I',data)
    if magic!=0x4D564450 or version!=1 or first>last or last>0xFFFFFF:
        raise ValueError(f'{path}: invalid snapshot header')
    return (first,last),struct.unpack_from('<1024I',data,16),struct.unpack_from('<636I',data,4112)


def floats(words):
    return [v if math.isfinite(v) else str(v)
            for v in struct.unpack('<4f',struct.pack('<4I',*words))]


def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('directory',type=Path)
    args=parser.parse_args()
    report=[]
    for path in sorted(args.directory.glob('motion_inputs_p1_mesh*.bin')):
        second=path.with_name(path.name.replace('_p1_','_p2_'))
        if not second.exists():
            continue
        bounds,a,dep_a=read(path)
        bounds_b,b,dep_b=read(second)
        changed=[]
        active_first,active_last=dep_a[2:4]
        for row in range(256):
            old,new=a[4*row:4*row+4],b[4*row:4*row+4]
            if old!=new:
                changed.append({'constant':row,'components':['xyzw'[c] for c in range(4) if old[c]!=new[c]],
                    'before':floats(old),'after':floats(new),
                    'in_world_rows':active_first<=row<=active_last})
        report.append({'mesh':path.stem.split('_mesh')[1],'vertex_range_before':bounds,
            'vertex_range_after':bounds_b,'world_rows_before':dep_a[2:4],'world_rows_after':dep_b[2:4],
            'instance_data_before':floats(a[16:20]),'instance_data_after':floats(b[16:20]),
            'changed_world_dependency_words':[i for i in range(636) if dep_a[i]!=dep_b[i]],
            'changed_constants':changed})
    output=args.directory/'input_inspection.json'
    output.write_text(json.dumps(report,indent=2,allow_nan=False),encoding='utf-8')
    # Keep terminal output bounded; the JSON preserves all 256 rows per pair.
    for item in report:
        print(json.dumps({k:v for k,v in item.items() if k!='changed_constants'}))
        print(json.dumps({'changed_world_rows':[r for r in item['changed_constants'] if r['in_world_rows']]}))
    print(f'{len(report)} frozen pairs; full evidence: {output.resolve()}')


if __name__=='__main__':
    main()
