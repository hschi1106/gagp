"""Full final-population fitness fidelity via exact within-call AST dedup.

Searches are never rerun. The diagnostic evaluates every exported final AST,
then maps exact duplicates back to every original population member. Physical
CPU executions and logical member/case observations remain separate.
"""
import fcntl
import time

import champion_audit as dedup
import serial


def validate(document,search,population,count):
    rows=document['programs'];reference=search['final_population']
    assert document['population']==len(rows)==len(reference)==population
    assert document['cases']==count
    assert document['diagnostic_owner'].startswith('native-gpu-phase-')
    mismatch=dict(cpu_gpu_fitness=0,cpu_gpu_wrong=0,cpu_gpu_errors=0,cpu_gpu_timeouts=0,
                  search_cpu_fitness=0,search_cpu_wrong=0)
    for index,(row,old) in enumerate(zip(rows,reference)):
        assert row['program']==index and row['cases']==row['cpu_cases']==count and row['unscored']==0
        mismatch['cpu_gpu_fitness']+=dedup.canonical(row['fitness'])!=dedup.canonical(row['cpu_fitness'])
        for field in ('wrong','errors','timeouts'):
            mismatch['cpu_gpu_'+field]+=row[field]!=row['cpu_'+field]
        mismatch['search_cpu_fitness']+=old['fitness']!=dedup.canonical(row['cpu_fitness'])
        mismatch['search_cpu_wrong']+=old['wrong']!=row['cpu_wrong']
    return dict(native_program_observations=population,native_case_observations=population*count,
        native_mismatches=mismatch,native_fidelity_pass=not any(mismatch.values()))


def run(stage,search_job,inputs,freeze):
    population=search_job['population'];task=search_job['task'];variant=search_job['variant']
    tag=f'{task}.p{population}.{variant}.final.full-native'
    meta=dict(task=task,population=population,variant=variant,seed=search_job['seed'],
        repetition=search_job['repetition'],diagnostic='all final members, exact AST dedup, native CPU/GPU fitness and wrong')
    if search_job['status']!='ok':
        assert tag not in stage.m['jobs']
        stage.m['jobs'][tag]=dict(status='not_run_search_failure',**meta);stage.persist();return stage.m['jobs'][tag]
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        if time.time()>=serial.DEADLINE-freeze['reserve_s']:
            assert tag not in stage.m['jobs']
            job=dict(status='budget_not_run',inputs_not_read=True,
                     reason='Timed cutoff after GPU-lock wait, before full AST parsing',**meta)
            stage.m['jobs'][tag]=job;stage.persist();return job
        for field in ('output','export'):assert serial.sha(search_job[field])==search_job[field+'_sha256']
        search=serial.read(search_job['output']);export=serial.read(search_job['export'])
        asts=export['programs'];count=search['cases']
        assert len(asts)==len(search['final_population'])==population
        assert search['population']==population and 0<count<=200
        unique,mapping,hashes=dedup.compress_programs(asts)
        source=serial.read(inputs['source'])
        assert len(source['population'])>=len(unique) and len(source['cases'])==count
        original=serial.read(inputs['prefixes'][str(population)]['prepared'])
        # Original source parents are only decoded by the snapshot interface.
        # The prepared programs are precisely the exported final native ASTs.
        source['population']=source['population'][:len(unique)]
        source['diagnostic_source_parents']='decode-only original prefix; final exported native ASTs evaluated; no search/resampling'
        source_path,prepared_path,mapping_path,raw_path,expanded_path=[stage.out/(tag+suffix) for suffix in
            ('.source.json','.prepared.json','.mapping.json','.raw.json','.json')]
        assert not any(p.exists() for p in (source_path,prepared_path,mapping_path,raw_path,expanded_path))
        serial.save(source_path,source)
        serial.save(prepared_path,dict(format='fixed-asgp-prepared-v1',task=task,
                    grammar_hash=original['grammar_hash'],programs=unique))
        serial.save(mapping_path,dict(observation_to_unique=mapping,unique_ast_sha256=hashes,
                    logical_program_observations=population,actual_unique_programs=len(unique)))
    extra=dict(freeze['variants'][variant]['extra'],GAGP_BM_CASE_COUNT=str(count),GAGP_SNAPSHOT_CPU='1',GAGP_SNAPSHOT_NATIVE_OWNER='1')
    job=stage.run(tag,['measure',source_path,prepared_path,inputs['grammar'],len(unique),'snapshot',raw_path],
        extra,timeout=freeze.get('full_audit_timeout_s',7200),reserve=freeze['reserve_s'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        job.update(**meta,cases=count,source=str(source_path),prepared=str(prepared_path),mapping=str(mapping_path),
            raw_output=str(raw_path),output=str(expanded_path),native_cpu_executed_programs=len(unique),
            native_cpu_executed_cases=len(unique)*count)
        if job['status']=='ok':
            try:
                raw=serial.read(raw_path);assert raw['population']==len(unique)
                expanded=dedup.expand_document(raw,mapping)
                job.update(**validate(expanded,search,population,count))
                serial.save(expanded_path,expanded)
                for field,path in [('raw_output',raw_path),('output',expanded_path),('mapping',mapping_path)]:job[field+'_sha256']=serial.sha(path)
                if not job['native_fidelity_pass']:job['status']='native_fidelity_mismatch'
            except Exception as error:job.update(status='invalid_output',error=repr(error))
        stage.persist()
    return job
