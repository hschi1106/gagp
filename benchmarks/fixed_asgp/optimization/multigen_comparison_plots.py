"""Render six all-mode comparison figures from archived generation CSV data."""
import argparse
import csv
import hashlib
import json
from pathlib import Path

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np

TASKS = ('sum_of_elements', 'house_robber', 'median')
TITLES = ('Sum of Elements', 'House Robber', 'Median')
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
LABELS = ('ASGP 1T', 'GAGP CPU', 'GAGP GPU-e', 'GAGP GPU-er', 'GAGP GPU-er-o')
COLORS = ('#222222', '#377EB8', '#009E73', '#D55E00', '#984EA3')
STYLES = ('-', '--', '-.', '--', ':')
MARKERS = ('o', 's', '^', 'D', 'x')


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--results', type=Path, required=True)
    args = parser.parse_args()
    source = args.results / 'generations.csv'
    out = args.results / 'all-mode-comparison'
    out.mkdir(exist_ok=True)
    with source.open() as f:
        rows = list(csv.DictReader(f))
    groups = {}
    for row in rows:
        key = (row['stage'], row['task'], row['mode'], int(row['seed']))
        groups.setdefault(key, []).append(row)
    for records in groups.values():
        records.sort(key=lambda r: int(r['generation']))
        assert len({int(r['generation']) for r in records}) == len(records)
    plt.rcParams.update({'font.family': 'DejaVu Sans', 'font.size': 11,
                         'axes.spines.top': False, 'axes.spines.right': False,
                         'axes.titleweight': 'bold', 'svg.fonttype': 'none'})
    handles = [Line2D([0], [0], color=c, linestyle=s, marker=m, lw=2,
                      label=l) for c, s, m, l in zip(COLORS, STYLES, MARKERS, LABELS)]
    exported = []
    medians = {}
    for task in TASKS:
        for mode in MODES:
            seed_rows = [groups[('main', task, mode, seed)] for seed in range(3)]
            assert all([int(r['generation']) for r in rs] == list(range(16)) for rs in seed_rows)
            medians[task, mode] = {}
            for field in ('best', 'generation_ms'):
                values = np.array([[float(r[field]) for r in rs] for rs in seed_rows])
                assert np.isfinite(values).all()
                if field == 'generation_ms':
                    assert (values > 0).all()
                medians[task, mode][field] = np.median(values, axis=0)
            for g in range(16):
                exported.append(dict(task=task, mode=mode, generation=g,
                                     best_fitness_median=medians[task, mode]['best'][g],
                                     generation_ms_median=medians[task, mode]['generation_ms'][g]))
    def save(fig, name):
        fig.savefig(out / f'{name}.png', dpi=180, facecolor='white')
        svg = out / f'{name}.svg'
        fig.savefig(svg, facecolor='white')
        svg.write_text('\n'.join(s.rstrip() for s in svg.read_text().splitlines()) + '\n')
        plt.close(fig)
    def decorate(ax, title):
        ax.set_title(title, loc='left', pad=12)
        ax.set_xlabel('Generation (0-based)')
        ax.grid(axis='y', alpha=.18)
        ax.set_axisbelow(True)
    def plot_mode(ax, x, y, i):
        ax.plot(x, y, color=COLORS[i], linestyle=STYLES[i], marker=MARKERS[i],
                markevery=(i, 5), markersize=5, markerfacecolor='white',
                markeredgewidth=1.3, linewidth=2, label=LABELS[i], zorder=5-i*.1)
    x = np.arange(16)
    fig, axes = plt.subplots(1, 3, figsize=(16, 5.4))
    fig.subplots_adjust(left=.07, right=.98, top=.70, bottom=.22, wspace=.31)
    fig.suptitle('Fitness vs. generation | ASGP and all GAGP modes', x=.05, ha='left', y=.97, fontsize=20)
    fig.text(.05, .89, 'P = 1024 | All 1024 training cases | Per-generation best fitness, median of 3 seeds | Higher is better', fontsize=11)
    fig.legend(handles=handles, loc='upper center', bbox_to_anchor=(.5, .85), ncol=5, frameon=False)
    for ax, task, title in zip(axes, TASKS, TITLES):
        for i, mode in enumerate(MODES):
            plot_mode(ax, x, medians[task, mode]['best']/1024, i)
        decorate(ax, title)
        ax.set_xticks([0, 3, 6, 9, 12, 15]); ax.set_xlim(-.3, 15.3)
        ax.set_ylabel('Fitness / case (negative capped error)' if task != 'median' else 'Fitness / case (negative mismatch rate)')
        ax.ticklabel_format(axis='y', style='plain', useOffset=False)
        if task == 'sum_of_elements':
            ax.set_ylim(-.05, .05)
            ax.text(.5, .16, 'All five modes overlap at 0\nInitial population already contains a solution', transform=ax.transAxes, ha='center', fontsize=10)
    fig.text(.05, .09, 'Current-generation best, not best-so-far. CPU / GPU-e best curves overlap; GPU-er / GPU-er-o best curves overlap.', fontsize=10)
    fig.text(.05, .04, 'Native runtime scores: Sum / House use capped absolute error (cap 1000); Median uses exact mismatch. Final-only evaluation excluded.', fontsize=10, color='#555555')
    save(fig, '01_fitness_vs_generation')

    fig, axes = plt.subplots(2, 3, figsize=(16, 9))
    fig.subplots_adjust(left=.07, right=.98, top=.79, bottom=.16, hspace=.55, wspace=.31)
    fig.suptitle('Generation time and later-search cost | All five modes', x=.05, ha='left', y=.98, fontsize=20)
    fig.text(.05, .925, 'Complete generation time | P = 1024, all 1024 cases | Logarithmic time axes | Lower is faster', fontsize=11)
    fig.legend(handles=handles, loc='upper center', bbox_to_anchor=(.5, .90), ncol=5, frameon=False)
    long_counts = {}
    for col, (task, title) in enumerate(zip(TASKS, TITLES)):
        for i, mode in enumerate(MODES):
            plot_mode(axes[0, col], x, medians[task, mode]['generation_ms'], i)
            key = ('extended', task, mode, 0)
            records = groups.get(key, groups[('main', task, mode, 0)])
            long_counts[f'{task}/{mode}'] = len(records)
            # Generation+1 permits a log axis without omitting generation zero.
            xs = [int(r['generation'])+1 for r in records]
            ys = [float(r['generation_ms']) for r in records]
            ax = axes[1, col]
            ax.plot(xs, ys, color=COLORS[i], linestyle=STYLES[i], lw=1.6,
                    marker=MARKERS[i], markevery=[len(xs)-1], markersize=6,
                    markerfacecolor='white', markeredgewidth=1.3)
        decorate(axes[0, col], title + ' | 3-seed median')
        axes[0, col].set_xlim(0,15); axes[0,col].set_xticks([0,3,6,9,12,15])
        decorate(axes[1, col], title + ' | Full available seed 0 run')
        axes[1, col].set_xscale('log')
        axes[1, col].set_xlabel('Generation + 1 (log scale)')
        for ax in axes[:, col]:
            ax.set_yscale('log'); ax.set_ylabel('Complete generation time (ms, log scale)')
    fig.text(.05, .075, 'Top: common 16-generation runs. Bottom: seed 0 only, using extended runs where available; each line ends at its last measured generation.', fontsize=10)
    fig.text(.05, .04, 'No extrapolation or seed switching. Longer runs evolve different populations; timing changes are not identical-program executor speedups.', fontsize=10, color='#555555')
    save(fig, '02_generation_time_all_modes')

    for i, mode in enumerate(MODES[1:], 1):
        fig, axes = plt.subplots(1, 3, figsize=(16, 5.4))
        fig.subplots_adjust(left=.06, right=.98, top=.71, bottom=.22, wspace=.23)
        fig.suptitle(f'Generation speedup | {LABELS[i]} vs. ASGP 1T', x=.05, ha='left', y=.97, fontsize=20)
        fig.text(.05, .88, 'Speedup = ASGP generation time median / GAGP generation time median | 3 seeds per mode | Higher is faster', fontsize=11)
        fig.text(.05, .82, 'One bar per generation | Full population = 1024, all 1024 cases | Dashed line: ASGP parity (1x)', fontsize=11)
        for ax, task, title in zip(axes, TASKS, TITLES):
            ratio = medians[task, 'asgp_1t']['generation_ms']/medians[task, mode]['generation_ms']
            bars = ax.bar(x, ratio, color=COLORS[i], width=.76)
            ax.axhline(1, color='#222222', linestyle='--', linewidth=1.3)
            ax.bar_label(bars, labels=[f'{v:.2f}' if v < 1 else f'{v:.1f}' for v in ratio], fontsize=8, rotation=90, padding=4)
            decorate(ax, title)
            ax.set_xticks(x); ax.tick_params(axis='x', labelsize=9)
            ax.set_xlim(-.7,15.7); ax.set_ylim(0, max(1, max(ratio))*1.30)
            ax.set_ylabel('Speedup over ASGP (x)')
            for r in exported:
                if r['task'] == task and r['mode'] == mode:
                    r['speedup_asgp'] = ratio[r['generation']]
        fig.text(.05, .09, 'Above 1x: GAGP is faster. Below 1x: ASGP is faster. Per-generation ratios exclude one-time initialization and final-only evaluation.', fontsize=10)
        fig.text(.05, .04, 'These are evolving-population system comparisons, not fixed-program speedups or complete-search wall-time ratios. Panel scales differ.', fontsize=10, color='#555555')
        save(fig, f'{i+2:02d}_speedup_{mode}')
    with (out/'plotted_values.csv').open('w') as f:
        writer = csv.DictWriter(f, fieldnames=list(exported[0])+['speedup_asgp'], lineterminator='\n')
        writer.writeheader(); writer.writerows(exported)
    (out/'manifest.json').write_text(json.dumps(dict(source='../generations.csv',
        source_sha256=hashlib.sha256(source.read_bytes()).hexdigest(),
        generation_range=[0,15], seeds=[0,1,2], fitness='median current-generation native best / 1024',
        speedup='ratio of generation-time medians, not median of seed ratios',
        long_run_seed=0, long_run_generations=long_counts), indent=2)+'\n')
    (out/'README.md').write_text('''# ASGP 與全部 GAGP 模式比較圖

圖中文字均為英文；所有資料取自既有 CSV，未重跑或更改 benchmark。

1. [Fitness vs. generation](01_fitness_vs_generation.png)：三題、五模式，每代當代 best fitness 的三 seed 中位數，除以1024；越高越好。不是 best-so-far。完全重合的模式以不同線型與錯開的 marker 表示。
2. [單代時間與後期成本](02_generation_time_all_modes.png)：上排為共同16代的三 seed 中位數；下排為 seed0 的完整可用搜尋，沒有延長資料者只畫16代。時間用 log 軸，下排代數用 generation+1 的 log 軸。不同長度不外推。
3. [GAGP CPU 加速比](03_speedup_gagp_cpu.png)
4. [GPU-e 加速比](04_speedup_gpu_eval.png)
5. [GPU-er 加速比](05_speedup_gpu_repro.png)
6. [GPU-er-o 加速比](06_speedup_gpu_overlap.png)

加速比每代一柱，定義為 ASGP 該代時間的三 seed 中位數 ÷ GAGP 該代時間的三 seed 中位數。大於1代表GAGP較快；小於1代表ASGP較快。四張圖均用全部模式共同量測的generation 0–15，不用不同run／seed拼接分母。

單代時間包含完整評估、selection與reproduction／下一代準備；一次性初始化及final-only evaluation不在逐代比值內。這些比值不是整趟搜尋加速比。各模式演化出的population不同，亦非固定程式的executor比較。ASGP採原生評分；對齊fitness公式不表示語言、operator或RNG相同。Sum初始即有解，不能證明搜尋優勢。

每張 PNG 均有同名 SVG。可用 [plotted_values.csv](plotted_values.csv) 核對主比較數值；[manifest.json](manifest.json) 記錄來源SHA256、口徑與長跑覆蓋代數。

重畫命令（repo root）：

```bash
python3 benchmarks/fixed_asgp/optimization/multigen_comparison_plots.py --results benchmarks/fixed_asgp/optimization/results/multigen-20261013
```
''')
    print(out)

if __name__ == '__main__':
    main()
