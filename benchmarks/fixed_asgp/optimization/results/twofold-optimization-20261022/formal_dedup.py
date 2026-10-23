"""Frozen full30 timings followed by separate complete final-population native audits."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import serial
import full_population_audit_v2 as full_population_audit


def validate(data,population,generations,full):
    assert data['population']==population and data['cases']==1024
    assert len(data['generations'])==generations and len(data['final_population'])==population
    assert data['reproduction_profile'].startswith('native-gpu-phase-')
    assert not data['reproduction_fallback_reason']
    assert data['verification_top_k']==data['source_depth_cap']==data['gpu_phase_site_depth']==0
    assert not data['gpu_phase_site_parsimony']
    assert data['gpu_blocksize']==data['cpu_audit_reduction_lanes']==512
    expected=population if full else 16
    assert data['cpu_audit_program_count']==data['cpu_audit_wrong_program_count']==expected
    assert data['cpu_audit_mismatches']==data['cpu_audit_wrong_mismatches']==0
    assert math.isfinite(data['evolve_wall_ms']) and data['evolve_wall_ms']>0
    assert max(row['nodes'] for row in data['final_population'])<=2048
    for row in data['generations']:
        assert row['cases']==row['expected_program_cases']==population*1024
        assert row['unscored']==0 and row['repro_kernel_ms']>0
    return dict(search_s=data['evolve_wall_ms']/1000,
        cpu_fitness_mismatches=data['cpu_audit_mismatches'],
        cpu_wrong_mismatches=data['cpu_audit_wrong_mismatches'],
        cpu_audit_program_count=expected,
        physical_program_cases=sum(r.get('physical_program_cases',population*1024) for r in data['generations']))


def main(freeze_path,name):
    freeze=serial.read(freeze_path)
    assert freeze['format']=='gagp-twofold-speed-freeze-v2'
    assert freeze['started_epoch']==serial.START and freeze['deadline_epoch']==serial.DEADLINE
    assert freeze['tasks']==list(serial.PAPER_TASKS) and freeze['populations']==[1024,16384]
    assert freeze['generations']>=8 and freeze['repetitions']>=2
    assert set(freeze['variants'])=={'baseline','candidate'}
    for path,digest in freeze['harness_sha256'].items():assert serial.sha(path)==digest,'frozen harness changed'
    record=serial.RAW/(name+'-controller.json')
    lock=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
    if record.exists():raise RuntimeError('formal attempt exists; preserve it, never overwrite')
    with serial.GPU_LOCK.open('a') as initialization_lock:
        fcntl.flock(initialization_lock,fcntl.LOCK_EX)
        stages={}
        for variant,spec in freeze['variants'].items():
            assert serial.sha(spec['binary'])==spec['binary_sha256']
            stages[variant]=serial.Stage(name+'-'+variant,spec['binary'],settings=freeze['settings'])
            stages[variant].m.update(freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path),
                                    planned_runs=30*2*freeze['repetitions'])
            stages[variant].persist()
    state=dict(pid=os.getpid(),status='running',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
               freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path))
    serial.save(record,state)
    comparisons=[]
    try:
        for index,task in enumerate(freeze['tasks']):
            source=freeze['inputs'][task]
            for population in freeze['populations']:
                with serial.GPU_LOCK.open('a') as gpu:
                    fcntl.flock(gpu,fcntl.LOCK_EX)
                    prepared=source['prefixes'][str(population)]
                    for path,digest in [(source['source'],source['source_sha256']),
                                        (source['grammar'],source['grammar_sha256']),
                                        (prepared['prepared'],prepared['sha256'])]:
                        assert serial.sha(path)==digest,'frozen input changed: '+path
                for repetition in range(freeze['repetitions']):
                    tag=f'{task}.p{population}.s0.g{freeze["generations"]}.r{repetition}'
                    order=['baseline','candidate'] if (index+repetition)%2==0 else ['candidate','baseline']
                    for variant in order:
                        stage=stages[variant]
                        output,export,history=[stage.out/(tag+suffix) for suffix in
                                                ('.json','.export.json','.history.json')]
                        full=repetition==0
                        extra=dict(freeze['variants'][variant]['extra'],
                            GAGP_SEARCH_GENERATIONS=str(freeze['generations']),GAGP_BM_SEED='0',
                            GAGP_BM_CASE_COUNT='1024',GAGP_SEARCH_EXPORT=str(export),
                            GAGP_SEARCH_HISTORY_EXPORT=str(history))
                        if not full:extra['GAGP_SEARCH_EXPORT_BEST_ONLY']='1'
                        job=stage.run(tag,['measure',source['source'],
                            source['prefixes'][str(population)]['prepared'],source['grammar'],
                            population,'search_gpu_overlap',output],extra,
                            timeout=freeze['timeout_s'],reserve=freeze['reserve_s'])
                        with serial.GPU_LOCK.open('a') as gpu:
                            fcntl.flock(gpu,fcntl.LOCK_EX)
                            job.update(task=task,population=population,variant=variant,seed=0,
                                repetition=repetition,generations=freeze['generations'],
                                output=str(output),export=str(export),history=str(history),full_final_audit=False,full_final_export=full)
                            if job['status']=='ok':
                                try:
                                    job.update(**validate(serial.read(output),population,freeze['generations'],False))
                                    for field,path in [('output',output),('export',export),('history',history)]:
                                        job[field+'_sha256']=serial.sha(path)
                                except Exception as error:job.update(status='invalid_output',error=repr(error))
                            stage.persist()
                    with serial.GPU_LOCK.open('a') as gpu:
                        fcntl.flock(gpu,fcntl.LOCK_EX)
                        a,b=[stages[v].m['jobs'][tag] for v in ('baseline','candidate')]
                        observation=dict(task=task,population=population,repetition=repetition,
                                         statuses=[a['status'],b['status']])
                        if a['status']==b['status']=='ok':
                            x,y=[serial.read(j['output']) for j in (a,b)]
                            ah,bh=[serial.read(j['history'])['history'] for j in (a,b)]
                            observation.update(speedup=x['evolve_wall_ms']/y['evolve_wall_ms'],
                                final_descriptors_equal=x['final_population']==y['final_population'],
                                champion_ast_fitness_equal=[(r.get('ast',r.get('program')),r['fitness']) for r in ah]==
                                                          [(r.get('ast',r.get('program')),r['fitness']) for r in bh],
                                final_export_equal=a['export_sha256']==b['export_sha256'],full_final_ast_comparison=repetition==0)
                        comparisons.append(observation)
                        serial.save(serial.RAW/(name+'-comparisons.json'),comparisons)
        for stage in stages.values():
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
        state.update(status='timings_complete_native_audits_pending',timings_finished_epoch=time.time());serial.save(record,state)
        with serial.GPU_LOCK.open('a') as initialization_lock:
            fcntl.flock(initialization_lock,fcntl.LOCK_EX)
            audit_stages={variant:serial.Stage(name+'-full-native-'+variant,spec['binary'],settings=freeze['settings'])
                          for variant,spec in freeze['variants'].items()}
        audit_jobs=[]
        for task in freeze['tasks']:
            for population in freeze['populations']:
                tag=f'{task}.p{population}.s0.g{freeze["generations"]}.r0'
                for variant in ('baseline','candidate'):
                    search_job=stages[variant].m['jobs'][tag]
                    audit_jobs.append(full_population_audit.run(audit_stages[variant],search_job,freeze['inputs'][task],freeze))
        for stage in audit_stages.values():
            stage.m.update(status='requested_phase_complete',freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path),
                           finished_epoch=time.time());stage.persist()
        state.update(status='requested_phase_complete',finished_epoch=time.time(),
            full_native_audits_expected=120,full_native_audits_ok=sum(j['status']=='ok' and j.get('native_fidelity_pass',False) for j in audit_jobs),
            full_native_audits_pass=len(audit_jobs)==120 and all(j['status']=='ok' and j.get('native_fidelity_pass',False) for j in audit_jobs))
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--freeze',type=Path,required=True)
    parser.add_argument('--name',required=True)
    args=parser.parse_args();main(args.freeze,args.name)
