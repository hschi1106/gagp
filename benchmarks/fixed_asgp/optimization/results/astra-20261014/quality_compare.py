"""Audit before/after search champions at fixed completed-generation time budgets."""
import argparse
import fcntl
import hashlib
import json
import os
from pathlib import Path
import subprocess


def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--root', type=Path, required=True)
    p.add_argument('--before', type=Path, required=True)
    p.add_argument('--after', type=Path, required=True)
    p.add_argument('--out', type=Path, required=True)
    args = p.parse_args()
    root = args.root.resolve()
    out = args.out.resolve()
    out.mkdir(parents=True, exist_ok=False)
    baseline = json.loads((root / 'logs/optimization/parallel-20261013/followup-main/manifest.json').read_text())
    binary = Path(baseline['binary'])
    assert hashlib.sha256(binary.read_bytes()).hexdigest() == baseline['binary_sha256']
    manifests = {label: json.loads((path / 'manifest.json').read_text())
                 for label, path in [('before', args.before), ('after', args.after)]}
    report = dict(budgets_ms=[1000, 2000, 5000],
                  contract='Training best-so-far, earliest tie, available after a completed generation. Includes search initialization; excludes future final export/audit. Only budgets covered by both runs are compared. No interpolation of unfinished generations.',
                  audit_binary=str(binary), audit_sha256=baseline['binary_sha256'],
                  rows=[], excluded=[], commands=[], input_sha256={})
    env = {k: v for k, v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD', 'MALLOC_CONF')}
    env.update(baseline['flags'], GAGP_SNAPSHOT_CPU='1')
    art = root / 'logs/fixed-asgp/artifacts'
    for task in ('house_robber', 'median'):
        cells = {}
        for label, manifest in manifests.items():
            for c in manifest['cells']:
                if c['status'] == 'ok' and c['task'] == task and c['mode'] == 'gpu_overlap':
                    assert (label, c['seed']) not in cells, 'Quality input must have one run per seed'
                    result = json.loads(Path(c['output']).read_text())
                    history = json.loads(Path(c['output']).with_suffix('.history.json').read_text())['history']
                    assert len(history) == len(result['generations'])
                    assert result['population'] == result['cases'] == 1024
                    assert all(g['cases'] == 1024 * 1024 and g['unscored'] == 0 for g in result['generations'])
                    cells[label, c['seed']] = (result, history)
        seeds = sorted({seed for _, seed in cells})
        selected, unique, identities = [], [], {}
        for seed in seeds:
            assert all((label, seed) in cells for label in ('before', 'after'))
            for budget in report['budgets_ms']:
                if any(cells[label, seed][0]['generations'][-1]['elapsed_search_ms'] < budget for label in ('before', 'after')):
                    report['excluded'].append(dict(task=task, seed=seed, budget_ms=budget, reason='Both traces must span budget'))
                    continue
                for label in ('before', 'after'):
                    result, history = cells[label, seed]
                    available = [h for h, g in zip(history, result['generations']) if g['elapsed_search_ms'] <= budget]
                    if not available:
                        report['excluded'].append(dict(task=task, seed=seed, budget_ms=budget, label=label, reason='No completed generation'))
                        continue
                    champion = max(available, key=lambda h: h['fitness'])
                    ast = champion['ast']
                    identity = json.dumps(ast, sort_keys=True, separators=(',', ':'))
                    if identity not in identities:
                        identities[identity] = len(unique)
                        unique.append(ast)
                    selected.append(dict(task=task, seed=seed, label=label, budget_ms=budget,
                                         completed_generations=len(available), selected_generation=champion['generation'],
                                         recorded_fitness=champion['fitness'], program_index=identities[identity]))
        if not unique:
            continue
        prepared = json.loads((art / f'{task}.p1024.prepared.json').read_text())
        prepared['programs'] = unique
        pp = out / f'{task}.prepared.json'
        pp.write_text(json.dumps(prepared) + '\n')
        scores = {}
        for split in ('train', 'heldout'):
            source_path = (art / f'{task}.source.json' if split == 'train' else
                           root / f'logs/optimization/native-counted-validation/heldout/{task}.source.json')
            report['input_sha256'][f'{task}.{split}'] = hashlib.sha256(source_path.read_bytes()).hexdigest()
            source = json.loads(source_path.read_text())
            source['population'] = [source['population'][0]] * len(unique)
            sp = out / f'{task}.{split}.source.json'
            sp.write_text(json.dumps(source) + '\n')
            target = out / f'{task}.{split}.json'
            command = [str(binary), 'measure', str(sp), str(pp), str(art / f'{task}.grammar.json'), str(len(unique)), 'snapshot', str(target)]
            env['GAGP_SNAPSHOT_PENALTY'] = str(1 if task == 'median' else 1000)
            with open('/tmp/gagp-parallel-20261013-benchmark.lock', 'a') as lock:
                fcntl.flock(lock, fcntl.LOCK_EX)
                with (out / f'{task}.{split}.log').open('w') as log:
                    subprocess.run(command, cwd=root, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            report['commands'].append(command)
            scores[split] = json.loads(target.read_text())['programs']
        for row in selected:
            train, held = (scores[s][row['program_index']] for s in ('train', 'heldout'))
            assert train['cases'] == held['cases'] == 1024
            assert train['fitness'] == train['cpu_fitness'] == row['recorded_fitness']
            assert held['fitness'] == held['cpu_fitness']
            assert train['unscored'] == held['unscored'] == 0
            row.update(train_fitness=train['fitness'], heldout_fitness=held['fitness'], nodes=train['nodes'],
                       heldout_errors=held['errors'], heldout_timeouts=held['timeouts'])
            report['rows'].append(row)
        (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    (out / 'summary.json').write_text(json.dumps(report, indent=2) + '\n')
    print(f"Audited {len(report['rows'])} comparisons; excluded {len(report['excluded'])} uncovered budgets")


if __name__ == '__main__':
    main()
