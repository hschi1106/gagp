"""Project checked report evidence onto exactly the fields used by the dashboard."""
SPLITS = ('joint', 'train', 'test')
EVENTS = ('gen0', 'final', 'new_final', 'lost_initial')


def compact_payload(bundle):
    tasks = bundle['speed_freeze']['tasks']
    summary = bundle['speed']['summary']
    cells = [{key: row[key] for key in (
        'task', 'population', 'observed_baseline_search_s', 'observed_candidate_search_s',
        'speedup', 'repetitions', 'qualified', 'failures')} for row in summary['cells']]
    speed = {key: summary[key] for key in (
        'all_primary_targets_reached', 'native_audit_qualified', 'native_audit_expected')}
    speed.update(cells=cells, groups={'pooled': {key: summary['groups']['pooled'][key] for key in (
        'observed_geometric_mean', 'qualified_cells', 'expected_cells')}})
    groups = {(task, variant): dict(task=task, variant=variant, expected=0, audited=0,
        **{split: dict.fromkeys(EVENTS, 0) for split in SPLITS})
        for task in tasks for variant in ('baseline', 'candidate')}
    for row in bundle['quality']['runs']:
        key = (row['task'], row['variant'])
        if key not in groups:
            raise ValueError('unexpected dashboard task/method')
        group = groups[key]
        group['expected'] += 1
        if not row.get('quality_audited'):
            continue
        group['audited'] += 1
        for split in SPLITS:
            for event in EVENTS:
                group[split][event] += int(bool(row.get(split + '_' + event)))
    return dict(tasks=list(tasks), speed=speed, quality_counts=list(groups.values()),
                bound=bundle['quality']['clustered_bound'])
