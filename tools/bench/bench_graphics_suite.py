"""Run exactly three built-in benchmark rounds per selected graphics configuration.

Captures are separate runs: GPU readbacks never perturb the performance window.
The presentation matrix has its own bench_presentation.py entry point.
"""
import argparse, hashlib, json, os, re, subprocess, sys
from pathlib import Path


def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--prefix',required=True)
 parser.add_argument('--groups',nargs='+',choices=['camera','objects','dxr','vulkan'],default=['camera','objects','dxr','vulkan'])
 parser.add_argument('--captures',action='store_true')
 args=parser.parse_args()
 if not re.fullmatch(r'[A-Za-z0-9_-]+',args.prefix):parser.error('Invalid prefix')
 root=Path(__file__).resolve().parents[2]
 exe=root/'port/out/build/win-amd64-release/superman_returns.exe'
 identity=hashlib.sha256(exe.read_bytes()).hexdigest()
 def run(arguments,environment=None):
  if hashlib.sha256(exe.read_bytes()).hexdigest()!=identity:raise SystemExit('Executable changed during validation')
  name=arguments[arguments.index('--name')+1]
  existing=root/'logs'/name/'result.json'
  if existing.exists():
   previous=json.loads(existing.read_text())
   if not previous.get('valid') or previous.get('exe_sha256')!=identity:raise SystemExit('Cannot reuse invalid/different-build evidence: '+name)
   print('REUSE '+name,flush=True);return
  subprocess.run([sys.executable,'tools/bench/bench_dlss.py',*arguments],cwd=root,env=environment,check=True)
 modes={'camera':[], 'objects':['--object-motion'], 'dxr':['--object-motion','--with-rt']}
 if args.captures:
  for group in args.groups:
   if group=='vulkan':continue
   name=args.prefix+'_capture_'+group
   run(['--mode','quality','--name',name,'--fps','30','--measure','10','--capture-swap','120','--linear-output',*modes[group]])
   capture=root/'logs'/name/'capture'
   subprocess.run([sys.executable,'tools/analysis/check_dlss_capture.py',str(capture)],cwd=root,check=True)
   if group!='camera':subprocess.run([sys.executable,'tools/graphics/inspect_temporal_motion.py',str(capture),'--depth-format','d24'],cwd=root,check=True)
 for number in range(1,4):
  for group in (args.groups if number%2 else list(reversed(args.groups))):
   label={'camera':'m1_camera','objects':'m1_objects','dxr':'m2_dxr','vulkan':'vulkan'}[group]
   name=f'{args.prefix}_{label}_r{number}'
   if group=='vulkan':
    environment=os.environ.copy();environment['SR_VULKAN_PROFILE']='1'
    run(['--api','vulkan','--mode','off','--name',name],environment)
   else:run(['--mode','quality','--name',name,*modes[group]])
 print('COMPLETE three-round groups '+','.join(args.groups)+' SHA256='+identity,flush=True)

if __name__=='__main__':main()
