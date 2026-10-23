"""Conditional minimum FULL-grid validation, explicitly outside the fitting rule.

Run only after the cost calibration and standard successor both exit. A real
formal1 freeze suppresses this entire attempt. Otherwise, after a budget-only
refusal, pin the originally allowed minimum grid before opening fresh data.
Quality runs first; every missing/failed audit or search remains in the grid.
This does not turn the refused forecast into primary protocol qualification.
"""
import fcntl
import os
from pathlib import Path
import time
import traceback

import best_effort_plan
import formal_shared_policy
import quality_data
import quality_dedup
import serial


def exited(data):
    for key in ('pid', 'child_pid'):
        proc = Path(f'/proc/{data.get(key)}/stat')
        if proc.exists() and proc.read_text().split()[2] != 'Z':
            return False
    return True


def main():
    name = 'best-effort1'
    record = serial.RAW / (name + '-controller.json')
    assert not record.exists()
    lease = (serial.RAW / (name + '-controller.lock')).open('a')
    fcntl.flock(lease, fcntl.LOCK_EX | fcntl.LOCK_NB)
    paths = [Path(__file__)] + [serial.BASE / (module + '.py') for module in (
        'best_effort_plan', 'formal_shared_policy', 'full_population_audit_shared_policy',
        'full_population_audit_shared_timed', 'full_population_audit_shared',
        'full_population_audit_v2', 'full_population_audit', 'shared_cpu_reference',
        'quality_dedup', 'quality_data', 'champion_audit_dedup', 'champion_audit', 'serial')]
    paths += [serial.BASE / 'protocol.json', quality_data.HELPER]
    state = dict(pid=os.getpid(), status='waiting_for_standard_budget_decision',
                 started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
                 harness_sha256={str(p): serial.sha(p) for p in paths},
                 scope='same minimum full30/bothP/G8/R2 and all30/P1024/G64/seeds25..34; quality first; acknowledged nonfitting forecast, no primary qualification')
    serial.save(record, state)
    try:
        while True:
            if list(serial.RAW.glob('formal1-*-freeze.json')):
                state.update(status='not_needed_existing_formal1', finished_epoch=time.time())
                return
            cost = serial.read(serial.RAW / 'development-native-cost0-controller.json')
            prior = serial.read(serial.RAW / 'final-pipeline6-controller.json')
            if cost['status'] in ('requested_phase_complete', 'stopped') and exited(cost) and exited(prior):
                assert prior['status'] == 'budget_insufficient' or (
                    prior['status'] == 'stopped' and cost['status'] == 'stopped'), 'inspect nonbudget standard-pipeline failure'
                break
            if time.time() >= serial.DEADLINE - 10800:
                state.update(status='budget_insufficient', finished_epoch=time.time())
                return
            time.sleep(10)
        while True:
            path = serial.RAW / 'harness-checks-attempt35' / 'manifest.json'
            checks = serial.read(path) if path.exists() else {}
            if checks.get('status') == 'failed':
                raise RuntimeError('best-effort harness guards failed')
            if checks.get('status') == 'ok' and exited(checks):
                break
            if time.time() >= serial.DEADLINE - 10800:
                raise RuntimeError('original reporting reserve reached before guards')
            time.sleep(10)
        # The parent attempt failed at the pre-data literal guard: preserve it,
        # require actual process exit and an EMPTY cohort before repairing metadata.
        parent = serial.read(serial.RAW / 'best-effort0-controller.json')
        old_quality = serial.read(serial.RAW / 'best-effort0-quality-controller.json')
        assert parent['status'] == old_quality['status'] == 'stopped'
        assert exited(parent) and exited(old_quality)
        assert 'first_heldout_use' in parent['traceback']
        empty = serial.read(serial.RAW / 'best-effort0-quality-inputs' / 'manifest.json')
        assert not empty['jobs'] and not empty.get('inputs')
        assert not list((serial.RAW / 'best-effort0-quality-inputs').rglob('pool.json'))
        assert not list((serial.RAW / 'best-effort0-quality-inputs').rglob('*.source.json'))
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            assert not list(serial.RAW.glob('formal1-*-freeze.json'))
            for path, digest in state['harness_sha256'].items():
                assert serial.sha(path) == digest
            cost_stages = [serial.read(serial.RAW / ('development-native-cost0-' + v) / 'manifest.json')
                           for v in ('baseline', 'candidate')]
            best_effort_plan.calibration_allows_validation(cost, cost_stages)
            previous = serial.RAW / 'formal1-runtime-forecast.json'
            if not previous.exists():
                previous = serial.RAW / 'formal0-runtime-forecast.json'
            refusal = serial.read(previous)
            protocol = serial.read(serial.BASE / 'protocol.json')
            assert refusal['started_epoch'] == serial.START and refusal['deadline_epoch'] == serial.DEADLINE
            assert refusal['report_reserve_s'] == protocol['reserve_s'] == 10800
            chosen = best_effort_plan.minimum_grid(refusal, time.time())
            if chosen is None:
                state.update(status='budget_insufficient', finished_epoch=time.time())
                return
            selection_path = serial.RAW / 'training17-development-selection.json'
            selection = serial.read(selection_path)
            assert selection == refusal['selection']
            selected = selection['selected']
            build = serial.read(serial.RAW / selected['build_name'] / 'manifest.json')
            assert build['status'] == 'ok' and build['binary_sha256'] == selected['binary_sha256']
            variants = {v: dict(extra=selection['variants'][v]['extra'],
                binary=protocol['baseline_binary'] if v == 'baseline' else selected['binary'],
                binary_sha256=protocol['baseline_sha256'] if v == 'baseline' else selected['binary_sha256'])
                for v in ('baseline', 'candidate')}
            for spec in variants.values():
                assert serial.sha(spec['binary']) == spec['binary_sha256']
            for path, digest in refusal['cost_evidence_sha256'].items():
                assert serial.sha(path) == digest
            plan_path = serial.RAW / (name + '-runtime-plan.json')
            assert not plan_path.exists()
            serial.save(plan_path, dict(choice=chosen, prior_refused_forecast=str(previous),
                prior_refused_forecast_sha256=serial.sha(previous), calibration=str(serial.RAW / 'development-native-cost0-controller.json'),
                calibration_sha256=serial.sha(serial.RAW / 'development-native-cost0-controller.json'),
                planning_deviation='minimum full grid is attempted despite failing the conservative fitting rule; no full-coverage promise; missing work stays unknown'))
            common = dict(started_epoch=serial.START, deadline_epoch=serial.DEADLINE,
                frozen_epoch=time.time(), settings=protocol['settings'], inputs=protocol['inputs'],
                tasks=list(serial.PAPER_TASKS), variants=variants, resources=protocol['resources'],
                reserve_s=protocol['reserve_s'], harness_sha256=state['harness_sha256'],
                candidate_selection=selection, runtime_forecast=str(plan_path),
                runtime_forecast_sha256=serial.sha(plan_path), scale_selection=chosen,
                candidate_source_sha256=build['source_sha256'],
                candidate_source_snapshot=str(Path(build['binary']).parent / 'source-delta'),
                planning_mode='best_effort_after_budget_refusal', original_forecast_passed=False)
            speed = dict(common, format='gagp-twofold-speed-freeze-v3',
                cpu_reference_sharing='exact fresh paired CPU reference; fresh GPU snapshot for every method',
                native_audit_neutral_extra={'GAGP_FUSED_COLD_EXECUTABLE': '0'},
                populations=[1024, 16384], generations=8, repetitions=2, cases=1024, timeout_s=3600)
            quality = dict(common, format='gagp-twofold-quality-freeze-v1', population=1024,
                generations=64, seeds=list(range(25, 35)), data_seed=20261022,
                train_limit=200, test_limit=1000, first_heldout_use='after immutable candidate/scale freeze')
            speed_path = serial.RAW / (name + '-speed-freeze.json')
            quality_path = serial.RAW / (name + '-quality-freeze.json')
            parent_speed_path = serial.RAW / 'best-effort0-speed-freeze.json'
            parent_quality_path = serial.RAW / 'best-effort0-quality-freeze.json'
            parent_speed, parent_quality = serial.read(parent_speed_path), serial.read(parent_quality_path)
            for field in ('settings', 'inputs', 'tasks', 'variants', 'resources', 'reserve_s',
                          'candidate_selection', 'candidate_source_sha256', 'candidate_source_snapshot'):
                assert speed[field] == parent_speed[field] == quality[field] == parent_quality[field]
            for field in ('populations', 'generations', 'repetitions', 'cases'):
                assert speed[field] == parent_speed[field]
            for field in ('population', 'generations', 'seeds', 'data_seed', 'train_limit', 'test_limit'):
                assert quality[field] == parent_quality[field]
            repair = dict(parent_attempt='best-effort0', parent_cohort_empty=True,
                parent_speed_freeze_sha256=serial.sha(parent_speed_path),
                parent_quality_freeze_sha256=serial.sha(parent_quality_path),
                correction='canonical first_heldout_use literal only; same candidate, inputs, grid, seeds and resources; no executed sample existed')
            speed['empty_parent_recovery'] = quality['empty_parent_recovery'] = repair
            state['empty_parent_recovery'] = repair
            assert not speed_path.exists() and not quality_path.exists()
            serial.save(speed_path, speed)
            serial.save(quality_path, quality)
            state.update(status='frozen_best_effort', speed_freeze=str(speed_path), quality_freeze=str(quality_path),
                speed_freeze_sha256=serial.sha(speed_path), quality_freeze_sha256=serial.sha(quality_path),
                runtime_plan=str(plan_path), primary_protocol_qualified=False)
            serial.save(record, state)
            note = (f"\n- {time.strftime('%Y-%m-%d %H:%M:%S UTC', time.gmtime())}：保留原預估拒絕後，"
                    '明確凍結 best-effort1：同一CPP26全域候選；全30題、兩種P、G8/R2；'
                    '品質全30題P1024/G64、seeds25–34，新資料種子20261022。先品質後速度。'
                    '原安全係數1.35與完整核對不變，但預估未符合可排入規則，因此不保證跑完、不列為原規格完整達標。'
                    '所有未完成格保留；原截止與3h報告預留不變；新資料尚未建立。\n')
            progress = serial.ROOT / 'benchmarks/fixed_asgp/optimization/PROGRESS.md'
            progress.write_text(note + '\n' + progress.read_text())
        state['status'] = 'fresh_quality'
        serial.save(record, state)
        quality_dedup.main(quality_path, name + '-quality')
        state['status'] = 'fresh_speed'
        serial.save(record, state)
        formal_shared_policy.main(speed_path, name + '-speed')
        state.update(status='requested_phase_complete', finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
        raise
    finally:
        serial.save(record, state)


if __name__ == '__main__':
    main()
