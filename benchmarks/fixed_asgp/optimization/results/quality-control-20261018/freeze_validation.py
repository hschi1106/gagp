"""Training-only global selection and conservative time-based scale freeze."""
from __future__ import annotations
from collections import defaultdict
from pathlib import Path
import subprocess
import time

import select_candidate
import study

# Fixed order before fresh held-out use: retain replication before reducing it.
SCALES=[(20,128),(20,64),(10,128),(10,64)]
SAFETY=1.5
REPORT_RESERVE=3*3600
PREPARATION_RESERVE=45*60


def observations():
    timing=defaultdict(list)
    for phase in ('development','site-development','guard-development-v2'):
        manifest=study.read(study.RAW/phase/'manifest.json')
        assert manifest['status']=='complete',phase+' incomplete'
        for tag,row in manifest['jobs'].items():
            assert row['status']=='ok',(phase,tag,row['status'])
            if 'split' in row:continue
            doc=study.read(row['output'])
            timing[phase,row['task'],row['variant']].append(doc['evolve_wall_ms']/1000)
    return timing


def forecast(timing,selected,variants,now):
    # CPU judge durations are timing metadata only; no previous test scores are read.
    old=study.read(study.ROOT/'logs/day24-20261017/manifest.json')
    previous_judge=defaultdict(list)
    for tag,row in old['jobs'].items():
        command=row['command']
        if len(command)>5 and command[1]=='quality-source' and row['status']=='ok':
            for task in study.TASKS:
                if '.'.join(('quality',task))+'.' in tag:
                    previous_judge[task].append(row['process_s'])
    bounds={}
    for task in study.TASKS:
        def maximum(phase,variant):
            values=timing[phase,task,variant]
            assert values,(phase,task,variant)
            return max(values)
        bounds[task]=dict(asgp=maximum('development','asgp'),
            gpu_base=maximum('development','baseline'),
            gpu_exec=maximum('site-development','baseline'))
        if 'gpu_operator' in variants:
            bounds[task]['gpu_operator']=maximum('site-development',selected)
        # Include the strongest screened CPU dose as a conservative cost proxy;
        # this is a forecast, not a worst-case execution-time guarantee.
        bounds[task]['gpu_guard']=max(maximum('site-development',selected),
            maximum('guard-development-v2','guard8_i8'),maximum('guard-development-v2','guard32_i1'))
    candidates=[]
    for seeds,generations in SCALES:
        search=sum(sum(row.values()) for row in bounds.values())*seeds*generations/64
        judge=sum(max(previous_judge[task] or [15]) for task in study.TASKS)*2*len(variants)*seeds*generations/64
        # Include process startup/preparation overhead for each search and judge.
        predicted=SAFETY*(search+judge+len(study.TASKS)*seeds*len(variants)*2)+PREPARATION_RESERVE+REPORT_RESERVE
        candidates.append(dict(seeds=seeds,generations=generations,search_s=search,judge_s=judge,
            total_s=predicted,finishes_before_deadline=now+predicted<study.DEADLINE))
    chosen=next((c for c in candidates if c['finishes_before_deadline']),None)
    if chosen is None:raise RuntimeError('minimum frozen scale does not fit original deadline; review actual partial coverage')
    return dict(selected=chosen,candidates=candidates,per_task_G64_upper_observed_s=bounds,
        safety_factor=SAFETY,preparation_reserve_s=PREPARATION_RESERVE,report_reserve_s=REPORT_RESERVE,
        rule='first feasible scale in 20xG128,20xG64,10xG128,10xG64; training timing only; never reset deadline',
        limitation='extrapolates G64 training trajectories; fresh trajectories can cost more')


def freeze():
    destination=study.BASE/'validation-freeze.json'
    if destination.exists():raise RuntimeError('formal freeze exists; do not overwrite')
    for phase in ('initial','tuple-pilot','site-admission','checkpoint-audit','site-training-judges',
                  'development-training-judges','validation-harness-smoke','guard-development-v2','checkpoint-metrics'):
        assert study.read(study.RAW/phase/'manifest.json')['status']=='complete',phase+' incomplete'
    selection=select_candidate.training_summary()
    selection_path=study.BASE/'candidate-selection.json'
    if selection_path.exists():assert study.read(selection_path)['selected']==selection['selected']
    else:study.save(selection_path,selection)
    selected=selection['selected'];policy=selection['variants'][selected]
    variants={
        'asgp':dict(mode='asgp_1t',extra={}),
        'gpu_base':dict(mode='gpu_overlap',extra={}),
        'gpu_exec':dict(mode='gpu_overlap',extra={'GAGP_GPU_PRIVATE_TUPLE_SCORE':'1'}),
    }
    if selected!='baseline':variants['gpu_operator']=dict(mode='gpu_overlap',extra=policy)
    guard=study.read(study.BASE/'guard-policy-plan.json')['arms']['guard8_i8']
    variants['gpu_guard']=dict(mode='gpu_overlap',extra=dict(policy,**guard))
    timing=observations();now=time.time();budget=forecast(timing,selected,variants,now)
    binary=study.RAW/'raw-score-bench';build=study.read(study.RAW/'current-build-source.json')
    assert study.sha(binary)==build['binary_sha256']
    for path,digest in build['source_sha256'].items():assert study.sha(study.ROOT/path)==digest,'runtime source changed'
    external=study.read(study.ROOT/'benchmarks/fixed_asgp/optimization/results/day24-20261017/final/asgp-source-hashes.json')
    for path,digest in external['files'].items():assert study.sha(Path(external['root'])/path)==digest,'external ASGP changed'
    paths=['study.py','run_validation.py','validation_data.py','freeze_validation.py','select_candidate.py',
           'protocol.json','analysis-plan.json','guard-policy-plan.json','validation-exclusions.json','validation_pipeline.py']
    harness={str(study.BASE/p):study.sha(study.BASE/p) for p in paths}
    harness[str(study.ROOT/'benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py')]=study.sha(study.ROOT/'benchmarks/fixed_asgp/optimization/results/day24-20261017/quality_data.py')
    harness[str(study.ROOT/'tools/gagp_tools/experiments/fixed_asgp/low_budget.py')]=study.sha(study.ROOT/'tools/gagp_tools/experiments/fixed_asgp/low_budget.py')
    scale=budget['selected']
    document=dict(format='gagp-quality-control-validation-v1',started_epoch=study.START,deadline_epoch=study.DEADLINE,
        frozen_epoch=now,binary=str(binary),binary_sha256=study.sha(binary),
        source_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=study.ROOT,text=True).strip(),
        runtime_source_sha256=build['source_sha256'],asgp_source=external,harness_sha256=harness,
        settings=study.read(study.RAW/'baseline-settings.json'),tasks=study.TASKS,
        seeds=list(range(5,5+scale['seeds'])),generations=scale['generations'],variants=variants,
        operator_selection=selected,candidate_selection_sha256=study.sha(selection_path),forecast=budget,
        data_seed=20261018,population=1024,train_unique_limit=200,test_unique_limit=1000,
        first_heldout_use='after this freeze: deterministic fresh pool preparation; no fresh test-guided settings',
        missing_policy='failed initial admission and all failed/budget-not-run searches/judges remain visible; no replacement seeds or reruns')
    study.save(destination,document)
    return destination


if __name__=='__main__':print(freeze())
