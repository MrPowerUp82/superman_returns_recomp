"""RTX/NGX integration probe. Quality must evaluate, composite and complete on GPU."""
import argparse,datetime,hashlib,json,pathlib,re,subprocess

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--mode',choices=['off','quality','saved'],default='quality')
    p.add_argument('--name',default=datetime.datetime.now().strftime('dlss_%Y%m%d_%H%M%S'))
    p.add_argument('--capture-swap',type=int,default=0)
    p.add_argument('--with-rt',action='store_true')
    p.add_argument('--linear-output',action='store_true',help='Disable display gamma for pixel comparisons')
    a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+',a.name) or a.capture_swap<0: p.error('invalid name or swap')
    root=pathlib.Path(__file__).resolve().parents[2]
    exe=root/'port/out/build/win-amd64-release/superman_returns.exe'
    dest=root/'logs'/a.name;dest.mkdir(exist_ok=False)
    running=subprocess.run(['powershell','-NoProfile','-Command',
        '@(Get-Process superman_returns -ErrorAction SilentlyContinue).Id'],capture_output=True,text=True)
    if running.stdout.strip(): raise SystemExit('An existing game process must finish first')
    command=[str(exe),f'--game_data_root={root/"game"}',f'--log_file={dest/"game.log"}',
        '--sr_renderer=native','--sr_native_api=d3d12',
        f'--sr_rt_shadows={str(a.with_rt).lower()}',f'--sr_character_shadows={str(a.with_rt).lower()}',
        f'--sr_rt_reflections={str(a.with_rt).lower()}',
        '--sr_benchmark=true','--sr_benchmark_headless=true','--sr_benchmark_warmup_s=5',
        '--sr_benchmark_measure_s=30','--sr_native_fps_limit=0','--vsync=false','--sr_lsfg=false']
    if a.mode!='saved': command += [f'--sr_dlss={a.mode}']
    if a.capture_swap:
        command += [f'--sr_native_ab_swaps={a.capture_swap}',f'--sr_native_dump_dir={dest/"capture"}']
    if a.linear_output: command += ['--sr_native_gamma_ramp=false']
    with (dest/'stdout.log').open('wb') as out,(dest/'stderr.log').open('wb') as err:
        result=subprocess.run(command,cwd=exe.parent,stdout=out,stderr=err,timeout=180,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    log=(dest/'game.log').read_text(errors='replace')
    output=(dest/'stdout.log').read_text(errors='replace')
    match=re.search(r'json=(.+\.json)',output)
    benchmark=json.loads(pathlib.Path(match[1].strip()).read_text()) if match else {}
    events=[line for line in log.splitlines() if 'native DLSS' in line]
    healthy=not result.returncode and benchmark.get('valid',False)
    if a.mode in ('quality','saved'):
        healthy=healthy and all(s in log for s in ['native DLSS evaluation recorded:',
            'native DLSS output composited','native DLSS GPU execution confirmed:'])
        healthy=healthy and 'native DLSS evaluation failed' not in log
        healthy=healthy and any('native DLSS frames:' in line and 'applied=2' in line for line in events)
    else:
        healthy=healthy and 'native DLSS evaluation recorded:' not in log
    if a.capture_swap:
        if not list((dest/'capture').glob('*.raw')): healthy=False
        if a.mode!='off' and not list((dest/'capture').glob('*_dlss_upscaled.raw')): healthy=False
    report={'mode':a.mode,'rt_requested':a.with_rt,'linear_output':a.linear_output,'valid':bool(healthy),'exit_code':result.returncode,'benchmark':benchmark,
            'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'events':events}
    (dest/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'{a.mode}: valid={bool(healthy)}, FPS={benchmark.get("avg_fps")}, {dest/"result.json"}')
    if not healthy: raise SystemExit(1)

if __name__=='__main__': main()
