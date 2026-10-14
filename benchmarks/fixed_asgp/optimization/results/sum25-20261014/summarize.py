"""Read frozen Sum25 validation artifacts; write a NEW JSON/CSV report directory.

Example: python3 summarize.py --root MAIN --out NEW_REPORT_DIRECTORY
Missing cells fail unless --allow-incomplete; semantic mismatches always fail.
No benchmark, GPU, preparation, or historical artifact mutation is performed.
"""
import argparse
import csv
import hashlib
import importlib.util
import json
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[5]
TASKS = ('sum_of_elements', 'house_robber', 'median')
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')


def read(path):
    return json.loads(Path(path).read_text())


def semantic(value):
    if isinstance(value, dict):
        return {k: semantic(v) for k, v in value.items()
                if not k.endswith(('_ms', '_bytes')) and k != 'execution_profile'}
    if isinstance(value, list):
        return [semantic(v) for v in value]
    return value


def digest(value):
    return hashlib.sha256(json.dumps(value, sort_keys=True, separators=(',', ':')).encode()).hexdigest()


def write_csv(path, rows):
    fields = sorted(set().union(*(set(r) for r in rows))) if rows else ['no_rows']
    with path.open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=fields)
        writer.writeheader()
        writer.writerows({k: json.dumps(v, sort_keys=True) if isinstance(v, (list, dict)) else v
                          for k, v in row.items()} for row in rows)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--root', type=Path, default=ROOT)
    parser.add_argument('--runs', type=Path)
    parser.add_argument('--out', type=Path, required=True, help='New directory; existing paths are refused')
    parser.add_argument('--allow-incomplete', action='store_true')
    args = parser.parse_args()
    root = args.root.resolve()
    runs = args.runs or root/'logs/optimization/sum25-20261014/validation'
    contract_path = root/'benchmarks/fixed_asgp/optimization/results/sum25-20261014/contract.json'
    contract = read(contract_path)
    helper_path = root/'benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/summarize_final.py'
    spec = importlib.util.spec_from_file_location('bloat_summary', helper_path)
    helper = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(helper)
    missing, manifests, samples, generations, comparisons = [], {}, [], [], []
    data_cache = {}

    def load(relative, arm, modes, seeds=None, repeats=1, length=None, repeat_label=0):
        path = runs/relative/'manifest.json'
        if not path.exists():
            missing.append(str(path)); return []
        manifest = read(path)
        manifests[str(path)] = {k: v for k, v in manifest.items() if k != 'cells'}
        assert manifest['frozen_sha256'] == contract['baseline_manifest']['frozen_sha256'], path
        assert manifest['flags']['GAGP_HOST_THREADS'] == '1', path
        expected = {(t, m, s, r) for t in TASKS for m in modes
                    for s in (seeds if seeds is not None else [2 if t == 'median' else 0])
                    for r in range(repeats)}
        seen, result = set(), []
        for cell in manifest['cells']:
            key = (cell['task'], cell['mode'], cell['seed'], int(cell['tag'].rsplit('.r', 1)[1]))
            assert key in expected and key not in seen, (path, key)
            seen.add(key)
            count = length or (64 if cell['task'] == 'median' else 32)
            assert cell['generations'] == count, (path, key)
            output = Path(cell['output'])
            if cell.get('status') != 'ok' or not output.exists():
                missing.append(f'{path}:{key}: unfinished'); continue
            required = [output.with_suffix('.'+suffix) for suffix in ('final.json', 'history.json', 'resources')]
            absent = [str(p) for p in required if not p.exists()]
            if absent:
                missing.extend(absent); continue
            enriched = dict(cell, arm=arm, repeat=repeat_label if repeats == 1 else key[3],
                            manifest=str(path), binary_sha256=manifest['binary_sha256'],
                            source_head=manifest['source_head'], frozen_sha256=manifest['frozen_sha256'],
                            reused_control=False)
            summary, gs = helper.sample(enriched)
            # Keep all rejection/error diagnostics, including fields beyond the older helper.
            summary['generation_totals'].update({k: sum(g[k] for g in gs) for k in
                ('rejected', 'generation_rejections', 'changed', 'unchanged') if all(k in g for g in gs)})
            samples.append(summary)
            generations.extend(dict(task=cell['task'], mode=cell['mode'], seed=cell['seed'],
                                    arm=arm, repeat=enriched['repeat'], manifest=str(path), **g) for g in gs)
            data_cache[enriched['output']] = read(output)
            result.append(enriched)
        for key in sorted(expected-seen):
            missing.append(f'{path}:{key}: missing cell')
        return result

    def compare(a, b, scope):
        x, y = data_cache[a['output']], data_cache[b['output']]
        assert semantic(x) == semantic(y), ('search semantic mismatch', a['output'], b['output'])
        proof = dict(scope=scope, task=b['task'], seed=b['seed'], repeat=b['repeat'],
                     before=a['output'], after=b['output'], exact=True,
                     search_semantic_sha256=digest(semantic(x)))
        for suffix in ('final', 'history'):
            xx = semantic(read(Path(a['output']).with_suffix('.'+suffix+'.json')))
            yy = semantic(read(Path(b['output']).with_suffix('.'+suffix+'.json')))
            assert xx == yy, (suffix+' AST mismatch', a['output'], b['output'])
            proof[suffix+'_sha256'] = digest(xx)
            proof[suffix+'_count'] = len(xx['programs' if suffix == 'final' else 'history'])
        comparisons.append(proof)
        return proof

    before, after = [], []
    for repeat in range(9):
        if repeat % 3 == 0:
            before += load(f'timing/before-{repeat}', 'before', ['gpu_overlap'], repeat_label=repeat)
        after += load(f'timing/after-{repeat}', 'after', ['gpu_overlap'], repeat_label=repeat)
    drift = load('timing/asgp-drift', 'asgp-drift', ['asgp_1t'])
    old_long = load('quality/before', 'before-long', ['gpu_overlap'], seeds=[0, 1, 2], length=128)
    new_long = load('quality/after', 'after-long', ['gpu_overlap'], seeds=[0, 1, 2], length=128)
    other = load('other-modes/gpu-three', 'other-modes', ['gpu_eval', 'gpu_repro'], repeats=3)
    smoke = load('other-modes/five-mode-smoke', 'smoke', MODES, length=1)
    for b in after:
        matches = [a for a in before if a['task'] == b['task'] and a['repeat'] == (b['repeat']//3)*3]
        if matches:
            compare(matches[0], b, 'cold-search')
    for b in new_long:
        matches = [a for a in old_long if (a['task'], a['seed']) == (b['task'], b['seed'])]
        if matches:
            compare(matches[0], b, '128-generation-quality')
    candidate = after+new_long+other+smoke
    if candidate:
        assert len({c['binary_sha256'] for c in candidate}) == 1, 'mixed candidate binaries'
        assert all(manifests[c['manifest']]['flags'] == manifests[candidate[0]['manifest']]['flags']
                   for c in candidate), 'mixed candidate flags'
    assert all(c['binary_sha256'] == contract['baseline_manifest']['binary_sha256']
               for c in before+old_long+drift), 'changed archived control binary'

    grouped = []
    for arm in ('after', 'before', 'asgp-drift', 'other-modes'):
        for task in TASKS:
            for mode in MODES:
                ss = [s for s in samples if (s['arm'], s['task'], s['mode']) == (arm, task, mode)]
                if not ss:
                    continue
                vals = [s['evolve_wall_ms'] for s in ss]
                times = [s['process_wall_s'] for s in ss]
                median = st.median(vals)
                grouped.append(dict(arm=arm, task=task, mode=mode, count=len(ss), raw_ms=vals,
                    median_ms=median, range_ms=[min(vals), max(vals)], raw_process_wall_s=times,
                    median_process_wall_s=st.median(times), process_range_s=[min(times), max(times)],
                    frozen_asgp_search_ms=contract['frozen_asgp_search_ms'][task],
                    frozen_asgp_ratio=contract['frozen_asgp_search_ms'][task]/median,
                    target_ratio=contract['targets'][task],
                    target_met=(median <= contract['target_search_ms'][task]) if arm == 'after' and mode == 'gpu_overlap' else None,
                    diagnostic_stage_medians_ms={k: st.median(s['stages_ms'][k] for s in ss)
                        for k in sorted(set.intersection(*(set(s['stages_ms']) for s in ss)))}))

    snapshots = []
    for task in TASKS:
        paths = [runs/'snapshot'/f'{task}.{arm}.json' for arm in ('before', 'after')]
        if any(not p.exists() for p in paths):
            missing.extend(str(p) for p in paths if not p.exists()); continue
        aa, bb = (read(p) for p in paths)
        assert aa['programs'] == bb['programs'], ('initial snapshot changed', task)
        assert len(aa['programs']) == 1024
        assert all(p['cases'] == 1024 and p['unscored'] == 0 for p in aa['programs'])
        differences = [p for p in aa['programs'] if p['fitness'] != p['cpu_fitness']]
        assert len(differences) == (4 if task == 'house_robber' else 0), (task, differences)
        snapshots.append(dict(task=task, exact_before_after=True, programs=1024, cases_per_program=1024,
            inherited_cpu_gpu_differences=differences, rows_sha256=digest(aa['programs']), paths=list(map(str, paths))))

    scaling, scaling_table = [], []
    scaling_path = runs/'scaling/runs/manifest.json'
    if not scaling_path.exists():
        missing.append(str(scaling_path))
    else:
        manifest = read(scaling_path)
        manifests[str(scaling_path)] = {k: v for k, v in manifest.items() if k != 'cells'}
        if candidate:
            assert manifest['sha256'] == candidate[0]['binary_sha256'], 'scaling candidate binary changed'
        assert all(manifest['frozen_sha256'].get(k) == v for k, v in
                   contract['baseline_manifest']['frozen_sha256'].items()), 'scaling input identity changed'
        expected = {(t, p, m) for t in TASKS for p in (1024, 2048, 4096, 8192) for m in MODES}
        seen = set()
        for cell in manifest['cells']:
            key = (cell['task'], cell['population'], cell['mode'])
            assert key in expected and key not in seen, key
            assert cell['flags']['GAGP_HOST_THREADS'] == '1', key
            assert not cell.get('reused_from_tag'), ('scaling must use own rerun controls', key)
            seen.add(key)
            if 'median_ms' not in cell:
                missing.append(f'scaling unfinished:{key}'); continue
            assert [r['rep'] for r in cell['rows']] == [-1, 0, 1, 2]
            assert all(r['population'] == key[1] and r['cases'] == 1024 for r in cell['rows'])
            measured = [r['generation_ms'] for r in cell['rows'] if r['rep'] >= 0]
            assert cell['median_ms'] == st.median(measured)
            scaling.append(cell)
        missing.extend(f'scaling missing:{key}' for key in sorted(expected-seen))
        for cell in scaling:
            controls = [c for c in scaling if (c['task'], c['population'], c['mode']) ==
                        (cell['task'], cell['population'], 'asgp_1t')]
            scaling_table.append(dict(task=cell['task'], population=cell['population'], mode=cell['mode'],
                median_generation_ms=cell['median_ms'], own_asgp_ratio=controls[0]['median_ms']/cell['median_ms'] if controls else None,
                raw_generation_ms=[r['generation_ms'] for r in cell['rows'] if r['rep'] >= 0],
                process_wall_s=cell['wall_seconds'], output=cell['output'], resources=cell['resources']))

    result = dict(complete=not missing, missing=missing, contract=contract, contract_path=str(contract_path),
        search_summary=grouped, samples=samples, exact_comparisons=comparisons,
        long_quality=[s for s in samples if s['arm'].endswith('-long')],
        initial_snapshots=snapshots, scaling_table=scaling_table, scaling_raw=scaling, manifests=manifests,
        scopes=dict(timing='Nine fresh complete cold changing-population searches per task; three archived environmental controls; ASGP drift is separate from frozen denominators.',
            process='Process wall includes import/audit/export and excludes lock waiting; never substituted for evolve_wall_ms.',
            stages='Diagnostic stage sums/medians are attribution indicators. Overlap and accounting differ: do not add them as an exact wall-time decomposition or bound.',
            semantics='Compare complete result dictionaries and full final/history AST exports, recursively ignoring ONLY *_ms, *_bytes and execution_profile. Timing/memory diagnostics alone are excluded.',
            quality='Exact 128-generation trajectories across three seeds establish preservation, not improved convergence. All errors, timeouts, rejections, unique genomes and emitted CPU audit coverage remain visible.',
            memory='RSS is process peak from /usr/bin/time; genotype_device_bytes is reported genotype allocation, not total GPU peak.',
            scaling='Fixed-parent warmup plus three measured generations; ratios use each task/population own ASGP control, never frozen full-search denominators.',
            snapshot='All1024initial programs ×1024cases; inherited4House CPU/GPU fitness differences must remain exactly unchanged.'))
    if missing and not args.allow_incomplete:
        raise RuntimeError('Validation incomplete:\n'+'\n'.join(missing))
    args.out.mkdir(parents=True, exist_ok=False)
    (args.out/'summary.json').write_text(json.dumps(result, indent=2)+'\n')
    write_csv(args.out/'search-summary.csv', grouped)
    flat = []
    for sample in samples:
        row = {k: v for k, v in sample.items() if k not in ('stages_ms', 'resources', 'generation_totals')}
        for field in ('stages_ms', 'resources', 'generation_totals'):
            row.update({field+'_'+k: v for k, v in sample[field].items()})
        flat.append(row)
    write_csv(args.out/'search-samples.csv', flat)
    write_csv(args.out/'generations.csv', generations)
    write_csv(args.out/'exact-comparisons.csv', comparisons)
    write_csv(args.out/'scaling.csv', scaling_table)
    print('complete=', not missing, 'missing=', len(missing), 'samples=', len(samples))


if __name__ == '__main__':
    main()
