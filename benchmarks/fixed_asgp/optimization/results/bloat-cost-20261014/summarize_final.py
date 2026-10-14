"""Summarize archived complete searches; read-only inputs, no GPU work or preparation."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[5]
FROZEN = {'house_robber': 27687.368225, 'median': 5864.915193}
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
STAGES = ('generation_ms', 'eval_ms', 'repro_ms', 'eval_kernel_ms',
          'repro_kernel_ms', 'repro_decode_ms', 'pack_ms', 'upload_ms')


def read(path):
    return json.loads(Path(path).read_text())


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def resources(path):
    if not path.exists():
        return {'available': False, 'path': str(path)}
    result = {'available': True, 'path': str(path)}
    for line in path.read_text().replace('\\n', '\n').splitlines():
        key, sep, value = line.partition('=')
        if sep:
            try:
                result[key] = float(value) if '.' in value else int(value)
            except ValueError:
                result[key] = value
    return result


def check_search(cell, data):
    assert data['population'] == data['cases'] == 1024, cell['tag']
    rows = data['generations']
    assert len(rows) == cell['generations'], cell['tag']
    assert len(data['final_population']) == 1024, cell['tag']
    assert all(g['expected_program_cases'] == 1024 * 1024 for g in rows), cell['tag']
    if cell['mode'].startswith('gpu'):
        assert all(g['case_counters_available'] and g['cases'] == 1024 * 1024 and
                   g['unscored'] == 0 for g in rows), cell['tag']
    assert data.get('cpu_audit_mismatches', 0) == 0, cell['tag']
    if cell['mode'] in ('gpu_repro', 'gpu_overlap'):
        assert not data.get('reproduction_fallback_reason'), cell['tag']
        assert data['reproduction_profile'].startswith('native-gpu-phase'), cell['tag']


def stages(data):
    rows = data['generations']
    result = {k: sum(g[k] for g in rows) for k in STAGES if all(k in g for g in rows)}
    result.update({k: data[k] for k in ('initial_admission_ms', 'gpu_init_ms', 'final_eval_ms') if k in data})
    for name, group in [('early16', rows[:16]), ('late16', rows[-16:])]:
        result[name + '_count'] = len(group)
        for k in ('generation_ms', 'eval_ms', 'repro_ms'):
            if all(k in g for g in group):
                result[name + '_' + k] = st.median(g[k] for g in group)
    return result


def sample(cell):
    data = read(cell['output'])
    check_search(cell, data)
    row = {k: cell[k] for k in ('tag', 'task', 'mode', 'seed', 'generations', 'output',
                               'manifest', 'binary_sha256', 'source_head', 'reused_control', 'arm', 'repeat')}
    row.update(evolve_wall_ms=data['evolve_wall_ms'], process_wall_s=cell['process_wall_s'],
               final_best=data['final_best'], stages_ms=stages(data),
               resources=resources(Path(cell['output']).with_suffix('.resources')),
               genotype_device_bytes_reported=data.get('genotype_device_bytes'),
               case_counters_available_all_generations=all(g.get('case_counters_available', False) for g in data['generations']),
               unique_final_genomes=data.get('unique_final_genomes'),
               final_nodes={'min': min(x['nodes'] for x in data['final_population']),
                            'median': st.median(x['nodes'] for x in data['final_population']),
                            'max': max(x['nodes'] for x in data['final_population'])},
               audit={k: data[k] for k in ('cpu_audit_mismatches', 'cpu_audit_program_count',
                                           'reproduction_profile', 'reproduction_fallback_reason') if k in data},
               generation_totals={k: sum(g[k] for g in data['generations']) for k in
                                  ('cases', 'expected_program_cases', 'errors', 'timeouts', 'unscored',
                                   'fallbacks', 'operator_fallbacks', 'contract_rejections', 'budget_rejections')
                                  if all(k in g for g in data['generations'])})
    return row, data['generations']


def summarize(samples, task, mode):
    vals = [s['evolve_wall_ms'] for s in samples]
    keys = set.intersection(*(set(s['stages_ms']) for s in samples))
    return dict(task=task, mode=mode, count=len(samples), repeats=samples,
                raw_ms=vals, median_ms=st.median(vals), range_ms=[min(vals), max(vals)],
                process_median_s=st.median(s['process_wall_s'] for s in samples),
                process_range_s=[min(s['process_wall_s'] for s in samples), max(s['process_wall_s'] for s in samples)],
                frozen_asgp_ratio=FROZEN[task] / st.median(vals),
                diagnostic_stage_medians_ms={k: st.median(s['stages_ms'][k] for s in samples) for k in sorted(keys)})


def compare(before, after):
    a, b = read(before['output']), read(after['output'])
    check_search(before, a); check_search(after, b)
    def semantic_rows(data):
        return [{k: v for k, v in g.items() if not k.endswith('_ms') and not k.endswith('_bytes')}
                for g in data['generations']]
    assert semantic_rows(a) == semantic_rows(b), ('generation semantic mismatch', before['tag'])
    for k in ('final_best', 'final_population', 'unique_final_genomes', 'top16_cpu'):
        assert a.get(k) == b.get(k), (k, before['tag'])
    proofs = {'generation_semantic_sha256': digest(semantic_rows(a))}
    for suffix in ('final', 'history'):
        aa = read(Path(before['output']).with_suffix('.' + suffix + '.json'))
        bb = read(Path(after['output']).with_suffix('.' + suffix + '.json'))
        if suffix == 'history':
            # Only wall-clock completion timestamps differ in history exports.
            aa = {'history': [{k: v for k, v in x.items() if k != 'elapsed_search_ms'} for x in aa['history']]}
            bb = {'history': [{k: v for k, v in x.items() if k != 'elapsed_search_ms'} for x in bb['history']]}
        assert aa == bb, (suffix + ' AST mismatch', before['tag'])
        proofs[suffix + '_semantic_sha256'] = digest(aa)
        proofs[suffix + '_program_count'] = len(aa['programs' if suffix == 'final' else 'history'])
    assert before['frozen_sha256'] == after['frozen_sha256'], 'frozen input identity changed'
    return dict(task=after['task'], seed=after['seed'], repeat=after['repeat'],
                before_ms=a['evolve_wall_ms'], after_ms=b['evolve_wall_ms'],
                delta_ms=b['evolve_wall_ms'] - a['evolve_wall_ms'],
                reduction_pct=100 * (1 - b['evolve_wall_ms'] / a['evolve_wall_ms']),
                before_process_s=before['process_wall_s'], after_process_s=after['process_wall_s'],
                exact_semantics=True, **proofs)


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, default=ROOT)
    p.add_argument('--runs', type=Path, help='Default ROOT/logs/optimization/bloat-cost-20261014/validation/runs')
    p.add_argument('--out', type=Path, default=Path(__file__).resolve().parent)
    p.add_argument('--allow-incomplete', action='store_true', help='Diagnostic partial report; complete=false, never silently a final report')
    args = p.parse_args()
    root = args.root.resolve(); runs = args.runs or root/'logs/optimization/bloat-cost-20261014/validation/runs'
    prior = root/'logs/optimization/astra-20261014'
    missing, provenance, generation_rows = [], {}, []
    def load(path, arm, repeat=None, reused=False, modes=None):
        path = Path(path)
        if not path.exists():
            missing.append(str(path)); return []
        manifest = read(path)
        provenance[str(path)] = {k: manifest[k] for k in ('binary', 'binary_sha256', 'source_head', 'source_patch', 'flags', 'frozen_sha256', 'timing', 'contract') if k in manifest}
        result = []
        for cell in manifest['cells']:
            if modes is not None and cell['mode'] not in modes:
                continue
            if cell.get('status') != 'ok':
                missing.append(str(path) + ':' + cell['tag']); continue
            result.append(dict(cell, manifest=str(path), binary_sha256=manifest['binary_sha256'],
                               source_head=manifest['source_head'], frozen_sha256=manifest['frozen_sha256'],
                               reused_control=reused, arm=arm,
                               repeat=repeat if repeat is not None else int(cell['tag'].rsplit('.r', 1)[1])))
        return result
    controls = load(prior/'final-controls/manifest.json', 'archived-control', reused=True, modes=('asgp_1t', 'gagp_cpu'))
    gpu = load(runs/'gpu-three/manifest.json', 'after', modes=('gpu_eval', 'gpu_repro'))
    before, after = [], []
    for repeat in range(9):
        before += load(runs/f'before-{repeat}/manifest.json', 'before', repeat)
        after += load(runs/f'after-{repeat}/manifest.json', 'after', repeat)
    rows = []
    for task in FROZEN:
        for mode in MODES:
            group = [c for c in controls + gpu + after if c['task'] == task and c['mode'] == mode]
            expected = 9 if mode == 'gpu_overlap' else 3
            if len(group) != expected:
                missing.append(f'{task}/{mode}: {len(group)}/{expected}')
            samples = []
            for cell in group:
                s, gs = sample(cell); samples.append(s)
                generation_rows.extend(dict(task=task, mode=mode, repeat=cell['repeat'],
                                            arm=cell['arm'], seed=cell['seed'], reused_control=cell['reused_control'],
                                            manifest=cell['manifest'], binary_sha256=cell['binary_sha256'], **g) for g in gs)
            if samples:
                rows.append(summarize(samples, task, mode))
    before_rows = []
    for task in FROZEN:
        samples = []
        for cell in before:
            if cell['task'] != task:
                continue
            ss, gs = sample(cell); samples.append(ss)
            generation_rows.extend(dict(task=task, mode=cell['mode'], repeat=cell['repeat'],
                                        arm='before', seed=cell['seed'], reused_control=False,
                                        manifest=cell['manifest'], binary_sha256=cell['binary_sha256'], **g) for g in gs)
        if samples:
            before_rows.append(summarize(samples, task, 'gpu_overlap'))
    pairs = []
    for a in before:
        matches = [b for b in after if (a['task'], a['seed'], a['repeat']) == (b['task'], b['seed'], b['repeat'])]
        if len(matches) == 1:
            pairs.append(compare(a, matches[0]))
    if len(pairs) != 18:
        missing.append(f'paired short searches: {len(pairs)}/18')
    paired_summary = []
    for task in FROZEN:
        cells = [c for c in pairs if c['task'] == task]
        if cells:
            paired_summary.append(dict(task=task, pairs=cells,
                before_median_ms=st.median(c['before_ms'] for c in cells),
                after_median_ms=st.median(c['after_ms'] for c in cells),
                median_paired_delta_ms=st.median(c['delta_ms'] for c in cells),
                median_paired_reduction_pct=st.median(c['reduction_pct'] for c in cells)))
    old_long = load(prior/'final/long/manifest.json', 'archived-long')
    new_long = load(runs/'long/manifest.json', 'after-long')
    long_rows = []
    for cell in new_long:
        match = [c for c in old_long if (c['task'], c['seed']) == (cell['task'], cell['seed'])]
        assert len(match) == 1 and cell['generations'] == 128
        old_sample, old_gs = sample(match[0]); new_sample, gs = sample(cell)
        generation_rows.extend(dict(task=cell['task'], mode=cell['mode'], repeat=match[0]['repeat'],
                                    arm='archived-long', reused_control=True, seed=cell['seed'],
                                    manifest=match[0]['manifest'], binary_sha256=match[0]['binary_sha256'], **g) for g in old_gs)
        long_rows.append(dict(task=cell['task'], seed=cell['seed'], before=old_sample,
                              after=new_sample, parity=compare(match[0], cell)))
        generation_rows.extend(dict(task=cell['task'], mode=cell['mode'], repeat=cell['repeat'],
                                    arm='after-long', reused_control=False, seed=cell['seed'],
                                    manifest=cell['manifest'], binary_sha256=cell['binary_sha256'], **g) for g in gs)
    if len(long_rows) != 6:
        missing.append(f'128-generation task/seed cells: {len(long_rows)}/6')
    scaling_path = runs/'scaling/manifest.json'
    scaling = []
    if scaling_path.exists():
        m = read(scaling_path)
        provenance[str(scaling_path)] = {k: v for k, v in m.items() if k != 'cells'}
        for cell in m['cells']:
            if 'median_ms' not in cell:
                missing.append('unfinished scaling:' + cell['tag']); continue
            assert all(r['cases'] == 1024 for r in cell['rows'])
            scaling.append({k: cell[k] for k in ('task', 'population', 'mode', 'tag', 'median_ms', 'output', 'wall_seconds', 'resources', 'rows', 'flags') if k in cell})
    if len(scaling) != 40:
        missing.append(f'scaling: {len(scaling)}/40')
    auxiliary = {}
    for label, expected in [('formal', 6), ('daily', 5)]:
        path = runs/label/'manifest.json'
        ready = []
        if path.exists():
            manifest = read(path)
            provenance[str(path)] = {k: v for k, v in manifest.items() if k != 'cells'}
            ready = [c for c in manifest['cells'] if 'median_ms' in c]
        if len(ready) != expected:
            missing.append(f'{label}: {len(ready)}/{expected}')
        auxiliary[label] = ready
    smoke = load(runs/'five-mode-smoke/manifest.json', 'smoke')
    if len(smoke) != 10:
        missing.append(f'five-mode smoke: {len(smoke)}/10')
    auxiliary['five_mode_smoke'] = [sample(c)[0] for c in smoke]
    if after:
        expected_binary = after[0]['binary_sha256']
        assert all(c['binary_sha256'] == expected_binary for c in after + gpu + new_long + smoke), 'mixed final binaries'
        assert all(provenance[c['manifest']]['flags'] == provenance[after[0]['manifest']]['flags']
                   for c in after + gpu + new_long + smoke), 'mixed final flags'
    result = dict(complete=not missing, missing=missing, frozen_search_asgp_ms=FROZEN,
                  rows=rows, prior_best_rows=before_rows, paired_search=paired_summary, long_search=long_rows,
                  auxiliary_validation=auxiliary,
                  scaling=scaling, manifests=provenance,
                  timing='Unprofiled cold complete evolving-population search. Process wall additionally includes input parsing, audit and serialization; lock waiting excluded.',
                  fixed_parent_scope='scaling and auxiliary_validation.formal/daily use fixed-parent native generation timing, including each generation evaluation plus reproduction. Their rows include a warmup rep=-1 and measured reps0..2; reported cell median excludes warmup. Formal repeats are three fresh processes per task. These timings are not complete evolving searches and must not be divided into the frozen complete-search ASGP denominators.',
                  smoke_scope='auxiliary_validation.five_mode_smoke contains one-generation changing-population contract checks, not the headline32/64-generation performance measurement.',
                  cpu_control_reuse='ASGP1T and GAGP CPU rows reuse the previous archived final-controls binary and its recorded settings. CPU/ASGP implementation is unchanged; new flags affect native GPU reproduction only. Reuse is explicit in every sample and CSV row. Scaling controls are rerun.',
                  quality_scope='Exact same search trajectory/AST history is a semantic preservation check, not evidence of improved convergence. Errors/timeouts are reported, not silently discarded. CPU audit coverage is recorded as emitted by each run.',
                  memory_scope='peak_rss_kib is process RSS from /usr/bin/time. genotype_device_bytes_reported is the benchmark-reported genotype device allocation, not total GPU peak memory. Early/late stage medians use the first/last16 generations.')
    if after:
        metadata = provenance[after[0]['manifest']]
        result.update({k: metadata[k] for k in ('binary', 'binary_sha256', 'source_head', 'flags', 'frozen_sha256')})
        result['settings_manifest'] = after[0]['manifest']
    if missing and not args.allow_incomplete:
        raise RuntimeError('Validation incomplete:\n' + '\n'.join(missing))
    args.out.mkdir(parents=True, exist_ok=True)
    (args.out/'final.json').write_text(json.dumps(result, indent=2) + '\n')
    columns = ['task', 'mode', 'arm', 'repeat', 'generation', 'reused_control']
    columns += sorted(set().union(*(set(g) for g in generation_rows)) - set(columns))
    with (args.out/'generation-times.csv').open('w', newline='') as f:
        w = csv.DictWriter(f, fieldnames=columns); w.writeheader(); w.writerows(generation_rows)
    print('complete=', result['complete'], 'missing=', len(missing))
    for row in rows:
        print(row['task'], row['mode'], round(row['median_ms'], 3), round(row['frozen_asgp_ratio'], 3))


if __name__ == '__main__':
    main()
