"""Full-matrix target qualification requires independent native final audits."""
import study_statistics
import shared_cpu_reference


def speed_with_native_audits(comparisons,audits,tasks,populations,repetitions):
    result=study_statistics.speed_summary(comparisons,tasks,populations,repetitions)
    expected={(task,population,variant) for task in tasks for population in populations
              for variant in ('baseline','candidate')}
    lookup={}
    for row in audits:
        key=(row['task'],row['population'],row['variant'])
        if key not in expected or key in lookup:raise ValueError('unexpected or duplicate native audit')
        lookup[key]=row
    failures=[]
    for key in sorted(expected):
        row=lookup.get(key);population=key[1]
        valid=(row is not None and row.get('status')=='ok' and row.get('native_fidelity_pass') is True
               and row.get('native_program_observations')==population
               and row.get('native_case_observations')==population*1024
               and shared_cpu_reference.coverage_valid(row,population,1024)
               and bool(row.get('native_mismatches')) and not any(row['native_mismatches'].values()))
        if not valid:failures.append(dict(task=key[0],population=population,variant=key[2],
                                        status='missing' if row is None else row.get('status','unknown')))
    result['native_audit_expected']=len(expected)
    result['native_audit_qualified']=len(expected)-len(failures)
    result['native_audit_failures']=failures
    for cell in result['cells']:
        cell['timing_qualified']=cell['qualified']
        cell['native_audit_pass']=not any(row['task']==cell['task'] and row['population']==cell['population'] for row in failures)
        cell['qualified']=cell['timing_qualified'] and cell['native_audit_pass']
        if not cell['native_audit_pass']:cell['failures'].append('native_audit_missing_failed_or_incomplete')
    for label,group in result['groups'].items():
        group['timing_target_reached']=group['target_reached']
        group['timing_qualified_cells']=group['qualified_cells']
        group['timing_observed_geometric_mean']=group['observed_geometric_mean']
        qualified=[cell for cell in result['cells'] if cell['qualified'] and (label=='pooled' or str(cell['population'])==label)]
        group['qualified_cells']=len(qualified)
        group['observed_geometric_mean']=study_statistics.geometric_mean(cell['speedup'] for cell in qualified) if qualified else None
        group['full_coverage']=len(qualified)==group['expected_cells']
        group['native_audit_pass']=not any(label=='pooled' or str(row['population'])==label for row in failures)
        group['target_reached']=group['timing_target_reached'] and group['native_audit_pass']
    result['all_primary_targets_reached']=all(g['target_reached'] for g in result['groups'].values())
    return result
