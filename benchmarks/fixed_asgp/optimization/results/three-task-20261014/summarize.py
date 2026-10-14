"""Summarize the three-task rerun without mixing search and fixed-parent ratios."""
import argparse
import csv
import json
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[5]
OUT = Path(__file__).resolve().parent
RAW = ROOT / 'logs/optimization/three-task-20261014'
MODES = ('asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap')
TASKS = ('sum_of_elements', 'house_robber', 'median')

def read(p):
    return json.loads(p.read_text())

def main():
    p = argparse.ArgumentParser(description=__doc__)
    p.add_argument('--raw', type=Path, default=RAW)
    p.add_argument('--out', type=Path, default=OUT)
    args = p.parse_args()
    args.out.mkdir(parents=True, exist_ok=True)
    search = read(args.raw/'search/manifest.json')
    fixed = read(args.raw/'fixed/manifest.json')
    assert len(search['cells']) == 45 and all(c['status'] == 'ok' for c in search['cells'])
    assert len(fixed['cells']) == 15
    rows, generations = [], []
    for task in TASKS:
        for mode in MODES:
            cells = [c for c in search['cells'] if c['task'] == task and c['mode'] == mode]
            assert len(cells) == 3
            reports = [read(Path(c['output'])) for c in cells]
            assert all(r['population'] == r['cases'] == 1024 for r in reports)
            assert all(r['final_best'] == reports[0]['final_best'] for r in reports)
            assert all(r.get('cpu_audit_mismatches', 0) == 0 for r in reports)
            times = [r['evolve_wall_ms'] for r in reports]
            f = next(c for c in fixed['cells'] if c['task'] == task and c['mode'] == mode)
            row = dict(task=task, mode=mode, seed=cells[0]['seed'], generations=cells[0]['generations'],
                       raw_search_ms=times, search_median_ms=st.median(times), search_range_ms=[min(times), max(times)],
                       raw_process_s=[c['process_wall_s'] for c in cells],
                       process_median_s=st.median(c['process_wall_s'] for c in cells),
                       final_fitness=reports[0]['final_best'],
                       initial_fitness=reports[0]['generations'][0]['best'],
                       first_zero_generation=next((g['generation'] for g in reports[0]['generations'] if g['best']==0),None),
                       early_generation_ms=st.median(g['generation_ms'] for r in reports for g in r['generations'][:8]),
                       late_generation_ms=st.median(g['generation_ms'] for r in reports for g in r['generations'][-8:]),
                       fixed_parent_median_ms=f['median_ms'],
                       raw_fixed_parent_ms=[g['generation_ms'] for g in f['rows'] if g['rep'] >= 0],
                       final_nodes_median=st.median(p['nodes'] for p in reports[0]['final_population']),
                       raw_outputs=[c['output'] for c in cells])
            rows.append(row)
            for c, r in zip(cells, reports):
                for g in r['generations']:
                    assert g['expected_program_cases'] == 1024*1024
                    if mode.startswith('gpu'):
                        assert g['cases'] == 1024*1024 and g['unscored'] == 0
                    generations.append(dict(task=task, mode=mode, repeat=c['tag'].rsplit('.r',1)[1], **g))
    for r in rows:
        a = next(x for x in rows if x['task'] == r['task'] and x['mode'] == 'asgp_1t')
        r['same_round_search_speedup'] = a['search_median_ms']/r['search_median_ms']
        r['same_round_process_speedup'] = a['process_median_s']/r['process_median_s']
        r['same_round_fixed_speedup'] = a['fixed_parent_median_ms']/r['fixed_parent_median_ms']
        r['late_early_generation_ratio'] = r['late_generation_ms']/r['early_generation_ms']
        frozen = {'house_robber':27687.368225, 'median':5864.915193}
        if r['task'] in frozen:
            r['historical_frozen_search_speedup'] = frozen[r['task']]/r['search_median_ms']
    result = dict(rows=rows, search_manifest=search, fixed_manifest=fixed,
                  scope='All five native modes rerun; CPU1T, P1024/C1024. Three fresh complete cold searches per cell, one seed/task; timing repeats are not independent quality seeds.',
                  denominator='Same-round ASGP controls for each task and timing boundary. Frozen historical target ratios reported separately.',
                  runtime_commit='be455a4d7014ccc2594bb76ea12a1f74fc9ecb2c')
    (args.out/'summary.json').write_text(json.dumps(result,indent=2)+'\n')
    fields=['task','mode','repeat','generation']+sorted(set().union(*(set(g) for g in generations))-{'task','mode','repeat','generation'})
    with (args.out/'generations.csv').open('w',newline='') as f:
        w=csv.DictWriter(f,fieldnames=fields);w.writeheader();w.writerows(generations)
    lines=['# 三題五模式重跑 — 2026-10-14\n',
           'P=1024、完整 1024 cases，CPU1T，runtime `be455a4`，沿用最佳完整 flags；不改 runtime。Sum／House 各32代seed0，Median64代seed2。每格3個獨立process，包含cold start，沒有開profiler。\n',
           '## 完整搜尋時間\n',
           '各模式時間為3次中位數（秒）；最後兩欄使用**本輪相同題目的ASGP搜尋時間**。\n',
           '| Task | ASGP 1T | CPU | GPU-e | GPU-er | GPU-er-o | Fastest GPU | ASGP / fastest GPU |',
           '|---|---:|---:|---:|---:|---:|---|---:|']
    for task in TASKS:
        rr=[next(r for r in rows if r['task']==task and r['mode']==m) for m in MODES]
        fastest=min(rr[2:],key=lambda r:r['search_median_ms'])
        lines.append('| '+task+' | '+' | '.join(f"{r['search_median_ms']/1000:.3f}" for r in rr)+f" | {fastest['mode']} | {fastest['same_round_search_speedup']:.2f}× |")
    historical = [r for r in rows if r['mode']=='gpu_overlap' and r['task'] in ('house_robber','median')]
    lines.append('\n沿用歷史目標分母時，GPU-er-o：'+ '、'.join(f"{r['task']} {r['historical_frozen_search_speedup']:.2f}×" for r in historical)+'。Sum32使用本輪controls，不借用舊16代分母。\n')
    lines += ['\n搜尋邊界含冷啟動、必要admission/import、所有generation與最終evaluation/native export。額外輸入解析、audit、history／JSON輸出包含在另列process wall，不混入搜尋倍率。GPU-er與overlap的小差距不可當成可靠的overlap獨立收益。\n',
              '## Fixed-parent完整單代\n',
              '每格獨立process：warmup一次後量測三次，單位ms；各題使用本輪自己的ASGP單代分母，**不可用來宣稱跨代搜尋達標**。\n',
              '| Task | ASGP 1T | CPU | GPU-e | GPU-er | GPU-er-o | ASGP / GPU-er-o |',
              '|---|---:|---:|---:|---:|---:|---:|']
    for task in TASKS:
        rr=[next(r for r in rows if r['task']==task and r['mode']==m) for m in MODES]
        lines.append('| '+task+' | '+' | '.join(f"{r['fixed_parent_median_ms']:.3f}" for r in rr)+f" | {rr[-1]['same_round_fixed_speedup']:.2f}× |")
    lines += ['\n## 各模式搜尋品質與前後段成本\n',
              'Fitness越高越好，0為訓練集完全符合；單一seed只供這次性能結果的品質背景，不代表收斂排名。不同架構可能走不同軌跡；原有數值／執行profile差異仍存在，不能把所有耗時差當成identical-workload速度差。前後段為3次搜尋的前8／後8代單代中位數。\n',
              '| Task | Mode | Final fitness | Early gen ms | Late gen ms | Late / early | Process s | Process speedup |',
              '|---|---|---:|---:|---:|---:|---:|---:|']
    for r in rows:
        lines.append(f"| {r['task']} | {r['mode']} | {r['final_fitness']} | {r['early_generation_ms']:.3f} | {r['late_generation_ms']:.3f} | {r['late_early_generation_ratio']:.2f} | {r['process_median_s']:.3f} | {r['same_round_process_speedup']:.2f}× |")
    if all(r['initial_fitness'] == 0 for r in rows if r['task'] == 'sum_of_elements'):
        lines.append('\nSum 的 frozen 初始 population 在 generation0 已有 fitness0；這個32代實驗驗證持續演化的執行成本，**不能解讀為從未解到解出的搜尋能力比較**。沒有提前停止、替换初始輸入或跳過完整評估。\n')
    lines += ['\n## 原始重複量測\n', '| Task | Mode | Search repeats ms | Range ms |','|---|---|---|---|']
    for r in rows:
        lines.append(f"| {r['task']} | {r['mode']} | "+', '.join(f'{v:.6f}' for v in r['raw_search_ms'])+f" | {r['search_range_ms'][0]:.3f}–{r['search_range_ms'][1]:.3f} |")
    lines += ['\n所有binary／input SHA、flags、命令與raw repeats見[summary.json](summary.json)，逐代fitness／time／errors／size等見[generations.csv](generations.csv)。原始資料在 `logs/optimization/three-task-20261014/`。CPU audit覆蓋範圍依各run記錄，不宣稱全部programs皆獨立CPU重評。\n']
    lines += ['\n重現（新輸出目錄；不prepare原始輸入）：\n',
              '```bash',
              'python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/run.py --out logs/optimization/three-task-NEW',
              'python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/summarize.py --raw logs/optimization/three-task-NEW --out logs/optimization/three-task-NEW/summary',
              '```',
              '\n[完整搜尋比較圖](search-comparison.png)（英文標示、對數時間軸）／[SVG](search-comparison.svg)。']
    (args.out/'REPORT.zh-TW.md').write_text('\n'.join(lines)+'\n')
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    fig, axes = plt.subplots(1, 3, figsize=(13, 5.3), sharey=True)
    names = ['ASGP 1T', 'CPU', 'GPU-e', 'GPU-er', 'GPU-er-o']
    titles = ['Sum of Elements · 32 generations', 'House Robber · 32 generations', 'Median · 64 generations']
    colors = ['#6b7280', '#c67b34', '#729bc2', '#3677ac', '#17507b']
    for ax, task, title in zip(axes, TASKS, titles):
        rr = [next(r for r in rows if r['task'] == task and r['mode'] == mode) for mode in MODES]
        values = [r['search_median_ms']/1000 for r in rr]
        low = [v-r['search_range_ms'][0]/1000 for v,r in zip(values,rr)]
        high = [r['search_range_ms'][1]/1000-v for v,r in zip(values,rr)]
        ax.bar(range(5), values, color=colors, width=.65, yerr=[low,high], capsize=3)
        ax.set_yscale('log'); ax.set_ylim(.5, 600)
        ax.set_xticks(range(5), names, rotation=30, ha='right')
        ax.set_title(title, fontsize=11, pad=22)
        ax.grid(axis='y', alpha=.2); ax.set_axisbelow(True)
        for i,v in enumerate(values):
            ax.text(i, v*1.14, f'{v:.3f}s', ha='center', fontsize=9)
        ax.text(.5,.95, f"GPU-er-o: {rr[-1]['same_round_search_speedup']:.2f}× vs ASGP", transform=ax.transAxes, ha='center', fontsize=10)
        for side in ('top','right'):
            ax.spines[side].set_visible(False)
    axes[0].set_ylabel('Complete search time (seconds, log scale)')
    fig.suptitle('ASGP vs GAGP · Full-case evolving-population benchmark', fontsize=15)
    fig.text(.5,.015,'P = 1024 · Cases = 1024 · CPU 1T · 3 fresh processes · Bars: medians · Whiskers: min–max · Lower is faster',ha='center',fontsize=10)
    fig.tight_layout(rect=(0,.065,1,.94))
    fig.savefig(args.out/'search-comparison.png',dpi=180)
    fig.savefig(args.out/'search-comparison.svg')
    plt.close(fig)
    print('\n'.join(lines[:11]))

if __name__ == '__main__':
    main()
