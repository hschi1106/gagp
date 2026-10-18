"""Judge development champions on training only; no fresh held-out access."""
import study
from pathlib import Path

stage=study.Stage('development-training-judges',study.RAW/'diagnostic-bench')
development=study.read(study.RAW/'development/manifest.json')
assert development['status']=='complete'
rows=[]
for tag,job in development['jobs'].items():
    if job['status']!='ok':
        rows.append(dict(job=tag,status=job['status']));continue
    entry,inp=study.old_input(job['task'],job['seed'])
    export=Path(job['output']).with_suffix('.export.json')
    out=stage.out/(tag+'.judge.json')
    result=stage.run(tag,['quality-source',inp['source'],export,entry['grammar'],out],
                     {'GAGP_BM_CASE_COUNT':str(job['cases'])})
    row=dict(job=tag,task=job['task'],seed=job['seed'],variant=job['variant'],status=result['status'])
    if result['status']=='ok':
        reference=study.read(out)['final']
        row.update(training_wrong=reference['wrong'],training_accuracy=reference['exact_accuracy'])
    rows.append(row)
study.save(stage.out/'summary.json',dict(rows=rows,scope='training only; no held-out data used'))
stage.m['status']='complete';stage.persist()
