"""TRAINING-only forecast using the admitted exact paired CPU-reference policy."""
import forecast
import freeze_rule
import serial


def calculate(training='training11',timing='training-timing6',preparation='preparation-timing4',now=None):
    result=forecast.calculate(training,timing,preparation,now)
    smoke_path=serial.RAW/'shared-cpu-smoke0-controller.json';smoke=serial.read(smoke_path)
    assert smoke['status']=='ok' and len(smoke['rows'])==5
    small=[row for row in smoke['rows'] if row['population']==1024]
    large=[row for row in smoke['rows'] if row['population']==16384]
    assert len(small)==4 and len(large)==1
    assert all(row.get('new_cpu_executions',row.get('new_candidate_cpu_executions'))==0 for row in smoke['rows'])
    # Extrapolate the SLOWEST measured whole GPU-only child cost per member,
    # retaining its cold cost at every1024 members. This intentionally exceeds
    # the observed P16384 sum cost. Budget the overhead for BOTH methods.
    per_member=max(row['process_s']/row['population'] for row in smoke['rows'])
    tasks=serial.read(serial.BASE/'protocol.json')['tasks']
    total_members=2*len(tasks)*(1024+16384)
    speed={};details={}
    for generations,old in result['speed_cost_breakdown'].items():
        # One new reference per exact paired population, not two CPU repeats.
        # All candidates still get a fresh GPU snapshot. A failed identity
        # proof falls back to its own CPU execution and can consume reserve;
        # no failure or uncovered member may be discarded in the report.
        cpu=old['fresh_full_native_cpu_forecast_s']/2
        other=max(600.,per_member*total_members)*(generations/8)
        speed[generations]=old['search_process_s']+cpu+other
        details[generations]=dict(search_process_s=old['search_process_s'],
            fresh_single_paired_cpu_reference_s=cpu,fresh_native_gpu_construction_forecast_s=other,
            cpu_reference_policy='exact same AST/order/cases/grammar/CPU settings only; fresh percell reference in this round',
            failed_identity_policy='own fresh CPU audit; retain failures/budget_not_run; no historical CPU result reuse')
    result.update(format='gagp-twofold-shared-reference-runtime-forecast-v1',speed_costs_s=speed,speed_cost_breakdown=details,
        shared_reference_smoke=str(smoke_path),shared_reference_smoke_sha256=serial.sha(smoke_path),
        measured_gpu_only_process_s_per_member=per_member)
    result['cost_evidence_sha256'][str(smoke_path)]=serial.sha(smoke_path)
    result['choice']=freeze_rule.choose_scale(result['available_s'],speed,result['quality_per_seed']['total_s'],
        fixed_s=result['final_regression_and_fixed_floor_s'])
    return result
