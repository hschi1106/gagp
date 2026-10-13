"""Summarize this checked experiment; reads archived results, runs no benchmarks."""
import csv
import hashlib
import json
import statistics as stat
from pathlib import Path

ROOT = Path(__file__).resolve().parents[5]
RAW = ROOT / 'logs/optimization/parallel-20261013'
OUT = Path(__file__).resolve().parent
MODES = ['asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap']
TASKS = ['house_robber', 'median']
def read(p): return json.loads(p.read_text())
def median(v): return stat.median(v)
def csv_write(name, rows):
    if not rows: return
    keys = list(dict.fromkeys(k for r in rows for k in r))
    with (OUT / name).open('w') as f:
        w = csv.DictWriter(f, fieldnames=keys); w.writeheader(); w.writerows(rows)

main = read(RAW/'final-main/manifest.json')
extra = read(RAW/'final-gpu-extra/manifest.json')
baseline = read(RAW/'baseline/manifest.json')
repeats, generations, summary = [], [], []
for task in TASKS:
    den = median([c['evolve_wall_ms'] for c in main['cells'] if c['task']==task and c['mode']=='asgp_1t'])
    procden = median([c['process_wall_s'] for c in main['cells'] if c['task']==task and c['mode']=='asgp_1t'])
    for mode in MODES:
        cells = [(name,c) for name,m in [('final-main',main),('final-gpu-extra',extra)] for c in m['cells'] if c['task']==task and c['mode']==mode]
        vals=[]; procs=[]; stages=[]
        for name,c in cells:
            r=read(Path(c['output'])); gs=r['generations']; vals.append(c['evolve_wall_ms']);procs.append(c['process_wall_s'])
            resources=Path(c['output']).with_suffix('.resources').read_text()
            rss=next((float(line.split('=')[1]) for line in resources.splitlines() if line.startswith('peak_rss_kib=')),None)
            repeats.append(dict(batch=name,tag=c['tag'],task=task,mode=mode,search_ms=c['evolve_wall_ms'],process_s=c['process_wall_s'],rss_kb=rss,final_best=r['final_best'],best_seen=max([g['best'] for g in gs]+[r['final_best']]),unique_final=r.get('unique_final_genomes'),mean_final_nodes=sum(x['nodes'] for x in r['final_population'])/len(r['final_population']) if r.get('final_population') else None,initial_admission_ms=r.get('initial_admission_ms'),final_eval_and_export_ms=r.get('final_eval_ms'),genotype_device_bytes=r.get('genotype_device_bytes'),audit_mismatches=r.get('cpu_audit_mismatches'),raw=c['output']))
            for g in gs:generations.append(dict(batch=name,tag=c['tag'],task=task,mode=mode,**g))
            stage={}
            for phase,rows in [('early8',gs[:8]),('late8',gs[-8:])]:
                stage[phase]={k:median([g[k] for g in rows if k in g]) for k in ['generation_ms','eval_ms','repro_ms','compile_ms','pack_ms','eval_kernel_ms','repro_kernel_ms','repro_decode_ms','upload_ms'] if any(k in g for g in rows)}
            stages.append(stage)
        old=next(c['evolve_wall_ms'] for c in baseline['cells'] if c['task']==task and c['mode']==mode)
        summary.append(dict(task=task,mode=mode,n=len(vals),search_median_ms=median(vals),search_range_ms=[min(vals),max(vals)],search_raw_ms=vals,asgp_search_ratio=den/median(vals),process_median_s=median(procs),asgp_process_ratio=procden/median(procs),initial_control_search_ms=old,change_ratio=old/median(vals),stage_medians_ms={p:{k:median([v[p][k] for v in stages]) for k in stages[0][p]} for p in stages[0]}))
csv_write('search-repeats.csv',repeats);csv_write('generations.csv',generations)
fixed = read(RAW/'final-fixed/manifest.json'); scaling=[]; formals=[]
for c in fixed['cells']:
    if c['tag'].startswith('scaling.'):
        scaling.append(dict(task=c['task'],population=c['population'],mode=c['mode'],median_ms=c['median_ms'],raw_ms=json.dumps([r['generation_ms'] for r in c['rows'] if r['rep']>=0]),raw=c['output']))
    if c['tag'].startswith('formal'):
        for r in c['rows']:
            if r['rep']>=0:formals.append(dict(task=c['task'],process=c['tag'],repeat=r['rep'],generation_ms=r['generation_ms']))
csv_write('scaling.csv',scaling);csv_write('fixed-repeats.csv',formals)
long=[]
if (RAW/'final-long/manifest.json').exists():
    for c in read(RAW/'final-long/manifest.json')['cells']:
        if c.get('status')!='ok':continue
        r=read(Path(c['output'])); gs=r['generations'];long.append(dict(task=c['task'],seed=c['seed'],generations=len(gs),search_ms=c['evolve_wall_ms'],best_seen=max([g['best'] for g in gs]+[r['final_best']]),final_best=r['final_best'],unique_final=r['unique_final_genomes'],mean_nodes=sum(x['nodes'] for x in r['final_population'])/1024,genotype_device_bytes=r['genotype_device_bytes'],errors=sum(g['errors'] for g in gs),timeouts=sum(g['timeouts'] for g in gs),fallbacks=sum(g['fallbacks'] for g in gs),unscored=sum(g['unscored'] for g in gs),operator_fallbacks=sum(g['operator_fallbacks'] for g in gs),audit_mismatches=r['cpu_audit_mismatches'],first8_ms=median([g['generation_ms'] for g in gs[:8]]),last8_ms=median([g['generation_ms'] for g in gs[-8:]]),raw=c['output']))
csv_write('long-search.csv',long)
profile=[]
for p in (RAW/'final-profile').glob('*_cuda_gpu_kern_sum.csv'):
    for row in csv.DictReader(p.open()):profile.append(dict(task=p.name.split('_cuda_')[0],diagnostic_only=True,**row))
csv_write('profile-kernels.csv',profile)
exact=[]
for c in main['cells']:
    old=RAW/'combined2-main'/(c['tag'].replace('.r1','.r0').replace('.r2','.r0')+'.final.json')
    new=Path(c['output']).with_suffix('.final.json')
    if old.exists() and new.exists():exact.append(dict(tag=c['tag'],same_final_export=read(old)==read(new)))
report=dict(runtime_commit='b60a3367b37727bcf897be9cd66c27038da4d774',manifest_source_head=main['source_head'],binary_sha256=main['binary_sha256'],settings=main['flags'],frozen_sha256=main['frozen_sha256'],contract=main['contract'],timing=main['timing'],search=summary,fixed=formals,scaling=scaling,long_search=long,export_match_to_audited_candidate=exact,raw_root=str(RAW))
(OUT/'final.json').write_text(json.dumps(report,indent=2)+'\n')
for r in summary:print(r['task'],r['mode'],round(r['search_median_ms'],3),round(r['asgp_search_ratio'],3))
