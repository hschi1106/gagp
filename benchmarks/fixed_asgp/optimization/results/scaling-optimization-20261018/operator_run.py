"""One bounded isolated operator diagnosis after final source build, no search reruns."""
from argparse import ArgumentParser
import collections
import os
from pathlib import Path
import shutil
import subprocess
import time
import serial
from development import alive, gpu_lock, source_record


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--attempt', type=int, default=0)
    args = parser.parse_args()
    stage_name = f'operator-diagnosis-attempt{args.attempt}'
    controller = serial.RAW/(stage_name+'-controller.json')
    assert not controller.exists(), 'preserve existing attempt; no automatic retry'
    record = dict(pid=os.getpid(), status='waiting_final_build', deadline_epoch=serial.DEADLINE)
    serial.save(controller, record)
    folder = serial.RAW/stage_name
    try:
        path = serial.RAW/'final-build-attempt0/manifest.json'
        while True:
            build = serial.read(path)
            if build.get('pid') and not alive(build['pid']):
                break
            assert time.time() < serial.DEADLINE-12*3600, 'insufficient formal budget waiting for build'
            time.sleep(10)
        assert build['status'] == 'ok', 'final source build/tests failed; inspect preserved attempt'
        for p in serial.RAW.glob('*controller.json'):
            other = serial.read(p)
            assert other['pid'] == os.getpid() or not alive(other['pid']), 'other controller alive: '+str(p)
        with gpu_lock():
            assert source_record() == build['source_sha256'], 'source changed since final build'
            folder.mkdir()
            for name in ('operator_diagnosis.cu', 'operator_run.py', 'operator-plan.json'):
                shutil.copy2(serial.BASE/name, folder/name)
            plan = serial.read(folder/'operator-plan.json')
            object_path = folder/'operator.o'
            binary = folder/'operator'
            commands = [
                ['/usr/local/cuda-12.6/bin/nvcc', '-std=c++20', '-O3', '-DNDEBUG',
                 '-DGAGP_HAS_CUDA=1', '-DGAGP_ASGP_LIST_COMBINE_EXTENSION=1',
                 '-Icpp/include', '-Icpp/src/bench/fixed_asgp', '-Icpp/src/evolution/repro/gpu',
                 '-I/home/hschi1106/r13921069/src', '-c', str(folder/'operator_diagnosis.cu'), '-o', str(object_path)],
                ['/usr/bin/c++', '-O3', str(object_path),
                 'cpp/build/release/CMakeFiles/gagp_fixed_asgp_bench.dir/src/bench/fixed_asgp/adapter.cpp.o',
                 '-o', str(binary), '-L/usr/local/cuda-12.6/targets/x86_64-linux/lib',
                 *[str(Path('cpp/build/release')/name) for name in (
                     'libgagp_cli_support.a', 'libgagp_gpu.a', 'external-asgp/libasgp_core.a',
                     'libgagp_evolution.a', 'libgagp_gpu.a', 'libgagp_runtime_cpu.a',
                     'libgagp_grammar.a', 'libgagp_region_plan_json.a', 'libgagp_json.a', 'libgagp_core.a')],
                 '-lcrypto', '-lcudadevrt', '-lcudart_static', '-lrt', '-lpthread', '-ldl']]
            manifest = dict(status='building', pid=os.getpid(), commands=commands,
                started_epoch=time.time(), deadline_epoch=serial.DEADLINE, jobs={},
                source_sha256=build['source_sha256'], plan_sha256=serial.sha(folder/'operator-plan.json'))
            serial.save(folder/'manifest.json', manifest)
            env = {k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
            env.update(serial.read(serial.BASE/'protocol.json')['settings'])
            for index, command in enumerate(commands):
                with (folder/f'build{index}.log').open('x') as log:
                    result = subprocess.run(command, cwd=serial.ROOT, env=env, stdout=log,
                                            stderr=subprocess.STDOUT, timeout=600)
                manifest.setdefault('returncodes', []).append(result.returncode)
                serial.save(folder/'manifest.json', manifest)
                assert result.returncode == 0, 'operator diagnosis build failed; preserve attempt'
            manifest['binary_sha256'] = serial.sha(binary)
            libraries = [Path(p) for p in commands[1] if p.endswith(('.a', '.o'))]
            manifest['linked_input_sha256'] = {str(p):serial.sha(p) for p in libraries}
            manifest['status'] = 'running'
            record['status'] = 'running'
            serial.save(controller, record)
            day24 = serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')
            cutoff = min(time.time()+plan['budget_s'], serial.DEADLINE-12*3600)
            for task in serial.PAPER_TASKS:
                item = day24['inputs'][task]
                output = folder/(task+'.json')
                job = manifest['jobs'][task] = dict(status='budget_not_run', output=str(output))
                if time.time() < cutoff:
                    for field in ('source', 'grammar'):
                        assert serial.sha(item[field]) == item[field+'_sha256'], 'frozen input changed'
                    job['input_sha256'] = {item[field]:item[field+'_sha256'] for field in ('source','grammar')}
                    command = [str(binary), item['source'], item['grammar'], str(output)]
                    job.update(command=command, status='running', started_epoch=time.time())
                    serial.save(folder/'manifest.json', manifest)
                    try:
                        with (folder/(task+'.log')).open('x') as log:
                            result = subprocess.run(command, cwd=serial.ROOT, env=env, stdout=log,
                                stderr=subprocess.STDOUT, timeout=min(plan['per_task_timeout_s'],cutoff-time.time()))
                        job.update(status='ok' if result.returncode == 0 else 'failed', returncode=result.returncode)
                        if result.returncode == 0:
                            data = serial.read(output)
                            assert data['task'] == task
                            job.update(rows=len(data['rows']), row_statuses=dict(collections.Counter(r['status'] for r in data['rows'])),
                                       output_sha256=serial.sha(output))
                    except subprocess.TimeoutExpired:
                        job['status'] = 'timeout'
                    job['process_s'] = time.time()-job['started_epoch']
                serial.save(folder/'manifest.json', manifest)
            assert source_record() == build['source_sha256'], 'runtime source changed during diagnosis'
            manifest.update(status='requested_phase_complete', finished_epoch=time.time())
            serial.save(folder/'manifest.json', manifest)
        record.update(status='requested_phase_complete', finished_epoch=time.time())
    except Exception as error:
        record.update(status='stopped', error=repr(error), finished_epoch=time.time())
        if (folder/'manifest.json').exists():
            manifest = serial.read(folder/'manifest.json')
            manifest.update(status='failed', error=repr(error), finished_epoch=time.time())
            serial.save(folder/'manifest.json', manifest)
        raise
    finally:
        serial.save(controller, record)


if __name__ == '__main__':
    main()
