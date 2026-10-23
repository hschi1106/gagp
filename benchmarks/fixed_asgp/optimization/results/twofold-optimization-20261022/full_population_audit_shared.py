"""All final members checked, sharing only an exact fresh paired CPU reference."""
import fcntl
import time

import champion_audit_dedup as dedup
import full_population_audit_v2 as admitted
import serial
import shared_cpu_reference as shared

validate=admitted.validate


def run(stage,search_job,inputs,freeze,reference=None):
    if reference is None or reference.get('status')!='ok' or search_job['status']!='ok' or time.time()>=serial.DEADLINE-freeze['reserve_s']:
        return admitted.run(stage,search_job,inputs,freeze)
    task=search_job['task'];population=search_job['population'];variant=search_job['variant']
    tag=f'{task}.p{population}.{variant}.final.full-native'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        assert 'GAGP_SNAPSHOT_CPU' not in stage.m['settings']
        assert reference['native_cpu_executed_programs']>0 and reference['native_fidelity_pass']
        if 'frozen_epoch' in freeze:assert reference['started_epoch']>=freeze['frozen_epoch'],'CPU reference is from an older experiment'
        for field in ('output','export'):assert serial.sha(search_job[field])==search_job[field+'_sha256']
        for field in ('raw_output','output','mapping'):assert serial.sha(reference[field])==reference[field+'_sha256']
        for path,digest in reference['input_sha256'].items():assert serial.sha(path)==digest
        search=serial.read(search_job['output']);asts=serial.read(search_job['export'])['programs']
        assert search['population']==len(asts)==population and search['cases']==1024
        unique,mapping,ast_hashes=dedup.compress_programs(asts)
        source=serial.read(inputs['source']);prepared=serial.read(inputs['prefixes'][str(population)]['prepared'])
        old_source=serial.read(reference['source']);old_prepared=serial.read(reference['prepared'])
        extra=dict(freeze['variants'][variant]['extra'],GAGP_BM_CASE_COUNT='1024',GAGP_SNAPSHOT_NATIVE_OWNER='1')
        original_policy=shared.cpu_settings(stage.m['settings'],reference['extra'])
        current_policy=shared.cpu_settings(stage.m['settings'],extra)
        same,key=shared.same_evaluation(unique,source,prepared['grammar_hash'],current_policy,
            old_prepared['programs'],old_source,old_prepared['grammar_hash'],original_policy)
        same=same and shared.same_decode_context(unique,source,old_source)
        if same:
            source['population']=source['population'][:len(unique)]
            assert len(source['population'])==len(unique)
            source['diagnostic_source_parents']='decode-only prefix; exact final AST CPU reference is separately pinned'
            paths=[stage.out/(tag+suffix) for suffix in ('.source.json','.prepared.json','.mapping.json','.gpu.raw.json','.merged.json','.json')]
            assert not any(path.exists() for path in paths)
            src,prep,map_path,gpu_path,merged_path,expanded_path=paths
            serial.save(src,source);serial.save(prep,dict(format='fixed-asgp-prepared-v1',task=task,
                grammar_hash=prepared['grammar_hash'],programs=unique))
            serial.save(map_path,dict(observation_to_unique=mapping,unique_ast_sha256=ast_hashes,
                logical_program_observations=population,actual_unique_programs=len(unique)))
    if not same:
        result=admitted.run(stage,search_job,inputs,freeze)
        result['cpu_reference_declined']='complete AST/cases/grammar/CPU policy identity not proved';stage.persist();return result
    job=stage.run(tag,['measure',src,prep,inputs['grammar'],len(unique),'snapshot',gpu_path],extra,
        timeout=freeze.get('full_audit_timeout_s',7200),reserve=freeze['reserve_s'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        proof=dict(reference_job=reference,identity_sha256=key,decode_context_verified=True,
            rule='exact serialized AST/cases/grammar/CPU settings and full source decode-context prefix; fresh candidate GPU snapshot, reused original CPU executions')
        job.update(task=task,population=population,variant=variant,seed=search_job['seed'],repetition=search_job['repetition'],
            cases=1024,source=str(src),prepared=str(prep),mapping=str(map_path),gpu_raw_output=str(gpu_path),
            raw_output=str(merged_path),output=str(expanded_path),cpu_reference=proof,
            raw_output_kind='derived merge; original candidate process output retained in gpu_raw_output',
            native_cpu_executed_programs=0,native_cpu_executed_cases=0,
            native_cpu_reference_programs=len(unique),native_cpu_reference_cases=len(unique)*1024,
            diagnostic='all final members; candidate GPU rerun plus exact fresh paired CPU reference')
        if job['status']=='ok':
            try:
                gpu=serial.read(gpu_path);old=serial.read(reference['raw_output'])
                merged=shared.merge(gpu,old);expanded=shared.expand(merged,mapping)
                job.update(**validate(expanded,search,population,1024),cpu_reference_verified=True)
                serial.save(merged_path,merged);serial.save(expanded_path,expanded)
                for field,path in [('gpu_raw_output',gpu_path),('raw_output',merged_path),('output',expanded_path),('mapping',map_path)]:job[field+'_sha256']=serial.sha(path)
                if not job['native_fidelity_pass']:job['status']='native_fidelity_mismatch'
            except Exception as error:job.update(status='invalid_output',error=repr(error),cpu_reference_verified=False)
        stage.persist()
    return job
