"""Selected composed/component/older admitted global candidate: G64 old TRAINING parity/runtime."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import champion_audit_dedup as champion_audit
import best_selection_private_adjacent_recipe as best_selection_pool
import quality
import serial
import study_statistics
import vm_pilot


def main(name, prior_name, build_name):
    record=serial.RAW/(name+'-controller.json')
    lease=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
    assert not record.exists(),'preserve attempts; use a new name'
    paths=[Path(__file__),serial.BASE/'best_selection_short_edit_recipe.py',serial.BASE/'combined6_pilot.py',serial.BASE/'best_selection_extra_recipe.py',serial.BASE/'best_selection_coarse_recipe.py',serial.BASE/'combined2_pilot.py',serial.BASE/'combined3_pilot.py',serial.BASE/'champion_audit_dedup.py',serial.BASE/'best_selection_private_adjacent_recipe.py',serial.BASE/'best_selection_soa_recipe.py',serial.BASE/'best_selection_final_recipe.py',serial.BASE/'combined5_pilot.py',serial.BASE/'best_selection_combined.py',serial.BASE/'combined1_pilot.py',serial.BASE/'best_selection_private20.py',serial.BASE/'combined_pilot.py',serial.BASE/'projection_simple_pilot.py',serial.BASE/'best_selection_geometry1.py',serial.BASE/'private_launch_pilot.py',
           serial.BASE/'stage_pilot.py',serial.BASE/'view_geometry_pool_pilot.py',serial.BASE/'quality.py',
           serial.BASE/'quality_data.py',serial.BASE/'serial.py',serial.BASE/'study_statistics.py',
           serial.BASE/'vm_pilot.py',serial.BASE/'protocol.json']
    state=dict(pid=os.getpid(),status='waiting_for_full30_development',started_epoch=time.time(),
        deadline_epoch=serial.DEADLINE,harness_sha256={str(p):serial.sha(p) for p in paths})
    serial.save(record,state)
    try:
        while True:
            path=serial.RAW/(prior_name+'-controller.json')
            prior=serial.read(path) if path.exists() else {}
            if prior.get('status')=='stopped':raise RuntimeError('prior development stopped; inspect preserved failure')
            if prior.get('status')=='requested_phase_complete':
                proc=Path(f'/proc/{prior["pid"]}/stat')
                if not proc.exists() or proc.read_text().split()[2]=='Z':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('training pilot prerequisites exceeded budget')
            time.sleep(10)
        while True:
            checks=serial.read(serial.RAW/'harness-checks-attempt25/manifest.json')
            smoke=serial.read(serial.RAW/'champion-audit-dedup-smoke0-controller.json')
            if checks['status']=='failed' or smoke['status']=='stopped':
                raise RuntimeError('quality harness prerequisites failed; inspect before training pilot')
            if checks['status']==smoke['status']=='ok':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('quality harness prerequisites exceeded budget')
            time.sleep(10)
        for prerequisite in ('shape1','threads0','stage1'):
            while True:
                prior=serial.read(serial.RAW/(prerequisite+'-controller.json'))
                if prior['status']=='stopped':raise RuntimeError('selection prerequisite stopped: '+prerequisite)
                if prior['status']=='requested_phase_complete':
                    proc=Path(f'/proc/{prior["pid"]}/stat')
                    if not proc.exists() or proc.read_text().split()[2]=='Z':break
                if time.time()>serial.DEADLINE-10800:raise RuntimeError('selection prerequisite missed budget')
                time.sleep(10)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest,'training harness changed after launch'
        build=serial.read(serial.RAW/build_name/'manifest.json')
        assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            protocol=serial.read(serial.BASE/'protocol.json')
            selection=best_selection_pool.select()
            variants=selection['variants']
            build=dict(binary=selection['binary'],binary_sha256=selection['binary_sha256'])
            serial.save(serial.RAW/(name+'-development-selection.json'),selection)
            old=serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')
        with serial.GPU_LOCK.open('a') as initialization_lock:
            fcntl.flock(initialization_lock,fcntl.LOCK_EX)
            stages={v:serial.Stage(name+'-'+v,protocol['baseline_binary'] if v=='baseline' else build['binary'],
                                  settings=protocol['settings']) for v in variants}
            audits={v:serial.Stage(name+'-'+v+'-native',protocol['baseline_binary'] if v=='baseline' else build['binary'],
                                  settings=protocol['settings']) for v in variants}
        settings=dict(variants=variants,reserve_s=protocol['reserve_s'])
        state.update(status='old_training_g64',selection=selection);serial.save(record,state)
        comparisons=[]
        for index,task in enumerate(protocol['tasks']):
            entry=old['inputs'][task];current=entry['quality']['0']
            source=Path(current['source']);prepared=Path(current['prepared']);grammar=Path(entry['grammar'])
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                for path,digest in [(source,current['source_sha256']),(prepared,current['prepared_sha256']),
                                    (grammar,entry['grammar_sha256'])]:
                    assert serial.sha(path)==digest,'old training input changed: '+str(path)
            count=entry['split']['train_count'];tag=f'{task}.s0.g64'
            for variant in (('baseline','candidate') if index%2==0 else ('candidate','baseline')):
                stage=stages[variant]
                output,export,history=[stage.out/(tag+suffix) for suffix in ('.json','.export.json','.history.json')]
                extra=dict(variants[variant]['extra'],GAGP_SEARCH_GENERATIONS='64',GAGP_BM_SEED='0',
                    GAGP_BM_CASE_COUNT=str(count),GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_HISTORY_EXPORT=str(history))
                job=stage.run(tag,['measure',source,prepared,grammar,1024,'search_gpu_overlap',output],
                              extra,timeout=3600,reserve=protocol['reserve_s'])
                with serial.GPU_LOCK.open('a') as lock:
                    fcntl.flock(lock,fcntl.LOCK_EX)
                    job.update(task=task,seed=0,variant=variant,generations=64,population=1024,cases=count,
                               output=str(output),export=str(export),history=str(history),source=str(source),prepared=str(prepared),
                               source_sha256=current['source_sha256'],prepared_sha256=current['prepared_sha256'],grammar=str(grammar),grammar_sha256=entry['grammar_sha256'])
                    if job['status']=='ok':
                        try:
                            data=serial.read(output);quality.validate(data,count,64)
                            job.update(search_s=data['evolve_wall_ms']/1000,
                                cpu_audit_s=data['cpu_audit_ms']/1000,
                                evaluation_s=sum(g['eval_ms'] for g in data['generations'])/1000,
                                final_evaluation_s=data['final_eval_ms']/1000)
                            for field,path in [('output',output),('export',export),('history',history)]:
                                job[field+'_sha256']=serial.sha(path)
                        except Exception as error:job.update(status='invalid_output',error=repr(error))
                    stage.persist()
                if job['status']!='ok':raise RuntimeError(f'training pilot search failed: {task}/{variant}; preserve record')
                inputs=dict(metadata=dict(train_count=count),grammar=str(grammar),seeds={'0':dict(source=str(source),prepared=str(prepared))})
                native=champion_audit.run(audits[variant],job,'train',inputs,settings)
                if native['status']!='ok':raise RuntimeError(f'training pilot native fidelity failed: {task}/{variant}; preserve record')
            with serial.GPU_LOCK.open('a') as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                jobs=[stages[v].m['jobs'][tag] for v in ('baseline','candidate')]
                docs=[serial.read(j['output']) for j in jobs]
                histories=[serial.read(j['history'])['history'] for j in jobs]
                row=dict(task=task,population=1024,generations=64,cases=count,scope='old TRAINING only',
                    statuses=[j['status'] for j in jobs],search_s=[j['search_s'] for j in jobs],
                    process_s=[j['process_s'] for j in jobs],speedup=jobs[0]['search_s']/jobs[1]['search_s'],
                    native_audit_process_s=[audits[v].m['jobs'][f'{task}.s0.{v}.train.native']['process_s'] for v in ('baseline','candidate')],
                    full_final_ast_equal=jobs[0]['export_sha256']==jobs[1]['export_sha256'],
                    final_descriptors_equal=docs[0]['final_population']==docs[1]['final_population'],
                    champion_ast_fitness_equal=[(g['ast'],g['fitness']) for g in histories[0]]==[(g['ast'],g['fitness']) for g in histories[1]])
                comparisons.append(row);serial.save(serial.RAW/(name+'-comparisons.json'),comparisons)
                assert all(row[k] for k in ('full_final_ast_equal','final_descriptors_equal','champion_ast_fitness_equal')),'long-run execution mismatch'
        for stage in [*stages.values(),*audits.values()]:
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__);parser.add_argument('--name',required=True)
    parser.add_argument('--prior-name',required=True);parser.add_argument('--build-name',required=True)
    args=parser.parse_args();main(args.name,args.prior_name,args.build_name)
