"""Post-timing report for the frozen G8/R2 scaling matrix. Never launches searches.

A new output directory is mandatory. Run only after all timed controllers and
children exit; preserve a failed report attempt and use a new output on correction.
"""
from argparse import ArgumentParser
from collections import Counter
from datetime import datetime, timezone
import csv
import fcntl
import json
import math
import os
from pathlib import Path
import time
import traceback
from champion_judges import expected_tags
from development import alive, gpu_lock
from report_audit import FileHashes, audit_matrix
from report_quality import audit_judges, quality_counts
from report_stats import VARIANTS, population_summary, summarize_cell
import serial

LABELS = dict(asgp='ASGP 1T', gpu_original='Original GPU', gpu_improved='Tuple GPU')
COLORS = dict(asgp='#88958e', gpu_original='#ce995f', gpu_improved='#237c60')
DASHBOARD_MAX_CHARS = 480_000  # Leave room for preview-only checks below T3's512000 limit.


def utc(epoch):
    return datetime.fromtimestamp(epoch, timezone.utc).strftime('%Y-%m-%d %H:%M:%S UTC')


def fmt(value, digits=2):
    return f'{value:.{digits}f}' if isinstance(value, (int,float)) and math.isfinite(value) else '—'


def title(task):
    return task.replace('_', ' ').title()


def script_json(data):
    # Do not let data close the self-contained HTML script element.
    return json.dumps(data, ensure_ascii=False, allow_nan=False, separators=(',', ':')).replace('<', '\\u003c').replace('\u2028','\\u2028').replace('\u2029','\\u2029')


def dashboard_data(summary):
    """Keep all displayed numbers/statuses; full provenance stays in JSON/CSV."""
    data = {key:summary[key] for key in
            ('protocol', 'totals', 'cells', 'populations', 'quality', 'cost_note')}
    fields = ('tag', 'task', 'population', 'variant', 'repetition', 'status', 'original_status')
    data['runs'] = [{key:row.get(key) for key in fields} for row in summary['runs']]
    return data


def write_csv(path, rows, columns=()):
    columns = list(columns)+sorted({key for row in rows for key in row}-set(columns))
    with path.open('x', newline='') as stream:
        writer = csv.DictWriter(stream, fieldnames=columns)
        writer.writeheader()
        for row in rows:
            writer.writerow({k:json.dumps(v,ensure_ascii=False,sort_keys=True) if isinstance(v,(list,dict)) else v
                             for k,v in row.items()})


def assert_idle(parent, judges):
    for path in serial.RAW.glob('*controller.json'):
        item = serial.read(path)
        assert not alive(item['pid']) or item['pid'] == os.getpid(), 'controller still alive: '+str(path)
    for item in [parent, judges] if judges else [parent]:
        assert not (item.get('controller_pid') and alive(item['controller_pid'])), 'controller still alive'
        for job in item['jobs'].values():
            assert not (job.get('pid') and alive(job['pid'])), 'child still alive: '+str(job['pid'])


def serial_intervals(*manifests):
    intervals = []
    for manifest in manifests:
        if manifest:
            for tag, job in manifest['jobs'].items():
                if job.get('pid'):
                    intervals.append(dict(tag=tag, stage=manifest.get('stage'), start=job['started_epoch'],
                                          end=job['started_epoch']+job['process_s'],
                                          status=job['status'],
                                          end_scope='last heartbeat lower bound' if job['status']=='running' else 'recorded process exit'))
    intervals.sort(key=lambda x:x['start'])
    for before, after in zip(intervals,intervals[1:]):
        assert after['start'] >= before['end']-.001, 'recorded search/judge intervals overlap'
    return intervals


def totals_for(records, judge_records, cells, planned):
    return dict(requested_runs=planned,
        completed_runs=sum(r['original_status'] == 'ok' for r in records),
        audited_work_runs=sum(r['status'] == 'ok' and r.get('audit_ok') is True for r in records),
        analysis_status_counts=dict(Counter(r['status'] for r in records)),
        original_status_counts=dict(Counter(r['original_status'] for r in records)),
        native_fidelity_mismatch_runs=sum(r.get('fidelity_ok') is False for r in records),
        qualified_cells=sum(c['qualified'] for c in cells), requested_cells=len(cells),
        requested_champion_judges=planned,
        completed_champion_judges=sum(r['status'] == 'ok' and r.get('audit_ok') is True for r in judge_records),
        champion_judge_status_counts=dict(Counter(r['status'] for r in judge_records)),
        cpu_raw_fitness_program_observations=sum(r.get('cpu_audit_program_count') or 0 for r in records if r['status'] == 'ok'),
        cpu_raw_wrong_program_observations=sum(r.get('cpu_audit_wrong_program_count') or 0 for r in records if r['status'] == 'ok'),
        cpu_raw_fitness_mismatches=sum(r.get('cpu_audit_fitness_mismatches') or 0 for r in records if r['status'] == 'ok'),
        cpu_raw_wrong_mismatches=sum(r.get('cpu_audit_wrong_mismatches') or 0 for r in records if r['status'] == 'ok'),
        formal_search_s=sum(r.get('search_s') or 0 for r in records if r['status'] == 'ok'),
        formal_cpu_raw_audit_s=sum(r.get('cpu_audit_s') or 0 for r in records if r['status'] == 'ok'),
        cpu_training_guard_programs=sum(r.get('cpu_training_guard_programs') or 0 for r in records if r['status'] == 'ok'),
        cpu_training_guard_s=sum(r.get('cpu_training_guard_s') or 0 for r in records if r['status'] == 'ok'),
        cpu_training_fitness_corrections=sum(r.get('cpu_training_fitness_corrections') or 0 for r in records if r['status'] == 'ok'),
        cpu_training_wrong_corrections=sum(r.get('cpu_training_wrong_corrections') or 0 for r in records if r['status'] == 'ok'),
        formal_process_s=sum(r.get('process_s') or 0 for r in records),
        process_time_lower_bound_only=any(r['original_status']=='running' for r in records),
        champion_judge_process_s=sum(r.get('process_s') or 0 for r in judge_records),
        sampled_card_peak_bytes=max((r.get('sampled_card_peak_bytes') or 0 for r in records),default=0))


def diagnosis_evidence(hashes):
    folder = serial.RAW/'operator-diagnosis-attempt2'
    path = folder/'manifest.json'
    hashes.verify(path, serial.sha(path))
    manifest = serial.read(path)
    hashes.verify(folder/'operator', manifest['binary_sha256'])
    for field in ('source_sha256','linked_input_sha256'):
        for name, digest in manifest[field].items():
            hashes.verify(serial.ROOT/name if not Path(name).is_absolute() else name, digest)
    hashes.verify(serial.BASE/'operator-plan.json',manifest['plan_sha256'])
    records, statuses = [], Counter()
    for task, job in manifest['jobs'].items():
        if job['status'] != 'ok':
            records.append(dict(task=task,status=job['status'],scope='phase root only'))
            continue
        hashes.verify(job['output'],job['output_sha256'])
        for name, digest in job['input_sha256'].items():
            hashes.verify(name,digest)
        data = serial.read(job['output'])
        assert data['format'] == 'fixed-parent-root-splice-diagnosis-v1'
        for item in data['rows']:
            assert item['task'] == task
            statuses[item['status']] += 1
            if item['status'] == 'ok':
                assert item['child_equal'] and item['actual_child_sha256'] == item['expected_child_sha256']
                assert item['gpu_status'] == 0 and item['negative_guards'] == 2
                assert item['annotation_base'] == item['annotation_donor'] == 1
            records.append({k:v for k,v in item.items() if k != 'source_child'})
    return dict(task_process_statuses=dict(Counter(j['status'] for j in manifest['jobs'].values())),
                explicit_request_statuses=dict(statuses), requested_tasks=30,
                scope='first eight unchanged parents/four pairs; explicit phase-root crossover only; no internal-site/mutation/RNG equivalence'), records


def generation_rows(records):
    result = []
    for job in records:
        if job['status'] != 'ok':
            continue
        raw = serial.read(job['output'])
        for row in raw['generations']:
            item = dict(tag=job['tag'],task=job['task'],population=job['population'],
                        variant=job['variant'],repetition=job['repetition'])
            item.update({k:v for k,v in row.items() if not isinstance(v,(list,dict))})
            item.update({'verification_'+k:v for k,v in row.get('verification',{}).items()})
            result.append(item)
    return result


def make_figures(summary, out):
    # Standard plotting backend and standalone PNG/PDF; no browser dependencies.
    import matplotlib
    matplotlib.use('Agg')
    import matplotlib.pyplot as plt
    from matplotlib.lines import Line2D
    tasks = summary['tasks']
    fig, axes = plt.subplots(1,2,figsize=(15,12),sharey=True,layout='constrained')
    for ax, population in zip(axes,summary['protocol']['populations']):
        by_task = {c['task']:c for c in summary['cells'] if c['population'] == population}
        for i, task in enumerate(tasks):
            cell = by_task[task]
            if not cell['qualified']:
                ax.text(1,i,'incomplete / failed audit',fontsize=7,color='#9c3a22',ha='center')
                continue
            for field, offset, color in [('asgp_vs_original',-.12,COLORS['gpu_original']),('asgp_vs_improved',.12,COLORS['gpu_improved'])]:
                center = cell[field]
                low, high = cell[field+'_paired_min'],cell[field+'_paired_max']
                if low is not None and high is not None:
                    ax.plot([low,high],[i+offset,i+offset],color=color,linewidth=1.5)
                ax.scatter(center,i+offset,s=24,color=color,zorder=3)
        pop = next(p for p in summary['populations'] if p['population'] == population)
        ax.set_title(f'P = {population:,} | {pop["qualified_cells"]}/30 qualified cells')
        ax.axvline(1,color='#777777',linewidth=1,linestyle='--')
        ax.set_xscale('log');ax.set_xlabel('ASGP 1T search time / GPU search time (x)')
        ax.grid(axis='x',alpha=.2);ax.set_yticks(range(len(tasks)),[title(t) for t in tasks],fontsize=9)
    axes[0].invert_yaxis()
    fig.suptitle(f'Fixed work: G={summary["protocol"]["generations"]}, C=1024, seed 0, two cold-process repeats\nDots: ratio of median seconds; lines: paired-repeat range, not a confidence interval',fontsize=13)
    fig.legend(handles=[Line2D([],[],marker='o',linestyle='-',color=COLORS[v],label=LABELS[v])
                        for v in ('gpu_original','gpu_improved')],loc='lower center',bbox_to_anchor=(.5,-.025),ncol=2)
    for extension in ('png','pdf'):
        fig.savefig(out/('speedup.'+extension),dpi=170,bbox_inches='tight')
    plt.close(fig)
    fig, axes = plt.subplots(1,2,figsize=(13,6),layout='constrained')
    cost_rows = []
    for i, population in enumerate(summary['protocol']['populations']):
        for j, variant in enumerate(VARIANTS):
            jobs = [r for r in summary['runs'] if r['population'] == population and r['variant'] == variant and r['status'] == 'ok']
            search = sum(r['search_s'] for r in jobs)/3600
            cpu = sum(r['cpu_audit_s'] for r in jobs)/3600
            other = sum(max(0,r['process_s']-r['search_s']-r['cpu_audit_s']) for r in jobs)/3600
            index = i*4+j
            axes[0].bar(index,search,color=COLORS[variant])
            axes[0].bar(index,cpu,bottom=search,color='#a9c5d3')
            axes[0].bar(index,other,bottom=search+cpu,color='#ddd7cc')
            cost_rows.append(dict(population=population,variant=variant,completed_runs=len(jobs),
                                  search_hours=search,cpu_raw_audit_hours=cpu,other_process_hours=other))
    axes[0].set_xticks([0,1,2,4,5,6],[f'P{p}\n{LABELS[v]}' for p in summary['protocol']['populations'] for v in VARIANTS],rotation=25,ha='right',fontsize=8)
    axes[0].set_ylabel('Observed sum of completed process time (hours)')
    axes[0].set_title('Search, extra CPU raw-score audit, other process work')
    axes[0].legend(handles=[Line2D([],[],color='#777777',linewidth=8,label='Search'),Line2D([],[],color='#a9c5d3',linewidth=8,label='CPU audit'),Line2D([],[],color='#ddd7cc',linewidth=8,label='Other/export')],fontsize=8)
    for i, row in enumerate(summary['quality_counts']):
        axes[1].bar(i,row['initial_success'],color='#88958e',label='Gen0 solved' if i == 0 else None)
        # Show final success with a separate adjacent marker: initial successes
        # can be lost, so stacking all initial+new would overstate final success.
        axes[1].scatter(i,row['final_success'],marker='D',color='#237c60',s=40,label='Final solved' if i == 0 else None)
        axes[1].text(i,max(row['initial_success'],row['final_success'])+.5,
                     f'{row["audited_tasks"]}/30 audited',rotation=65,fontsize=7,ha='left')
    axes[1].set_xticks(range(6),[f'P{r["population"]}\n{LABELS[r["variant"]]}' for r in summary['quality_counts']],rotation=25,ha='right',fontsize=8)
    axes[1].set_ylim(0,35);axes[1].set_ylabel('Tasks with zero wrong cases (seed 0 only)')
    axes[1].set_title('Fixed training CPU audit; heldout not measured')
    axes[1].legend(fontsize=8)
    for extension in ('png','pdf'):
        fig.savefig(out/('cost-quality.'+extension),dpi=170)
    plt.close(fig)
    return cost_rows


def chinese_report(summary):
    t, protocol = summary['totals'], summary['protocol']
    outcome = '全部正式搜尋與核對完成' if summary['science_complete'] else '存在未完成、失敗或未通過核對的項目'
    lines = ['# 大 population GPU 加速結果', '',
        f'**{outcome}。P=1,024／16,384，G={protocol["generations"]}，每個體 1,024 案例；30 題、3 方法、同 seed0 的 2 次冷程序量測，共 360 筆。**', '',
        f'程序原始成功 {t["completed_runs"]}/360；獨立工作量／來源核對成功 {t["audited_work_runs"]}/360；完整合格速度格 {t["qualified_cells"]}/60；final 與逐代 champion CPU 判題 {t["completed_champion_judges"]}/360。', '',
        '| Population | 合格題數 | ASGP／原 GPU 幾何平均 | ASGP／優化 GPU 幾何平均 | 原 GPU／優化 GPU 幾何平均 |',
        '|---:|---:|---:|---:|---:|']
    for p in summary['populations']:
        lines.append(f'| {p["population"]:,} | {p["qualified_cells"]}/30 | {fmt(p["asgp_vs_original_geomean"])}× | {fmt(p["asgp_vs_improved_geomean"])}× | {fmt(p["original_vs_improved_geomean"])}× |')
    lines += ['', '先看這張表的 ASGP／優化 GPU：10× 表示完成這批固定代數工作所需的搜尋時間約為 ASGP 的十分之一。原 GPU／優化 GPU 則回答這條 tuple 執行路徑本身帶來多少收益；小於 1× 的負收益保留。跨題平均只使用合格題，分母始終明列；失敗與缺失均保留在逐 run CSV。', '',
        'P 是每代個體數，G 是演化代數。ASGP 初代從同一凍結來源族群依序取前 P 個體；GPU 使用對應的 prepared 初代，原 GPU 與優化 GPU 共用同一檔案。seed0 指演化亂數；兩次冷程序量測沿用相同初代與案例，不重新抽個體，也不把失敗個體換掉。因此兩次只用來看計時波動，不能當成兩個獨立的品質 seeds；固定輸入也不代表 ASGP／GAGP 後續算子分布相同。', '',
        '## 每一題的秒數與加速比', '',
        '秒數取兩次冷程序的中位數；倍率＝表中的 ASGP 秒數÷GPU 秒數。逐次配對最小／最大倍率在 cells.csv，僅描述計時波動。兩次使用同一 seed，不能當作兩個獨立品質樣本，也不提供多 seed 成功率信賴區間。', '']
    for population in protocol['populations']:
        lines += [f'### P={population:,}，G={protocol["generations"]}', '',
            '| 題目 | ASGP 秒 | 原 GPU 秒 | 優化 GPU 秒 | ASGP／優化 | 原／優化 | 配對／狀態 |',
            '|---|---:|---:|---:|---:|---:|---|']
        for c in summary['cells']:
            if c['population'] == population:
                status = '核對通過' if c['qualified'] else '未合格，詳見CSV原因'
                lines.append(f'| {title(c["task"])} | {fmt(c["asgp_s"])} | {fmt(c["gpu_original_s"])} | {fmt(c["gpu_improved_s"])} | {fmt(c["asgp_vs_improved"])}× | {fmt(c["original_vs_improved"])}× | {c["valid_pairs"]}/2；{status} |')
        lines.append('')
    lines += ['## CPU 核對與額外成本', '', summary['cost_note'], '',
        f'原始 native CPU fitness 核對 {t["cpu_raw_fitness_program_observations"]:,} 次個體觀測、wrong 核對 {t["cpu_raw_wrong_program_observations"]:,} 次；fitness 差異 {t["cpu_raw_fitness_mismatches"]}、wrong 差異 {t["cpu_raw_wrong_mismatches"]}。這些次數包含兩GPU／不同population／重複工作，並非互不重複的新程式。程序失敗或核對無效的資料不當作已驗證觀測。', '',
        f'搜尋計時內的 GPU 方法 CPU training guard：{t["cpu_training_guard_programs"]:,} 次個體、{t["cpu_training_guard_s"]:.3f} 秒，fitness 修正 {t["cpu_training_fitness_corrections"]} 次、wrong 修正 {t["cpu_training_wrong_corrections"]} 次。這個正式 execution-only arm 關閉 training guard，核對其設定、總量及各代紀錄皆為0；搜尋後的離線 CPU 核對另列，不注入、改分或重新挑冠軍。', '',
        '第一個 repetition 的兩個 GPU 方法都核對全部最終個體，並比較完整最終 AST；第二次核對前 16 個體及 final descriptors／每代 champion AST、fitness。這不證明所有世代的全族群一致或任意輸入的語意等價。GPU 使用評分及繁殖，無 CPU 繁殖替代；完整性判定另核對 unscored=0、各代案例量及實際 reproduction profile。', '',
        '## 初代成功與演化後才成功', '',
        '以下只數每題 seed0 的第一個 repetition，資料是本輪原有固定 1,024 個訓練案例；沒有新增獨立測試集。CPU 用共同輸出投影的 exact／capped-L1 重新判題，與搜尋內部 raw ranking fitness 分開。ASGP 冠軍用 source CPU evaluator；GPU 冠軍用 native CPU interpreter。', '',
        '| P | 方法 | 完整判題題數 | 初代全對 | 最終全對 | 初代未全對、最終才全對 | 初代全對但最終失去 |',
        '|---:|---|---:|---:|---:|---:|---:|']
    for r in summary['quality_counts']:
        lines.append(f'| {r["population"]:,} | {LABELS[r["variant"]]} | {r["audited_tasks"]}/30 | {r["initial_success"]} | {r["final_success"]} | {r["final_success_after_gen0"]} | {r["initial_success_lost"]} |')
    op = summary['operator_diagnosis']
    lines += ['', '初代是第一次繁殖前的 champion；history 的代數是 0–7，最終輸出是第 8 次繁殖之後的評分，因此首次成功也可能記為 generation8。全部逐代 wrong／共同 loss 在 champions.csv。不能把搜尋倍率解讀為找到正解所需時間的倍率。', '',
        '## 本輪做了哪些優化', '',
        '正式優化組採用 tuple execution-only 路徑，保留 512 評分分組，沒有調換算子或增加 CPU training guard。這條既有路徑在本輪被擴展到 P16384 的容量／全族群核對及全30題同輪控制；不把它描述為本輪新發明。', '',
        '本輪實作並測試 compact-root 小陣列 VM 原型；20 組完整程式／分數配對通過，但平均只快約 0.9%，未達事先訂定的 3% 門檻，已封存並移除。較小分組整體較慢；1024 分組平均約快 6.4%，卻有一組完整程式不同，亦淘汰。保留所有失敗／負結果，沒有放寬門檻或重挑正式資料。', '',
        f'固定父代診斷：{op["requested_tasks"]} 題的程序狀態 {json.dumps(op["task_process_statuses"],ensure_ascii=False)}；明列 phase-root 替換狀態 {json.dumps(op["explicit_request_statuses"],ensure_ascii=False)}。只覆蓋前8個未改父代／4組配對的 phase root；不涵蓋內部位置、mutation generator 或隨機取樣分布。五個動態規劃題的無法映射紀錄保留，不能據此認定與既知四題 HeadIndex site-count 差異原因相同。', '',
        '## 設定、可比範圍與時間預算', '',
        f'固定 binary SHA256：`{summary["binary_sha256"]}`。CPU1T、GPU evaluation＋GPU reproduction、16GiB workspace、physical AST2048／depth24／phase1024、C1024、G8、seed0、兩次冷程序。ASGP 使用其 source 限制；原生編譯與搜尋算子仍有差異，不宣稱搜尋空間或完整分布等價，也不是 ASGP 論文重現。', '',
        f'全部 ASGP 分母來自本輪新測速。正式前按固定時間規則選 G8／2次，預估完整程序及全族群 CPU 核對 {summary["forecast_s"]/3600:.3f} 小時、另留 4 小時；歷史數據只用於這個預算，未混入正式倍率。', '',
        f'固定窗口 {protocol["start_utc"]} 至 {protocol["deadline_utc"]}。本報告資料生成於 {summary["generated_utc"]}；起點至此 {summary["elapsed_h"]:.3f} 小時，報告生成是否在原截止前：{summary["generated_within_deadline"]}。所有重工作串行使用共用鎖，完成時間另記 completion.json；預覽／最後檢查尚須實際完成後記錄。', '',
        f'正式整卡約200ms NVML取樣峰值 {t["sampled_card_peak_bytes"]/1024**3:.3f} GiB，包含桌面、可能漏短尖峰；這不是單一程序專用顯存峰值。16GiB 指 workspace，另有 genotype／code／其他GPU資料。', '',
        '來源、binary、外部 ASGP source directory、linked inputs、frozen harness、每題 source／prepared／grammar、實際命令與輸出 hashes 在 audit.json。每個 P／方法／repetition 使用同題相同案例；本速度矩陣只有一個固定訓練 split，沒有可聲稱 input-disjoint 的新 test split。具體失敗與核對問題均保留，requested_phase_complete 不能代替成功。', '',
        '## 檔案', '',
        '- [互動頁](dashboard.html)：選 P／題目並查看秒數；獨立 test 切換會明示未量測。',
        '- [逐格表](cells.csv)、[全部搜尋與失敗](runs.csv)、[CPU 判題狀態](judges.csv)。',
        '- [逐代搜尋指標](generations.csv)、[逐代與最終 CPU 冠軍判題](champions.csv)、[算子明列請求](operators.csv)。',
        '- [搜尋加速 PNG](speedup.png)／[PDF](speedup.pdf)、[成本與初代／最終結果 PNG](cost-quality.png)／[PDF](cost-quality.pdf)。',
        '- [完整核對](audit.json)、[數值摘要](summary.json)、[產物 hashes 與生成完成時間](completion.json)。', '']
    return '\n'.join(lines)


def main():
    parser = ArgumentParser(description=__doc__)
    parser.add_argument('--matrix',type=Path,default=serial.RAW/'formal-attempt1')
    parser.add_argument('--freeze',type=Path,default=serial.RAW/'formal-freeze-attempt1.json')
    parser.add_argument('--judges',type=Path,default=serial.RAW/'champion-judges-attempt0')
    parser.add_argument('--out',type=Path,required=True)
    parser.add_argument('--allow-partial',action='store_true')
    args = parser.parse_args()
    guard = (serial.RAW/'report-controller.lock').open('a')
    fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
    parent = serial.read(args.matrix/'manifest.json')
    judges_path = args.judges/'manifest.json'
    judges = serial.read(judges_path) if judges_path.exists() else None
    assert_idle(parent,judges)
    args.out.mkdir(parents=False,exist_ok=False)
    started = time.time()
    try:
        with gpu_lock():
            assert_idle(parent,judges)
            frozen = serial.read(args.freeze)
            tags = {row[0] for row in expected_tags(frozen)}
            if not args.allow_partial:
                assert set(parent['jobs']) == tags, 'all requested statuses required; use explicit --allow-partial'
                assert judges is not None and set(judges['jobs']) == tags, 'all CPU judge statuses required'
            hashes = FileHashes()
            hashes.verify(args.freeze,serial.sha(args.freeze))
            parent_sha = hashes.verify(args.matrix/'manifest.json',serial.sha(args.matrix/'manifest.json'))
            audited = audit_matrix(frozen,parent,args.matrix,args.freeze,hashes=hashes)
            judged = audit_judges(frozen,parent,args.matrix,args.judges,parent_sha,hashes=hashes)
            cells = [summarize_cell(audited['records'],audited['execution_comparisons'],task,population,frozen['repetitions'])
                     for population in frozen['populations'] for task in frozen['tasks']]
            populations = [population_summary(cells,p) for p in frozen['populations']]
            counts = [quality_counts(judged['records'],p,v) for p in frozen['populations'] for v in VARIANTS]
            totals = totals_for(audited['records'],judged['records'],cells,frozen['planned_runs'])
            op, operator_rows = diagnosis_evidence(hashes)
            intervals = serial_intervals(parent,judges)
            generated = time.time()
            complete = (totals['audited_work_runs'] == frozen['planned_runs'] and totals['qualified_cells'] == len(cells)
                        and totals['completed_champion_judges'] == frozen['planned_runs'])
            cost_note = (f'正式成功搜尋累計 {totals["formal_search_s"]/3600:.3f} 小時；搜尋計時之外的原始 fitness／wrong CPU 核對 '
                         f'{totals["formal_cpu_raw_audit_s"]/3600:.3f} 小時。所有正式程序 wall 累計 {totals["formal_process_s"]/3600:.3f} 小時'
                         f'（含額外核對／匯出／失敗嘗試）；final＋逐代冠軍 CPU 判題程序另 {totals["champion_judge_process_s"]/3600:.3f} 小時。'
                         '這些累加成本不能全部套用搜尋加速比；也不等於整個24小時窗口，還有開發、hash、診斷與報告時間。'
                         + (' 有未寫入退出時間的中斷程序，其 process wall 只記最後 heartbeat 下界。' if totals['process_time_lower_bound_only'] else ''))
            protocol = {k:frozen[k] for k in ('populations','generations','repetitions','cases','seed')}
            protocol.update(start_utc=utc(serial.START),deadline_utc=utc(serial.DEADLINE))
            summary = dict(format='gagp-scaling-report-v1',protocol=protocol,tasks=frozen['tasks'],
                totals=totals,cells=cells,populations=populations,runs=audited['records'],
                quality=judged['quality'],quality_counts=counts,operator_diagnosis=op,cost_note=cost_note,
                binary_sha256=frozen['binary_sha256'],freeze_sha256=parent['freeze_sha256'],
                generated_utc=utc(generated),elapsed_h=(generated-serial.START)/3600,
                generated_within_deadline=generated <= serial.DEADLINE,science_complete=complete,
                forecast_s=frozen['forecast_s'],historical_denominators_used=False,
                requested_scope_complete=complete,
                quality_scope='seed0/fixed training only; timing repetitions are not independent quality seeds',
                analysis_issues=audited['issues']+judged['issues'])
            for name, rows, columns in [('cells.csv',cells,('task','population','qualified')),
                ('runs.csv',audited['records'],('tag','task','population','variant','repetition','status','original_status')),
                ('judges.csv',judged['records'],('tag','task','population','variant','repetition','status','original_status')),
                ('champions.csv',judged['champions'],('tag','generation','stage','wrong')),
                ('generations.csv',generation_rows(audited['records']),('tag','generation')),
                ('operators.csv',operator_rows,('task','status'))]:
                write_csv(args.out/name,rows,columns)
            cost_rows = make_figures(summary,args.out)
            write_csv(args.out/'costs.csv',cost_rows,('population','variant','completed_runs'))
            (args.out/'REPORT.zh-TW.md').write_text(chinese_report(summary))
            template = serial.BASE/'dashboard.template.html'
            hashes.verify(template,serial.sha(template))
            html = template.read_text()
            assert html.count('__DATA_JSON__') == 1
            dashboard = html.replace('__DATA_JSON__',script_json(dashboard_data(summary)))
            assert len(dashboard) <= DASHBOARD_MAX_CHARS, 'dashboard exceeds preview/render budget'
            (args.out/'dashboard.html').write_text(dashboard)
            serial.save(args.out/'summary.json',summary)
            hashes.verify_stable()
            serial.save(args.out/'audit.json',dict(format='gagp-scaling-report-audit-v1',
                matrix=str(args.matrix.resolve()),freeze=str(args.freeze.resolve()),judges=str(args.judges.resolve()),
                file_sha256={name:record['sha256'] for name,record in hashes.records.items()},
                execution_comparisons=audited['execution_comparisons'],issues=summary['analysis_issues'],
                recorded_nonoverlapping_intervals=intervals,cases_sha256=judged['cases_sha256'],
                initial_source_relation=audited['initial_source_relation'],dataset=audited['dataset'],
                cpu_metric_note='raw search fitness/wrong audit distinct from common projected CPU champion exact/capped-L1',
                report_harness_sha256={str(serial.BASE/name):serial.sha(serial.BASE/name) for name in
                    ('report.py','report_quality.py','report_audit.py','report_stats.py','champion_audit.py','champion_judges.py','dashboard.template.html','dashboard_checks.js')}))
            artifact_hashes = {p.name:serial.sha(p) for p in args.out.iterdir() if p.is_file()}
            finished = time.time()
            serial.save(args.out/'completion.json',dict(status='report_generated',science_complete=complete,
                report_started_epoch=started,report_finished_epoch=finished,
                original_start_epoch=serial.START,original_deadline_epoch=serial.DEADLINE,
                elapsed_h=(finished-serial.START)/3600,within_original_deadline=finished <= serial.DEADLINE,
                plotting_and_html_visual_inspection='pending actual inspection; do not infer from generation',
                artifact_sha256=artifact_hashes))
    except Exception as error:
        serial.save(args.out/'report-error.json',dict(error=repr(error),traceback=traceback.format_exc(),
            failed_epoch=time.time(),attempt_preserved=True))
        raise
    print(args.out.resolve(),flush=True)


if __name__ == '__main__':
    main()
