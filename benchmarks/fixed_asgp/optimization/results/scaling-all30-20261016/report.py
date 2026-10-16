"""Summarize recorded scaling cells; failures and pending cells have no ratio."""
from argparse import ArgumentParser
from pathlib import Path
import csv
import json

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, validate_search

POPS = (1024, 2048, 4096, 8192, 16384)
p = ArgumentParser(description=__doc__)
p.add_argument('--matrix', type=Path, required=True)
p.add_argument('--out', type=Path, required=True)
p.add_argument('--capacity-results', type=Path, help='Separate explicitly configured smaller-workspace runs')
a = p.parse_args()
m = read(a.matrix / 'manifest.json')
supplement = read(a.capacity_results) if a.capacity_results else None
if supplement:
    assert supplement['binary_sha256'] == m['binary_sha256']
    assert supplement['settings'] == m['settings']
a.out.mkdir(parents=True, exist_ok=False)
rows, generations = [], []


def status(cell):
    value = cell.get('status', 'pending')
    if value in ('ok', 'pending'):
        return value
    log = a.matrix / cell.get('log', '/missing')
    text = log.read_text().lower() if log.is_file() else ''
    if 'preparation item limit' in text:
        return 'constant_preparation_limit'
    if 'out of memory' in text:
        return 'oom'
    if 'allocation failure' in text or 'cuda alloc' in text:
        return 'allocation_failure'
    return value


for task in PAPER_TASKS:
    for population in POPS:
        record = m.get('results', {}).get(f'{task}.p{population}', {})
        selected = supplement.get('results', {}).get(f'{task}.p{population}', {}) if supplement else {}
        if selected:
            assert selected['inputs'] == record['inputs']
        row = {'task': task, 'population': population, 'cases': 1024, 'generations': 4,
               'asgp_status': 'pending', 'gpu_status': 'pending', 'speedup': None}
        for mode, label in [('asgp_1t', 'asgp'), ('gpu_overlap', 'gpu')]:
            cell = record.get(mode, {})
            if label == 'gpu' and selected.get('complete'):
                row['gpu_original_status'] = status(cell)
                row['gpu_original_log'] = cell.get('log')
                if selected.get('gpu_overlap'):
                    cell = selected['gpu_overlap']
                    row['gpu_separate_capacity_config'] = True
            row[label + '_status'] = status(cell)
            if label == 'gpu':
                row['workspace_mib'] = cell.get('workspace_mib')
            row[label + '_output'] = cell.get('output')
            row[label + '_process_s'] = cell.get('process_s')
            collection = supplement if label == 'gpu' and row.get('gpu_separate_capacity_config') else m
            process_cell = next((c for c in collection.get('cells', [])
                if cell.get('log') and Path(c.get('log', '/missing')).name == Path(cell['log']).name), {})
            row[label + '_peak_rss_bytes'] = process_cell.get('peak_rss_bytes')
            if cell.get('status') != 'ok':
                continue
            data = read(Path(cell['output']))
            validate_search(data, population, 4, mode)
            row.update({label + '_wall_s': data['evolve_wall_ms'] / 1000,
                        label + '_final_best': data['final_best'],
                        label + '_unique': data['unique_final_genomes'],
                        label + '_initial_admission_ms': data['initial_admission_ms'],
                        label + '_final_eval_ms': data['final_eval_ms']})
            if label == 'gpu':
                row.update(workspace_mib=cell['workspace_mib'],
                    cpu_audit_mismatches=data['cpu_audit_mismatches'],
                    cpu_audit_program_count=data['cpu_audit_program_count'],
                    sampled_device_peak_bytes=cell.get('sampled_device_peak_bytes'),
                    reproduction_profile=data['reproduction_profile'],
                    reproduction_fallback_reason=data['reproduction_fallback_reason'],
                    genotype_device_bytes=data['genotype_device_bytes'],
                    errors=sum(g['errors'] for g in data['generations']),
                    timeouts=sum(g['timeouts'] for g in data['generations']),
                    fallback_cases=sum(g['fallbacks'] for g in data['generations']),
                    unscored=sum(g['unscored'] for g in data['generations']),
                    evaluated_pairs=sum(g['cases'] for g in data['generations']))
            generations.extend(dict(task=task, population=population, mode=mode, **g)
                               for g in data['generations'])
        if row['asgp_status'] == row['gpu_status'] == 'ok':
            row['speedup'] = row['asgp_wall_s'] / row['gpu_wall_s']
        rows.append(row)


def csv_file(path, values):
    fields = list(dict.fromkeys(k for row in values for k in row))
    with path.open('w') as f:
        w = csv.DictWriter(f, fieldnames=fields)
        w.writeheader()
        w.writerows(values)


csv_file(a.out / 'cells.csv', rows)
if generations:
    csv_file(a.out / 'generations.csv', generations)
complete = sum(r['asgp_status'] != 'pending' and r['gpu_status'] != 'pending' for r in rows)
passed = sum(r['speedup'] is not None for r in rows)
summary = dict(note='One cold process per configured run. Complete1024cases/G4+finaleval, '
    'CPU1T, fresh own-population ASGP controls. Qualified P1024 GPU runs reused by hash. '
    'No extrapolated ratios or precision/quality-equivalence claim. Alternate workspace '
    'runs exclude earlier failed probes; all attempts remain in matrix manifest.',
    matrix_manifest=str(a.matrix / 'manifest.json'),
    capacity_manifest=str(a.capacity_results) if a.capacity_results else None, source_head=m['source_head'],
    binary_source_head=m.get('binary_source_head', m['source_head']),
    binary_sha256=m['binary_sha256'], completed_cells=complete, qualified_cells=passed,
    total_cells=150, rows=rows)
if m['stage'] == 'all30-scaling-same16':
    summary['note'] = m['note']
(a.out / 'summary.json').write_text(json.dumps(summary, indent=2) + '\n')
table = '# ASGP 1T / GPU-er-o: complete G4 scaling\n\n'
table += f'Completed cells: {complete}/150; qualified ratios: {passed}.\n\n'
table += '| Task | P1024 | P2048 | P4096 | P8192 | P16384 |\n| --- | ---: | ---: | ---: | ---: | ---: |\n'
for task in PAPER_TASKS:
    values = []
    for r in [x for x in rows if x['task'] == task]:
        if r['speedup'] is not None:
            value = f'{r["speedup"]:.2f}x'
            if r['workspace_mib'] != 16384:
                value += f' ({r["workspace_mib"] // 1024}GiB)'
        else:
            value = r['gpu_status'] if r['gpu_status'] != 'ok' else 'ASGP ' + r['asgp_status']
        values.append(value)
    table += '| ' + task + ' | ' + ' | '.join(values) + ' |\n'
table += ('\nFixed16GiB arena budget in every GPU cell; no smaller-capacity retries. '
          'Full final-population CPU fitness audit at P16384 is outside evolve wall time. '
          'Failed/pending cells have no speedup.\n' if m['stage'] == 'all30-scaling-same16' else
          '\nDefault:16GiB arena budget;8/4GiB allocation-only retries explicitly marked. Failed/pending cells have no speedup.\n')
(a.out / 'report.md').write_text(table)

if passed:
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    import matplotlib.colors as colors
    import numpy as np
    matrix = np.full((30, 5), np.nan)
    for i, task in enumerate(PAPER_TASKS):
        for j, population in enumerate(POPS):
            r = next(x for x in rows if x['task'] == task and x['population'] == population)
            if r['speedup'] is not None:
                matrix[i, j] = r['speedup']
    cmap = plt.get_cmap('cividis').copy()
    cmap.set_bad('#eeeeee')
    fig, ax = plt.subplots(figsize=(10, 13.5), layout='constrained')
    im = ax.imshow(matrix, cmap=cmap, norm=colors.LogNorm(vmin=min(.5, np.nanmin(matrix)),
        vmax=max(40, np.nanmax(matrix))), aspect='auto')
    for i in range(30):
        for j in range(5):
            r = rows[i * 5 + j]
            if r['speedup'] is None:
                state = r['gpu_status'] if r['gpu_status'] != 'ok' else 'ASGP ' + r['asgp_status']
                label = {'constant_preparation_limit': 'limit', 'allocation_failure': 'alloc',
                         'validation_failed': 'audit', 'timeout': 'timeout'}.get(state, state)
            else:
                label = f'{r["speedup"]:.2f}x'
                if r['workspace_mib'] != 16384:
                    label += f'\n{r["workspace_mib"] // 1024} GiB'
            ax.text(j, i, label, ha='center', va='center', fontsize=8,
                    color='white' if np.isfinite(matrix[i, j]) and matrix[i, j] < 4 else 'black')
    ax.set_xticks(range(5), [str(p) for p in POPS])
    ax.set_yticks(range(30), [t.replace('_', ' ').title() for t in PAPER_TASKS])
    ax.set_xlabel('Population')
    ax.set_title('ASGP 1T / GPU-er-o complete-search speedup\n1024 full cases, G4 + final evaluation, CPU 1T; one cold process/cell')
    fig.colorbar(im, ax=ax, label='Speedup (log scale)', shrink=.65)
    fig.savefig(a.out / 'scaling.png', dpi=180)
    fig.savefig(a.out / 'scaling.pdf')


if m['stage'] == 'all30-scaling-same16':
    dashboard = r"""<!doctype html><html lang="zh-Hant"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1">
<style>
:root{font-family:system-ui,sans-serif}
*{box-sizing:border-box}body{margin:0;color:var(--foreground,#182334);font:14px/1.5 var(--font-sans,system-ui,sans-serif);background:transparent}
h2{font-size:21px;line-height:1.35;margin:0 0 8px}p{margin:8px 0;color:var(--muted-foreground,#657184)}
.stats{display:grid;grid-template-columns:repeat(3,1fr);gap:10px;margin:18px 0}.stat{padding:16px;background:var(--card,#f4f6f9);border-radius:var(--radius,10px)}
.stat b{display:block;font-size:23px;font-variant-numeric:tabular-nums}.stat span{font-size:12px;color:var(--muted-foreground,#657184)}
.controls{display:flex;gap:10px;margin:16px 0}input,select{font:inherit;color:var(--foreground,#182334);background:var(--card,#f4f6f9);padding:9px 10px;border:1px solid var(--border,#dce1e8);border-radius:7px}input{min-width:0;flex:1}select{max-width:150px}
.scroll{overflow:auto}table{border-collapse:collapse;width:100%;min-width:615px;font-size:12px;font-variant-numeric:tabular-nums}th,td{border-bottom:1px solid var(--border,#dce1e8);padding:8px 5px;text-align:center}th:first-child,td:first-child{text-align:left;max-width:210px}thead{color:var(--muted-foreground,#657184)}
.cell{width:100%;border:0;padding:5px 2px;font:inherit;font-weight:600;cursor:pointer;border-radius:5px;background:color-mix(in srgb,#168468 10%,transparent);color:var(--foreground,#182334)}.cell.slow{background:color-mix(in srgb,#cc6c30 15%,transparent)}.cell.failed{background:var(--card,#f4f6f9);font-weight:400}.cell:focus-visible{outline:2px solid #168468;outline-offset:2px}.cell:hover{outline:1px solid #168468}
.detail{margin:18px 0;padding:16px;background:var(--card,#f4f6f9);border-radius:var(--radius,10px)}.detail h3{margin:0 0 10px;font-size:15px}.grid{display:grid;grid-template-columns:repeat(3,minmax(0,1fr));gap:10px}.grid div{min-width:0}.grid small{display:block;color:var(--muted-foreground,#657184);font-size:11px}.grid b{font-size:14px;font-variant-numeric:tabular-nums}.note{font-size:12px;margin-top:16px}
@media(max-width:450px){.stats{gap:6px}.stat{padding:16px 10px}.stat b{font-size:19px}.grid{grid-template-columns:repeat(2,minmax(0,1fr))}h2{font-size:19px}}
</style>
<h2>30 題 × 5 種 population · 固定 16 GiB</h2>
<p>ASGP 1T / GAGP GPU-er-o；1024 cases、四代＋最後評分。倍率越大代表 GAGP 越快。</p>
<div class="stats"><div class="stat"><b id="support"></b><span>完成且有合格加速比</span></div><div class="stat"><b id="large"></b><span>P16384 全量 CPU audit 通過題數</span></div><div class="stat"><b id="peak"></b><span>整張 24 GiB 顯卡取樣峰值</span></div></div>
<div class="controls"><input id="filter" type="search" placeholder="搜尋題目" aria-label="搜尋題目"><select id="metric" aria-label="表格指標"><option value="speedup">加速比</option><option value="gpu_wall_s">GAGP 秒數</option><option value="asgp_wall_s">ASGP 秒數</option><option value="vram">顯存 GiB</option></select></div>
<div class="scroll"><table><thead><tr><th>Task</th><th>1024</th><th>2048</th><th>4096</th><th>8192</th><th>16384</th></tr></thead><tbody id="body"></tbody></table></div>
<div class="detail" id="detail"><h3 id="selected"></h3><div class="grid" id="grid"></div></div>
<p class="note">點擊任一格查看原始秒數與驗證範圍。每格一次獨立冷啟動，屬粗略量測；時間含初始化、四代、最後評分與必要清理。CPU audit 與檔案 I/O 在計時之外。所有 GPU 格固定同一 binary／16 GiB；ASGP 使用各 population 自己的實測控制組。</p>
<p class="note">顯存每 200ms 取樣整張卡用量，含桌面背景，可能漏掉更短的尖峰。errors／timeouts 是隨機程式的執行結果，已按契約計入 fitness；程式整體是否完成另列。舊失敗及較小 workspace 資料保留為歷史，未混入此表時間。</p>
<script>
const data=__DATA__, rows=data.rows, pops=[1024,2048,4096,8192,16384];
const tasks=[...new Set(rows.map(r=>r.task))];
const fmt=(v,n=2)=>typeof v==='number'?v.toLocaleString('en-US',{maximumFractionDigits:n,minimumFractionDigits:n}):'—';
const title=t=>t.replaceAll('_',' ').replace(/\b\w/g,c=>c.toUpperCase());
document.getElementById('support').textContent=data.qualified_cells+'/150';
document.getElementById('large').textContent=rows.filter(r=>r.population===16384&&r.cpu_audit_program_count===16384&&r.cpu_audit_mismatches===0&&r.gpu_status==='ok').length+'/30';
const peaks=rows.map(r=>r.sampled_device_peak_bytes||0);document.getElementById('peak').textContent=fmt(Math.max(...peaks)/2**30,2)+' GiB';
function choose(r){
 document.getElementById('selected').textContent=title(r.task)+' · P='+r.population;
 const fields=[['加速比',r.speedup==null?'無合格倍率':fmt(r.speedup)+'×'],['ASGP 1T',fmt(r.asgp_wall_s,3)+' s'],['GPU-er-o',fmt(r.gpu_wall_s,3)+' s'],['GPU 狀態',r.gpu_status],['Workspace',r.workspace_mib==null?'—':fmt(r.workspace_mib/1024,0)+' GiB'],['顯存取樣峰值',r.sampled_device_peak_bytes?fmt(r.sampled_device_peak_bytes/2**30)+' GiB':'—'],['CPU audit 個體數',fmt(r.cpu_audit_program_count,0)],['CPU mismatch',fmt(r.cpu_audit_mismatches,0)],['四代已評分 program-cases',fmt(r.evaluated_pairs,0)],['Runtime errors',fmt(r.errors,0)],['Timeouts（errors 子集）',fmt(r.timeouts,0)],['漏評 cases',fmt(r.unscored,0)]];
 const grid=document.getElementById('grid');grid.replaceChildren();for(const [k,v] of fields){const d=document.createElement('div'),s=document.createElement('small'),b=document.createElement('b');s.textContent=k;b.textContent=v;d.append(s,b);grid.append(d)}
}
function draw(){
 const q=document.getElementById('filter').value.toLowerCase(),metric=document.getElementById('metric').value;
 const body=document.getElementById('body');body.replaceChildren();
 for(const task of tasks.filter(t=>title(t).toLowerCase().includes(q)||t.includes(q))){
  const tr=document.createElement('tr'),name=document.createElement('td');name.textContent=title(task);tr.append(name);
  for(const pop of pops){const r=rows.find(x=>x.task===task&&x.population===pop),td=document.createElement('td'),b=document.createElement('button');b.className='cell'+(r.speedup==null?' failed':r.speedup<1?' slow':'');b.setAttribute('aria-label',title(task)+' population '+pop);
   const v=metric==='vram'?(r.sampled_device_peak_bytes?r.sampled_device_peak_bytes/2**30:null):r[metric];
   b.textContent=r.speedup==null?(r.gpu_status==='ok'?'ASGP '+r.asgp_status:r.gpu_status):fmt(v)+(metric==='speedup'?'×':'');b.onclick=()=>choose(r);td.append(b);tr.append(td)
  }body.append(tr)
 }
}
document.getElementById('filter').addEventListener('input',draw);document.getElementById('metric').addEventListener('change',draw);draw();choose(rows.find(r=>r.population===16384&&r.gpu_status==='ok')||rows[0]);
console.log('dashboard checks',JSON.stringify({rows:rows.length,tasks:tasks.length,columns:pops.length,qualified:data.qualified_cells,completed:data.completed_cells}));
</script></html>"""
    (a.out / 'dashboard.html').write_text(dashboard.replace('__DATA__', json.dumps(summary).replace('<', '\\u003c')))
