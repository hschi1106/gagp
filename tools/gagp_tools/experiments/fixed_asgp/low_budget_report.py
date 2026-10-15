"""Summarize bounded validation without promoting smoke timings to speedups."""
from __future__ import annotations
import argparse
import csv
import json
from pathlib import Path


def summarize(folder: Path, out: Path) -> None:
    manifest = json.loads((folder / 'manifest.json').read_text())
    out.mkdir(parents=True, exist_ok=False)
    rows = []
    generations = []
    timings = []
    for task, record in manifest['tasks'].items():
        cells = [c for c in manifest['cells'] if c['task'] == task]
        modes = {m: next((c['status'] for c in cells if c['phase'] == m + '.s0'), 'not_run')
                 for m in ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')}
        issues = []
        if record.get('reason'):
            issues.append(record['reason'])
        for cell in cells:
            if cell['status'] != 'ok':
                reason = cell.get('reason', '')
                log = folder / cell.get('log', '')
                if not reason and log.is_file():
                    lines = log.read_text(errors='replace').strip().splitlines()
                    reason = lines[-1] if lines else ''
                issues.append(cell['phase'] + ': ' + cell['status'] + (' (' + reason + ')' if reason else ''))
            output = cell.get('output')
            if output:
                data = json.loads((folder / output).read_text())
                seed = data.get('seed', 0)
                for gen in data['generations']:
                    generations.append({'task': task, 'mode': data['mode'], 'seed': seed, **gen})
                timings.append({'task': task, 'mode': data['mode'], 'seed': seed,
                    'evolve_wall_ms': data['evolve_wall_ms'], 'process_s': cell['process_s'],
                    'final_best': data['final_best'], 'unique_final_genomes': data['unique_final_genomes'],
                    'cpu_audit_mismatches': data.get('cpu_audit_mismatches', 0),
                    'peak_rss_bytes': cell.get('peak_rss_bytes'), 'status': cell['status']})
        rows.append({'task': task, 'status': record['status'],
            'known_solution': record.get('known_solution_audit', 'not_run'),
            'source_output_parity': record.get('source_output_parity', 'not_run'),
            'cpu_gpu_snapshot': record.get('cpu_gpu_snapshot', 'not_run'), **modes,
            'issues': '; '.join(issues)})
    for name, data in [('tasks', rows), ('searches', timings), ('generations', generations)]:
        (out / (name + '.json')).write_text(json.dumps(data, indent=2) + '\n')
        if data:
            fields = list(dict.fromkeys(k for row in data for k in row))
            with (out / (name + '.csv')).open('w') as stream:
                writer = csv.DictWriter(stream, fieldnames=fields)
                writer.writeheader()
                writer.writerows(data)
    lines = ['# Low-budget validation', '',
             f"Stage: {manifest['stage']}; P={manifest['population']}, cases=1024, generations=2.",
             f"Elapsed {manifest['elapsed_s']:.1f} s. CPU1T; GPU jobs serialized.",
             'These are smoke/short-search samples, not precision speedups or convergence equivalence.', '',
             '| Task | Status | Known solution | Source outputs | CPU/GPU snapshot | ASGP | CPU | GPU-e | GPU-er | GPU-er-o |',
             '|---|---|---|---|---|---|---|---|---|---|']
    keys = ['task', 'status', 'known_solution', 'source_output_parity', 'cpu_gpu_snapshot',
            'asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap']
    lines += ['| ' + ' | '.join(str(r[k]) for k in keys) + ' |' for r in rows]
    lines += ['', 'Failure details: tasks.csv. Raw samples: searches.csv / generations.csv.',
              'Search-rule differences are embedded in each native search JSON; aligned fitness does not mean identical search.']
    (out / 'README.md').write_text('\n'.join(lines) + '\n')
    (out / 'provenance.json').write_text(json.dumps({k: manifest.get(k) for k in
        ('format', 'stage', 'population', 'cases', 'generations', 'elapsed_s', 'binary',
         'binary_sha256', 'source_head', 'settings', 'wall_budget_s')}, indent=2) + '\n')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--runs', type=Path, required=True)
    parser.add_argument('--out', type=Path, required=True)
    args = parser.parse_args()
    summarize(args.runs, args.out)


if __name__ == '__main__':
    main()
