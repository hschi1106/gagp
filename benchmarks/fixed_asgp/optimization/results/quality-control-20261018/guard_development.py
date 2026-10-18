"""Pre-heldout verification cost screen; all arms use old training only."""
import os
import time
import study
import run_validation

settings=study.read(study.RAW/'baseline-settings.json')
variants={name:dict(mode='gpu_overlap',extra=dict(GAGP_GPU_PRIVATE_TUPLE_SCORE='1',**extra))
          for name,extra in study.read(study.BASE/'guard-policy-plan.json')['arms'].items()}
stage=study.Stage('guard-development-v2',study.RAW/'raw-score-bench',settings,strict=True)
study.save(study.RAW/'guard-development-v2-controller.json',dict(pid=os.getpid(),started_epoch=time.time()))
freeze=dict(generations=64,variants=variants)
for index,task in enumerate(study.TASKS):
    entry,old=study.old_input(task,0)
    inp=dict(old,status='ok')
    current=dict(metadata=entry['split'],grammar=entry['grammar'])
    labels=list(variants);shift=index%len(labels)
    for variant in labels[shift:]+labels[:shift]:
        row=run_validation.measure(stage,freeze,task,0,variant,inp,current)
        run_validation.judge(stage,row,'train',current,inp)
stage.m.update(status='complete',finished_epoch=time.time());stage.persist()
