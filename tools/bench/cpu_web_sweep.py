"""Sequential, checkpointed CPU hypothesis sweep using the embedded benchmark."""
import argparse, ctypes, datetime, hashlib, json, os, pathlib, re, subprocess, time
from ctypes import wintypes

def process_cpu_seconds(handle):
    """Read child kernel/user time before Popen closes its Windows handle."""
    values=[wintypes.FILETIME() for _ in range(4)]
    query=ctypes.windll.kernel32.GetProcessTimes
    query.argtypes=[wintypes.HANDLE]+[ctypes.POINTER(wintypes.FILETIME)]*4
    query.restype=wintypes.BOOL
    if not query(int(handle),*[ctypes.byref(value) for value in values]):
        raise ctypes.WinError()
    return sum((value.dwHighDateTime<<32)|value.dwLowDateTime for value in values[2:])*1e-7

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('--blocks',type=int,default=3)
    parser.add_argument('--suite',choices=['settings','query','compare','arm','arm_compare','arm_default'],default='settings')
    parser.add_argument('--cpu-profile',choices=['0','1'],default='1')
    parser.add_argument('--name',default=datetime.datetime.now().strftime('cpu_web_%Y%m%d_%H%M%S'))
    args=parser.parse_args()
    if args.blocks<1 or not re.fullmatch(r'[A-Za-z0-9_-]+',args.name):
        parser.error('positive block count and simple unique label required')
    if args.suite=='arm_default' and args.cpu_profile!='1':
        parser.error('arm_default requires --cpu-profile 1 to validate the selected path')
    root=pathlib.Path(__file__).resolve().parents[2]
    exe=root/'port/out/build/win-amd64-release/superman_returns.exe'
    dest=root/'logs'/args.name
    dest.mkdir(exist_ok=False)
    running=subprocess.run(['powershell','-NoProfile','-Command',
        "@(Get-Process superman_returns -ErrorAction SilentlyContinue).Id"],capture_output=True,text=True)
    if running.stdout.strip():
        raise SystemExit('An existing game process must finish first')
    variants={
        'settings':{'baseline':{},'shared_cache':{'SR_NATIVE_SHARED_CONSTANTS_CACHE':'1'},
                    'tracking_off':{'SR_D3D12_TEXTURE_WRITE_TRACKING':'off'}},
        'query':{'query_legacy':{},'query_admission':{'SR_NATIVE_SKIP_UNPUBLISHED_TEXTURE_QUERY':'1'}},
        'compare':{'compare_admission':{'SR_D3D12_TEXTURE_WRITE_TRACKING':'compare',
                                       'SR_NATIVE_SKIP_UNPUBLISHED_TEXTURE_QUERY':'1'}},
        'arm':{'arm_legacy':{'SR_TRACKING_BATCH_ARM':'0'},'arm_regions':{'SR_TRACKING_BATCH_ARM':'1'}},
        'arm_default':{'arm_default':{'SR_TRACKING_BATCH_ARM':None}},
        'arm_compare':{'arm_regions':{'SR_D3D12_TEXTURE_WRITE_TRACKING':'compare','SR_TRACKING_BATCH_ARM':'1'},
                       'arm_legacy':{'SR_D3D12_TEXTURE_WRITE_TRACKING':'compare','SR_TRACKING_BATCH_ARM':'0'}}}[args.suite]
    base={'SR_NATIVE_CPU_PROFILE':args.cpu_profile,'SR_NATIVE_CONSTANTS_PROFILE':args.cpu_profile,
          'SR_TRACKING_BATCH_ARM':'1','SR_TRACKING_ARM_PROFILE':args.cpu_profile,
          'SR_VULKAN_PROFILE':'0','SR_NATIVE_TEXTURE_AUDIT':'0',
          'SR_D3D12_TEXTURE_TRACKING_TIMING':'0','SR_NATIVE_STREAMS_PROFILE':'0',
          'SR_NATIVE_PM4_READ_PROFILE':'0',
          'SR_NATIVE_SHARED_CONSTANTS_CACHE':'0','SR_NATIVE_USED_CONSTANT_MASK':'0',
          'SR_NATIVE_SKIP_UNPUBLISHED_TEXTURE_QUERY':'0',
          'SR_NATIVE_TEXTURE_UPDATES':'0','SR_NATIVE_TEXTURE_STORAGE_REUSE':'1',
          'SR_D3D12_TEXTURE_WRITE_TRACKING':'on','SR_NATIVE_BUFFER_TRACKING':'1'}
    runtime=exe.parent/'rexruntime.dll'
    report={'suite':args.suite,'exe_sha256':hashlib.sha256(exe.read_bytes()).hexdigest(),
            'runtime_sha256':hashlib.sha256(runtime.read_bytes()).hexdigest(),
            'logical_processors':os.cpu_count(),'processor':os.environ.get('PROCESSOR_IDENTIFIER',''),
            'base_env':base,'variants':variants,'runs':[],
            'note':'tracking_off also disables buffer proofs; diagnostic bundle, not isolated texture-only change'}
    def save():
        temp=dest/'results.tmp'
        temp.write_text(json.dumps(report,indent=2),encoding='utf-8')
        temp.replace(dest/'results.json')
    save()
    for block in range(1,args.blocks+1):
        order=list(variants)
        if block%2==0: order.reverse()
        for variant in order:
            if hashlib.sha256(exe.read_bytes()).hexdigest()!=report['exe_sha256']:
                raise SystemExit('Executable changed during sweep; refusing mixed-build results')
            if hashlib.sha256(runtime.read_bytes()).hexdigest()!=report['runtime_sha256']:
                raise SystemExit('Runtime changed during sweep; refusing mixed-build results')
            label=f'{block:02}_{variant}'
            env=os.environ.copy();env.update(base)
            for key,value in variants[variant].items():
                if value is None: env.pop(key,None)
                else: env[key]=value
            command=[str(exe),f'--game_data_root={root / "game"}',
                f'--log_file={dest / (label+"_game.log")}',
                '--sr_renderer=native','--sr_native_api=d3d12',
                '--sr_benchmark=true','--sr_benchmark_headless=true',
                '--sr_benchmark_warmup_s=5','--sr_benchmark_measure_s=30',
                '--sr_native_fps_limit=0','--vsync=false','--window_width=1280',
                '--window_height=720','--sr_lsfg=false']
            run={'block':block,'variant':variant,'status':'running'}
            report['runs'].append(run);save()
            try:
                with (dest/(label+'_stdout.log')).open('wb') as out,(dest/(label+'_stderr.log')).open('wb') as err:
                    started=time.perf_counter()
                    with subprocess.Popen(command,cwd=exe.parent,env=env,stdout=out,stderr=err,
                            creationflags=subprocess.CREATE_NO_WINDOW) as child:
                        try:
                            run['exit_code']=child.wait(timeout=150)
                        except subprocess.TimeoutExpired:
                            child.kill();child.wait();raise
                        run['process_cpu_s']=process_cpu_seconds(child._handle)
                    run['process_wall_s']=time.perf_counter()-started
                if run['process_wall_s']>150:
                    raise RuntimeError('Host wall time exceeded 150s; suspend or scheduling delay invalidates comparison')
                output=(dest/(label+'_stdout.log')).read_text(errors='replace')
                match=re.search(r'json=(.+\.json)',output)
                if run['exit_code'] or not match:
                    raise RuntimeError('Benchmark failed or did not publish JSON')
                benchmark=json.loads(pathlib.Path(match[1].strip()).read_text())
                if not benchmark.get('valid'):
                    raise RuntimeError('Benchmark marked invalid')
                run['benchmark']=benchmark;run['status']='complete'
                log=(dest/(label+'_game.log')).read_text(errors='replace')
                run['cpu_profiles']=[line for line in log.splitlines() if 'native CPU ' in line]
                run['tracking_profiles']=[line for line in log.splitlines() if 'native texture tracking: frame=' in line]
                run['arm_profiles']=[line for line in log.splitlines() if 'memory tracking arm: calls=' in line]
                if args.suite=='arm_default':
                    if not run['arm_profiles']:
                        raise RuntimeError('Default-mode validation needs Arm profiling snapshots')
                    arm=dict(re.findall(r'(\w+)=(\d+)',run['arm_profiles'][-1]))
                    if not int(arm['protects']) or not int(arm['queries']) or int(arm['queries'])>int(arm['requested_alias_pages']):
                        raise RuntimeError('Default mode did not demonstrate region-based Arm')
                if 'native texture tracking: coverage mismatch' in log:
                    raise RuntimeError('Tracking audit mismatch; benchmark FPS cannot accept this variant')
                if args.suite in ('compare','arm_compare'):
                    counters=dict(re.findall(r'(\w+)=(\d+)',run['tracking_profiles'][-1]))
                    if not int(counters['checks']) or int(counters['unpublished_queries_skipped']):
                        raise RuntimeError('Compare mode lost its audit or entered the early reuse gate')
                print(f'{label}: {benchmark["avg_fps"]:.3f} FPS p99={benchmark["p99_ms"]:.3f}ms',flush=True)
            except Exception as error:
                run['status']='failed';run['error']=str(error);save();raise
            save()
    print(f'Completed {len(report["runs"])} runs: {dest / "results.json"}',flush=True)

if __name__=='__main__':
    main()
