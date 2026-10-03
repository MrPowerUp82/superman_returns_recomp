"""Compile and inspect the local corpus; generated game data remains untracked."""
import argparse,concurrent.futures,json,shutil,subprocess,sys
from pathlib import Path
from compile_vulkan import Toolchain,compile_shader
from spirv_metadata import inspect_spirv,contract_errors
ROOT=Path(__file__).resolve().parents[2]

def requirements(m,stage):
 rows=m['descriptors'];caps=m['capabilities']
 r={kind:sum(x['count'] for x in rows if x['type'].startswith(prefix)) for kind,prefix in [('storage_buffers','storage_buffer'),('uniform_buffers','uniform_buffer'),('sampled_images','sampled_image'),('samplers','sampler')]}
 r['descriptor_sets']=max((x['set']+1 for x in rows),default=0)
 r['vertex_attributes']=max((x['location']+1 for x in m['inputs'] if 'location' in x),default=0) if stage=='vs' else 0
 r.update(sampled_image_dynamic_indexing=29 in caps or any(x.get('dynamic_indexing') and x['type'].startswith('sampled_image') for x in rows),storage_buffer_dynamic_indexing=30 in caps or any(x.get('dynamic_indexing') and x['type']=='storage_buffer' for x in rows),clip_distance=32 in caps,cull_distance=33 in caps)
 return r

def inspect_one(raw,root,tools,validator):
 stage=raw.stem.rsplit('.',1)[-1];result=compile_shader(raw,stage,root,tools)
 row={'container':raw.name,'stage':stage,'status':result.status,'cache_key':result.cache_key,'diagnostic':result.diagnostic}
 if result.status!='ready':return row
 try:
  m=inspect_spirv(result.binary_path.read_bytes());errors=contract_errors(m)
  expected=0 if stage=='vs' else 4
  if len(m['entrypoints'])!=1 or m['entrypoints'][0]['model']!=expected:errors.append('Entry point/stage mismatch')
  row['metadata']=m;row['requirements']=requirements(m,stage)
  if validator:
   v=subprocess.run([str(validator),'--target-env','vulkan1.1',str(result.binary_path)],capture_output=True,text=True,timeout=30)
   if v.returncode:errors.append('spirv-val: '+v.stderr)
  if errors:row.update(status='failed',diagnostic='; '.join(errors))
 except (ValueError,OSError,subprocess.TimeoutExpired) as e:row.update(status='failed',diagnostic=str(e))
 return row

def main():
 ap=argparse.ArgumentParser();ap.add_argument('--corpus',type=Path,default=ROOT/'artifacts/shaders');ap.add_argument('--output',type=Path,default=ROOT/'artifacts/shaders/vulkan-m2/report.json')
 ap.add_argument('--emitter',type=Path,default=ROOT/'build/vulkan-m2/emitter-build/XenosRecompCorpus.exe');ap.add_argument('--common',type=Path,default=ROOT/'build/vulkan-m2/emitter-tree/src/XenosRecomp/shader_common.h');ap.add_argument('--dxc',type=Path,default=ROOT/'.tools/dxc/bin/x64/dxc.exe');ap.add_argument('--jobs',type=int,default=4)
 a=ap.parse_args();raws=sorted((a.corpus/'raw').glob('*.bin'))
 if not raws:ap.error('No local shader containers; empty corpus is not a pass')
 if a.jobs<1 or a.jobs>32:ap.error('jobs must be 1..32')
 tools=Toolchain(a.emitter,a.common,a.dxc)
 for path in (tools.emitter,tools.common,tools.dxc):
  if not path.is_file():ap.error('Missing tool '+str(path))
 validator=shutil.which('spirv-val') or shutil.which('spirv-val.exe')
 with concurrent.futures.ThreadPoolExecutor(max_workers=a.jobs) as pool:rows=list(pool.map(lambda raw:inspect_one(raw,a.output.parent,tools,validator),raws))
 good=sum(r['status']=='ready' for r in rows)
 maxima={}
 for row in rows:
  for name,value in row.get('requirements',{}).items():maxima[name]=max(maxima.get(name,0),value)
 report={'schema':1,'abi':'sr-vulkan-buffers-v1','total':len(rows),'ready':good,'failed':len(rows)-good,'external_validation':str(validator) if validator else 'spirv-val absent; no external validator claim','maximum_requirements':maxima,'shaders':rows}
 a.output.parent.mkdir(parents=True,exist_ok=True);a.output.write_text(json.dumps(report,indent=2),encoding='utf-8')
 print(f'Vulkan corpus: {good}/{len(rows)} ready, {len(rows)-good} failed. {report["external_validation"]}')
 for row in rows:
  if row['status']!='ready':print(row['container'],row['diagnostic'][:300])
 return 0 if good==len(rows) else 1
if __name__=='__main__':sys.exit(main())
