"""Small sequential matrix. Emits a summary immediately after every cell."""
import csv
import hashlib
import json
import os
from pathlib import Path
import statistics
import subprocess

ROOT = Path(__file__).resolve().parents[2]
HERE = Path(__file__).resolve().parent
OUT = ROOT / 'logs/asgp-quick-1024cases'
MODES = ['asgp_1', 'asgp_10', 'asgp_20', 'gagp_off', 'gagp_on']
CELLS = [(population, 1024) for population in (1024, 2048, 4096, 8192)]


def digest(path):
    return hashlib.sha256(path.read_bytes()).hexdigest()


def summarize():
    summary = []
    for population, cases in CELLS:
        prefix = OUT / f'ready-p{population}-c{cases}-s0'
        path = prefix.with_suffix('.timings.jsonl')
        if not prefix.with_suffix('.done').exists():
            continue
        trials = [json.loads(line) for line in path.read_text().splitlines()]
        medians = {}
        for mode in MODES:
            samples = [r['generation_ms'] for r in trials if r['mode'] == mode and r['rep'] >= 0]
            if len(samples) != 3:
                raise ValueError(f'{prefix}: expected 3 measured repetitions for {mode}')
            medians[mode] = statistics.median(samples)
        parity = json.loads(prefix.with_suffix('.parity.json').read_text())
        row = {'population': population, 'cases': cases, **medians,
               'off_vs_asgp_1': medians['asgp_1'] / medians['gagp_off'],
               'on_vs_asgp_1': medians['asgp_1'] / medians['gagp_on'],
               'off_vs_asgp_10': medians['asgp_10'] / medians['gagp_off'],
               'on_vs_asgp_10': medians['asgp_10'] / medians['gagp_on'],
               'off_vs_asgp_20': medians['asgp_20'] / medians['gagp_off'],
               'on_vs_asgp_20': medians['asgp_20'] / medians['gagp_on'],
               'overlap_speedup': medians['gagp_off'] / medians['gagp_on'],
               'cpu_fitness_mismatches': parity['cpu_fitness_mismatches'],
               'gpu_fitness_mismatches': parity['gpu_fitness_mismatches']}
        summary.append(row)
    if summary:
        with (HERE / 'summary.csv').open('w') as f:
            writer = csv.DictWriter(f, fieldnames=list(summary[0]))
            writer.writeheader()
            writer.writerows(summary)
    return summary


def main():
    env = dict(os.environ, GAGP_CUDA_DEVICE='0')
    env.pop('ASGP_CONTROLLED', None)
    env.pop('ASGP_CASE_MAX_LENGTH', None)
    manifest = {'cells': CELLS, 'seed': 0, 'repeats': 3, 'warmups': 1,
                'binary_sha256': digest(OUT / 'quick'),
                'grammar_sha256': digest(HERE / 'grammar.json'),
                'adapter_sha256': digest(HERE / 'quick.cpp'), 'commands': []}
    prior_path = HERE / 'manifest.json'
    if prior_path.exists():
        prior = json.loads(prior_path.read_text())
        for key in ['binary_sha256', 'grammar_sha256', 'adapter_sha256']:
            if prior.get(key) != manifest[key]:
                raise RuntimeError(f'{key} changed; use a new experiment/output directory')
    for population, cases in CELLS:
        prefix = OUT / f'ready-p{population}-c{cases}-s0'
        command = [str(OUT / 'quick'), str(HERE / 'grammar.json'), str(population), str(cases), '0', '3', str(prefix)]
        manifest['commands'].append(command)
        if not prefix.with_suffix('.done').exists():
            if prefix.with_suffix('.timings.jsonl').exists():
                raise RuntimeError(f'Incomplete existing run; do not overwrite {prefix}')
            print(f'START population={population} cases={cases}', flush=True)
            with prefix.with_suffix('.log').open('w') as log:
                subprocess.run(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT, check=True)
            prefix.with_suffix('.done').write_text('success\n')
        rows = summarize()
        print(json.dumps(rows[-1]), flush=True)
        (HERE / 'manifest.json').write_text(json.dumps(manifest, indent=2) + '\n')
    evidence = []
    for population, cases in CELLS:
        prefix = OUT / f'ready-p{population}-c{cases}-s0'
        evidence.append({'population':population,'cases':cases,
                         'input_sha256':digest(prefix.with_suffix('.inputs.json')),
                         'parity':json.loads(prefix.with_suffix('.parity.json').read_text()),
                         'trials':[json.loads(line) for line in prefix.with_suffix('.timings.jsonl').read_text().splitlines()]})
    (HERE / 'measurements.json').write_text(json.dumps(evidence, indent=2)+'\n')

if __name__ == '__main__':
    main()
