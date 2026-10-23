"""Native fitness audits of every selected champion, separate from common loss.

The readable quality judge measures exact output accuracy and common loss.
Those are not the tuple/list discounted training fitness. This module separately
checks the original native fitness and wrong counts, using the same pinned
binary, including each generation's selected AST and the final selected AST.
"""
import fcntl
import math

import serial


def canonical(value):
    if not math.isfinite(value) or value == 0:
        return value
    mantissa, exponent = math.frexp(value)
    scaled = math.ldexp(mantissa, 40)
    rounded = math.floor(scaled + .5) if scaled >= 0 else math.ceil(scaled - .5)
    return math.ldexp(float(rounded), exponent - 40)


def validate(document, search, split, count):
    members = document['programs']
    expected = len(search['generations']) + 1
    assert document['cases'] == count and document['population'] == len(members) == expected
    assert document['diagnostic_owner'].startswith('native-gpu-phase-')
    mismatch = dict(cpu_gpu_fitness=0, cpu_gpu_wrong=0, cpu_gpu_errors=0,
                    cpu_gpu_timeouts=0, search_cpu_fitness=0, search_cpu_wrong=0)
    reference = [(g['best'], g['best_wrong']) for g in search['generations']]
    reference.append((search['final_population'][0]['fitness'], search['final_population'][0]['wrong']))
    for index, row in enumerate(members):
        assert row['program'] == index and row['cases'] == row['cpu_cases'] == count
        assert row['unscored'] == 0
        mismatch['cpu_gpu_fitness'] += canonical(row['fitness']) != canonical(row['cpu_fitness'])
        mismatch['cpu_gpu_wrong'] += row['wrong'] != row['cpu_wrong']
        mismatch['cpu_gpu_errors'] += row['errors'] != row['cpu_errors']
        mismatch['cpu_gpu_timeouts'] += row['timeouts'] != row['cpu_timeouts']
        if split == 'train':
            mismatch['search_cpu_fitness'] += reference[index][0] != canonical(row['cpu_fitness'])
            mismatch['search_cpu_wrong'] += reference[index][1] != row['cpu_wrong']
    return dict(native_program_observations=expected, native_case_observations=expected*count,
                native_mismatches=mismatch, native_fidelity_pass=not any(mismatch.values()))


def run(stage, search_job, split, inputs, freeze):
    tag = f'{search_job["task"]}.s{search_job["seed"]}.{search_job["variant"]}.{split}.native'
    metadata = dict(task=search_job['task'], seed=search_job['seed'], variant=search_job['variant'],
                    split=split, diagnostic='all generation champions plus final; native fitness/wrong')
    if search_job['status'] != 'ok':
        assert tag not in stage.m['jobs']
        stage.m['jobs'][tag] = dict(status='not_run_search_failure', **metadata)
        stage.persist()
        return stage.m['jobs'][tag]
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for field in ('output', 'export', 'history'):
            assert serial.sha(search_job[field]) == search_job[field+'_sha256']
        search = serial.read(search_job['output'])
        history = serial.read(search_job['history'])['history']
        programs = [row['ast'] for row in history] + [serial.read(search_job['export'])['programs'][0]]
        assert len(programs) == len(search['generations'])+1
        entry = inputs['seeds'][str(search_job['seed'])]
        original = serial.read(entry['source'])
        native = serial.read(entry['prepared'])
        count = inputs['metadata'][split+'_count']
        source = dict(original)
        if split == 'test':
            source['cases'] = serial.read(inputs['test_source'])['cases']
        # The snapshot interface decodes these original source parents but never
        # evaluates/uses them: it evaluates the explicitly exported native ASTs.
        # Retain a prefix only to satisfy that interface. No new initial/search
        # population is created, selected, or retried here.
        assert len(programs) <= len(source['population'])
        source['population'] = source['population'][:len(programs)]
        source['diagnostic_source_parents'] = 'decode-only original prefix; native ASTs evaluated'
        paths = [stage.out/(tag+suffix) for suffix in ('.source.json', '.prepared.json', '.json')]
        source_path, prepared_path, output = paths
        assert not any(path.exists() for path in paths)
        serial.save(source_path, source)
        serial.save(prepared_path, dict(format='fixed-asgp-prepared-v1', task=search_job['task'],
                    grammar_hash=native['grammar_hash'], programs=programs))
    extra = dict(freeze['variants'][search_job['variant']]['extra'], GAGP_BM_CASE_COUNT=str(count),
                 GAGP_SNAPSHOT_CPU='1', GAGP_SNAPSHOT_NATIVE_OWNER='1')
    job = stage.run(tag, ['measure', source_path, prepared_path, inputs['grammar'], len(programs),
                         'snapshot', output], extra, timeout=2400, reserve=freeze['reserve_s'])
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        job.update(**metadata, cases=count, output=str(output), source=str(source_path), prepared=str(prepared_path))
        if job['status'] == 'ok':
            try:
                job.update(**validate(serial.read(output), search, split, count))
                job['output_sha256'] = serial.sha(output)
                if not job['native_fidelity_pass']:
                    job.update(status='native_fidelity_mismatch')
            except Exception as error:
                job.update(status='invalid_output', error=repr(error))
        stage.persist()
    return job
