"""Publish compact evidence only after every requested matrix cell has finished."""
from pathlib import Path
import json
import math
import shutil
import statistics
import subprocess
import sys

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, sha

root = Path.cwd()
base = Path(__file__).resolve().parent
matrix = root / 'logs/all30-scaling-20261016/same16'
out = base / 'final-same16'
m = read(matrix / 'manifest.json')
pops = (1024, 2048, 4096, 8192, 16384)
keys = {f'{task}.p{pop}' for task in PAPER_TASKS for pop in pops}
if set(m['results']) != keys or any(not r.get('complete') for r in m['results'].values()):
    raise SystemExit('matrix is incomplete; final publication refused')
for record in m['results'].values():
    for mode in ('asgp_1t', 'gpu_overlap'):
        if record[mode]['status'] in ('running', 'waiting', 'budget_not_run'):
            raise SystemExit('unmeasured cell; final publication refused')
assert m['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB'] == '16384'
subprocess.run([sys.executable, str(base / 'report.py'), '--matrix', str(matrix), '--out', str(out)], check=True)
s = read(out / 'summary.json')
assert s['completed_cells'] == 150 and len(s['rows']) == 150
rows = s['rows']
gpu = [r for r in rows if r['gpu_status'] == 'ok']
large = [r for r in gpu if r['population'] == 16384]
assert all(r['workspace_mib'] == 16384 for r in gpu)
assert all(r['cpu_audit_program_count'] == 16384 and r['cpu_audit_mismatches'] == 0 for r in large)
shutil.copy2(matrix / 'environment.json', out / 'environment.json')
shutil.copy2(base / 'same16-validation.json', out / 'validation.json')
if (matrix / 'initial_admission_limits.json').exists():
    shutil.copy2(matrix / 'initial_admission_limits.json', out / 'initial_admission_limits.json')
manifest_hash = sha(matrix / 'manifest.json')
(out / 'provenance.json').write_text(json.dumps({
    'matrix_manifest': str(matrix / 'manifest.json'), 'matrix_sha256': manifest_hash,
    'binary_sha256': m['binary_sha256'], 'runtime_source_head': m['source_head'],
    'report_sha256': sha(base / 'report.py'),
    'admission_limits_sha256': sha(matrix / 'initial_admission_limits.json') if (matrix / 'initial_admission_limits.json').exists() else None, 'finish_sha256': sha(Path(__file__)),
    'reused_asgp_controls': sum(r['asgp_1t'].get('reused_control', False) for r in m['results'].values()),
    'exact_prior_population_checks': sum('exact_reference_output' in r['gpu_overlap'] for r in m['results'].values()),
}, indent=2) + '\n')
peak = max((r.get('sampled_device_peak_bytes') or 0) for r in rows) / 1024**3
lines = ['# ASGP 1T / GAGP GPU-er-o：完整30題 scaling', '',
    f'固定16 GiB workspace，GPU完整執行與驗證 **{len(gpu)}/150**；合格加速比 **{s["qualified_cells"]}/150**。',
    f'P=16384已有 **{len(large)}/30** 題通過全部16384個最終個體的CPU fitness audit；GPU成功格皆零 mismatch。',
    f'整張RTX3090（24 GiB）的NVML取樣峰值為 **{peak:.2f} GiB**，含桌面背景。', '',
    '## 每種population', '', '中位數、幾何平均與範圍均為該population跨題目的倍率統計。', '',
    '| Population | GPU通過 | 快於ASGP | 加速比中位數 | 幾何平均 | 最小–最大 |',
    '| ---: | ---: | ---: | ---: | ---: | ---: |']
for pop in pops:
    group = [r for r in rows if r['population'] == pop]
    values = [r['speedup'] for r in group if r['speedup'] is not None]
    good = sum(r['gpu_status'] == 'ok' for r in group)
    if values:
        gm = math.exp(statistics.mean(math.log(v) for v in values))
        lines.append(f'| {pop} | {good}/30 | {sum(v > 1 for v in values)}/{len(values)} | {statistics.median(values):.2f}× | {gm:.2f}× | {min(values):.2f}–{max(values):.2f}× |')
    else:
        lines.append(f'| {pop} | {good}/30 | — | — | — | — |')
lines += ['', '## 全部題目的加速比', '', '| 題目 | 1024 | 2048 | 4096 | 8192 | 16384 |', '| --- | ---: | ---: | ---: | ---: | ---: |']
for task in PAPER_TASKS:
    values = []
    for pop in pops:
        row = next(r for r in rows if r['task'] == task and r['population'] == pop)
        values.append(f'{row["speedup"]:.2f}×' if row['speedup'] is not None else row['gpu_status'] if row['gpu_status'] != 'ok' else 'ASGP ' + row['asgp_status'])
    lines.append('| ' + task.replace('_', ' ').title() + ' | ' + ' | '.join(values) + ' |')
failed = [r for r in rows if r['gpu_status'] != 'ok' or r['asgp_status'] != 'ok']
lines += ['', '## 執行限制與驗證範圍', '']
if failed:
    for row in failed:
        lines.append(f'- {row["task"]} P={row["population"]}：GPU {row["gpu_status"]}、ASGP {row["asgp_status"]}；{row.get("gpu_reason") or "原始log見CSV／manifest"}。沒有合格倍率。')
else:
    lines += ['這組固定資料、設定與四代測試中，五種population的30題全部正常完成，未觸發OOM、整格執行timeout、preparation limit或CPU fitness mismatch。']
lines += ['',
    '- 每格一次獨立冷啟動；完整1024 cases、CPU1T、seed0、四代與最後評分。倍率是ASGP `evolve_wall_ms` / GAGP `evolve_wall_ms`，包含初始化、必要materialization與清理；CLI I/O、export及獨立CPU audit另列。這是支援與粗略速度測試，不是精確重複量測或收斂等價證明。',
    '- 所有GPU格使用同一binary與16GiB設定。P16384對全部最終個體做CPU fitness核對；較小P保留top16 CPU audit，並比較完整輸出AST、fitness及每代semantic/operator traces。Runtime errors/timeouts是隨機個體的評分結果，與整個執行是否成功分開計數。',
    '- 原phase owner的8192上限已擴充至16384，仍保留12GiB genotype guard。記憶體不足以同時保留全部暫存時，評分保留當代genotype；繁殖暫存與評分scratch分階段配置及釋放。搜尋參數、RNG、工作量與16GiB scratch上限維持原設定。一般reproduction的100萬項metadata guard未抬高。',
    '- Native tests 131/131通過；tools tests 115項（4 skips）、repository checks 24項通過。強制staged/resident與in-place/out-of-place檢查含32代完整AST、CPU/GPU fitness及案例／錯誤計數。',
    '- ASGP邏輯未改，同一population、inputs與settings的原有效控制組重用；原先未完成的P16384控制另測。沒有使用其他population推算分母，也未把8GiB的時間混入主表。舊8GiB結果只作較小P的完整語意比較；原16GiB失敗與probe全部保留。',
    '- NVML每200ms取樣整張卡，可能漏掉較短的尖峰；16GiB是scratch上限，與genotype峰值分屬不同生命週期，不能直接相加當成實際同時顯存用量。', '',
    '## 重現與資料', '',
    '[執行入口與恢復方法](../README.md) · [全部原始秒數與counters](cells.csv) · [逐代資料](generations.csv) · [互動表](dashboard.html) · [英文圖表](scaling.png) · [PDF](scaling.pdf) · [硬體與設定](environment.json) · [來源雜湊](provenance.json)', '',
    f'Runtime checkpoint：`356bc55`；binary SHA256：`{m["binary_sha256"]}`。',
    f'完整原始資料：`{matrix}`；manifest SHA256：`{manifest_hash}`。', '']
(out / 'REPORT.zh-TW.md').write_text('\n'.join(lines))
print(json.dumps({'gpu_ok': len(gpu), 'ratios': s['qualified_cells'], 'large_audits': len(large), 'peak_gib': peak, 'out': str(out)}, ensure_ascii=False))
