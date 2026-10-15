from pathlib import Path
import csv,json
from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read,save,sha
r=Path.cwd();b=r/'logs/all-task-repair-20261015';out=r/'benchmarks/fixed_asgp/optimization/results/repair-20261015/final-all30';out.mkdir(exist_ok=True)
m=read(b/'final-all30-g4/manifest.json');old=read(b/'benchmark-all-g4/manifest.json')
rows=[];gens=[];details={};counts={'cases':0,'errors':0,'timeouts':0,'fallbacks':0,'unscored':0,'operator_fallbacks':0,'gpu_donor_fallback':0,'cpu_audit_program_count':0,'cpu_audit_mismatches':0}
for task in PAPER_TASKS:
 c=next(x for x in m['cells'] if x['task']==task and x['phase']=='gpu_overlap.s0');control=next(x for x in old['cells'] if x['task']==task and x['phase']=='asgp_1t.s0')
 row={'task':task,'status':c['status'],'asgp_s':control.get('evolve_wall_ms',0)/1000,'gpu_ero_s':None,'speedup':None,'before_gpu_ero_s':None,'asgp_best':control.get('final_best'),'gpu_best':None}
 prev=next(x for x in old['cells'] if x['task']==task and x['phase']=='gpu_overlap.s0')
 if prev['status']=='ok':row['before_gpu_ero_s']=prev['evolve_wall_ms']/1000
 if c['status']=='ok':
  d=read(b/'final-all30-g4'/c['output']);row.update(gpu_ero_s=d['evolve_wall_ms']/1000,speedup=control['evolve_wall_ms']/d['evolve_wall_ms'],gpu_best=d['final_best'],unique_final_genomes=d['unique_final_genomes'],peak_host_rss_mib=c['peak_rss_bytes']/1048576,process_s=c['process_s'],cpu_audit_program_count=d.get('cpu_audit_program_count',0),cpu_audit_mismatches=d.get('cpu_audit_mismatches',0),reproduction_profile=d['reproduction_profile'],reproduction_fallback_reason=d.get('reproduction_fallback_reason',''),fully_aligned=d['search_contract']['fully_aligned'])
  detail={k:v for k,v in d.items() if k not in ['final_population','generations','top16_cpu']}
  for k in ['cases','errors','timeouts','fallbacks','unscored','operator_fallbacks','gpu_donor_fallback']:
   detail['generation_total_'+k]=sum(g.get(k,0) for g in d['generations']);counts[k]+=detail['generation_total_'+k]
  for k in ['cpu_audit_program_count','cpu_audit_mismatches']:counts[k]+=d.get(k,0)
  if prev['status']=='ok':
   p=read(b/'benchmark-all-g4'/prev['output']);detail['before_same_final_population_metrics']=p['final_population']==d['final_population'];detail['before_same_generation_best']=[g['best'] for g in p['generations']]==[g['best'] for g in d['generations']]
  for g in d['generations']:gens.append({'task':task,**g})
  details[task]=detail
 rows.append(row)
for name,data in [('speedups.csv',rows),('generations.csv',gens)]:
 fields=list(dict.fromkeys(k for x in data for k in x))
 with (out/name).open('w',newline='') as f:w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(data)
drift=[]
for c in m['cells']:
 if c['phase']=='asgp_1t.s0' and c['status']=='ok':
  oldc=next(x for x in old['cells'] if x['task']==c['task'] and x['phase']==c['phase']);drift.append({'task':c['task'],'reused_ms':oldc['evolve_wall_ms'],'same_round_ms':c['evolve_wall_ms'],'ratio_new_old':c['evolve_wall_ms']/oldc['evolve_wall_ms']})
save(out/'summary.json',{'contract':'P1024 C1024 G4 seed0 CPU1T native GPU reproduction; one cold process/task, own G4 ASGP controls reused; no precision claim','source_head':m['source_head'],'binary_sha256':m['binary_sha256'],'elapsed_s':m['elapsed_s'],'settings':m['settings'],'rows':rows,'counts':counts,'details':details,'drift_controls':drift})
save(out/'manifest.json',m)
for x in rows:print(f"{x['task']:27} {x['asgp_s']:8.3f} {str(round(x['gpu_ero_s'],3)) if x['gpu_ero_s'] else '-':>8} {str(round(x['speedup'],2)) if x['speedup'] else '-':>7} {x['status']}")
print('counts',counts);print('drift',drift)
