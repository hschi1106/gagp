"""Immutable development-only paired searches; never reads held-out cases."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import serial


def validate(data, population, generations):
    assert data['population'] == population and data['cases'] == 1024
    assert len(data['generations']) == generations
    assert len(data['final_population']) == population
    assert max(row['nodes'] for row in data['final_population']) <= 2048
    assert data['reproduction_profile'].startswith('native-gpu-phase-')
    assert not data['reproduction_fallback_reason']
    assert data['verification_top_k'] == 0
    assert data['gpu_blocksize'] == data['cpu_audit_reduction_lanes'] == 512
    assert data['cpu_audit_program_count'] == data['cpu_audit_wrong_program_count'] == 16
    assert data['cpu_audit_mismatches'] == data['cpu_audit_wrong_mismatches'] == 0
    for row in data['generations']:
        assert row['cases'] == row['expected_program_cases'] == population*1024
        assert row['unscored'] == 0 and row['repro_kernel_ms'] > 0
    return dict(search_s=data['evolve_wall_ms']/1000,
                cpu_fitness_mismatches=data['cpu_audit_mismatches'],
                cpu_wrong_mismatches=data['cpu_audit_wrong_mismatches'],
                unique_final_genomes=data['unique_final_genomes'],
                simple_phase_count=sum(row.get('simple_phase_count',0) for row in data['generations']),
                shape_result_programs=sum(row.get('shape_result_programs',0) for row in data['generations']),
                shape_result_groups=sum(row.get('shape_result_groups',0) for row in data['generations']),
                physical_program_cases=sum(row.get('physical_program_cases',population*1024) for row in data['generations']))


def run(candidate, name, populations, tasks):
    protocol = serial.read(serial.BASE/'protocol.json')
    controller_lock = (serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    record = serial.RAW/(name+'-controller.json')
    if record.exists():
        raise RuntimeError('controller attempt exists; preserve it and use a new attempt name')
    state = dict(pid=os.getpid(), status='running', started_epoch=time.time(),
                 deadline_epoch=serial.DEADLINE, population=populations, tasks=tasks)
    serial.save(record, state)
    variants = ('baseline', 'case_only', 'shape', 'combined')
    stages = {v: serial.Stage(name+'-'+v,
        protocol['baseline_binary'] if v == 'baseline' else candidate,
        settings=protocol['settings']) for v in variants}
    for stage in stages.values():
        stage.m.update(harness_sha256={str(p):serial.sha(p) for p in
            (Path(__file__), serial.BASE/'serial.py', serial.BASE/'protocol.json')})
        stage.persist()
    comparisons = []
    try:
        for ti, task in enumerate(tasks):
            for population in populations:
                source = protocol['inputs'][task]
                for variant in variants[ti%len(variants):]+variants[:ti%len(variants)]:
                    stage = stages[variant]
                    tag = f'{task}.p{population}.s0.g8'
                    output, export, history = [stage.out/(tag+suffix) for suffix in
                                              ('.json', '.export.json', '.history.json')]
                    extra = dict(GAGP_SEARCH_GENERATIONS='8', GAGP_BM_SEED='0',
                        GAGP_BM_CASE_COUNT='1024', GAGP_SEARCH_EXPORT=str(export),
                        GAGP_SEARCH_HISTORY_EXPORT=str(history),
                        GAGP_GPU_COALESCED_COUNTS='0',
                        GAGP_GPU_INTEGER_CASE_REUSE='0' if variant == 'baseline' else '1',
                        GAGP_GPU_SIMPLE_PHASES='1' if variant == 'combined' else '0',
                        GAGP_GPU_SHAPE_RESULT_CACHE='1' if variant in ('shape','combined') else '0')
                    job = stage.run(tag, ['measure', source['source'],
                        source['prefixes'][str(population)]['prepared'], source['grammar'],
                        population, 'search_gpu_overlap', output], extra, timeout=2400,
                        reserve=protocol['reserve_s'])
                    with serial.GPU_LOCK.open('a') as lock:
                        fcntl.flock(lock, fcntl.LOCK_EX)
                        job.update(task=task, population=population, variant=variant,
                            generations=8, seed=0, output=str(output), export=str(export), history=str(history))
                        if job['status'] == 'ok':
                            try:
                                job.update(**validate(serial.read(output), population, 8))
                                for field, path in (('output',output),('export',export),('history',history)):
                                    job[field+'_sha256'] = serial.sha(path)
                            except Exception as error:
                                job.update(status='invalid_output', error=repr(error))
                        stage.persist()
                    if job['status'] != 'ok':
                        raise RuntimeError('development search or native fidelity check failed: '+str(job))
                with serial.GPU_LOCK.open('a') as lock:
                    fcntl.flock(lock, fcntl.LOCK_EX)
                    jobs = {v: stages[v].m['jobs'][tag] for v in variants}
                    if all(j['status'] == 'ok' for j in jobs.values()):
                        base = serial.read(jobs['baseline']['output'])
                        base_history = serial.read(jobs['baseline']['history'])
                        for variant in variants[1:]:
                            current = serial.read(jobs[variant]['output'])
                            observation = dict(task=task, population=population, variant=variant,
                                speedup=base['evolve_wall_ms']/current['evolve_wall_ms'],
                                final_descriptors_equal=base['final_population']==current['final_population'],
                                full_final_ast_equal=jobs['baseline']['export_sha256']==jobs[variant]['export_sha256'],
                                history_equal=base_history==serial.read(jobs[variant]['history']))
                            # History contains timing metadata in some builds;
                            # compare only each training-selected AST and fitness.
                            a, b = base_history['history'], serial.read(jobs[variant]['history'])['history']
                            observation['champion_ast_fitness_equal'] = [
                                (r.get('ast',r.get('program')),r['fitness']) for r in a] == [
                                (r.get('ast',r.get('program')),r['fitness']) for r in b]
                            comparisons.append(observation)
                        serial.save(serial.RAW/(name+'-comparisons.json'),comparisons)
                        print('comparison',task,population,comparisons[-(len(variants)-1):],flush=True)
                        if any(not row[key] for row in comparisons[-(len(variants)-1):] for key in
                               ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')):
                            raise RuntimeError('execution-only candidate changed paired scores or exported programs')
        for stage in stages.values():
            stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:
        serial.save(record,state)


if __name__ == '__main__':
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--candidate', type=Path, required=True)
    parser.add_argument('--name', required=True)
    parser.add_argument('--populations', type=int, nargs='+', default=[1024])
    parser.add_argument('--tasks', nargs='+')
    args = parser.parse_args()
    protocol = serial.read(serial.BASE/'protocol.json')
    run(args.candidate,args.name,args.populations,args.tasks or protocol['development']['tasks'])
