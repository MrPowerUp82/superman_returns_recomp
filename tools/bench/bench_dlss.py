"""RTX/NGX integration probe. Quality must evaluate, composite and complete on GPU."""
import argparse,datetime,hashlib,json,pathlib,re,subprocess,shutil,os

def main():
    p=argparse.ArgumentParser()
    p.add_argument('--api',choices=['d3d12','vulkan'],default='d3d12')
    p.add_argument('--extra-arg',action='append',default=[])
    p.add_argument('--mode',choices=['off','quality','balanced','performance','dlaa','saved'],default='quality')
    p.add_argument('--name',default=datetime.datetime.now().strftime('dlss_%Y%m%d_%H%M%S'))
    p.add_argument('--capture-swap',type=int,default=0)
    p.add_argument('--with-rt',action='store_true')
    p.add_argument('--rt-failure-interval',type=int,default=0)
    p.add_argument('--fps',type=int,choices=[0,30,60,120],default=0)
    p.add_argument('--warmup',type=float,default=5)
    p.add_argument('--measure',type=float,default=30)
    p.add_argument('--object-motion',action='store_true',help='Require continuous object motion to qualify and evaluate NGX; raster fallback fails this probe')
    p.add_argument('--linear-output',action='store_true',help='Disable display gamma for pixel comparisons')
    a=p.parse_args()
    if not re.fullmatch(r'[A-Za-z0-9_-]+',a.name) or a.capture_swap<0: p.error('invalid name or swap')
    if a.api=='vulkan' and (a.mode!='off' or a.object_motion or a.with_rt): p.error('Vulkan probe requires off and no D3D12-only features')
    root=pathlib.Path(__file__).resolve().parents[2]
    exe=root/'port/out/build/win-amd64-release/superman_returns.exe'
    dest=root/'logs'/a.name;dest.mkdir(exist_ok=False)
    running=subprocess.run(['powershell','-NoProfile','-Command',
        '@(Get-Process superman_returns -ErrorAction SilentlyContinue).Id'],capture_output=True,text=True)
    if running.stdout.strip(): raise SystemExit('An existing game process must finish first')
    command=[str(exe),f'--game_data_root={root/"game"}',f'--log_file={dest/"game.log"}',
        '--sr_renderer=native',f'--sr_native_api={a.api}','--window_width=1920','--window_height=1080','--sr_native_render_scale=1','--sr_native_msaa_samples=1','--sr_native_fxaa=false',
        f'--sr_rt_shadows={str(a.with_rt).lower()}',f'--sr_character_shadows={str(a.with_rt).lower()}',
        f'--sr_rt_reflections={str(a.with_rt).lower()}',
        '--sr_shadow_softness=2','--sr_benchmark=true',f'--sr_benchmark_cache_route={a.name}','--sr_benchmark_headless=true',f'--sr_benchmark_warmup_s={a.warmup}',
        f'--sr_benchmark_measure_s={a.measure}',f'--sr_native_fps_limit={a.fps}',f'--vsync={str(bool(a.fps)).lower()}','--sr_lsfg=false']
    if a.rt_failure_interval>0: command += [f'--sr_native_debug_visible_rt_failure_interval={a.rt_failure_interval}']
    if a.mode!='saved': command += [f'--sr_dlss={a.mode}']
    command += [f'--sr_native_object_motion={str(a.object_motion).lower()}']
    if a.capture_swap:
        command += [f'--sr_native_ab_swaps={a.capture_swap}',f'--sr_native_dump_dir={dest/"capture"}']
        if a.object_motion: command += [f'--sr_native_object_motion_capture_swap={a.capture_swap}']
    if a.linear_output: command += ['--sr_native_gamma_ramp=false']
    command += a.extra_arg
    env=os.environ.copy()
    if a.mode!='saved': env.setdefault('SR_LAUNCHER_SETTINGS',str(dest/'launcher.ini'))
    with (dest/'stdout.log').open('wb') as out,(dest/'stderr.log').open('wb') as err:
        result=subprocess.run(command,cwd=exe.parent,env=env,stdout=out,stderr=err,timeout=180,
                              creationflags=subprocess.CREATE_NO_WINDOW)
    log=(dest/'game.log').read_text(errors='replace')
    output=(dest/'stdout.log').read_text(errors='replace')
    match=re.search(r'json=(.+\.json)',output)
    benchmark=json.loads(pathlib.Path(match[1].strip()).read_text()) if match else {}
    if match:
        source=pathlib.Path(match[1].strip())
        shutil.copy2(source,dest/'benchmark.json')
        if source.with_suffix('.csv').exists(): shutil.copy2(source.with_suffix('.csv'),dest/'frame_times.csv')
    events=[line for line in log.splitlines() if 'native DLSS' in line]
    motion_events=[line for line in log.splitlines() if 'native object motion:' in line]
    unexpected_motion_diagnostics=sum(any(tag in line for tag in (
        'native surface sample:', 'native geometry sample:', 'native motion color copy:'))
        for line in log.splitlines()) if a.object_motion else 0
    healthy=not result.returncode and benchmark.get('valid',False)
    if a.mode!='off':
        healthy=healthy and all(s in log for s in ['native DLSS evaluation recorded:',
            'native DLSS output composited','native DLSS GPU execution confirmed:'])
        healthy=healthy and 'native DLSS evaluation failed' not in log
        healthy=healthy and any('native DLSS frames:' in line and f'applied={dict(quality=2,balanced=3,performance=4,dlaa=5,saved=2)[a.mode]}' in line for line in events)
    else:
        healthy=healthy and 'native DLSS evaluation recorded:' not in log
    if a.capture_swap:
        if not list((dest/'capture').glob('*.raw')): healthy=False
        if a.mode!='off' and not list((dest/'capture').glob('*_dlss_upscaled.raw')): healthy=False
    if a.with_rt:
        healthy=healthy and bool(re.search(r'native visible DXR recorded:.*shadows=true.*reflections=true',log)) and bool(re.search(r'native visible DXR executed:.*sun_pixels=[1-9][0-9]* reflection_pixels=[1-9][0-9]*',log))
    if a.object_motion:
        healthy=healthy and not unexpected_motion_diagnostics and a.mode!='off' and bool(motion_events) and any('reason=ready ' in line for line in motion_events)
    report={'mode':a.mode,'object_motion_requested':a.object_motion,'object_motion_events':motion_events,'unexpected_motion_diagnostics':unexpected_motion_diagnostics,'rt_requested':a.with_rt,'linear_output':a.linear_output,'valid':bool(healthy),'exit_code':result.returncode,'benchmark':benchmark,
            'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),'command':command,'events':events}
    (dest/'result.json').write_text(json.dumps(report,indent=2),encoding='utf-8')
    print(f'{a.mode}: valid={bool(healthy)}, FPS={benchmark.get("avg_fps")}, {dest/"result.json"}')
    if not healthy: raise SystemExit(1)

if __name__=='__main__': main()
