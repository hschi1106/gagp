"""Test the future formal runner only on old training cases; no new test data."""
import study
import run_validation

settings=study.read(study.RAW/'baseline-settings.json')
stage=study.Stage('validation-harness-smoke',study.RAW/'raw-score-bench',settings,strict=True)
stage.m['purpose']='harness only: old training cases, not a held-out quality result';stage.persist()
entry,old=study.old_input('count_odds',0)
inp=dict(old,status='ok')
current=dict(metadata=entry['split'],grammar=entry['grammar'])
freeze=dict(generations=2,variants={
    'source':dict(mode='asgp_1t',extra={}),
    'gpu':dict(mode='gpu_overlap',extra={'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1'}),
    'guard':dict(mode='gpu_overlap',extra={'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1','GAGP_VERIFY_TOP_K':'8','GAGP_VERIFY_INTERVAL':'2'}),
})
for variant in freeze['variants']:
    run=run_validation.measure(stage,freeze,'count_odds',0,variant,inp,current)
    assert run['status']=='ok',run
    judge=run_validation.judge(stage,run,'train',current,inp)
    assert judge['status']=='ok',judge
    original=(run['pid'],run['started_epoch'],run['output_sha256'])
    resumed=run_validation.measure(stage,freeze,'count_odds',0,variant,inp,current)
    assert original==(resumed['pid'],resumed['started_epoch'],resumed['output_sha256'])
# A shared test-source-shaped fixture has no initial population or seed. Reuse
# old TRAINING examples to validate its API shape without touching fresh test.
document=study.read(inp['source']);document.pop('population_seed',None);document['population']=[]
source=stage.out/'no-population.train.source.json';study.save(source,document)
export=stage.out/'count_odds.s0.source.export.json'
out=stage.out/'source.no-population.judge.json'
job=stage.run('source.no-population',['quality-source',source,export,entry['grammar'],out],
              {'GAGP_BM_CASE_COUNT':str(current['metadata']['train_count'])})
assert job['status']=='ok' and study.read(out)['cases']==current['metadata']['train_count']
stage.m['status']='complete';stage.persist()
