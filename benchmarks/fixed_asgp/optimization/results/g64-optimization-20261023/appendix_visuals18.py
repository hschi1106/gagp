"""Create a NEW development-only supplement from audited, completed evidence."""
import argparse
import base64
import csv
import fcntl
import json
from pathlib import Path
import time

import serial


HTML = r'''<!doctype html><html lang="zh-Hant"><meta charset="utf-8">
<meta name="viewport" content="width=device-width,initial-scale=1"><title>G64追加開發</title>
<style>
*{box-sizing:border-box}body{margin:0;font:15px/1.55 Arial,"Noto Sans CJK TC",sans-serif;color:var(--foreground,#111)}
h1{font-size:22px;margin:0 0 10px}h2{font-size:18px;margin:24px 0 8px}p{margin:8px 0}small,.note{font-size:13px;color:var(--muted-foreground,#555)}
table{width:100%;border-collapse:collapse;font-variant-numeric:tabular-nums}td,th{padding:8px 5px;border-bottom:1px solid var(--border,#ccc);text-align:right;vertical-align:top}td:first-child,th:first-child{text-align:left;overflow-wrap:anywhere}th{font-size:12px;font-weight:600}td{font-size:13px}td:first-child{width:43%}
.controls{display:flex;flex-wrap:wrap;gap:10px;margin:12px 0}.controls label{flex:1 1 210px;min-width:0}input,select{display:block;width:100%;font:inherit;padding:6px;border:1px solid #888;background:transparent;color:inherit;border-radius:0;min-width:0}
.task{font-weight:600;overflow-wrap:anywhere}.barrow{display:grid;grid-template-columns:90px minmax(0,1fr) 72px;gap:8px;align-items:center;margin:8px 0;font-size:13px}.bar{height:12px;background:currentColor}.barwrap{min-width:0}.numbers{display:flex;gap:22px;flex-wrap:wrap;margin:12px 0}.numbers div{min-width:100px}.numbers strong{display:block;font-size:25px;font-weight:600}.numbers span{font-size:12px}
button{font:inherit;background:transparent;color:inherit;border:1px solid #888;padding:5px 9px;cursor:pointer}button[aria-pressed=true]{font-weight:bold;border-width:2px}.switch{display:flex;gap:8px;flex-wrap:wrap;margin:10px 0}
details{margin:12px 0}summary{cursor:pointer}img{max-width:100%;height:auto}a{color:inherit}#empty{padding:15px 0}svg{max-width:100%}@media(max-width:420px){body{font-size:14px}td,th{padding:7px 3px}th{font-size:11px}.barrow{grid-template-columns:78px minmax(0,1fr) 66px;gap:5px}}
</style>
<h1>同代分組沒有增加整體速度</h1>
<p>P1024／G64，舊training，seed0，每格一次。原正式成績仍為1.079×，1.3×目標未達。</p>
<div class="numbers"><div><strong id="freshgm"></strong><span>相對本輪選定控制</span></div><div><strong id="offgm"></strong><span>相對同一binary關閉功能</span></div><div><strong>120/120</strong><span>搜尋完成，30/30題完整配對</span></div></div>
<p>同代完全相同的genotype只評分一次，再複製fitness／wrong／計數。完整比較不改RNG、算子、案例、fuel或個體容量。</p>
<p id="mechanism"></p><p>六項native回歸通過。未達原定額外2%收益門檻，因此沒有執行seeds1–2與65冠軍CPU確認；功能未採用。</p>
<h2>逐題時間</h2><p class="note">倍率＝對照搜尋時間÷分組搜尋時間；大於1表示分組較快。一次計時不提供穩定的小幅收益證明。</p>
<div class="controls"><label>篩選題目<input id="filter" type="search" placeholder="例如 house"></label><label>選擇題目<select id="task"></select></label></div>
<div class="task" id="selected"></div><div class="switch"><button id="search" aria-pressed="true">搜尋wall</button><button id="process" aria-pressed="false">完整程序</button></div><div id="bars"></div><p id="detail" class="note"></p>
<table><thead><tr><th>題目</th><th>本輪選定控制<br>÷分組</th><th>同binary關閉<br>÷分組</th><th>實體評分<br>減少</th></tr></thead><tbody id="rows"></tbody></table><p id="empty" hidden>沒有符合的題目。</p>
<h2>品質與成本的核對範圍</h2><p>30題四種設定的完整最終1024個AST、64筆冠軍AST／fitness與逐代best_wrong等紀錄一致。120次搜尋各用native CPU抽查16個最終個體，共1920筆；fitness與wrong皆無不符。未逐代重算每個個體，也沒有新增test或獨立品質種子。</p>
<p>搜尋中CPU核對為0個體、0修正；搜尋後CPU audit區段合計7.360秒，另列於CSV。完整程序包含準備、搜尋、匯出與audit；搜尋wall及完整程序分開呈現。</p>
<h2>Packed-frame：建置未完成</h2><p>AoS／SoA來源整合保留於隔離patch。07:39:34UTC觸及原60分鐘建置上限，0項native、0/150次搜尋；全部150個未執行格保留。沒有速度或品質結論。</p>
<details><summary>來源與統計限制</summary><p>正式來源81b8a9c與封存結果未修改。分組binary ed96ff1c…；captured source、harness、輸出與固定input hashes已重核。所有搜尋在原09:51:28UTC cutoff前完成；硬截止13:51:28UTC不變。</p><p>既有shape／case cache已減少physical work，不能用「分組個體數×logical cases」替代實際節省量。首次手動統計草稿的錯誤假設與修正保留於ANALYSIS_REVISION18.json。沒有重新搜尋或替換樣本。這份舊training附錄不新增ASGP分母、heldout、5pp非劣性或1.3×達標證據。</p></details>
<details><summary>完整30題圖（另有PNG／PDF）</summary><img alt="All30 development speed ratios and physical work savings" src="data:image/png;base64,__PNG__"></details>
<script>const D=__DATA__;
const $=id=>document.getElementById(id);let mode='search';
$('freshgm').textContent=D.freshgm.toFixed(3)+'×';$('offgm').textContent=D.summary.groups_vs_same_binary_off_gm.toFixed(3)+'×';
$('mechanism').textContent='實際physical program-cases減少'+(D.summary.physical_cases_saved_fraction*100).toFixed(2)+'%，同binary搜尋時間的幾何平均比值仍變差。1585/1920個generation啟用分組。';
const labels={baseline:'原基準',selected_control:'選定控制',ordinary:'同binary關閉',current_groups:'同binary分組'};
function draw(){const r=D.tasks.find(x=>x.task===$('task').value);if(!r){$('bars').replaceChildren();$('selected').textContent='';$('detail').textContent='';return;}
 $('selected').textContent=r.task;const key=mode==='search'?'_search_s':'_process_s';const max=Math.max(...Object.keys(labels).map(v=>r[v+key]));$('bars').replaceChildren();
 for(const [v,label]of Object.entries(labels)){let row=document.createElement('div');row.className='barrow';let l=document.createElement('span');l.textContent=label;let wrap=document.createElement('div');wrap.className='barwrap';let bar=document.createElement('div');bar.className='bar';bar.style.width=(100*r[v+key]/max)+'%';wrap.append(bar);let n=document.createElement('span');n.textContent=r[v+key].toFixed(3)+' s';row.append(l,wrap,n);$('bars').append(row);}
 $('detail').textContent='分組啟用 '+r.group_active_generations+'/64代；physical cases減少 '+(100*r.physical_cases_saved_fraction).toFixed(2)+'%。此題四臂AST／fitness／wrong軌跡核對一致。';
 $('search').setAttribute('aria-pressed',String(mode==='search'));$('process').setAttribute('aria-pressed',String(mode==='process'));}
function filter(){const q=$('filter').value.trim().toLowerCase(),old=$('task').value,list=D.tasks.filter(r=>r.task.toLowerCase().includes(q));$('task').replaceChildren();$('rows').replaceChildren();
 for(const r of list){let o=document.createElement('option');o.value=r.task;o.textContent=r.task;$('task').append(o);let tr=document.createElement('tr');for(const value of [r.task,r.fresh_ratio.toFixed(3)+'×',r.groups_vs_same_binary_off.toFixed(3)+'×',(100*r.physical_cases_saved_fraction).toFixed(2)+'%']){let td=document.createElement('td');td.textContent=value;tr.append(td);}$('rows').append(tr);}
 if(list.some(r=>r.task===old))$('task').value=old;$('empty').hidden=list.length>0;draw();}
$('filter').addEventListener('input',filter);$('task').addEventListener('change',draw);$('search').addEventListener('click',()=>{mode='search';draw()});$('process').addEventListener('click',()=>{mode='process';draw()});filter();
</script></html>'''


def run(out):
    out = Path(out)
    if out.exists():
        raise ValueError("Preserve previous supplement; use NEW --out")
    if time.time() >= serial.DEADLINE:
        raise ValueError("Original hard deadline reached")
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        paths = {"groups": serial.BASE / "development-groups17/results.json",
                 "tasks": serial.BASE / "development-groups17/tasks.csv",
                 "audit": serial.BASE / "development-audit18/results.json",
                 "packed": serial.BASE / "development-packed15/results.json"}
        groups, audit, packed = (serial.read(paths[k]) for k in ("groups", "audit", "packed"))
        assert groups["qualified_cells"] == 30 and groups["successful_searches"] == 120 and not groups["issues"]
        summary, rows = audit["summary"], audit["tasks"]
        assert not summary["issues"] and summary["all_generation_scalar_trajectories_equal"]
        ratios = {r["task"]: float(r["current_groups"]) for r in csv.DictReader(paths["tasks"].open())}
        for row in rows:
            row["fresh_ratio"] = ratios[row["task"]]
        out.mkdir()
        import matplotlib
        matplotlib.use("Agg")
        import matplotlib.pyplot as plt
        with plt.rc_context({"font.size": 9, "axes.spines.top": False, "axes.spines.right": False}):
            fig, (ax, work) = plt.subplots(1, 2, figsize=(9, 10.5), sharey=True,
                                           gridspec_kw={"width_ratios": [1.8, 1]})
            y = list(range(30))
            ax.scatter([r["fresh_ratio"] for r in rows], y, facecolors="none", edgecolors=".45", label="Fresh selected control / groups", s=25)
            ax.scatter([r["groups_vs_same_binary_off"] for r in rows], y, color="black", marker="x", label="Same binary off / groups", s=24)
            ax.axvline(1, color=".5", linewidth=.8)
            ax.set_yticks(y, [r["task"] for r in rows], fontsize=8)
            ax.invert_yaxis(); ax.set_xlabel("Search-wall ratio (>1 = grouping faster)")
            ax.legend(loc="lower center", bbox_to_anchor=(.5, 1.01), fontsize=7, frameon=False)
            work.barh(y, [100*r["physical_cases_saved_fraction"] for r in rows], color=".25", height=.45)
            work.set_xlabel("Physical program-cases\nsaved (%)")
            fig.suptitle("Exact current-generation grouping | OLD TRAINING | P1024 / G64 / seed0 / R1", fontsize=10)
            fig.text(.04, .012, "All30 pairs qualified; 120/120 searches. No heldout / new quality seeds. No confidence interval from one run.\n"
                     "Ratios use this round's unprofiled controls. Physical counters already include shape/case caches.", fontsize=8)
            fig.tight_layout(rect=(0, .045, 1, .96))
            fig.savefig(out / "development.png", dpi=170)
            fig.savefig(out / "development.pdf")
            plt.close(fig)
        data = dict(summary=summary, tasks=rows, freshgm=groups["incremental_means_over_fresh_selected_control"]["current_groups"])
        serial.save(out / "data.json", data)
        with (out / "tasks.csv").open("x", newline="") as stream:
            writer = csv.DictWriter(stream, fieldnames=list(rows[0]), lineterminator="\n")
            writer.writeheader(); writer.writerows(rows)
        html = HTML.replace("__DATA__", json.dumps(data, ensure_ascii=False).replace("<", "\\u003c"))
        html = html.replace("__PNG__", base64.b64encode((out / "development.png").read_bytes()).decode())
        (out / "dashboard.html").write_text(html)
        totals = summary["totals_by_arm"]
        report = f'''# G64追加開發結果

**同代精確分組未增加整體速度，未採用。Packed-frame建置逾時，尚無性能結論。**

原正式結果仍為1.079257×，1.3×目標未達。本附錄使用舊training，P1024／G64／seed0／每格一次，沒有替換正式資料或加入新heldout。

| 對照 | 全30題搜尋wall幾何平均倍率 |
|---|---:|
| 本輪新測選定控制 ÷ 同代分組 | {data['freshgm']:.6f}× |
| 同一binary關閉分組 ÷ 開啟分組 | {summary['groups_vs_same_binary_off_gm']:.6f}× |
| 本輪新測beb5585d原基準 ÷ 同代分組 | {groups['means_over_fresh_beb_baseline']['current_groups']:.6f}× |

倍率大於1表示分組較快。這是一次舊training計時，沒有將小幅差異當成穩定收益，也沒有使用歷史分母。額外2%門檻未過，seeds1–2及65冠軍CPU確認未執行。

## 做了什麼

- Packed-frame在隔離來源整合40／48-byte AoS／SoA儲存、host配置、lane位址、VM initial／replay；沒有降低精度或容量。觸及既定60分鐘建置上限，0項native、0/150搜尋；150個未執行格保留於[CSV](../development-packed15/runs.csv)。來源／schema失敗13、14及逾時15均保留。
- 同代分組在GPU建hash表，逐欄比對完整genotype／fuel／origins／Value位元。各組直接指向代表，代表本次評分後只複製fitness／wrong／case counters；不重用payload handle，不改RNG、算子、案例或資源限制。已有跨代parent-cache繼續運作。
- 17的隔離Release建置通過；六項native實際6/6通過（458.60秒）。四臂120/120搜尋成功，30/30題獨立核對完整配對。功能沒有合併到正式CPP。

## 為何沒有收益

實際physical program-cases由{summary['physical_cases_ordinary']:,}降至{summary['physical_cases_groups']:,}，只減少{100*summary['physical_cases_saved_fraction']:.3f}%。1585/1920個generation出現新增分組，共跳過13,083筆非既有parent-cache的個體評分。

同binary的30題內部分項加總：reproduction由{totals['ordinary']['repro_s']:.3f}秒增加至{totals['current_groups']['repro_s']:.3f}秒；evaluation由{totals['ordinary']['eval_s']:.3f}秒降至{totals['current_groups']['eval_s']:.3f}秒。新增分組／傳回工作列在原搜尋wall內；分項不是額外的headline或獨立kernel profile。資料支持目前工作節省太少、未抵銷新增開銷；沒有新增nsys分析細分每個kernel的因果成本。

## 正確性與品質證據

- 四臂完整最終1024個AST、最終fitness／wrong描述、64筆冠軍AST／fitness一致。逐代best、best_wrong、best_nodes、mean、errors、timeouts及logical cases也一致。
- 搜尋後native CPU每次抽查16個最終個體，共1920筆fitness／wrong觀測，0不符；CPU audit區段合計{summary['post_search_cpu_audit_section_s']:.6f}秒，另列。搜尋中CPU verification為0個體、0修正。
- 未逐代重算每個個體，未新增逐代冠軍native CPU judges、heldout或獨立品質種子。未證明5pp非劣性或普遍等價；沒有新的ASGP倍率。沒有從初代／後續解出率推導新結論，因未執行本附錄的新train/test冠軍判題。
- 固定input、captured harness、官方451檔／隔離來源、binary角色及原始輸出hash均重核。搜尋皆於原09:51:28UTC cutoff前完成，硬截止13:51:28UTC不變。抽樣VRAM／完整程序／audit成本保留於[明細](tasks.csv)；整卡取樣包含桌面且可能漏掉短峰。

首次手動分析錯把「分組個體數×logical cases」當成physical節省量，忽略既有shape／case cache。錯誤草稿保留，修正後使用實際counter差值與保守上界；原始資料、搜尋、倍率及資格沒有更動。見[修正紀錄](../ANALYSIS_REVISION18.json)。新增5項分析guard實際通過；這不等於新候選通過完整136項原生回歸。

[互動頁](dashboard.html) · [PNG](development.png) · [PDF](development.pdf) · [逐題CSV](tasks.csv) · [搜尋資格](../development-groups17/report.md) · [完整audit](../development-audit18/results.json)
'''
        (out / "report.md").write_text(report)
        serial.save(out / "completion.json", dict(scope="Development-only appendix; formal0 immutable and1.3target unmet", inputs_sha256={str(p): serial.sha(p) for p in paths.values()},
                    source_sha256=serial.sha(Path(__file__)), artifacts_sha256={p.name: serial.sha(p) for p in sorted(out.iterdir()) if p.is_file()}, finished_epoch=time.time()))
        return dict(out=str(out), fresh_control_ratio=data["freshgm"], same_binary_off_ratio=summary["groups_vs_same_binary_off_gm"], physical_work_saved=summary["physical_cases_saved_fraction"])


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--out", type=Path, required=True)
    print(json.dumps(run(parser.parse_args().out), indent=2))
