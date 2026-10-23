"""Use complete current TRAINING native costs; retain the original scale rule."""
import math

import forecast_shared
import freeze_rule
import serial


def complete_native_cost(rows, tasks):
    expected = {(task, p, v) for task in tasks for p in (1024, 16384)
                for v in ('baseline', 'candidate')}
    observed = set()
    costs = []
    for row in rows:
        key = row['task'], row['population'], row['variant']
        if key not in expected or key in observed or row['status'] != 'ok':
            raise ValueError('incomplete, failed or duplicate native timing cell')
        observed.add(key)
        elapsed, process = row['elapsed_s'], row['process_s']
        if not all(math.isfinite(v) and v > 0 for v in (elapsed, process)) or elapsed < process:
            raise ValueError('invalid native timing')
        if row['variant'] == 'candidate':
            if row['actual_cpu_programs'] != 0 or not row['cpu_reference_verified']:
                raise ValueError('candidate did not prove the exact fresh paired reference')
        elif not 0 < row['actual_cpu_programs'] <= row['population']:
            raise ValueError('baseline CPU execution coverage is missing')
        costs.append(elapsed)
    if observed != expected:
        raise ValueError('complete all-task, both-population native timing grid required')
    return math.fsum(costs)


def calculate(training='training17', timing='training-timing12', preparation='preparation-timing10', now=None):
    result = forecast_shared.calculate(training, timing, preparation, now)
    path = serial.RAW / 'development-native-cost0-controller.json'
    native = serial.read(path)
    assert native['status'] == 'requested_phase_complete'
    assert native['auxiliary_snapshot_policy'] == {'GAGP_FUSED_COLD_EXECUTABLE': '0'}
    assert native['selection_sha256'] == serial.sha(serial.RAW / (training + '-development-selection.json'))
    for source, digest in native['harness_sha256'].items():
        assert serial.sha(source) == digest
    protocol = serial.read(serial.BASE / 'protocol.json')
    measured = complete_native_cost(native['rows'], protocol['tasks'])
    evidence = {str(path): serial.sha(path)}
    for variant in ('baseline', 'candidate'):
        stage_path = serial.RAW / ('development-native-cost0-' + variant) / 'manifest.json'
        stage = serial.read(stage_path)
        assert stage['status'] == 'requested_phase_complete' and len(stage['jobs']) == 60
        for job in stage['jobs'].values():
            assert job['status'] == 'ok' and job['native_fidelity_pass']
            assert not any(job['native_mismatches'].values())
            assert job['native_program_observations'] == job['population']
            assert job['native_case_observations'] == job['population'] * 1024
        evidence[str(stage_path)] = serial.sha(stage_path)
    speed, details = {}, {}
    for generations, old in result['speed_cost_breakdown'].items():
        # Preserve the original conservative G/8 extrapolation. The actual
        # G8 measurements include both native child wall and Python/JSON work;
        # no extra historical overhead or optimistic dedup discount is added.
        projected = measured * (generations / 8)
        speed[generations] = old['search_process_s'] + projected
        details[generations] = dict(search_process_s=old['search_process_s'],
            current_complete_native_call_s=measured, projected_native_call_s=projected,
            scope='120 current complete OLD TRAINING audits; exact fresh CPU reference plus fresh GPU per method; all original logical observations',
            forecast_only='formal searches and native CPU/GPU references must be newly executed')
    result.update(format='gagp-twofold-measured-native-runtime-forecast-v1',
                  speed_costs_s=speed, speed_cost_breakdown=details,
                  prior_forecast_scope='formal0 correctly refused the historical conservative forecast; this uses new complete measured native costs')
    result['cost_evidence_sha256'].update(evidence)
    result['choice'] = freeze_rule.choose_scale(result['available_s'], speed,
        result['quality_per_seed']['total_s'], fixed_s=result['final_regression_and_fixed_floor_s'])
    return result
