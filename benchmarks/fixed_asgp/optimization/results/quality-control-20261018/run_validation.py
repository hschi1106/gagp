"""Frozen-candidate, fresh-seed validation. Requires an explicit pre-test freeze."""
from __future__ import annotations

from argparse import ArgumentParser
import json
import math
import os
from pathlib import Path
import time

import study
import validation_data


def validate_run(data, mode, population, cases, generations):
    assert data['population']==population and data['cases']==cases
    assert len(data['generations'])==generations and len(data['final_population'])==population
    assert math.isfinite(data['final_best']) and data['evolve_wall_ms']>0
    assert all(r['expected_program_cases']==population*cases for r in data['generations'])
    if mode=='gpu_overlap':
        assert data['reproduction_profile'].startswith('native-gpu-phase-')
        assert not data['reproduction_fallback_reason']
        assert all(r['cases']==population*cases and r['unscored']==0 and r['eval_kernel_ms']>0
                   and r['repro_kernel_ms']>0 for r in data['generations'])
        assert max(r['nodes'] for r in data['final_population'])<=2048
        # Fidelity is an observed result, separate from process/work completion.
        # Partial CPU verification does not certify every unverified candidate.
        assert data['cpu_audit_program_count']==16
        assert data['cpu_audit_wrong_program_count']==16


def missing_record(stage,tag,status,metadata):
    if tag not in stage.m['jobs']:
        stage.m['jobs'][tag]=dict(status=status,**metadata)
        stage.persist()
    return stage.m['jobs'][tag]


def measure(stage,freeze,task,seed,variant,inp,task_input):
    tag=f'{task}.s{seed}.{variant}'
    spec=freeze['variants'][variant]
    count=task_input['metadata']['train_count']
    mode=spec['mode']
    metadata=dict(task=task,seed=seed,variant=variant,mode=mode)
    if not inp.get('source'):
        return missing_record(stage,tag,'not_run_input_preparation_failure',metadata)
    if inp['status']!='ok' and mode!='asgp_1t':
        return missing_record(stage,tag,'not_run_initial_admission_failure',metadata)
    output=stage.out/(tag+'.json')
    export=stage.out/(tag+'.export.json')
    history=stage.out/(tag+'.history.json')
    extra=dict(spec['extra'],GAGP_BM_CASE_COUNT=str(count),GAGP_BM_SEED=str(seed),
               GAGP_SEARCH_GENERATIONS=str(freeze['generations']),
               GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_HISTORY_EXPORT=str(history),
               GAGP_SEARCH_EXPORT_BEST_ONLY='1')
    # Source mode never opens prepared native ASTs; keep an existing explicit
    # placeholder if native initial admission failed. Failure remains visible.
    prepared=inp['prepared'] if inp['status']=='ok' else inp['source']
    job=stage.run(tag,['measure',inp['source'],prepared,task_input['grammar'],1024,'search_'+mode,output],
                  extra,timeout=3600,reserve=10800)
    for field,path in (('output',output),('export',export),('history',history)):
        if field+'_sha256' in job:assert study.sha(path)==job[field+'_sha256'],'completed search artifact changed'
    job.update(**metadata,output=str(output),export=str(export),history=str(history),cases=count,
               population=1024,generations=freeze['generations'])
    if job['status']=='ok':
        try:
            data=study.read(output)
            validate_run(data,mode,1024,count,freeze['generations'])
            job.update(validation='complete',output_sha256=study.sha(output),export_sha256=study.sha(export),
                       history_sha256=study.sha(history),search_s=data['evolve_wall_ms']/1000)
            if mode=='gpu_overlap':
                job.update(cpu_audit_fitness_mismatches=data['cpu_audit_mismatches'],
                           cpu_audit_wrong_mismatches=data['cpu_audit_wrong_mismatches'])
        except Exception as error:
            job.update(status='invalid_output',validation_error=str(error))
    stage.persist()
    return job


def judge(stage,run,split,task_input,inp):
    tag=run['task']+f'.s{run["seed"]}.'+run['variant']+'.judge.'+split
    metadata=dict(task=run['task'],seed=run['seed'],variant=run['variant'],split=split)
    if run['status']!='ok':return missing_record(stage,tag,'not_run_search_failure',metadata)
    for field in ('output','export','history'):
        assert study.sha(run[field])==run[field+'_sha256'],'search artifact changed before judging'
    source=inp['source'] if split=='train' else task_input['test_source']
    count=task_input['metadata'][split+'_count']
    output=stage.out/(tag+'.json')
    job=stage.run(tag,['quality-source',source,run['export'],task_input['grammar'],output,run['history']],
                  {'GAGP_BM_CASE_COUNT':str(count)},timeout=1800,reserve=3600)
    if 'output_sha256' in job:assert study.sha(output)==job['output_sha256'],'completed judge artifact changed'
    job.update(**metadata,output=str(output),cases=count)
    if job['status']=='ok':
        try:
            data=study.read(output)
            assert data['cases']==count and data['final']['cases']==count
            assert len(data['history'])==run['generations']
            assert all(row['cases']==count for row in data['history'])
            assert len(data['final']['case_results'])==count
            job['output_sha256']=study.sha(output)
        except Exception as error:job.update(status='invalid_output',validation_error=str(error))
    stage.persist()
    return job


def active_process(pid):
    try:
        state=Path(f'/proc/{pid}/stat').read_text().split()[2]
        command=Path(f'/proc/{pid}/cmdline').read_bytes()
        return state!='Z' and b'quality-control-20261018' in command
    except FileNotFoundError:return False


def preflight():
    for path in study.RAW.glob('*controller.json'):
        info=study.read(path);pid=info['pid']
        if pid!=os.getpid() and active_process(pid):
            raise RuntimeError('prior diagnostic/controller is still active: '+str(path))
    for path in study.RAW.glob('*/manifest.json'):
        for tag,job in study.read(path).get('jobs',{}).items():
            if job['status']=='running' and active_process(job.get('pid',-1)):
                raise RuntimeError('prior child is still active: '+tag)


def run(freeze_path,resume=False):
    preflight()
    exists=(study.RAW/'validation/manifest.json').exists()
    if exists!=resume:raise RuntimeError('existing validation requires --resume; new validation must omit it')
    freeze=study.read(freeze_path)
    assert freeze['format']=='gagp-quality-control-validation-v1'
    assert freeze['deadline_epoch']==study.DEADLINE and freeze['started_epoch']==study.START
    assert study.sha(freeze['binary'])==freeze['binary_sha256']
    for path,digest in freeze['harness_sha256'].items():assert study.sha(path)==digest,'frozen harness changed'
    stage=study.Stage('validation',Path(freeze['binary']),freeze['settings'],strict=True)
    freeze_sha=study.sha(freeze_path)
    if 'freeze_sha256' in stage.m:assert stage.m['freeze_sha256']==freeze_sha
    else:stage.m.update(freeze_sha256=freeze_sha,freeze=str(freeze_path),planned_runs=len(freeze['tasks'])*len(freeze['seeds'])*len(freeze['variants']))
    stage.persist()
    inputs=validation_data.prepare(stage,freeze['tasks'],freeze['seeds'])
    for seed in freeze['seeds']:
        for index,task in enumerate(freeze['tasks']):
            current=inputs[task]
            variants=list(freeze['variants']);shift=(seed+index)%len(variants)
            for variant in variants[shift:]+variants[:shift]:
                if current.get('status')!='prepared':
                    missing_record(stage,f'{task}.s{seed}.{variant}','not_run_input_preparation_failure',
                                   dict(task=task,seed=seed,variant=variant))
                else:measure(stage,freeze,task,seed,variant,current['seeds'].get(str(seed),{}),current)
    searches=[dict(row) for row in stage.m['jobs'].values() if 'variant' in row and 'split' not in row]
    for row in searches:
        current=inputs[row['task']]
        for split in ('train','test'):
            judge(stage,row,split,current,current.get('seeds',{}).get(str(row['seed']),{}))
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time())
    stage.persist()
    # Reporting must independently verify planned coverage, failures and hashes.


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--freeze',type=Path,required=True)
    parser.add_argument('--resume',action='store_true')
    args=parser.parse_args()
    run(args.freeze.resolve(),args.resume)
