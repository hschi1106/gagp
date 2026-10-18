"""Full training scoring and bounded behavior probes on evolved checkpoints."""
import json
import os
from pathlib import Path
import statistics
import time

import study

record=study.RAW/'checkpoint-metrics-controller.json'
if record.exists():raise RuntimeError('checkpoint metrics attempt exists')
study.save(record,dict(pid=os.getpid(),status='waiting',started_epoch=time.time()))
while True:
    paths=[study.RAW/name/'manifest.json' for name in ('checkpoint-audit','site-development')]
    if all(p.exists() and study.read(p)['status']=='complete' for p in paths):break
    if time.time()>study.DEADLINE-8*3600:raise RuntimeError('checkpoint prerequisite missed deadline')
    time.sleep(15)
stage=study.Stage('checkpoint-metrics',study.RAW/'raw-score-bench',study.read(study.RAW/'baseline-settings.json'),strict=True)
rows=[]
for task in study.TASKS:
    entry,inp=study.old_input(task,0)
    for variant in ('asgp','baseline','site7','site7_parsimony'):
        for generation in (0,8,32,64):
            source=Path(inp['source'])
            if generation==0:prepared=Path(inp['prepared'])
            elif variant=='asgp':
                folder=study.RAW/'checkpoint-audit';tag=f'{task}.asgp.g{generation}'
                source=folder/(tag+'.source.json');prepared=folder/(tag+'.prepared.json')
            else:
                phase='development' if variant=='baseline' else 'site-development'
                prepared=study.RAW/phase/f'{task}.s0.{variant}.g64.checkpoints'/f'gen{generation}.native.json'
            tag=f'{task}.{variant}.g{generation}'
            row=dict(task=task,variant=variant,generation=generation,requested_population=1024)
            if not prepared.exists():
                row.update(status='missing_checkpoint');rows.append(row);continue
            asts=study.read(prepared)['programs'];population=len(asts)
            row.update(population=population,rejected_from_population=1024-population)
            if not population:
                row.update(status='no_admitted_candidates');rows.append(row);continue
            output=stage.out/(tag+'.json')
            job=stage.run(tag,['measure',source,prepared,entry['grammar'],population,'snapshot',output],
                dict(GAGP_BM_CASE_COUNT=str(entry['split']['train_count']),GAGP_SNAPSHOT_CPU='1',
                     GAGP_SNAPSHOT_NATIVE_OWNER='1',GAGP_SNAPSHOT_SITE_METRICS='1',GAGP_SNAPSHOT_BEHAVIOR='1',
                     GAGP_GPU_PRIVATE_TUPLE_SCORE='1'),timeout=600)
            job.update(task=task,variant=variant,generation=generation,output=str(output));stage.persist()
            row['status']=job['status']
            if job['status']=='ok':
                doc=study.read(output);members=doc['programs']
                assert doc['population']==population and len(members)==population
                row.update(cases=doc['cases'],cpu_gpu_fitness_mismatches=sum(study.canonical(m['fitness'])!=study.canonical(m['cpu_fitness']) for m in members),
                    cpu_gpu_wrong_mismatches=sum(m['wrong']!=m['cpu_wrong'] for m in members),
                    raw_max_abs_error=max(abs(m['fitness']-m['cpu_fitness']) for m in members),
                    unscored=sum(m['unscored'] for m in members),
                    mean_physical_nodes=statistics.mean(m['nodes'] for m in members),
                    mean_grammar_sites=statistics.mean(m['grammar_site_nodes'] for m in members),
                    max_grammar_site_depth=max(m['grammar_site_depth'] for m in members),
                    unique_serialized_ast=len({json.dumps(ast,sort_keys=True,separators=(',',':')) for ast in asts}),
                    unique_cpu_behavior_probes=doc['unique_cpu_behavior_probes'],
                    cpu_behavior_probe_cases=doc['cpu_behavior_probe_cases'],output_sha256=study.sha(output))
            rows.append(row)
    study.save(stage.out/'summary.json',dict(rows=rows,scope='old training only; bounded probes use first32 cases; ASGP evolved populations may lose native-unadmitted candidates, reported explicitly'))
stage.m.update(status='complete',finished_epoch=time.time());stage.persist()
study.save(record,dict(pid=os.getpid(),status='complete',finished_epoch=time.time()))
