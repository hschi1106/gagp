"""Publish completed all30 P16384 support results, keeping failed rows explicit."""
from argparse import ArgumentParser
import csv
import json
import math
from pathlib import Path
import shutil
import statistics

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, sha, validate_search

p=ArgumentParser(description=__doc__)
p.add_argument('--matrix',type=Path,default=Path('logs/nodes2048-searches-20261016'))
p.add_argument('--out',type=Path,required=True)
a=p.parse_args()
m=read(a.matrix/'manifest.json')
assert m.get('requested_phase_complete') and set(m['results'])==set(PAPER_TASKS),'matrix is incomplete'
assert m['settings']['GAGP_GPU_PAYLOAD_WORKSPACE_MIB']=='16384'
assert sha(Path(m['binary']))==m['binary_sha256']
assert len(m['small_modes'])==5 and all(r['status']=='ok' for r in m['small_modes'].values()),'five-mode support incomplete'
rows=[]
for task in PAPER_TASKS:
    r=m['results'][task]
    assert r['status'] not in ('pending','running','waiting','budget_not_run')
    row=dict(task=task,population=16384,cases=1024,generations=4,native_nodes=2048,native_depth=24,
        status=r['status'],workspace_mib=16384,gpu_wall_s=r.get('evolve_wall_s'),
        asgp_wall_s=r.get('asgp_control',{}).get('evolve_wall_s'),speedup=r.get('speedup'),
        sampled_device_peak_bytes=r.get('sampled_device_peak_bytes'),reason=r.get('reason'))
    for k in ('cpu_audit_program_count','cpu_audit_mismatches','errors','timeouts','unscored',
              'reproduction_profile','final_max_nodes','process_s','output','log'):
        row[k]=r.get(k)
    assert r['inputs']['native_limits']==dict(max_nodes=2048,max_depth=24)
    if r['status']=='ok':
        data=read(Path(r['output']));validate_search(data,16384,4,'gpu_overlap')
        assert data['cpu_audit_program_count']==16384 and data['cpu_audit_mismatches']==0
        assert data['reproduction_profile'].startswith('native-gpu-phase-') and not data['reproduction_fallback_reason']
        assert row['unscored']==0 and row['final_max_nodes']<=2048
        assert row['asgp_wall_s']>0 and math.isclose(row['speedup'],row['asgp_wall_s']/row['gpu_wall_s'])
        control=r['asgp_control'];assert sha(Path(control['output']))==control['sha256']
        validate_search(read(Path(control['output'])),16384,4,'asgp_1t')
    rows.append(row)
passed=[r for r in rows if r['status']=='ok']
values=[r['speedup'] for r in passed]
peak=max(r.get('sampled_device_peak_bytes') or 0 for r in rows)/1024**3
s=dict(population=16384,cases=1024,generations=4,uniform_native_limits=m['uniform_native_limits'],
    phase_capacity=1024,workspace_mib=16384,completed=30,passed=len(passed),
    full_cpu_audits=len(passed),sampled_device_peak_gib=peak,
    median_speedup=statistics.median(values) if values else None,
    geometric_mean_speedup=math.exp(statistics.mean(math.log(v) for v in values)) if values else None,
    faster_than_asgp=sum(v>1 for v in values),rows=rows)
a.out.mkdir(parents=True,exist_ok=False)
(a.out/'summary.json').write_text(json.dumps(s,indent=2)+'\n')
with (a.out/'cells.csv').open('w',newline='') as f:
    w=csv.DictWriter(f,fieldnames=list(rows[0]),lineterminator='\n');w.writeheader();w.writerows(rows)
for source,name in [(a.matrix/'environment.json','environment.json'),
                    (Path(__file__).parent/'validation.json','validation.json')]:
    shutil.copy2(source,a.out/name)
preparation=read(Path(m['preparation_manifest']))
admissions={t:{k:v for k,v in preparation['catalog'][t].items() if k not in ('prepared',)} for t in PAPER_TASKS}
(a.out/'admissions.json').write_text(json.dumps(admissions,indent=2)+'\n')
(a.out/'provenance.json').write_text(json.dumps(dict(matrix_manifest=str(a.matrix/'manifest.json'),
    matrix_sha256=sha(a.matrix/'manifest.json'),runtime_source_head=m['source_head'],binary_sha256=m['binary_sha256'],
    runner_sha256=m['runner_sha256'],report_sha256=sha(Path(__file__)),
    template_sha256=sha(Path(__file__).with_name('dashboard.template.html')),
    preparation_manifest=m['preparation_manifest'],preparation_sha256=m['preparation_sha256'],
    controls_manifest=m['controls_manifest'],controls_sha256=m['controls_sha256']),indent=2)+'\n')
lines=['# 全30題 P16384：統一2048-node／16GiB workspace','',
    f'完整執行與全量CPU fitness audit通過 **{len(passed)}/30**。NVML每200ms取樣整張24GiB RTX3090，最高 **{peak:.2f} GiB**（含桌面，可能漏掉短尖峰）。','',
    '所有題目採同一binary、CPU1T、seed0、完整16384初始個體與1024cases、四代加最後評分。',
    'GAGP全AST上限統一2048、physical depth24；每phase GPU容量仍1024、genotype配置guard仍12GiB、workspace仍16GiB。ASGP來源phase depth7及其他演化規則維持原樣，搜尋空間仍未完全對齊。','']
if values:
    lines += [f'跨題倍率中位數 **{s["median_speedup"]:.2f}×**，幾何平均 **{s["geometric_mean_speedup"]:.2f}×**；{s["faster_than_asgp"]}/{len(values)}題快於ASGP。每題一次cold process，屬粗略速度與支援檢查，非精確重複量測或收斂等價證明。','']
lines += ['| 題目 | ASGP 1T秒 | GPU-er-o秒 | 倍率 | CPU mismatch | 顯存取樣GiB |','| --- | ---: | ---: | ---: | ---: | ---: |']
for r in rows:
    if r['status']=='ok':
        lines.append(f'| {r["task"]} | {r["asgp_wall_s"]:.3f} | {r["gpu_wall_s"]:.3f} | {r["speedup"]:.2f}× | {r["cpu_audit_mismatches"]} | {r["sampled_device_peak_bytes"]/1024**3:.2f} |')
    else:lines.append(f'| {r["task"]} | — | {r["status"]} | — | — | — |')
lines += ['','## 驗證與限制','',
    '- 全30題先逐一重新準備16384個初始個體；grammar僅改node budget，不抽換／刪除個體。29題完整初始AST與舊artifact相同，House前8192個體相同。House僅2個初始AST超過1024，最大1086，2048設定可接受這批來源。',
    '- 每個成功格對全部16384個最終個體、全部1024cases做CPU fitness audit，零mismatch、四代零unscored。這是fitness核對，不宣稱每個隨機程式都正確或都能解題；errors/timeouts照原契約計入fitness。House另有原生五模式P8/C1024/G2檢查。',
    '- 搜尋時間採evolve_wall_ms，包含evolve呼叫內的GPU初始化、admission/materialization、四代、最後評分與清理；CLI輸入解析／匯入準備、離線準備、輸出及獨立CPU audit在此計時外。ASGP同P控制組以相同source/cases hash驗證後重用，ASGP路徑未改且不讀native grammar budget。',
    '- 只提高整體node budget，沒有把每phase緩衝區加倍。Mutation donor同時符合whole budget與剩餘phase容量；phase owner先通過獨立admission才略過generic GPU preflight。若phase profile不支援，generic kernel仍保留1024-node硬限制，不以CPU繁殖替代。',
    '- 完整Release build及131/131 native tests通過；tools116項（4 skips）、repository24項通過。大AST公共evolve入口、CPU/GPU fitness、generic fallback拒絕與既有32代staged/resident回歸均覆蓋。',
    '- 這份表只包含新budget的P16384。舊1024-node五種population主表保持原樣，沒有把新House單格補進舊表。先期2048-node pilot的generic preflight失敗也保留；本表使用後續修正的同一binary重新實測全部30題。']
for r in rows:
    if r['status']!='ok':lines.append(f'- {r["task"]}：{r["status"]}；{r["reason"] or "原始log見CSV／manifest"}，沒有合格倍率。')
lines += ['','[設定與重現](../README.md) · [原始秒數與counters](cells.csv) · [互動表](dashboard.html) · [PNG](speedup.png) · [PDF](speedup.pdf) · [來源雜湊](provenance.json)','',
    f'Runtime checkpoint：`{m["source_head"][:7]}`。完整raw資料：`{a.matrix.resolve()}`。','']
(a.out/'REPORT.zh-TW.md').write_text('\n'.join(lines))
template=Path(__file__).with_name('dashboard.template.html').read_text()
(a.out/'dashboard.html').write_text(template.replace('__DATA__',json.dumps(s,separators=(',',':')).replace('<','\\u003c')))
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
fig,ax=plt.subplots(figsize=(8.5,10.5))
for i,r in enumerate(rows):
    v=r['speedup']
    if v is not None:
        ax.scatter(v,i,color='#177d68' if v>=1 else '#bb6a35',s=25,zorder=3)
        ax.annotate(f'{v:.2f}x',(v,i),xytext=(6,0),textcoords='offset points',va='center',fontsize=8)
    else:ax.text(.6,i,r['status'],va='center',fontsize=7,color='#a84030')
ax.set_yticks(range(30),[r['task'].replace('_',' ').title() for r in rows],fontsize=8)
ax.invert_yaxis();ax.set_xscale('log');ax.set_xlim(min(.5,min(values)*.6) if values else .5,max(80,max(values)*2) if values else 80);ax.axvline(1,color='#999',lw=1)
from matplotlib.ticker import FuncFormatter
ax.xaxis.set_major_formatter(FuncFormatter(lambda value,position:f'{value:g}'))
ax.set_xlabel('ASGP 1T / GPU-er-o speedup (log scale)')
ax.set_title('All30 / P16384 / full1024 cases / G4 + final evaluation\nUniform2048 native AST nodes;16GiB workspace;CPU1T',fontsize=10)
ax.grid(axis='x',alpha=.2);fig.tight_layout()
fig.savefig(a.out/'speedup.png',dpi=180);fig.savefig(a.out/'speedup.pdf');plt.close(fig)
print(json.dumps({k:v for k,v in s.items() if k!='rows'},indent=2))
