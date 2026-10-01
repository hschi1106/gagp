"""Summarize complete search prefixes; do not equate different populations."""
from pathlib import Path
import csv
import json
import statistics
import sys

source = Path(sys.argv[1])
output = Path(sys.argv[2]); output.mkdir(parents=True, exist_ok=True)
# Declared before inspecting the 128-generation candidates. These are screening
# thresholds, not solved-task criteria (except Sum's zero error).
thresholds = {'sum_of_elements': 0, 'house_robber': -7500, 'median': -25000}
summary = []
rows = []
for task in thresholds:
    for seed in range(3):
        kind = 'native'
        path = source / f'{kind}.s{seed}.{task}.json'
        if not path.exists(): continue
        data = json.loads(path.read_text())
        generations = data['generations']
        total = data['evolve_call_ms']
        # Charge all non-generation work in the measured search call up front.
        # Native's separate final evaluation is excluded, and final fitness
        # is never used for prefix quality. This is a conservative bound.
        cold = max(0, total - sum(g['generation_ms'] for g in generations) - data.get('final_eval_ms', 0))
        cumulative = cold
        prefix = []
        for g in generations:
            cumulative += g['generation_ms']
            prefix.append((cumulative, g['best']))
            rows.append(dict(task=task, seed=seed, mode=kind, cold_charged_ms=cold,
                prefix_ms=cumulative, **g))
        budgets = {}
        for budget in (1000, 2000, 5000, 10000):
            eligible = [best for t, best in prefix if t <= budget]
            budgets[budget] = {'best': max(eligible) if eligible else None,
                'covered': cumulative >= budget,
                'threshold_hit': bool(eligible and max(eligible) >= thresholds[task])}
        item = {'task': task, 'seed': seed, 'mode': kind, 'threshold': thresholds[task],
            'generations': len(generations), 'search_total_ms': total, 'cold_charged_ms': cold,
            'budget_prefix': budgets, 'final_best': max(p['fitness'] for p in data['final_population']),
            'unique_genotypes': data['unique_final_genomes'],
            'first_16_gen_median_ms': statistics.median(g['generation_ms'] for g in generations[:16]),
            'last_16_gen_median_ms': statistics.median(g['generation_ms'] for g in generations[-16:]),
            'timeouts': sum(g['timeouts'] for g in generations),
            'unscored': sum(g['unscored'] for g in generations),
            'fallbacks': sum(g['fallbacks'] for g in generations),
            'last_error_fraction': generations[-1]['errors'] / generations[-1]['cases']}
        summary.append(item)
(output / 'quality.json').write_text(json.dumps(summary, indent=2)+'\n')
with (output / 'quality-generations.csv').open('w') as f:
    keys = list(dict.fromkeys(k for r in rows for k in r))
    writer = csv.DictWriter(f, fieldnames=keys); writer.writeheader(); writer.writerows(rows)
for item in summary:
    print(item['task'], item['seed'], item['mode'], item['final_best'])
