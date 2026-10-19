"""Paired cold-process timing summaries, with failed/missing records visible."""
import math
import statistics

VARIANTS = ('asgp', 'gpu_original', 'gpu_improved')
COMPARISONS = ('final_descriptors_equal', 'champion_fitness_equal', 'champion_ast_equal', 'final_export_equal')


def finite_positive(value):
    return isinstance(value, (int, float)) and math.isfinite(value) and value > 0


def summarize_cell(jobs, comparisons, task, population, repetitions):
    """Only complete paired coverage yields a headline ratio; raw jobs stay intact.

    The center is median(source time)/median(GPU time), so the displayed seconds
    reproduce the displayed ratio. Per-repetition paired min/max is descriptive
    timing variation, not a confidence interval or between-seed quality evidence.
    """
    cell = dict(task=task, population=population, requested_repetitions=repetitions,
                valid_pairs=0, requested_runs=3*repetitions, completed_runs=0,
                status_counts={}, failure_reasons=[], qualified=False)
    times = {v: [] for v in VARIANTS}
    ratios = {'asgp_vs_original': [], 'asgp_vs_improved': [], 'original_vs_improved': []}
    for repetition in range(repetitions):
        group = {v: next((j for j in jobs if j.get('task') == task and j.get('population') == population
                         and j.get('variant') == v and j.get('repetition') == repetition), None)
                 for v in VARIANTS}
        valid = True
        for variant, job in group.items():
            status = job['status'] if job else 'missing'
            cell['status_counts'][status] = cell['status_counts'].get(status, 0)+1
            if status == 'ok':
                cell['completed_runs'] += 1
            if status != 'ok' or not finite_positive(job.get('search_s') if job else None):
                cell['failure_reasons'].append(f'r{repetition}:{variant}:{status}')
                valid = False
            elif variant != 'asgp' and job.get('fidelity_ok') is not True:
                cell['failure_reasons'].append(f'r{repetition}:{variant}:fidelity_unconfirmed')
                valid = False
        comparison = comparisons.get(f'{task}.p{population}.r{repetition}', {})
        if not all(comparison.get(field) is True for field in COMPARISONS):
            cell['failure_reasons'].append(f'r{repetition}:execution_comparison_unconfirmed')
            valid = False
        if repetition == 0 and comparison.get('full_final_ast_comparison') is not True:
            cell['failure_reasons'].append('r0:full_final_ast_unconfirmed')
            valid = False
        for variant in ('gpu_original', 'gpu_improved'):
            job = group[variant]
            if repetition == 0 and (not job or job.get('full_final_cpu_audit') is not True or
                                   job.get('cpu_audit_program_count') != population):
                cell['failure_reasons'].append(f'r0:{variant}:full_final_cpu_audit_unconfirmed')
                valid = False
        if valid:
            cell['valid_pairs'] += 1
            for variant, job in group.items():
                times[variant].append(job['search_s'])
            a, b, c = (group[v]['search_s'] for v in VARIANTS)
            ratios['asgp_vs_original'].append(a/b)
            ratios['asgp_vs_improved'].append(a/c)
            ratios['original_vs_improved'].append(b/c)
    cell['qualified'] = cell['valid_pairs'] == repetitions
    # Preserve partial coverage and its raw observations, but never quietly turn
    # an incomplete requested cell into an official successful comparison.
    for variant, observations in times.items():
        cell[variant+'_s'] = statistics.median(observations) if cell['qualified'] else None
    for name, observations in ratios.items():
        cell[name] = None
        cell[name+'_paired_min'] = min(observations) if len(observations) > 1 else None
        cell[name+'_paired_max'] = max(observations) if len(observations) > 1 else None
        if cell['qualified']:
            numerator, denominator = {
                'asgp_vs_original': ('asgp', 'gpu_original'),
                'asgp_vs_improved': ('asgp', 'gpu_improved'),
                'original_vs_improved': ('gpu_original', 'gpu_improved')}[name]
            cell[name] = cell[numerator+'_s']/cell[denominator+'_s']
    return cell


def population_summary(cells, population):
    selected = [cell for cell in cells if cell['population'] == population]
    qualified = [cell for cell in selected if cell['qualified']]
    out = dict(population=population, requested_cells=len(selected), qualified_cells=len(qualified))
    for field in ('asgp_vs_original', 'asgp_vs_improved', 'original_vs_improved'):
        values = [cell[field] for cell in qualified]
        out[field+'_geomean'] = math.exp(statistics.mean(map(math.log, values))) if values else None
        out[field+'_median'] = statistics.median(values) if values else None
    return out
