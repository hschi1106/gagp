"""Paired new-seed searches and exact-AST-deduplicated native CPU champion audits; every observation still checked."""
from argparse import ArgumentParser
import fcntl
import math
import os
from pathlib import Path
import time
import traceback

import champion_audit_dedup as champion_audit
import quality_data
import serial


def validate(data,count,generations):
    assert data['population']==1024 and data['cases']==count
    assert len(data['generations'])==generations and len(data['final_population'])==1024
    assert data['reproduction_profile'].startswith('native-gpu-phase-') and not data['reproduction_fallback_reason']
    assert data['verification_top_k']==data['source_depth_cap']==data['gpu_phase_site_depth']==0
    assert not data['gpu_phase_site_parsimony']
    assert data['gpu_blocksize']==data['cpu_audit_reduction_lanes']==512
    assert data['cpu_audit_program_count']==data['cpu_audit_wrong_program_count']==16
    assert data['cpu_audit_mismatches']==data['cpu_audit_wrong_mismatches']==0
    assert max(row['nodes'] for row in data['final_population'])<=2048
    assert math.isfinite(data['evolve_wall_ms']) and data['evolve_wall_ms']>0
    for row in data['generations']:
        assert row['cases']==row['expected_program_cases']==1024*count and row['unscored']==0
        assert row['repro_kernel_ms']>0


def validate_judge(document,count,generations,task):
    assert document['cases']==count and document['task']==task
    assert len(document['history'])==generations and len(document['cases_sha256'])==64
    for score in document['history']+[document['final']]:
        rows=score['case_results']
        assert score['backend']=='gagp_cpu_1t'
        assert score['cases']==len(rows)==count
        assert [row['case'] for row in rows]==list(range(count))
        assert all(row['exact'] in (0,1) and row['runtime_error'] in (0,1) for row in rows)
        correct=sum(row['exact'] for row in rows)
        assert score['correct_count']==score['exact']==correct
        assert score['wrong']==count-correct
        assert score['runtime_errors']==sum(row['runtime_error'] for row in rows)
        assert math.isclose(score['exact_accuracy'],correct/count,rel_tol=0,abs_tol=1e-12)


def missing(stage,tag,reason,metadata):
    assert tag not in stage.m['jobs']
    stage.m['jobs'][tag]=dict(status=reason,**metadata);stage.persist()
    return stage.m['jobs'][tag]


def search(stage,freeze,task,seed,variant,inputs):
    tag=f'{task}.s{seed}.{variant}'
    meta=dict(task=task,seed=seed,variant=variant,generations=freeze['generations'],population=1024)
    current=inputs.get('seeds',{}).get(str(seed),{})
    if inputs.get('status')!='prepared':
        meta['preparation_status']=inputs.get('status','missing')
        return missing(stage,tag,'budget_not_run' if inputs.get('status')=='budget_not_run' else 'not_run_fixture_failure',meta)
    if current.get('status')!='ok':
        meta['preparation_status']=current.get('status','missing')
        return missing(stage,tag,'budget_not_run' if current.get('status')=='budget_not_run' else 'not_run_preparation_failure',meta)
    output,export,history=[stage.out/(tag+suffix) for suffix in ('.json','.export.json','.history.json')]
    count=inputs['metadata']['train_count']
    extra=dict(freeze['variants'][variant]['extra'],GAGP_SEARCH_GENERATIONS=str(freeze['generations']),
        GAGP_BM_SEED=str(seed),GAGP_BM_CASE_COUNT=str(count),GAGP_SEARCH_EXPORT=str(export),
        GAGP_SEARCH_HISTORY_EXPORT=str(history),GAGP_SEARCH_EXPORT_BEST_ONLY='1')
    job=stage.run(tag,['measure',current['source'],current['prepared'],inputs['grammar'],1024,
        'search_gpu_overlap',output],extra,timeout=3600,reserve=freeze['reserve_s'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        job.update(**meta,output=str(output),export=str(export),history=str(history),cases=count)
        if job['status']=='ok':
            try:
                document=serial.read(output);validate(document,count,freeze['generations'])
                job.update(search_s=document['evolve_wall_ms']/1000,
                    cpu_fitness_mismatches=document['cpu_audit_mismatches'],
                    cpu_wrong_mismatches=document['cpu_audit_wrong_mismatches'])
                for field,path in [('output',output),('export',export),('history',history)]:
                    job[field+'_sha256']=serial.sha(path)
            except Exception as error:job.update(status='invalid_output',error=repr(error))
        stage.persist()
    return job


def judge(stage,run,split,inputs,reserve=10800):
    tag=f'{run["task"]}.s{run["seed"]}.{run["variant"]}.{split}'
    meta=dict(task=run['task'],seed=run['seed'],variant=run['variant'],split=split)
    if run['status']!='ok':return missing(stage,tag,'not_run_search_failure',meta)
    for field in ('export','history'):assert serial.sha(run[field])==run[field+'_sha256']
    source=inputs['seeds'][str(run['seed'])]['source'] if split=='train' else inputs['test_source']
    count=inputs['metadata'][split+'_count'];output=stage.out/(tag+'.json')
    job=stage.run(tag,['quality-source',source,run['export'],inputs['grammar'],output,run['history']],
        {'GAGP_BM_CASE_COUNT':str(count)},timeout=2400,reserve=reserve)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        job.update(**meta,cases=count,output=str(output))
        if job['status']=='ok':
            try:
                document=serial.read(output)
                validate_judge(document,count,run['generations'],run['task'])
                job['output_sha256']=serial.sha(output)
            except Exception as error:job.update(status='invalid_output',error=repr(error))
        stage.persist()
    return job


def main(freeze_path,name):
    freeze=serial.read(freeze_path)
    assert freeze['format']=='gagp-twofold-quality-freeze-v1'
    assert freeze['started_epoch']==serial.START and freeze['deadline_epoch']==serial.DEADLINE
    assert freeze['tasks']==list(serial.PAPER_TASKS) and set(freeze['variants'])=={'baseline','candidate'}
    assert freeze['generations']>=64 and len(freeze['seeds'])>=10
    assert min(freeze['seeds'])>=25
    for path,digest in freeze['harness_sha256'].items():assert serial.sha(path)==digest,'frozen harness changed'
    for spec in freeze['variants'].values():assert serial.sha(spec['binary'])==spec['binary_sha256'],'frozen binary changed'
    record=serial.RAW/(name+'-controller.json')
    lock=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lock,fcntl.LOCK_EX | fcntl.LOCK_NB)
    if record.exists():raise RuntimeError('quality controller attempt exists; do not overwrite')
    state=dict(pid=os.getpid(),status='preparing',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
               freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path))
    serial.save(record,state)
    try:
        prepare=serial.Stage(name+'-inputs',freeze['variants']['candidate']['binary'],settings=freeze['settings'])
        prepare.m.update(freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path));prepare.persist()
        inputs=quality_data.prepare(prepare,freeze)
        prepare.m.update(status='requested_phase_complete',finished_epoch=time.time());prepare.persist()
        stages={v:serial.Stage(name+'-'+v,s['binary'],settings=freeze['settings']) for v,s in freeze['variants'].items()}
        judges={v:serial.Stage(name+'-'+v+'-judges',s['binary'],settings=freeze['settings']) for v,s in freeze['variants'].items()}
        for stage in [*stages.values(),*judges.values()]:
            stage.m.update(freeze=str(freeze_path),freeze_sha256=serial.sha(freeze_path));stage.persist()
        state['status']='search_and_judge';serial.save(record,state)
        comparisons=[]
        for seed in freeze['seeds']:
            for index,task in enumerate(freeze['tasks']):
                order=['baseline','candidate'] if (seed+index)%2 else ['candidate','baseline']
                for variant in order:
                    run=search(stages[variant],freeze,task,seed,variant,inputs[task])
                    for split in ('train','test'):
                        judge(judges[variant],run,split,inputs[task],reserve=freeze['reserve_s'])
                        champion_audit.run(judges[variant],run,split,inputs[task],freeze)
                with serial.GPU_LOCK.open('a') as gpu:
                    fcntl.flock(gpu,fcntl.LOCK_EX)
                    a,b=[stages[v].m['jobs'][f'{task}.s{seed}.{v}'] for v in ('baseline','candidate')]
                    pair=dict(task=task,seed=seed,statuses=[a['status'],b['status']],
                        common_audit_statuses=[judges[v].m['jobs'][f'{task}.s{seed}.{v}.{split}']['status']
                            for v in ('baseline','candidate') for split in ('train','test')],
                        native_audit_statuses=[judges[v].m['jobs'][f'{task}.s{seed}.{v}.{split}.native']['status']
                            for v in ('baseline','candidate') for split in ('train','test')])
                    if a['status']==b['status']=='ok':
                        x,y=[serial.read(j['output']) for j in (a,b)]
                        ah,bh=[serial.read(j['history'])['history'] for j in (a,b)]
                        pair.update(final_descriptors_equal=x['final_population']==y['final_population'],
                            champion_ast_fitness_equal=[(r.get('ast',r.get('program')),r['fitness']) for r in ah]==
                                                      [(r.get('ast',r.get('program')),r['fitness']) for r in bh],
                            best_export_equal=a['export_sha256']==b['export_sha256'])
                    comparisons.append(pair)
                    serial.save(serial.RAW/(name+'-comparisons.json'),comparisons)
        for stage in [*stages.values(),*judges.values()]:
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--freeze',type=Path,required=True);parser.add_argument('--name',required=True)
    args=parser.parse_args();main(args.freeze,args.name)
