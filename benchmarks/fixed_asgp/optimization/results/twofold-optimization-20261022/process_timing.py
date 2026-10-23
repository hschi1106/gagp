"""Supplement the unchanged search metric with matched benchmark-process wall."""
import math

import study_statistics


def summarize(runs, cells, tasks, populations, repetitions):
    expected = {(task, population, repetition, variant)
                for task in tasks for population in populations for repetition in range(repetitions)
                for variant in ('baseline', 'candidate')}
    lookup = {}
    for run in runs:
        key = run['task'], run['population'], run['repetition'], run['variant']
        if key in lookup or key not in expected:
            raise ValueError('unexpected or duplicate process-time run')
        lookup[key] = run
    masks = {(cell['task'], cell['population']): cell for cell in cells}
    if len(masks) != len(cells) or set(masks) != {(task, p) for task in tasks for p in populations}:
        raise ValueError('complete primary qualification mask required')
    result = []
    for task in tasks:
        for population in populations:
            row = dict(task=task, population=population, qualified=masks[task, population]['qualified'],
                       metric='benchmark subprocess wall; includes setup, inlineCPU16 and exports; excludes offline native audits')
            if row['qualified']:
                values = {}
                for variant in ('baseline', 'candidate'):
                    times = []
                    for repetition in range(repetitions):
                        run = lookup.get((task, population, repetition, variant), {})
                        value = run.get('process_s')
                        if run.get('status') != 'ok' or value is None or not math.isfinite(value) or value <= 0:
                            raise ValueError('qualified primary cell lacks a valid matched process time')
                        times.append(value)
                    values[variant] = study_statistics.geometric_mean(times)
                row.update(baseline_process_s=values['baseline'], candidate_process_s=values['candidate'],
                           speedup=values['baseline'] / values['candidate'])
            result.append(row)
    groups = {}
    for label, allowed in [(str(p), [p]) for p in populations] + [('pooled', populations)]:
        rows = [row for row in result if row['population'] in allowed and row['qualified']]
        groups[label] = dict(expected_cells=len(tasks) * len(allowed), qualified_cells=len(rows),
            observed_geometric_mean=study_statistics.geometric_mean(row['speedup'] for row in rows) if rows else None)
    return dict(cells=result, groups=groups,
                scope='same qualified task/population cells and equal repetition weighting as the unchanged primary search metric')


def markdown(summary):
    lines = ['## 搜尋函式與程序時間', '',
        '主要倍率仍使用 `evolve_wall_ms`：從呼叫搜尋函式前至返回後，包含函式內初始化、GPU評分與繁殖、最終評分與族群還原。',
        '它不包含先前的來源／prepared JSON解碼、grammar編譯與初始AST／derivation重建，也不包含返回後的CPU16核對及JSON／AST匯出。新程序啟動不代表所有程序準備都算在這個搜尋計時內。',
        '下表另列完整benchmark子程序時間的倍率，含上述前後處理與內建CPU16核對；不含獨立資料生成／prepare命令及額外完整原生核對。兩項指標不混用，程序倍率不替代2×搜尋目標。', '',
        '| Population | 程序時間倍率 | 合格／預期格 |', '|---:|---:|---:|']
    for label, group in summary['groups'].items():
        ratio = group['observed_geometric_mean']
        lines.append('| ' + ('兩種P合併' if label == 'pooled' else label) + ' | ' +
                     ('—' if ratio is None else f'{ratio:.2f}×') +
                     f" | {group['qualified_cells']}/{group['expected_cells']} |")
    lines += ['', '[逐格程序時間](speed_process_cells.csv)。額外CPU核對成本仍分列於成本與限制。', '']
    return '\n'.join(lines)
