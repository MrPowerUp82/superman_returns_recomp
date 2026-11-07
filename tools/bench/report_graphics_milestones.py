"""Summarize three real benchmark rounds without silently pooling missing runs."""
import argparse,csv,hashlib,json,math,re,statistics
from pathlib import Path

def frames(path):
 with path.open(newline='',encoding='utf-8-sig') as stream:
  values=[float(row['frame_ms']) for row in csv.DictReader(stream)]
 if not values or any(not math.isfinite(v) or v<=0 for v in values):raise ValueError('Invalid frame CSV: '+str(path))
 ordered=sorted(values);n=len(values)
 return {'samples':n,'mean_ms':statistics.mean(values),'stddev_ms':statistics.pstdev(values),
   'p50_ms':ordered[math.ceil(n*.50)-1],'p95_ms':ordered[math.ceil(n*.95)-1],
   'p99_ms':ordered[math.ceil(n*.99)-1],'p999_ms':ordered[math.ceil(n*.999)-1],
   'stutters_over_100ms':sum(v>100 for v in values),
   'low1_fps':1000/statistics.mean(ordered[-max(1,math.ceil(n*.01)):]),
   'low01_fps':1000/statistics.mean(ordered[-max(1,math.ceil(n*.001)):])}

def average(runs,key):
 values=[row.get(key) for row in runs]
 return statistics.mean(values) if all(v is not None for v in values) else None

def main():
 parser=argparse.ArgumentParser(description=__doc__)
 parser.add_argument('--prefix',required=True);parser.add_argument('--out',type=Path,required=True)
 parser.add_argument('--groups',nargs='+',choices=['m1_camera','m1_objects','m2_dxr','vulkan'],
                     default=['m1_camera','m1_objects','m2_dxr','vulkan'])
 parser.add_argument('--pacing-matrix',type=Path,help='Completed presentation matrix; its recorded display rate is retained')
 args=parser.parse_args();root=Path(__file__).resolve().parents[2]
 if not re.fullmatch(r'[A-Za-z0-9_-]+',args.prefix):parser.error('Invalid prefix')
 report={'schema':1,'meaning':'Arithmetic means of exactly three per-run metrics; no claim of physical scanout','groups':{},'pacing':{},'errors':[]}
 report['selected_groups']=args.groups
 for label in args.groups:
  runs=[]
  for n in range(1,4):
   folder=root/'logs'/f'{args.prefix}_{label}_r{n}'
   if not (folder/'result.json').exists():report['errors'].append('missing '+folder.name);continue
   result=json.loads((folder/'result.json').read_text());row=frames(folder/'frame_times.csv')
   row.update(round=n,path=str(folder),valid=result['valid'],avg_fps=result['benchmark']['avg_fps'],exe_sha256=result['exe_sha256'])
   log=(folder/'game.log').read_text(errors='replace')
   matches=re.findall(r'route_end name=\S+ frames=(\d+) draws=(\d+) pso_runtime_creates=(\d+) shader_runtime_compiles=(\d+) shader_misses=(\d+) valid=(\d+)',log)
   row['cache_counters']=dict(zip(['frames','draws','pso_runtime_creates','shader_runtime_compiles','shader_misses','valid'],map(int,matches[-1]))) if matches else None
   runs.append(row)
  complete=len(runs)==3 and all(r['valid'] for r in runs) and len({r['exe_sha256'] for r in runs})==1
  report['groups'][label]={'complete':complete,'runs':runs,'mean_of_three':{k:average(runs,k) for k in ['avg_fps','mean_ms','stddev_ms','p50_ms','p95_ms','p99_ms','p999_ms','low1_fps','low01_fps','stutters_over_100ms']} if complete else {}}
 matrix=args.pacing_matrix or root/'logs'/f'{args.prefix}_pacing_60hz/matrix.json'
 if matrix.exists():
  data=json.loads(matrix.read_text());report['pacing']['exe_sha256']=data['exe_sha256']
  report['pacing']['source']=str(matrix.resolve())
  report['pacing']['display']=data.get('display',data.get('display_mode'))
  for fps in [30,60,120]:
   report['pacing'][str(fps)]={}
   for mechanism in ['legacy','dedicated']:
    runs=[r for r in data['runs'] if r['target_fps']==fps and r['mechanism']==mechanism]
    rows=[]
    for r in runs:
     folder=Path(r['summary']['path']).parent;row=frames(folder/'frame_times.csv');row.update(avg_fps=r['benchmark'].get('avg_fps'),round=r['round'])
     row['presentation']=r['summary'];rows.append(row)
    complete=len(rows)==3
    report['pacing'][str(fps)][mechanism]={'complete':complete,'runs':rows,
      'mean_of_three':{k:average(rows,k) for k in ['avg_fps','mean_ms','stddev_ms','p50_ms','p95_ms','p99_ms','p999_ms','low1_fps','low01_fps','stutters_over_100ms']} if complete else {},
      'presentation_mean_of_three':{k:average([r['presentation'] for r in rows],k) for k in ['accepted_api_fps','unique_source_api_fps','unique_source_interval_stddev_ms','unique_source_interval_p99_ms','interval_stddev_ms','interval_p99_ms','interval_p999_ms','low1_api_fps','low01_api_fps']} if complete else {}}
 else:report['errors'].append('missing presentation matrix')
 args.out.parent.mkdir(parents=True,exist_ok=True);args.out.write_text(json.dumps(report,indent=2)+'\n',encoding='utf-8')
 print(json.dumps({k:{'complete':v['complete'],**v['mean_of_three']} for k,v in report['groups'].items()},indent=2))
 print('Evidence: '+str(args.out.resolve()))
if __name__=='__main__':main()
