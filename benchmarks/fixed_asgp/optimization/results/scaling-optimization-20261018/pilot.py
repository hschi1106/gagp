"""Largest-population calibration; no source/initial-population replacement."""
from argparse import ArgumentParser
from pathlib import Path
import os
import time
import serial


def validate(data, mode, population, generations, full_cpu):
    assert data['population']==population and data['cases']==1024
    assert len(data['generations'])==generations and len(data['final_population'])==population
    assert data['evolve_wall_ms']>0
    assert all(r['expected_program_cases']==population*1024 for r in data['generations'])
    if mode=='gpu_overlap':
        assert data['reproduction_profile'].startswith('native-gpu-phase-')
        assert not data['reproduction_fallback_reason']
        assert data['source_depth_cap']==data['gpu_phase_site_depth']==data['verification_top_k']==0
        assert not data['gpu_phase_site_parsimony']
        assert all(r['cases']==population*1024 and r['unscored']==0 and r['eval_kernel_ms']>0 and r['repro_kernel_ms']>0 for r in data['generations'])
        assert max(r['nodes'] for r in data['final_population'])<=2048
        expected=population if full_cpu else 16
        assert data['cpu_audit_program_count']==data['cpu_audit_wrong_program_count']==expected
    return dict(search_s=data['evolve_wall_ms']/1000,cpu_audit_fitness_mismatches=data.get('cpu_audit_mismatches'),
        cpu_audit_wrong_mismatches=data.get('cpu_audit_wrong_mismatches'),cpu_audit_program_count=data.get('cpu_audit_program_count'))


def main():
    parser=ArgumentParser(description=__doc__);parser.add_argument('--resume',action='store_true');args=parser.parse_args()
    protocol=serial.read(serial.BASE/'protocol.json')
    assert protocol['started_epoch']==serial.START and protocol['deadline_epoch']==serial.DEADLINE
    assert serial.sha(protocol['baseline_binary'])==protocol['baseline_sha256']
    stage=serial.Stage('pilot16384',protocol['baseline_binary'],settings=protocol['settings'],resume=args.resume)
    controller=serial.RAW/'pilot-controller.json'
    serial.save(controller,dict(pid=os.getpid(),status='running',started_epoch=serial.START,deadline_epoch=serial.DEADLINE,
        stage=str(stage.out),log=str(serial.RAW/'pilot-controller.log')))
    old=serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')
    tasks=protocol['pilot']['tasks'];variants=protocol['pilot']['variants'];g=protocol['pilot']['generations'];population=16384
    for ti,task in enumerate(tasks):
        inp=old['inputs'][task];prep=inp['prefixes'][str(population)]
        for path,digest in [(inp['source'],inp['source_sha256']),(inp['grammar'],inp['grammar_sha256']),(prep['prepared'],prep['sha256'])]:
            assert serial.sha(path)==digest,'frozen input changed: '+path
        for variant in variants[ti:]+variants[:ti]:
            mode='asgp_1t' if variant=='asgp' else 'gpu_overlap';tag=f'{task}.p{population}.{variant}.s0.g{g}'
            output=stage.out/(tag+'.json');export=stage.out/(tag+'.export.json');history=stage.out/(tag+'.history.json')
            extra=dict(GAGP_SEARCH_GENERATIONS=str(g),GAGP_BM_SEED='0',GAGP_BM_CASE_COUNT='1024',
                GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_HISTORY_EXPORT=str(history))
            if mode=='gpu_overlap':extra.update(GAGP_GPU_PRIVATE_TUPLE_SCORE='1' if variant=='gpu_exec' else '0',GAGP_SEARCH_CPU_ALL='1')
            job=stage.run(tag,['measure',inp['source'],prep['prepared'],inp['grammar'],population,'search_'+mode,output],extra,timeout=5400,reserve=10800)
            job.update(task=task,variant=variant,mode=mode,population=population,cases=1024,generations=g,output=str(output),export=str(export),history=str(history))
            if job['status']=='ok':
                try:
                    data=serial.read(output);job.update(**validate(data,mode,population,g,True))
                    for field,path in [('output',output),('export',export),('history',history)]:
                        if field+'_sha256' in job:assert serial.sha(path)==job[field+'_sha256'],'completed artifact changed'
                        job[field+'_sha256']=serial.sha(path)
                    job['validation']='work_complete'
                    if mode=='gpu_overlap' and (job['cpu_audit_fitness_mismatches'] or job['cpu_audit_wrong_mismatches']):job['validation']='fidelity_mismatch'
                except Exception as error:job.update(status='invalid_output',validation_error=str(error))
            stage.persist()
        a=stage.m['jobs'][f'{task}.p{population}.gpu_base.s0.g{g}'];b=stage.m['jobs'][f'{task}.p{population}.gpu_exec.s0.g{g}']
        if a['status']==b['status']=='ok':
            x,y=[serial.read(j['output']) for j in (a,b)]
            comparison=dict(final_descriptors_equal=x['final_population']==y['final_population'],
                champion_fitness_equal=[r['best'] for r in x['generations']]==[r['best'] for r in y['generations']],
                final_full_ast_equal=serial.read(a['export'])==serial.read(b['export']),
                speedup=x['evolve_wall_ms']/y['evolve_wall_ms'])
            stage.m.setdefault('comparisons',{})[task]=comparison;stage.persist()
    stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    serial.save(controller,dict(pid=os.getpid(),status='requested_phase_complete',finished_epoch=time.time(),stage=str(stage.out),deadline_epoch=serial.DEADLINE))


if __name__=='__main__':main()
