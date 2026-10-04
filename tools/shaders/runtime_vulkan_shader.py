"""Bounded binary result for the native Vulkan runtime shader service."""
import argparse,os,struct
from pathlib import Path
from compile_vulkan import compile_shader,Toolchain
from spirv_metadata import inspect_spirv,contract_errors
from validate_vulkan_corpus import requirements

FIELDS=('storage_buffers','uniform_buffers','sampled_images','samplers','descriptor_sets',
        'vertex_attributes','vertex_output_components','fragment_input_components',
        'sampled_image_dynamic_indexing','storage_buffer_dynamic_indexing','clip_distance','cull_distance')
def word(n):return struct.pack('<I',int(n))
def text(s):
    b=s.encode('utf-8')[:16384]
    return word(len(b))+b
def failure(stage,diagnostic):
    return struct.pack('<4I',0x33525653,1,0,0 if stage=='vs' else 1)+text(diagnostic)
def ready(stage,binary):
    m=inspect_spirv(binary);errors=contract_errors(m)
    if len(m['entrypoints'])!=1 or m['entrypoints'][0]['model']!=(0 if stage=='vs' else 4):errors.append('Shader entry point/stage mismatch')
    if errors:raise ValueError('; '.join(errors))
    q=requirements(m,stage)
    result=struct.pack('<4I',0x33525653,1,1,0 if stage=='vs' else 1)
    result+=b''.join(word(q[k]) for k in FIELDS)
    for category in ('inputs','outputs'):
        rows=[r for r in m[category] if 'location' in r]
        if len(rows)>256 or len({r['location'] for r in rows})!=len(rows):raise ValueError('Duplicate/excessive shader locations')
        result+=word(len(rows))
        for r in rows:result+=word(r['location'])+text(r['type'])
    return result+word(len(binary))+binary
def main():
    p=argparse.ArgumentParser()
    for name in ('container','cache','emitter','common','dxc','result'):p.add_argument('--'+name,type=Path,required=True)
    p.add_argument('--stage',choices=('vs','ps'),required=True);a=p.parse_args()
    try:
        compiled=compile_shader(a.container,a.stage,a.cache,Toolchain(a.emitter,a.common,a.dxc))
        data=ready(a.stage,compiled.binary_path.read_bytes()) if compiled.status=='ready' else failure(a.stage,compiled.diagnostic)
    except (OSError,ValueError,TypeError) as e:data=failure(a.stage,str(e))
    a.result.parent.mkdir(parents=True,exist_ok=True);temporary=a.result.with_suffix('.pending')
    temporary.write_bytes(data);os.replace(temporary,a.result)
    return 0
if __name__=='__main__':raise SystemExit(main())
