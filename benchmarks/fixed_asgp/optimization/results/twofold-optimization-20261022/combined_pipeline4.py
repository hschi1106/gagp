"""Measure SoA on the best COMPLETE global recipe only after coarse admission."""
import fcntl
import os
from pathlib import Path
import time
import traceback

import best_selection_final_recipe
import combined4_pilot
import serial


name = 'combined4'
record = serial.RAW / (name + '-controller.json')
assert not record.exists()
paths = [Path(__file__), serial.BASE / 'combined4_pilot.py', serial.BASE / 'best_selection_final_recipe.py',
         serial.BASE / 'best_selection_extra_recipe.py', serial.BASE / 'serial.py', serial.BASE / 'protocol.json']
state = dict(pid=os.getpid(), status='waiting_for_full30_and_cell_soa', started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE, harness_sha256={str(p): serial.sha(p) for p in paths},
             rule='fixed cell-SoA coarse >=2% vs BOTH controls; compose on best COMPLETE60-cell '
                  'measured recipe; NEW full30/bothP/G8/fresh731 controls and exact fidelity; '
                  'native/coarse rejection keeps prior successful candidates and all failed evidence')
serial.save(record, state)
try:
    controls = {}
    for component in ('cell-soa0', 'combined3'):
        while True:
            path = serial.RAW / (component + '-controller.json')
            data = serial.read(path) if path.exists() else {}
            proc = Path(f'/proc/{data.get("pid")}/stat')
            exited = not proc.exists() or proc.read_text().split()[2] == 'Z'
            if component != 'cell-soa0' and data.get('status') == 'stopped':
                raise RuntimeError('prior complete recipe chain stopped: ' + component)
            if data.get('status') in ('requested_phase_complete', 'stopped') and exited:
                controls[component] = data
                break
            if time.time() > serial.DEADLINE - 10800:
                raise RuntimeError('combined4 prerequisite missed original budget')
            time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        for path, digest in state['harness_sha256'].items():
            assert serial.sha(path) == digest
        coarse = controls['cell-soa0']
        evidence = {str(serial.RAW / 'cell-soa0-controller.json'): serial.sha(serial.RAW / 'cell-soa0-controller.json')}
        if coarse['status'] == 'stopped':
            state.update(status='requested_phase_complete', run_full=False, rejected_component=True,
                         reason='new SoA component failed native/provenance/coarse admission; preserve attempt, retain prior recipes',
                         component_error=coarse.get('error'), evidence_sha256=evidence, finished_epoch=time.time())
        else:
            path = serial.RAW / 'cell-soa0-development-selection.json'
            choice = serial.read(path)
            comparisons = serial.RAW / 'cell-soa0-coarse-comparisons.json'
            assert serial.sha(comparisons) == choice['comparisons_sha256']
            evidence.update({str(path): serial.sha(path), str(comparisons): serial.sha(comparisons)})
            if not choice['enabled']:
                state.update(status='requested_phase_complete', run_full=False,
                             reason='SoA did not pass fixed coarse rule; no full30 tail',
                             evidence_sha256=evidence, finished_epoch=time.time())
            else:
                prior = best_selection_final_recipe.select()
                build_path = serial.RAW / 'build-attempt23/manifest.json'
                build = serial.read(build_path)
                admission_path = serial.RAW / 'cell-soa-admission0/manifest.json'
                admission = serial.read(admission_path)
                assert build['status'] == admission['status'] == 'ok'
                assert serial.sha(build['binary']) == build['binary_sha256'] == admission['binary_sha256']
                evidence.update({str(build_path): serial.sha(build_path), str(admission_path): serial.sha(admission_path)})
                extra = dict(prior['selected']['extra'], GAGP_GPU_PRIVATE_CELL_SOA='1', GAGP_FUSED_COLD_EXECUTABLE='0')
                chosen = dict(extra=extra, binary=build['binary'], binary_sha256=build['binary_sha256'],
                              build_name='build-attempt23', base_selection=prior, cell_soa_selection=choice,
                              evidence_sha256=evidence, epoch=time.time(), scope=state['rule'])
                settings = serial.RAW / 'combined4-settings.json'
                assert not settings.exists()
                serial.save(settings, chosen)
                state.update(status='all30_p1024', run_full=True, selection=chosen,
                             settings=str(settings), settings_sha256=serial.sha(settings))
                protocol = serial.read(serial.BASE / 'protocol.json')
        serial.save(record, state)
    if state['run_full']:
        for population in (1024, 16384):
            state['status'] = f'all30_p{population}'
            serial.save(record, state)
            combined4_pilot.run(build['binary'], f'{name}-p{population}', [population], protocol['tasks'],
                                'candidate', pool_mode=int(extra.get('GAGP_GPU_CUDA_POOL', '0')))
        state.update(status='requested_phase_complete', finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
