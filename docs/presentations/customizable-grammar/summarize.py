"""Summarize the complete 4 x 4 x 3 x 5 matrix; reject missing/mismatched runs."""
import csv
import hashlib
import json
from pathlib import Path
from statistics import median
import sys

MODES = {
    'A': ('cpu', 'cpu', False), 'B': ('gpu', 'cpu', False),
    'C': ('gpu', 'gpu', False), 'D': ('gpu', 'gpu', True),
}


def summarize(directory: Path) -> None:
    measurements = []
    times = {}
    identities = set()
    history_checks = 0
    for population in (1024, 2048, 4096, 8192):
        for seed in (0, 1, 2):
            history = None
            for mode, (engine, reproduction, overlap) in MODES.items():
                samples = []
                for repeat in range(1, 6):
                    path = directory / f'p{population}-s{seed}-{mode}-r{repeat}.json'
                    data = json.loads(path.read_text())
                    meta, timing = data['meta'], data['timing']
                    expected = {
                        'population_size': population, 'seed': seed, 'generations': 1,
                        'eval_engine': engine, 'reproduction_backend': reproduction,
                        'repro_overlap': overlap, 'skip_final_eval': True,
                        'gpu_blocksize': 1024, 'cpu_repro_ablation': 'none',
                    }
                    for key, value in expected.items():
                        if meta[key] != value:
                            raise ValueError(f'{path}: {key}={meta[key]!r}, expected {value!r}')
                    if Path(meta['population_json']).name != f'p{population}-s{seed}.population.json':
                        raise ValueError(f'Unpaired population in {path}')
                    identities.add(meta['grammar_definition']['content_sha256'])
                    if history is None:
                        history = data['history']
                    elif data['history'] != history:
                        raise ValueError(f'Generation-0 fitness summary mismatch in {path}')
                    history_checks += 1
                    generation = timing['generation_total_ms'][0]
                    durations = {
                        'evolution': meta['timing']['total_ms'],
                        'generation': generation,
                        'fixed_pop': generation + (meta['timing']['gpu_eval_init_ms'] if mode != 'A' else 0),
                        'process': float(Path(str(path) + '.wall-seconds').read_text()) * 1000,
                    }
                    if not all(0 < value < float('inf') for value in durations.values()):
                        raise ValueError(f'Invalid timing in {path}')
                    samples.append(durations)
                    measurements.append({
                        'population': population, 'seed': seed, 'mode': mode, 'repeat': repeat,
                        'file': path.name, 'sha256': hashlib.sha256(path.read_bytes()).hexdigest(),
                        'durations_ms': durations, 'timing': timing,
                        'gpu_eval_init_ms': meta['timing']['gpu_eval_init_ms'],
                        'best_fitness': data['history'][0]['best_fitness'],
                        'mean_fitness': data['history'][0]['mean_fitness'],
                    })
                for scope in samples[0]:
                    times[population, seed, mode, scope] = median(x[scope] for x in samples)
    if len(identities) != 1:
        raise ValueError('Mixed grammar identities')
    rows = []
    for population in (1024, 2048, 4096, 8192):
        for scope in ('evolution', 'generation', 'fixed_pop', 'process'):
            for mode in MODES:
                values = [times[population, s, mode, scope] for s in (0, 1, 2)]
                speedups = [times[population, s, 'A', scope] / times[population, s, mode, scope] for s in (0, 1, 2)]
                overlaps = [times[population, s, 'C', scope] / times[population, s, 'D', scope] for s in (0, 1, 2)]
                rows.append({
                    'population': population, 'scope': scope, 'mode': mode,
                    'median_ms': median(values),
                    'speedup_median': median(speedups),
                    'speedup_min': min(speedups), 'speedup_max': max(speedups),
                    'overlap_speedup_median': median(overlaps) if mode == 'D' else '',
                    'overlap_speedup_min': min(overlaps) if mode == 'D' else '',
                    'overlap_speedup_max': max(overlaps) if mode == 'D' else '',
                    **{f'seed_{s}_median_ms': times[population, s, mode, scope] for s in (0, 1, 2)},
                })
    destination = directory / 'summary.csv'
    with destination.open('w', newline='') as handle:
        writer = csv.DictWriter(handle, fieldnames=list(rows[0]))
        writer.writeheader()
        writer.writerows(rows)
    (directory / 'measurements.json').write_text(json.dumps({
        'grammar_hash': next(iter(identities)), 'formal_run_count': len(measurements),
        'generation0_history_checks': history_checks,
        'generation0_history_check_scope': 'Exact best/mean fitness and best program key, not full fitness-vector comparison',
        'scopes': {
            'evolution': 'meta.timing.total_ms; engine wall time including backend setup and population initialization; excludes CLI parsing/loading/serialization',
            'generation': 'timing.generation_total_ms[0]; one complete generation; excludes initialization',
            'fixed_pop': 'Canonical repository scope: generation plus gpu_eval_init_ms for GPU; reproduction-resource first-touch may occur outside this scope',
            'process': '/usr/bin/time whole-process elapsed seconds converted to ms; 10 ms reporting resolution',
        },
        'measurements': measurements,
    }, indent=2) + '\n')
    print(destination)


if __name__ == '__main__':
    if len(sys.argv) != 2:
        raise SystemExit('Usage: python3 summarize.py RAW_RESULT_DIRECTORY')
    summarize(Path(sys.argv[1]))
