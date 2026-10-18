"""Serial site-depth admission, controlled operator development, training judges."""
import study
import site_admission

binary=study.RAW/'site-final-bench'
variants={
    'baseline':{'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1'},
    'phase_gate':{'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1','GAGP_GPU_UNIFORM_PHASE_SITES':'1','GAGP_GPU_SOURCE_CROSSOVER_GATE':'1'},
    'site7':{'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1','GAGP_GPU_UNIFORM_PHASE_SITES':'1','GAGP_GPU_SOURCE_CROSSOVER_GATE':'1','GAGP_GPU_SITE_DEPTH':'7'},
    'site7_parsimony':{'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1','GAGP_GPU_UNIFORM_PHASE_SITES':'1','GAGP_GPU_SOURCE_CROSSOVER_GATE':'1','GAGP_GPU_SITE_DEPTH':'7','GAGP_GPU_SITE_PARSIMONY':'1'},
}
stage=study.Stage('site-development',binary)
for seed in range(5):
    for index,task in enumerate(study.TASKS):
        entry,inp=study.old_input(task,seed)
        labels=list(variants);shift=(index+seed)%len(labels)
        for variant in labels[shift:]+labels[:shift]:
            study.search_job(stage,task,seed,variant,inp,entry['grammar'],entry['split']['train_count'],
                             extra=variants[variant],checkpoints=seed==0 and variant in ('site7','site7_parsimony'))
stage.m['status']='complete';stage.persist()
judges=study.Stage('site-training-judges',binary)
rows=[]
for tag,job in stage.m['jobs'].items():
    row=dict(job=tag,task=job['task'],seed=job['seed'],variant=job['variant'],status=job['status'])
    if job['status']=='ok':
        entry,inp=study.old_input(job['task'],job['seed'])
        output=judges.out/(tag+'.json')
        result=judges.run(tag,['quality-source',inp['source'],stage.out/(tag+'.export.json'),entry['grammar'],output],
                          {'GAGP_BM_CASE_COUNT':str(job['cases'])})
        row['status']=result['status']
        if result['status']=='ok':
            reference=study.read(output)['final']
            row.update(training_wrong=reference['wrong'],training_accuracy=reference['exact_accuracy'])
    rows.append(row)
study.save(judges.out/'summary.json',dict(rows=rows,variants=variants,scope='training only'))
judges.m['status']='complete';judges.persist()
