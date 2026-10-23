"""Plain Chinese report views; all numbers come from checked frozen evidence."""
import html
import json
import math

import dashboard_data


def number(value,digits=2):
    return '—' if value is None else f'{value:.{digits}f}'


def recipe_rows(freeze):
    """Describe only enabled options in the actual frozen candidate."""
    settings=dict(freeze.get('settings',{}))
    settings.update(freeze['variants']['candidate']['extra'])
    mechanisms=[
        ('GAGP_FUSED_COLD_EXECUTABLE','合併匯入驗證與bytecode取得',
         '同一次完整驗證後取用其唯讀bytecode，省去重建身分字串；保留輸入順序及payload snapshot檢查。'),
        ('GAGP_GPU_INTEGER_CASE_REUSE','相同案例合併',
         '有效輸入及答案完全相同時只執行一次，按重複次數累計分數與wrong。'),
        ('GAGP_GPU_SHAPE_RESULT_CACHE','輸入形狀結果重用',
         '僅限已證明不讀取串列元素、回傳純量的程式；形狀及有效純量參數相同才重用結果。'),
        ('GAGP_GPU_UNCHANGED_FITNESS_REUSE','未改變子代沿用分數',
         '基因型及資料版本完全一致時，沿用前一代已完成的GPU分數。'),
        ('GAGP_GPU_PARENT_FITNESS_CACHE','跨父代查找已完成分數',
         'GPU先以hash找候選，再完整比較基因型；碰撞或查找失敗就正常評分。'),
        ('GAGP_GPU_PRIVATE_ADJACENT_CONCAT','相鄰不可變串列共用儲存',
         '兩段資料在同一儲存區相鄰時，以區間表示串接結果，省去再次複製。實際寫入位置與配置額度分開記錄；長度、邏輯配置、容量上限及溢位重跑規則保留原值。僅限已通過驗證的私有連續整數串列。'),
        ('GAGP_GPU_PRIVATE_CELL_SOA','暫存整數串列交錯排列',
         '將同一實體block各執行緒的相同位置排在一起，改善記憶體存取；容量、handle及溢位規則相同。'),
        ('GAGP_GPU_SHORT_EDIT_DISTANCE','精確編輯距離評分',
         '私有整數串列的Levenshtein評分：通過答案檢查且不超過64項時用位元平行計算；長答案保留原DP，空串列及相同串列直接取得精確距離。建表成本計入搜尋。'),
        ('GAGP_GPU_LINEAR_ELITE','單菁英線性選取',
         '用O(P)選出唯一菁英，保留原fitness及index的排序規則。'),
        ('GAGP_GPU_STAGE_STORAGE','評分／繁殖共用儲存區',
         '順序重用16GiB空間；另列64KiB對齊成本，減少兩階段同時保留的配置。'),
        ('GAGP_GPU_CUDA_POOL','CUDA配置重用',
         '在原生GPU路徑重用已釋放的記憶體，配置及清理時間仍計入搜尋。')]
    rows=[(title,description) for key,title,description in mechanisms
          if settings.get(key)=='1']
    physical=settings.get('GAGP_GPU_VIEW_PHYSICAL_BLOCK','0')
    if physical!='0':
        rows.append(('評分核心實體threads',f'{physical} threads；仍處理原512個邏輯評分位置及全部案例。'))
    bound=settings.get('GAGP_GPU_PRIVATE_LAUNCH_BOUND','0')
    if bound!='0':
        rows.append(('私有串列核心編譯配置',f'launch bound={bound}；改變暫存器配置，保持執行語意。'))
    rows.append(('繁殖核心block',settings.get('GAGP_PHASE_REPRO_THREADS','64')+' threads；每個子代的RNG與運算不變。'))
    return rows


def markdown(bundle):
    speed=bundle['speed'];quality=bundle['quality'];freeze=bundle['speed_freeze']
    summary=speed['summary'];bound=quality['clustered_bound']
    reached=summary['all_primary_targets_reached']
    lines=['# GAGP 全30題額外加速與品質驗證','',
        '**速度目標：'+('達成' if reached else '未達成／證據未齊全')+'。** 目標是相對上一個最快 GAGP 再快2倍。',
        '',f"P=1024與16384；每次 {freeze['generations']} 代、1024案例，{freeze['repetitions']} 次冷啟動。速度重複使用同一初始族群與seed0。CPU1T，GPU評分及繁殖，16GiB workspace。",'',
        '| Population | 基準搜尋(s) | 候選搜尋(s) | 加速比 | 合格／預期格 | 冷啟動重複倍率範圍 |',
        '|---:|---:|---:|---:|---:|---:|']
    for p in freeze['populations']:
        g=summary['groups'][str(p)];interval=g['cold_repeat_range']
        lines.append(f"| {p} | {number(g.get('observed_baseline_search_s'))} | {number(g.get('observed_candidate_search_s'))} | {number(g['observed_geometric_mean'])}× | {g['qualified_cells']}/{g['expected_cells']} | "+
            ('—' if interval is None else f'{number(interval[0])}–{number(interval[1])}×')+' |')
    pooled=summary['groups']['pooled']
    lines+=['',f"兩種population合併：{number(pooled['observed_geometric_mean'])}×；合格{pooled['qualified_cells']}/{pooled['expected_cells']}格。",
        f"完整最終族群有CPU參考核對：{summary['native_audit_qualified']}/{summary['native_audit_expected']}組（含完全相同程式／案例的跨方法精確重用；候選GPU另行執行）。",
        '', '搜尋秒數與倍率均為合格格子的幾何平均。搜尋wall time包含演化函式內的冷啟動admission、GPU初始化、逐代演化及最終族群還原；不包含輸入JSON讀取、結果JSON／AST檔案寫出及額外CPU核對，這些另計程序成本。缺失、失敗與核對未完成的格保留；不完整範圍的平均不代表全題達標。冷啟動範圍是描述性變動，並非跨題泛化信賴區間。',
        '', '## 本輪採用的機制','',
        '| 機制 | 工作方式 |','|---|---|']
    lines += ['| '+title+' | '+description+' |' for title,description in recipe_rows(freeze)]
    lines += ['', '這些機制共同構成上表的候選；整體倍率不能解讀成各機制的個別收益。完整族群／逐代冠軍與原生CPU核對用來檢查執行是否改變；獨立seeds另檢查演化品質。',
        '', '## 演化品質','',
        f"獨立演化seeds：{len(bundle['quality_freeze']['seeds'])}；每題P1024／G64。完整配對 {bound['expected_pairs']-bound['unknown_pairs']}/{bound['expected_pairs']}；未知{bound['unknown_pairs']}對，觀測退步{bound['observed_lost_pairs']}對。",
        f"相對基準的成功率保守下界：{number(bound['success_difference_lower_bound']*100)}個百分點。5pp不劣性門檻："+
            ('支持' if bound['supports_noninferiority'] else '證據不足')+'。',
        '', '成功指同一個訓練選出的最終程式在train與test全部答對；gen0、最終成功及演化後新增成功分開記錄。未知配對當作可能退步，不以「無顯著差異」宣稱等價。品質界限只適用固定題目／資料切分與獨立演化seed。',
        '', '| 方法 | 已核對runs | gen0已成功 | 最終成功 | 新增最終成功 | 失去初始成功 |',
        '|---|---:|---:|---:|---:|---:|']
    for variant,label in [('baseline','上一個最快GAGP'),('candidate','候選GAGP')]:
        rows=[r for r in quality['runs'] if r['variant']==variant and r.get('quality_audited')]
        vals=[len(rows)]+[sum(bool(r['joint_'+key]) for r in rows) for key in ('gen0','final','new_final','lost_initial')]
        lines.append('| '+label+' | '+' | '.join(map(str,vals))+' |')
    lines+=['','## 成本與限制','',
        f"速度搜尋程序（含內建CPU16核對）：{number(speed['costs']['search_process_s']/3600)}h；額外完整原生核對程序：{number(speed['costs']['native_audit_process_s']/3600)}h。",
        f"品質搜尋程序：{number(quality['costs']['search_process_s']/3600)}h；CPU可讀判題：{number(quality['costs']['common_judge_process_s']/3600)}h；原生核對程序：{number(quality['costs']['native_judge_process_s']/3600)}h。",
        '', 'CPU核對成本不納入搜尋倍率，也不能由搜尋倍率推論總工作時間或time-to-solution。原生CPU實際執行量與AST去重後展開觀測量分列於summary.json；跨方法重用CPU參考的群組新增CPU執行量為0，原始參考只計算一次。',
        '這不是ASGP論文重現、任意程式語意等價或所有輸入分布的品質保證。有限輸入域、舊輸入重疊、初始admission失敗、整卡VRAM取樣限制與實際回歸結果見validation.json及逐run CSV。',
        '', '資料：[逐格速度](speed_cells.csv)、[搜尋runs](speed_runs.csv)、[原生核對](speed_native_audits.csv)、[品質runs](quality_runs.csv)、[品質配對](quality_pairs.csv)、[逐題品質](quality_tasks.csv)、[互動頁](dashboard.html)。','']
    return '\n'.join(lines)


def plots(bundle,out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    tasks=bundle['speed_freeze']['tasks'];cells=bundle['speed']['summary']['cells']
    fig,axes=plt.subplots(1,2,figsize=(13,11),sharey=True)
    for ax,p in zip(axes,bundle['speed_freeze']['populations']):
        lookup={r['task']:r for r in cells if r['population']==p}
        values=[lookup[t]['speedup'] if lookup[t]['qualified'] else float('nan') for t in tasks]
        ax.barh(range(len(tasks)),values,color='black',height=.65)
        for i,value in enumerate(values):
            if not math.isfinite(value):ax.text(.07,i,'missing / failed',va='center',fontsize=7)
        ax.axvline(1,color='.5',lw=.7);ax.axvline(2,color='black',ls='--',lw=1)
        ax.set_yticks(range(len(tasks)),[t.replace('_',' ') for t in tasks],fontsize=8)
        ax.set_title(f'Population {p}');ax.set_xlabel('Previous fastest GAGP / candidate (search wall)')
        ax.set_xlim(0,max(2.2,max([v for v in values if math.isfinite(v)] or [2])*1.08))
        ax.spines[['top','right']].set_visible(False)
    axes[0].invert_yaxis();fig.suptitle('All 30 tasks — dashed line: 2× target')
    fig.tight_layout()
    for suffix in ('png','pdf'):fig.savefig(out/('speed.'+suffix),dpi=180,bbox_inches='tight')
    plt.close(fig)
    rows=bundle['quality']['tasks'];fig,ax=plt.subplots(figsize=(10,11))
    for i,row in enumerate(rows):
        lo,hi=row['difference_ci95'];mid=sum(row['identification_range'])/2
        ax.errorbar(mid*100,i,xerr=[[max(0,mid-lo)*100],[max(0,hi-mid)*100]],fmt='o',color='black',ms=3,capsize=2)
        if row['unknown_pairs']:ax.text(51,i,f"unknown {row['unknown_pairs']}",va='center',fontsize=7)
    ax.axvline(0,color='.5',lw=.7);ax.axvline(-5,color='black',ls='--',lw=1)
    ax.set_yticks(range(len(rows)),[r['task'].replace('_',' ') for r in rows],fontsize=8)
    ax.invert_yaxis();ax.set_xlim(-105,105);ax.spines[['top','right']].set_visible(False)
    ax.set_xlabel('Candidate − baseline joint final success (percentage points)')
    ax.set_title('Per-task paired exact bounds; unknown assignments enclosed\nExploratory intervals; primary seed-cluster bound is reported separately')
    fig.tight_layout()
    for suffix in ('png','pdf'):fig.savefig(out/('quality.'+suffix),dpi=180,bbox_inches='tight')
    plt.close(fig)


def dashboard(bundle):
    payload=dashboard_data.compact_payload(bundle)
    data=json.dumps(payload,ensure_ascii=False).replace('<','\\u003c')
    return '''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GAGP 全題加速與品質</title><style>
*{box-sizing:border-box}body{margin:24px auto;padding:0 16px;max-width:1100px;color:#111;background:white;font:16px/1.5 system-ui,sans-serif}h1{font-size:26px}h2{font-size:21px;margin-top:28px}p{max-width:80ch}label{display:inline-block;margin:8px 14px 8px 0}input,select,button{font:inherit;max-width:100%;background:white;color:#111;border:1px solid #555;border-radius:0;padding:5px}input{width:180px}.scroll{overflow-x:auto;max-width:100%}table{border-collapse:collapse;width:100%;font-variant-numeric:tabular-nums}th,td{padding:7px 8px;border-bottom:1px solid #aaa;text-align:left}th{border-bottom:2px solid black}td:first-child{overflow-wrap:anywhere}a{color:#111}.note{font-size:14px;color:#333}@media(max-width:480px){body{margin:16px auto;padding:0 12px}h1{font-size:23px}th,td{padding:4px 2px;font-size:12px;overflow-wrap:anywhere}label{display:block}input,select{width:100%}table{table-layout:fixed}.speed-table th:nth-child(3),.speed-table td:nth-child(3),.speed-table th:nth-child(4),.speed-table td:nth-child(4),.speed-table th:nth-child(6),.speed-table td:nth-child(6){display:none}.speed-table th:first-child,.speed-table td:first-child{width:48%}.speed-table th:nth-child(2),.speed-table td:nth-child(2){width:16%}.speed-table th:nth-child(5),.speed-table td:nth-child(5){width:20%}.speed-table th:nth-child(7),.speed-table td:nth-child(7){width:16%}.quality-table th:first-child,.quality-table td:first-child{width:35%}.quality-table th:not(:first-child),.quality-table td:not(:first-child){width:13%}}</style>
<h1>GAGP 全30題加速與品質</h1><p id="decision"></p><p class="note">倍率使用本輪未profile搜尋wall time；完整CPU核對成本另列。缺失與失敗均保留。</p>
<label>篩選題目 <input id="filter" type="search" aria-label="篩選題目"></label>
<label>選擇題目 <select id="task" aria-label="選擇題目"><option value="all">全部30題</option></select></label>
<h2>速度</h2><p id="scope"></p><div class="scroll" tabindex="0" aria-label="速度表"><table class="speed-table"><thead><tr><th>題目</th><th>P</th><th>基準(s)</th><th>候選(s)</th><th>加速比</th><th>重複</th><th>狀態</th></tr></thead><tbody id="speed"></tbody></table></div>
<h2>品質</h2><label>成功條件 <select id="split" aria-label="成功條件"><option value="joint">Train + Test</option><option value="train">Train</option><option value="test">Test</option></select></label>
<p class="note">同一個訓練選出的程式需在指定split全部答對。gen0是初始冠軍；新增是gen0未成功但最終成功。Test切換僅供閱讀，不用於挑選候選。</p>
<div class="scroll" tabindex="0" aria-label="品質表"><table class="quality-table"><thead><tr><th>題目／方法</th><th>核對／預期</th><th>gen0</th><th>最終</th><th>新增</th><th>失去</th></tr></thead><tbody id="quality"></tbody></table></div>
<p id="bound"></p><p><a href="REPORT.zh-TW.md">完整報告</a> · <a href="speed_cells.csv">速度CSV</a> · <a href="quality_runs.csv">品質CSV</a> · <a href="validation.json">來源與核對</a></p>
<p class="note">固定題目與資料切分、本機GPU、有限輸入域與整卡取樣皆有限制；本輪不宣稱ASGP論文重現或任意程式等價。</p>
<script type="application/json" id="data">'''+data+'''</script><script>
const d=JSON.parse(document.querySelector('#data').textContent), $=id=>document.getElementById(id), fmt=x=>x==null?'—':x.toFixed(2), esc=s=>String(s).replace(/[&<>"']/g,c=>({'&':'&amp;','<':'&lt;','>':'&gt;','"':'&quot;',"'":'&#39;'}[c]));
for(const task of d.tasks){const option=document.createElement('option');option.value=task;option.textContent=task.replaceAll('_',' ');$('task').append(option)}
$('decision').textContent=d.speed.all_primary_targets_reached?'全題速度目標達成。':'全題2×速度目標尚未達成，或所需核對未齊全。';
const g=d.speed.groups.pooled;$('scope').textContent=`兩種P合併：${fmt(g.observed_geometric_mean)}×；合格 ${g.qualified_cells}/${g.expected_cells} 格。原生全族群核對 ${d.speed.native_audit_qualified}/${d.speed.native_audit_expected} 組。`;
$('bound').textContent=`主要品質界限：${(d.bound.success_difference_lower_bound*100).toFixed(2)}pp；5pp不劣性${d.bound.supports_noninferiority?'有支持':'證據不足'}。未知配對 ${d.bound.unknown_pairs}，列為可能退步。`;
function render(){const q=$('filter').value.trim().toLowerCase(),selected=$('task').value,split=$('split').value;const tasks=d.tasks.filter(t=>(selected==='all'||t===selected)&&t.replaceAll('_',' ').toLowerCase().includes(q.replaceAll('_',' ')));$('speed').innerHTML=d.speed.cells.filter(r=>tasks.includes(r.task)).map(r=>`<tr><td>${esc(r.task.replaceAll('_',' '))}</td><td>${r.population}</td><td>${fmt(r.observed_baseline_search_s)}</td><td>${fmt(r.observed_candidate_search_s)}</td><td>${fmt(r.speedup)}×</td><td>${r.repetitions}</td><td>${r.qualified?'已核對':esc(r.failures.join('; ')||'未完成')}</td></tr>`).join('');$('quality').innerHTML=tasks.flatMap(t=>['baseline','candidate'].map(v=>{const group=d.quality_counts.find(r=>r.task===t&&r.variant===v),count=k=>group[split][k];return `<tr><td>${esc(t.replaceAll('_',' '))}<br>${v==='baseline'?'上一個最快GAGP':'候選GAGP'}</td><td>${group.audited}/${group.expected}</td><td>${count('gen0')}</td><td>${count('final')}</td><td>${count('new_final')}</td><td>${count('lost_initial')}</td></tr>`})).join('')}
for(const id of ['filter','task','split'])$(id).addEventListener(id==='filter'?'input':'change',render);render();
</script></html>'''
