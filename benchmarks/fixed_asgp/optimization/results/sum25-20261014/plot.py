"""Render English figures from the completed, checked summary (no benchmark work)."""
import argparse
import json
from pathlib import Path
import statistics
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
import numpy as np

p = argparse.ArgumentParser(description=__doc__)
p.add_argument('--summary', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
a = p.parse_args()
d = json.loads(a.summary.read_text())
assert d['complete'], 'Only plot completed validation'
a.out.mkdir(exist_ok=True, parents=True)
tasks = ['sum_of_elements', 'house_robber', 'median']
names = ['Sum of Elements\n32 generations', 'House Robber\n32 generations', 'Median\n64 generations']
colors = ['#64748b', '#087f8c']
plt.rcParams.update({'font.size': 11, 'axes.spines.top': False, 'axes.spines.right': False})
fig, ax = plt.subplots(figsize=(10, 5.2))
for offset, arm, color, label in [(-.18, 'before', colors[0], 'Previous GPU-er-o (3 runs)'), (.18, 'after', colors[1], 'Optimized GPU-er-o (9 runs)')]:
    medians, lows, highs = [], [], []
    for task in tasks:
        den = d['contract']['frozen_asgp_search_ms'][task]
        values = [x['evolve_wall_ms'] for x in d['samples'] if x['arm'] == arm and x['task'] == task]
        medians.append(den/statistics.median(values)); lows.append(den/max(values)); highs.append(den/min(values))
    xx = np.arange(3)+offset
    ax.bar(xx, medians, width=.33, color=color, label=label,
           yerr=[np.array(medians)-lows, np.array(highs)-medians], capsize=4)
    for x, y in zip(xx, medians):
        ax.text(x, y+1.1, f'{y:.2f}x', ha='center', fontweight='bold', fontsize=12)
for i, target in enumerate([25, 25, 5]):
    ax.hlines(target, i-.43, i+.43, colors='#c2410c', linestyles='--', linewidth=1.7)
ax.set(xticks=np.arange(3), xticklabels=names, ylabel='Speedup vs frozen ASGP 1T', ylim=(0, 33))
ax.set_title('Native GPU-er-o: complete-search speedup', loc='left', fontsize=16, fontweight='bold', pad=16)
ax.plot([], [], color='#c2410c', linestyle='--', label='Targets: 25x / 25x / 5x')
ax.grid(axis='y', alpha=.18); ax.set_axisbelow(True); ax.legend(loc='upper right', frameon=False, fontsize=10)
fig.text(.08, .015, 'P=1024, all 1024 cases, CPU 1T. Cold evolution + all generations + final evaluation.\nInput parsing, independent audit and JSON export are reported separately. Whiskers: run range.', fontsize=9, color='#475569')
fig.tight_layout(rect=(0, .09, 1, 1)); fig.savefig(a.out/'search-speedup.png', dpi=180); plt.close(fig)

fig, axes = plt.subplots(1, 3, figsize=(14, 4.5), sharex=True)
for ax, task, name in zip(axes, tasks, names):
    for arm, color, label in [('before-long', colors[0], 'Previous'), ('after-long', colors[1], 'Optimized')]:
        paths = [x['output'] for x in d['long_quality'] if x['arm'] == arm and x['task'] == task]
        values = np.array([[g['generation_ms'] for g in json.loads(Path(path).read_text())['generations']] for path in paths])
        assert values.shape == (3, 128)
        gen = np.arange(128)
        ax.fill_between(gen, values.min(axis=0), values.max(axis=0), color=color, alpha=.12)
        ax.plot(gen, np.median(values, axis=0), color=color, linewidth=2, label=label)
    ax.set(title=name.split('\n')[0], xlabel='Generation', ylabel='Generation time (ms)', xlim=(0, 127), ylim=(0, None))
    ax.grid(alpha=.18)
axes[0].legend(frameon=False)
fig.suptitle('Cost through 128 generations — identical evolution trajectories', fontsize=15, fontweight='bold')
fig.text(.07, .01, 'P=1024, all 1024 cases, CPU 1T. Three seeds: median line and min–max band (not a confidence interval).', fontsize=10, color='#475569')
fig.tight_layout(rect=(0, .06, 1, .94)); fig.savefig(a.out/'generation-cost.png', dpi=180); plt.close(fig)
