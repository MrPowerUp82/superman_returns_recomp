"""Summarize completed runs without treating experimental FPS as a guarantee."""
import argparse, collections, json, pathlib, re, statistics

def main():
    parser=argparse.ArgumentParser()
    parser.add_argument('reports',nargs='+',type=pathlib.Path)
    args=parser.parse_args()
    for path in args.reports:
        report=json.loads(path.read_text(encoding='utf-8'))
        print(f'\n{path}: SHA256 {report["exe_sha256"]}, profile={report["base_env"]["SR_NATIVE_CPU_PROFILE"]}')
        print('variant | n | median FPS [min,max] | median p99 ms | tail worker ms | tail tracking query ms/frame')
        for variant in report['variants']:
            runs=[r for r in report['runs'] if r['variant']==variant and r['status']=='complete']
            if not runs: continue
            fps=[r['benchmark']['avg_fps'] for r in runs]
            p99=[r['benchmark']['p99_ms'] for r in runs]
            worker=[];queries=[]
            for run in runs:
                values=[float(m.group(1)) for line in run.get('cpu_profiles',[]) if (m:=re.search(r'native CPU worker .*execute=([\d.]+)',line))]
                if values: worker.append(statistics.median(values[-3:]))
                tracking=run.get('tracking_profiles')
                if tracking is None:
                    log=path.parent/f'{run["block"]:02}_{variant}_game.log'
                    tracking=[line for line in log.read_text(errors='replace').splitlines() if 'native texture tracking: frame=' in line]
                if tracking:
                    pairs=dict(re.findall(r'(\w+)=(\d+)',tracking[-1]))
                    first=dict(re.findall(r'(\w+)=(\d+)',tracking[-4])) if len(tracking)>3 else {'frame':'0','query_ns':'0'}
                    queries.append((int(pairs['query_ns'])-int(first['query_ns']))/
                                   max(int(pairs['frame'])-int(first['frame']),1)/1e6)
            def median(values): return f'{statistics.median(values):.3f}' if values else '-'
            print(f'{variant} | {len(runs)} | {median(fps)} [{min(fps):.3f},{max(fps):.3f}] | {median(p99)} | {median(worker)} | {median(queries)}')
        other=collections.Counter(r['status'] for r in report['runs'] if r['status']!='complete')
        if other: print(f'Other run statuses: {dict(other)}. Completed-run summary only.')
        for run in report['runs']:
            profiles=run.get('arm_profiles',[])
            if run['status']=='complete' and profiles:
                a=dict(re.findall(r'(\w+)=(\d+)',profiles[-1]))
                pages=int(a['requested_alias_pages'])
                ratio=int(a['queries'])/pages if pages else 0
                print(f'Arm block {run["block"]} {run["variant"]}: calls={a["calls"]} '
                      f'requested_alias_pages={pages} queries={a["queries"]} protects={a["protects"]} '
                      f'queries/requested_page={ratio:.4f} total_ms={int(a["arm_ns"])/1e6:.3f} (last snapshot)')

if __name__=='__main__':
    main()
