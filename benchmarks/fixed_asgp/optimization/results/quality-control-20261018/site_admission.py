"""Audit source logical size/depth against native grammar-invocation metrics."""
import study
from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS
stage=study.Stage('site-admission',study.RAW/'site-final-bench')
rows=[]
for task in PAPER_TASKS:
    entry,inp=study.old_input(task,0)
    env={'GAGP_BM_CASE_COUNT':str(entry['split']['train_count']),
         'GAGP_SNAPSHOT_CPU':'1','GAGP_SNAPSHOT_NATIVE_OWNER':'1','GAGP_SNAPSHOT_SITE_METRICS':'1'}
    for mode in ('asgp_snapshot','snapshot'):
        stage.run(task+'.'+mode,['measure',inp['source'],inp['prepared'],entry['grammar'],1024,mode,stage.out/(task+'.'+mode+'.json')],env)
    source=study.read(stage.out/(task+'.asgp_snapshot.json'))['programs']
    native=study.read(stage.out/(task+'.snapshot.json'))['programs']
    row=dict(task=task,programs=1024,node_mismatches=sum(a['nodes']!=b['grammar_site_nodes'] for a,b in zip(source,native)),
             depth_mismatches=sum(a['logical_depth']!=b['grammar_site_depth'] for a,b in zip(source,native)),
             maximum_source_depth=max(a['logical_depth'] for a in source),maximum_native_site_depth=max(a['grammar_site_depth'] for a in native))
    job=study.search_job(stage,task,0,'site7',inp,entry['grammar'],entry['split']['train_count'],generations=1,
        extra={'GAGP_GPU_SITE_DEPTH':'7','GAGP_GPU_SITE_PARSIMONY':'1','GAGP_GPU_UNIFORM_PHASE_SITES':'1',
               'GAGP_GPU_SOURCE_CROSSOVER_GATE':'1','GAGP_SEARCH_CPU_ALL':'1','GAGP_GPU_PRIVATE_TUPLE_SCORE':'1'})
    row['admission_status']=job['status'];rows.append(row)
    study.save(stage.out/'summary.json',dict(rows=rows))
stage.m['status']='complete';stage.persist()
