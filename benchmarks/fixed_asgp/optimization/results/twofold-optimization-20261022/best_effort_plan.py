"""Explicit full minimum-grid validation after the conservative plan was refused.

This never changes a failed forecast into a passing one. All30/bothP/R2 and
ten independent quality seeds remain planned; missing work remains failed or
unknown, and the report must disclose this planning deviation.
"""
import math


def minimum_grid(refusal, now):
    if refusal['choice'] is not None:
        raise ValueError('a fitting standard plan must use the standard pipeline')
    if not math.isfinite(now) or now < refusal['epoch']:
        raise ValueError('runtime planning cannot move before the recorded refusal')
    available = refusal['deadline_epoch'] - now - refusal['report_reserve_s']
    if available <= 0:
        return None
    speed = {int(k): v for k, v in refusal['speed_costs_s'].items()}
    if set(speed) != {8, 16, 32}:
        raise ValueError('complete speed cost forecasts required')
    quality = refusal['quality_per_seed']['total_s']
    fixed = refusal['final_regression_and_fixed_floor_s']
    if any(not math.isfinite(v) or v <= 0 for v in (*speed.values(), quality, fixed)):
        raise ValueError('invalid prior runtime evidence')
    raw = speed[8] + 10 * quality
    return dict(speed_generations=8, speed_repetitions=2,
                quality_generations=64, quality_seed_count=10,
                quality_seeds=list(range(25, 35)), raw_estimated_s=raw,
                estimated_total_s=fixed + 1.35 * raw, available_s=available,
                safety_factor=1.35, fixed_preparation_floor_s=fixed,
                original_forecast_passed=False, planned_full_grid=True,
                rule='explicit best effort: minimum originally allowed full grid after forecast refusal; quality first; no promised full completion or primary protocol qualification')


def calibration_allows_validation(control, stages):
    """A budget stop is unknown; any actual discrepancy blocks validation."""
    if control['status'] not in ('requested_phase_complete', 'stopped'):
        raise ValueError('calibration is not terminal')
    timed_out = False
    for stage in stages:
        for job in stage.get('jobs', {}).values():
            timed_out |= job['status'] == 'timeout'
            if job['status'] not in ('ok', 'timeout', 'budget_not_run'):
                raise ValueError('calibration has a nonbudget failure requiring inspection')
            if any(job.get('native_mismatches', {}).values()):
                raise ValueError('confirmed native discrepancy cannot be bypassed')
            if job['status'] == 'ok' and job.get('native_fidelity_pass') is not True:
                raise ValueError('completed calibration lacks native fidelity')
    if control['status'] == 'stopped':
        elapsed = control['finished_epoch'] - control['started_epoch']
        if elapsed < control['calibration_limit_s'] - 3:
            raise ValueError('calibration stopped before its declared budget cutoff')
        message = control.get('error', '')
        if not ('calibration limit reached' in message or
                (timed_out and 'native calibration failed' in message)):
            raise ValueError('calibration stop is not the declared budget condition')
    return True
