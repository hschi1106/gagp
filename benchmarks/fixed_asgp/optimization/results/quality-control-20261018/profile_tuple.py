"""Profile the execution-only tuple optimization; never use these times as speedups."""
import study
stage=study.Stage('profile-tuple',study.RAW/'raw-score-bench')
for task in ('is_sorted','all_equal'):
    entry,inp=study.old_input(task,0)
    prefix=['nsys','profile','--trace=cuda,nvtx','--sample=none','--cpuctxsw=none',
            '-o',str(stage.out/task)]
    study.search_job(stage,task,0,'profile-tuple',inp,entry['grammar'],entry['split']['train_count'],
                     extra={'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1'},prefix=prefix)
stage.m['status']='complete';stage.persist()
