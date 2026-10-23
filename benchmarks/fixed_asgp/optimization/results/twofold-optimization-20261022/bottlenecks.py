"""Generation timer decomposition and explicit, unmeasured Amdahl scenarios."""
import math


def components(document):
    rows=document['generations']
    if not rows:raise ValueError('empty generation timing grid')
    result=dict(search_s=document['evolve_wall_ms']/1000,
        initial_admission_s=document['initial_admission_ms']/1000,
        gpu_init_s=document['gpu_init_ms']/1000,final_eval_export_s=document['final_eval_ms']/1000)
    for key,field in [('generation_s','generation_ms'),('eval_s','eval_ms'),('repro_s','repro_ms'),
        ('eval_kernel_s','eval_kernel_ms'),('pack_s','pack_ms'),('upload_s','upload_ms'),
        ('repro_kernel_s','repro_kernel_ms'),('repro_decode_s','repro_decode_ms')]:
        result[key]=math.fsum(row[field] for row in rows)/1000
    if any(not math.isfinite(x) or x<0 for x in result.values()) or result['search_s']<=0:
        raise ValueError('invalid nonnegative timing decomposition')
    result['eval_share_of_search']=result['eval_s']/result['search_s']
    result['repro_share_of_search']=result['repro_s']/result['search_s']
    result['profiles']=sorted({row['execution_profile'] for row in rows})
    result['physical_program_cases']=sum(row.get('physical_program_cases',row['expected_program_cases']) for row in rows)
    result['logical_program_cases']=sum(row['expected_program_cases'] for row in rows)
    return result


def scenario(baseline,candidate,evaluation_factor=2.):
    if evaluation_factor<1 or not math.isfinite(evaluation_factor):raise ValueError('invalid what-if evaluation factor')
    before=baseline['search_s'];wall=candidate['search_s'];evaluation=candidate['eval_s']
    # This is a stated what-if using generation wall timers, not a measured
    # speedup or a proof of a hardware bound. Final evaluation is not scaled.
    remaining=wall-evaluation+evaluation/evaluation_factor
    if remaining<=0 or evaluation>wall+.01:return dict(valid=False,reason='timers not suitable for additive scenario')
    needed=(wall-before/2)/evaluation if evaluation>0 else None
    return dict(valid=True,evaluation_factor=evaluation_factor,scenario_search_s=remaining,
        scenario_speedup_over_baseline=before/remaining,
        required_generation_eval_fraction_removed_for2x=needed,
        scope='unmeasured additive what-if, generation eval only; every other timer fixed; never headline evidence')
