"""Wait for capacity calibration, build the opt-in adapter, profile and tune geometry.

This stage does not start the formal matrix or select by task success/test data.
"""
from argparse import ArgumentParser
from contextlib import contextmanager
import fcntl
import math
import os
from pathlib import Path
import shutil
import subprocess
import time
import serial
from pilot import validate

TASKS = ('is_sorted', 'all_equal', 'house_robber', 'filter_odd', 'double_letters')
POPULATIONS = (1024, 16384)
BLOCKS = (512, 256, 128, 64)
RESERVE = 10 * 3600


def alive(pid):
    try:
        return Path(f'/proc/{pid}/stat').read_text().split(') ', 1)[1].split()[0] != 'Z'
    except FileNotFoundError:
        return False


@contextmanager
def gpu_lock():
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        yield


def source_record():
    files = subprocess.check_output(['git', 'ls-files', 'cpp'], cwd=serial.ROOT, text=True).splitlines()
    return {p: serial.sha(serial.ROOT / p) for p in files if (serial.ROOT / p).is_file()}


def build_adapter():
    folder = serial.RAW / 'geometry-build'
    if folder.exists():
        raise RuntimeError('build attempt already exists; inspect it instead of rebuilding silently')
    folder.mkdir()
    record = dict(started_epoch=time.time(), status='running', source_sha256=source_record(),
                  commands=[['python3', '-m', 'unittest', 'discover', '-s', str(serial.BASE),
                             '-p', 'test_serial.py', '-v'],
                            ['cmake', '--build', 'cpp/build/release', '--target',
                             'gagp_fixed_asgp_bench', '--parallel', '4'],
                            ['ctest', '--test-dir', 'cpp/build/release', '-R',
                             '^gagp_test_fixed_asgp_bench$', '--output-on-failure']])
    serial.save(folder / 'manifest.json', record)
    (folder / 'source.patch').write_text(subprocess.check_output(
        ['git', 'diff', '--', 'cpp'], cwd=serial.ROOT, text=True))
    with (folder / 'build-and-adapter.log').open('w') as log:
        for command in record['commands']:
            result = subprocess.run(command, cwd=serial.ROOT, stdout=log, stderr=subprocess.STDOUT,
                                    timeout=min(1800, serial.DEADLINE - RESERVE - time.time()))
            record.setdefault('returncodes', []).append(result.returncode)
            serial.save(folder / 'manifest.json', record)
            if result.returncode:
                record.update(status='failed', finished_epoch=time.time())
                serial.save(folder / 'manifest.json', record)
                raise RuntimeError('build/adapter failed; preserve attempt and inspect log')
    assert record['source_sha256'] == source_record(), 'source changed during build'
    binary = folder / 'bench'
    shutil.copy2(serial.ROOT / 'cpp/build/release/gagp_fixed_asgp_bench', binary)
    record.update(status='ok', finished_epoch=time.time(), binary_sha256=serial.sha(binary))
    serial.save(folder / 'manifest.json', record)
    return binary


def inputs(task, population):
    item = serial.read(serial.ROOT / 'logs/day24-20261017/manifest.json')['inputs'][task]
    prefix = item['prefixes'][str(population)]
    for path, digest in ((item['source'], item['source_sha256']),
                         (item['grammar'], item['grammar_sha256']),
                         (prefix['prepared'], prefix['sha256'])):
        assert serial.sha(path) == digest
    return item['source'], prefix['prepared'], item['grammar']


def run_search(stage, task, population, block, generations, profile=False):
    source, prepared, grammar = inputs(task, population)
    tag = f'{task}.p{population}.b{block}.s0.g{generations}'
    output = stage.out / (tag + '.json')
    export = stage.out / (tag + '.export.json')
    history = stage.out / (tag + '.history.json')
    extra = dict(GAGP_SEARCH_GENERATIONS=str(generations), GAGP_BM_SEED='0',
                 GAGP_BM_CASE_COUNT='1024', GAGP_BM_GPU_BLOCKSIZE=str(block),
                 GAGP_GPU_PRIVATE_TUPLE_SCORE='1', GAGP_SEARCH_EXPORT=str(export),
                 GAGP_SEARCH_HISTORY_EXPORT=str(history))
    prefix = ()
    if profile:
        extra['GAGP_SEARCH_EXPORT_BEST_ONLY'] = '1'
        prefix = ('nsys', 'profile', '--trace=cuda', '--sample=none', '--cpuctxsw=none',
                  '--force-overwrite=false', '-o', str(stage.out / tag))
    job = stage.run(tag, ['measure', source, prepared, grammar, population,
                         'search_gpu_overlap', output], extra, timeout=1800,
                    reserve=RESERVE, prefix=prefix)
    with gpu_lock():
        job.update(task=task, population=population, blocksize=block, generations=generations,
                   profiled=profile, output=str(output), export=str(export), history=str(history))
        if job['status'] == 'ok':
            try:
                data = serial.read(output)
                job.update(**validate(data, 'gpu_overlap', population, generations, False))
                assert data['gpu_blocksize'] == data['cpu_audit_reduction_lanes'] == block
                job['fidelity_ok'] = (job['cpu_audit_fitness_mismatches'] ==
                                      job['cpu_audit_wrong_mismatches'] == 0)
                for field, path in (('output', output), ('export', export), ('history', history)):
                    job[field + '_sha256'] = serial.sha(path)
            except Exception as error:
                job.update(status='invalid_output', validation_error=repr(error))
        stage.persist()
    return job


def tune(binary, settings):
    stage = serial.Stage('launch-geometry-development', binary, settings=settings)
    for ti, task in enumerate(TASKS):
        for pi, population in enumerate(POPULATIONS):
            order = (ti + pi) % len(BLOCKS)
            for block in BLOCKS[order:] + BLOCKS[:order]:
                run_search(stage, task, population, block, 8)
    with gpu_lock():
        comparisons = []
        for task in TASKS:
            for population in POPULATIONS:
                base = stage.m['jobs'][f'{task}.p{population}.b512.s0.g8']
                for block in BLOCKS:
                    candidate = stage.m['jobs'][f'{task}.p{population}.b{block}.s0.g8']
                    row = dict(task=task, population=population, blocksize=block, valid=False)
                    if base['status'] == candidate['status'] == 'ok':
                        a, b = [serial.read(j['output']) for j in (base, candidate)]
                        row.update(final_descriptors_equal=a['final_population'] == b['final_population'],
                                   champion_fitness_equal=[r['best'] for r in a['generations']] ==
                                                         [r['best'] for r in b['generations']],
                                   final_full_ast_equal=base['export_sha256'] == candidate['export_sha256'],
                                   speedup=base['search_s'] / candidate['search_s'])
                        row['valid'] = (base['fidelity_ok'] and candidate['fidelity_ok'] and
                                        row['final_descriptors_equal'] and row['champion_fitness_equal'] and
                                        row['final_full_ast_equal'])
                    comparisons.append(row)
        ranking = []
        for block in BLOCKS:
            rows = [r for r in comparisons if r['blocksize'] == block]
            valid = len(rows) == len(TASKS) * len(POPULATIONS) and all(r['valid'] for r in rows)
            ratios = [r['speedup'] for r in rows if r.get('speedup')]
            # One global setting, no per-task selection; retain 512 unless every
            # fixed development cell passes and no cell regresses more than 10%.
            admissible = valid and min(ratios) >= 1 / 1.1
            ranking.append(dict(blocksize=block, valid=valid, admissible=admissible,
                                geometric_mean=math.exp(sum(map(math.log, ratios)) / len(ratios)) if ratios else None,
                                minimum=min(ratios) if ratios else None))
        admitted = [r for r in ranking if r['admissible']]
        chosen = max(admitted, key=lambda r: r['geometric_mean'])['blocksize'] if admitted else None
        serial.save(serial.RAW / 'launch-geometry-selection.json',
                    dict(selected_blocksize=chosen, selection_epoch=time.time(),
                         basis='fixed development training workloads, fidelity and unprofiled wall only',
                         formal_freeze=False, binary_sha256=stage.m['binary_sha256'],
                         comparisons=comparisons, ranking=ranking))
        stage.m.update(status='requested_phase_complete', finished_epoch=time.time())
        stage.persist()


def main():
    parser = ArgumentParser(description=__doc__)
    parser.parse_args()
    controller_lock = (serial.RAW / 'development.lock').open('a')
    fcntl.flock(controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
    controller = serial.RAW / 'development-controller.json'
    if controller.exists():
        raise RuntimeError('development controller record already exists; inspect it before recovery')
    record = dict(pid=os.getpid(), status='waiting_pilot', started_epoch=serial.START,
                  deadline_epoch=serial.DEADLINE, prerequisite_pid=3196611)
    serial.save(controller, record)
    try:
        while True:
            pilot = serial.read(serial.RAW / 'pilot16384/manifest.json')
            running = [j['pid'] for j in pilot['jobs'].values()
                       if j.get('pid') and alive(j['pid'])]
            if not alive(3196611) and not running:
                break
            if time.time() >= serial.DEADLINE - RESERVE:
                raise RuntimeError('development budget exhausted while waiting for pilot')
            time.sleep(10)
        assert pilot['status'] == 'requested_phase_complete', 'pilot exited incompletely'
        assert len(pilot['jobs']) == 9 and all(j['status'] == 'ok' for j in pilot['jobs'].values()), 'pilot work failure'
        assert all(j.get('validation') == 'work_complete' for j in pilot['jobs'].values()), 'pilot fidelity failure'
        comparisons = pilot.get('comparisons', {})
        assert len(comparisons) == 3 and all(
            c['final_full_ast_equal'] and c['final_descriptors_equal'] and c['champion_fitness_equal']
            for c in comparisons.values()), 'pilot execution paths differ'
        with gpu_lock():
            record['status'] = 'build_and_adapter'; serial.save(controller, record)
            binary = build_adapter()
        protocol = serial.read(serial.BASE / 'protocol.json')
        record['status'] = 'profiling'; serial.save(controller, record)
        profile = serial.Stage('nsys-development', binary, settings=protocol['settings'])
        for task in ('is_sorted', 'filter_odd'):
            run_search(profile, task, 16384, 512, 2, profile=True)
        profile.m.update(status='requested_phase_complete', finished_epoch=time.time()); profile.persist()
        record['status'] = 'tuning'; serial.save(controller, record)
        tune(binary, protocol['settings'])
        record.update(status='requested_phase_complete', finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped', error=repr(error), finished_epoch=time.time())
        raise
    finally:
        serial.save(controller, record)


if __name__ == '__main__':
    main()

