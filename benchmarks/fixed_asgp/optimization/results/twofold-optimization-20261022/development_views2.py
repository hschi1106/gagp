"""Plain development-only views; never promote them to formal/quality evidence."""
import json
import math


def number(value):
    return '—' if value is None else f'{value:.3f}'


def chosen(result):
    recipe = result.get('reported_recipe', result['best_complete'])
    if recipe is not None and not recipe['complete']:
        raise ValueError('reported recipe must have complete development coverage')
    return recipe


def markdown(result):
    recipe = chosen(result)
    lines = ['# GAGP 開發對照', '',
        '**範圍：舊 TRAINING、G8、每方法一次、seed0、CPU16 抽查。**',
        '本頁不提供正式重複量測、完整族群 CPU 核對或獨立品質的不劣性結論；2× 目標尚未由本頁驗證。', '',
        '| 組合 | 合格／預期格 | P1024 | P16384 | 合併 |',
        '|---|---:|---:|---:|---:|']
    for row in result['recipes']:
        means = row['geometric_means']
        lines.append(f"| {row['family']}/{row['variant']} | {row['qualified_cells']}/{row['expected_cells']} | "
                     f"{number(means['1024'])}× | {number(means['16384'])}× | {number(means['pooled'])}× |")
    lines += ['', '只有全部題目、兩種 P 都完成且配對一致的組合才列合併幾何平均；失敗與缺格保留。',
        '分母使用本輪重新量測的上一個最快 GAGP。各組合只使用自身配對控制，不混用控制秒數。', '']
    if recipe:
        lines.append(f"圖表與預設篩選組合：{recipe['family']}/{recipe['variant']}。")
    else:
        lines.append('目前沒有完整開發組合可供圖表比較。')
    lines += ['', '搜尋 wall time包含演化函式內的 admission、GPU 初始化、逐代演化與最終族群還原。'
        '輸入／結果 JSON、AST 寫出與額外 CPU 核對另計，不能由這個倍率推論總工作時間或 time-to-solution。',
        'GPU 評分及繁殖、16 GiB workspace、CPU1T；來源、binary、輸入與實際讀取的證據雜湊見 summary.json。',
        '', '資料：[逐格 CSV](development_cells.csv)、[各組合摘要](development_recipes.csv)、'
        '[互動頁](dashboard.html)、[PNG](development_speed.png)、[PDF](development_speed.pdf)。', '']
    return '\n'.join(lines)


def dashboard(result, tasks):
    recipe = chosen(result)
    payload = dict(cells=result['cells'], tasks=tasks,
                   recipe=None if recipe is None else recipe['family'] + '/' + recipe['variant'],
                   means=None if recipe is None else recipe['geometric_means'])
    data = json.dumps(payload, ensure_ascii=False).replace('<', '\\u003c')
    return r'''<!doctype html><html lang="zh-Hant"><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>GAGP 開發對照</title><style>
*{box-sizing:border-box}html,body{background:#fff;color:#111}body{margin:0;font:16px/1.5 system-ui,sans-serif}h1{font-size:24px}p{max-width:78ch}label{display:inline-block;margin:6px 12px 6px 0}input,select{max-width:100%;font:inherit;background:#fff;color:#111;border:1px solid #555;padding:4px;border-radius:0}.scroll{max-width:100%;overflow-x:auto}table{border-collapse:collapse;min-width:690px;width:100%;font-variant-numeric:tabular-nums}td,th{text-align:left;padding:7px;border-bottom:1px solid #aaa}th{border-bottom:2px solid #111}td:first-child,td:nth-child(2){overflow-wrap:anywhere}a{color:#111}@media(max-width:480px){label{display:block}input,select{width:100%}h1{font-size:21px}table{min-width:0;table-layout:fixed}th,td{padding:6px 3px;font-size:13px;overflow-wrap:anywhere}th:nth-child(2),td:nth-child(2),th:nth-child(4),td:nth-child(4),th:nth-child(5),td:nth-child(5){display:none}th:first-child,td:first-child{width:42%}th:nth-child(3),td:nth-child(3){width:20%}th:nth-child(6),td:nth-child(6){width:22%}th:nth-child(7),td:nth-child(7){width:16%}}
</style><h1>GAGP 開發對照</h1>
<p><strong>舊 TRAINING · G8 · 一次冷啟動 · CPU16 抽查</strong></p>
<p>本頁不提供正式重複量測、完整族群 CPU 核對或獨立品質結論。2× 目標尚未由本頁驗證。</p>
<p id="means"></p>
<label>篩選題目 <input id="filter" type="search" aria-label="篩選題目"></label>
<label>選擇題目 <select id="task" aria-label="選擇題目"><option value="all">全部題目</option></select></label>
<label>Population <select id="pop" aria-label="Population"><option value="all">1024 + 16384</option><option>1024</option><option>16384</option></select></label>
<label>組合 <select id="recipe" aria-label="組合"><option value="all">全部組合</option></select></label>
<p id="coverage"></p><div class="scroll" tabindex="0" aria-label="開發速度表"><table><thead><tr><th>題目</th><th>組合</th><th>P</th><th>基準(s)</th><th>候選(s)</th><th>加速比</th><th>狀態</th></tr></thead><tbody id="rows"></tbody></table></div>
<p>Train／test 品質數據不在本頁；此處不提供切換或不劣性推論。</p>
<p><a href="REPORT.zh-TW.md">報告</a> · <a href="development_cells.csv">CSV</a> · <a href="summary.json">證據與範圍</a></p>
<script>const data=__PAYLOAD__;
const el=id=>document.getElementById(id), recipeKey=r=>r.family+'/'+r.variant;
el('means').textContent=data.means?'完整60格幾何平均：P1024 '+data.means['1024'].toFixed(3)+'×；P16384 '+data.means['16384'].toFixed(3)+'×；合併 '+data.means.pooled.toFixed(3)+'×。':'尚無完整開發範圍。';
function option(select,value){const o=document.createElement('option');o.value=value;o.textContent=value;select.appendChild(o)}
data.tasks.forEach(t=>option(el('task'),t));[...new Set(data.cells.map(recipeKey))].sort().forEach(r=>option(el('recipe'),r));
if(data.recipe)el('recipe').value=data.recipe;
function render(){
const f=el('filter').value.trim().toLowerCase(),t=el('task').value,p=el('pop').value,v=el('recipe').value;
const rows=data.cells.filter(r=>(!f||r.task.toLowerCase().includes(f))&&(t==='all'||r.task===t)&&(p==='all'||String(r.population)===p)&&(v==='all'||recipeKey(r)===v));
el('rows').replaceChildren();for(const r of rows){const tr=document.createElement('tr');
for(const value of [r.task,recipeKey(r),r.population,r.baseline_search_s,r.candidate_search_s,r.speedup,r.status]){
const td=document.createElement('td');td.textContent=value==null?'—':typeof value==='number'&&!Number.isInteger(value)?value.toFixed(3):String(value);tr.appendChild(td)}el('rows').appendChild(tr)}
el('coverage').textContent='顯示 '+rows.length+' 格；僅屬開發對照。'}
['filter','task','pop','recipe'].forEach(id=>el(id).addEventListener(id==='filter'?'input':'change',render));render();
</script></html>'''.replace('__PAYLOAD__', data)


def plots(result, tasks, out):
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    recipe = chosen(result)
    rows = [] if recipe is None else [r for r in result['cells']
        if r['family'] == recipe['family'] and r['variant'] == recipe['variant']]
    fig, axes = plt.subplots(1, 2, figsize=(13, 11), sharey=True)
    for ax, p in zip(axes, (1024, 16384)):
        lookup = {r['task']: r for r in rows if r['population'] == p}
        values = [lookup[t]['speedup'] if lookup.get(t, {}).get('status') == 'ok'
                  else float('nan') for t in tasks]
        ax.barh(range(len(tasks)), values, color='black', height=.65)
        for i, value in enumerate(values):
            if not math.isfinite(value):
                ax.text(.05, i, 'missing / failed', va='center', fontsize=7)
        ax.axvline(1, color='.5', lw=.7)
        ax.axvline(2, color='black', ls='--', lw=1)
        ax.set_yticks(range(len(tasks)), [t.replace('_', ' ') for t in tasks], fontsize=8)
        ax.set_title(f'Population {p}')
        ax.set_xlabel('Previous fastest GAGP / candidate (search wall)')
        ax.set_xlim(0, max(2.2, max([v for v in values if math.isfinite(v)] or [2]) * 1.08))
        ax.spines[['top', 'right']].set_visible(False)
    axes[0].invert_yaxis()
    fig.suptitle('Development only — G8 / one cold run / CPU16 audit')
    fig.tight_layout()
    for suffix in ('png', 'pdf'):
        fig.savefig(out / ('development_speed.' + suffix), dpi=180, bbox_inches='tight')
    plt.close(fig)
