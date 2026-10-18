"""Chinese narrative and compact interactive view of the audited fixed matrix."""
from collections import defaultdict
from pathlib import Path
import json
import statistics

import study
from report import LABELS,COLORS


def number(value,digits=2):return '—' if value is None else f'{value:.{digits}f}'


def percentile(values,fraction):
    values=sorted(values);position=(len(values)-1)*fraction;index=int(position);part=position-index
    return values[index]*(1-part)+values[min(index+1,len(values)-1)]*part


def write(out,freeze,coverage,rows,curves,details,tests,diagnostics):
    operator='gpu_operator' if 'gpu_operator'in freeze['variants'] else 'gpu_exec'
    totals={v:dict(audited=sum(r['audited'] for r in rows if r['variant']==v),
        success=sum(bool(r['success']) for r in rows if r['variant']==v),
        initial=sum(bool(r['initial_success']) for r in rows if r['variant']==v),
        new=sum(bool(r['new_final_success']) for r in rows if r['variant']==v),
        lost=sum(bool(r['lost_initial_success']) for r in rows if r['variant']==v)) for v in freeze['variants']}
    training_readouts=[]
    for phase in ('development-training-judges','site-training-judges'):
        group=[r for r in diagnostics['development_training'] if r['phase']==phase and r.get('variant')!='asgp']
        variants=list(dict.fromkeys(r.get('variant','unknown_failure') for r in group))
        values=[]
        for variant in variants:
            selected=[r for r in group if r.get('variant','unknown_failure')==variant]
            perfect=sum(r['status']=='ok' and r.get('training_wrong')==0 for r in selected)
            values.append(f'{variant}={perfect}/{len(selected)}')
        training_readouts.append(('原算子factorial' if phase=='development-training-judges' else '新增site政策')+'：'+'、'.join(values)+'。')
    training_readout=' '.join(training_readouts)
    count=len(freeze['tasks'])*len(freeze['seeds']);table=['|題目|'+'|'.join(LABELS[v] for v in freeze['variants'])+'|','|---|'+'|'.join('---:' for v in freeze['variants'])+'|']
    for task in freeze['tasks']:
        values=[next(r for r in details if r['task']==task and r['variant']==v) for v in freeze['variants']]
        table.append('|'+task+'|'+'|'.join(f"{r['successes']}/{r['planned']} ({r['audited']} audited)" for r in values)+'|')
    initial=diagnostics['initial'];checkpoints=[r for r in diagnostics['checkpoints'] if r['status']=='ok']
    evaluated=sum(r['population'] for r in checkpoints)
    fidelity=sum(r['cpu_gpu_fitness_mismatches'] for r in checkpoints);wrong=sum(r['cpu_gpu_wrong_mismatches'] for r in checkpoints)
    source_rows=diagnostics.get('evolved_source_comparison',[])
    source_fitness=sum(r.get('source_native_cpu_fitness_mismatches',0) for r in source_rows)
    source_wrong=sum(r.get('source_native_cpu_wrong_mismatches',0) for r in source_rows)
    source_admitted=sum(r.get('admitted',0) for r in source_rows)
    source_successful=sum(r['status']=='ok' for r in source_rows)
    identity=[r for r in diagnostics.get('execution_identity',[]) if r['status']=='ok']
    identity_asts=sum(r['final_champion_ast_equal'] and r['all_observed_champion_asts_equal'] for r in identity)
    guard_identity=[r for r in diagnostics.get('verification_identity',[]) if r['status']=='ok']
    guard_no_correction=[r for r in guard_identity if not r['reference_fitness_corrections'] and not r['reference_wrong_corrections']]
    guard_unchanged=sum(r['final_champion_ast_equal'] and r['all_observed_champion_asts_equal'] and r['all_observed_champion_fitness_equal'] and r['final_score_wrong_size_arrays_equal'] for r in guard_no_correction)
    native=[r for r in rows if r['variant']!='asgp' and r['search_status']=='ok']
    formal_fidelity=dict(successful_gpu_runs=len(native),program_observations=16*len(native),
        fitness_mismatches=sum(r['cpu_top16_fitness_mismatches'] or 0 for r in native),
        wrong_mismatches=sum(r['cpu_top16_wrong_mismatches'] or 0 for r in native),
        runs_with_mismatches=sum(bool(r['cpu_top16_fitness_mismatches'] or r['cpu_top16_wrong_mismatches']) for r in native))
    guards=[r for r in rows if r['variant']=='gpu_guard' and r['search_status']=='ok']
    checked=sum(r['verification_programs'] for r in guards);cases=sum(r['verification_cases'] for r in guards)
    corrections=sum(r['verification_fitness_corrections'] for r in guards);wrong_corrections=sum(r['verification_wrong_corrections'] for r in guards)
    guard_ms=sum(r['verification_ms'] for r in guards);guard_search_ms=1000*sum(r['search_s'] for r in guards)
    status='全部指定搜尋與train/test稽核完成' if coverage['all_complete'] else '部分完成；失敗／缺漏明列'
    aggregate='\n'.join(f"- {LABELS[v]}：final聯合成功{totals[v]['success']}/{count}；gen0 champion已成功{totals[v]['initial']}，後續final新增{totals[v]['new']}，初代成功但final失去{totals[v]['lost']}；完整判題{totals[v]['audited']}/{count}。" for v in freeze['variants'])
    if operator!='gpu_exec':
        op={r['task']:r for r in details if r['variant']==operator}
        ex={r['task']:r for r in details if r['variant']=='gpu_exec'}
        wins=sum(op[t]['successes']>ex[t]['successes'] for t in freeze['tasks'])
        losses=sum(op[t]['successes']<ex[t]['successes'] for t in freeze['tasks'])
        primary=[r for r in tests if r['family']=='operator']
        supported=sum(r['holm_p_across9tasks']<.05 and r['a_only']>r['b_only'] for r in primary)
        regressions=sum(r['holm_p_across9tasks']<.05 and r['a_only']<r['b_only'] for r in primary)
        quality_readout=f"相對execution-only，新算子final聯合成功由{totals['gpu_exec']['success']}/{count}變為{totals[operator]['success']}/{count}；以計畫seed的成功數作描述性逐題比較，{wins}題增加、{losses}題減少，其餘相同。主要9題配對成功檢定經Holm後，{supported}題支持提升、{regressions}題支持退步（p<0.05）；完整paired樣本數及區間見CSV。沒有顯著差異不代表等價。"
    else:
        quality_readout='全局training規則保留baseline；本輪沒有將新算子政策帶入正式矩陣。這是負面採用結果，沒有挑選單題最佳政策補成整體品質提升。'
    quality_readout+=f" 所選GPU政策與ASGP的final聯合成功分別為{totals[operator]['success']}/{count}與{totals['asgp']['success']}/{count}；此跨題加總僅描述本輪cohort，品質差距仍須逐題與不確定性解讀。"
    speeds=[]
    for v in freeze['variants']:
        if v=='asgp':continue
        selected=[r for r in tests if r['a']==v and r['b']=='asgp']
        speeds.append('|'+LABELS[v]+'|'+'|'.join(number(next(r for r in selected if r['task']==t)['median_speedup'])+'×' for t in freeze['tasks'])+'|')
    report=f'''# 品質診斷、可控CPU校驗與長代GPU改善

本輪狀態：**{status}**。原24小時時間窗為2026-10-18 01:17:45 UTC至2026-10-19 01:17:45 UTC（台灣10/19 09:17:45），沒有重設。正式搜尋{coverage['search_status']}；完整雙split CPU判題{coverage['fully_audited']}/{coverage['planned_searches']}。所有失敗／budget_not_run均保留，沒有替換初代或補跑成功列。

## 第1項：品質差距的來源

初代全30題、30,720個相同候選，ASGP CPU/native CPU/resident GPU在指定訓練案例的fitness與wrong均0差異。CPU/GPU抽樣偏好差異0；source/native因logical與physical節點破同分不同，491,520對中1011對（0.206%）偏好不同。這不能把先前品質差距直接歸因為GPU評分錯誤。

中後代新增checkpoint metrics：成功快照{len(checkpoints)}個，合計{evaluated}個候選觀測；CPU/GPU 40-bit canonical ranking fitness差異{fidelity}個、wrong差異{wrong}個，raw fitness最大絕對差{number(max([r['raw_max_abs_error'] for r in checkpoints] or [0]),8)}。ASGP演化後、成功轉譯的同候選source/native CPU比較（{source_successful}/27快照、{source_admitted}個候選觀測）fitness差異{source_fitness}、wrong差異{source_wrong}；詳見evolved_source_comparison.csv。source未被native准入的個體另外記錄，不能把接受子集當完整population。相同個體跨checkpoint可重複，數字不是獨立樣本數或任意程式正確性證明。

原4種算子factorial（phase均勻抽樣／source crossover gate）及新增site-depth／parsimony四組，都使用舊training的5配對seeds。每代變異嘗試、changed/unchanged、contract/budget/generation rejection及節點大小保存在development_variation.csv；final CPU訓練判題保存在development_training.csv。first32訓練案例的CPU輸出探針量測行為多樣性，包含IntList實際內容及錯誤，但不是完整語意等價分類。

舊training的final訓練全對數：{training_readout} ASGP控制只有seed0共9組，不與45組直接作配對成功率比較。這些開發數字只用於診斷與全局選擇，正式泛化結果另以新資料／新seeds判定。

ASGP source phase depth7與native physical depth24不是相同限制。新增可選grammar-invocation depth7，保留physical AST2048/depth24/phase1024，並可使用grammar-site數量破同分。全30題初代及G1+final admission90/90通過；其中4題（Index of Element、Fuel Cost、Luhn、Last Index of Zero）因HeadIndex把固定InputList子節點lower成bound(lo)，共有1274個node-count／61個depth差異。故新政策仍不等於ASGP完整搜尋空間、節點成本或RNG。

全局選擇為**{freeze['operator_selection']}**；選擇只看舊training CPU判題：更多training-perfect champion，其次較低task/seed內common loss rank，同分偏好較少控制；改善題數須不低於惡化題數。所有控制組保留。候選與正式規模在新heldout pool建立前凍結，沒有test調參。正式驗證比較所選組合政策，各元件的因果歸屬仍限於開發factorial，未在新heldout重做全部元件組合。

## 第2項：可量測且可設定的CPU訓練校驗

公共EvolutionConfig新增top-K、interval、near-zero policy，預設關閉；明確要求resident GPU evaluation及GPU reproduction。每次只重評選定候選的當前training，用原fuel、metric、allocation policy與CPU discard overlay，修正ranking用fitness及wrong，再排序；繁殖仍在GPU。raw GPU/CPU最大差異與40-bit ranking修正分開記錄。K=0維持關閉；partial top-K不保證未檢查的新leader或所有population正確。

舊training seed0/G64固定cost screen為none、top1/i8、top8/i8、top32/i1（near-zero=0）；完整負面／成本結果在guard_development.csv與verification-cost圖。正式guard預先指定top8/i8，並在final或sum error<=0觸發，套用所選全局算子。正式已執行CPU複核{checked}個program觀測／{cases}個program-case；fitness ranking修正{corrections}、wrong修正{wrong_corrections}，raw最大絕對差{number(max([r['verification_max_abs_error'] for r in guards] or [0]),8)}。CPU複核共{guard_ms/1000:.3f}s，占guard搜尋總wall {100*guard_ms/guard_search_ms if guard_search_ms else 0:.2f}%；逐代數據在verification.csv。

正式guard對所選政策共有{len(guard_identity)}對可比搜尋；其中{len(guard_no_correction)}對未發生ranking或wrong修正，{guard_unchanged}對的全部已匯出champion AST／fitness及最終score/wrong/size arrays也相同。逐對見verification_identity.csv；這仍不是完整population AST證明。

計數可能包含同一候選在多代重複檢查；修正數為0只代表受檢候選，不能證明近似從不影響搜尋。若無實際修正或heldout提升，這項成果應定位為控制／診斷機制，不能宣稱已證明品質改善。

## 第3項：長代數GPU瓶頸改善

nsys顯示舊TupleSummary路徑decline private offsets、typed/window與workspace reuse。新增預設關閉的私有內容評分路徑，保留signed64整數、primary correctness及原auxiliary capped error；zero logical cap需由每次當前bytecode保守證明concat不超過3cells，不能證明則整體decline，未偷偷加入較小logical cap。GPU overflow replay及原fuel／容量仍生效。

舊training seed0、未profile的G64成對開發測試：Is Sorted88.716→9.128s（9.72×），All Equal89.122→9.006892s（9.90×）。兩題完整最終1024 AST JSON、fitness/wrong、全代champion fitness相同，最終全部1024個候選CPU fitness audit0差異；不表示每個中間population或任意輸入語意皆相同。全30題同候選tuple-on初代稽核亦通過。

新資料正式execution-only對baseline共有{len(identity)}對可比搜尋，其中{identity_asts}對的全部已匯出generation champion AST及final champion AST相同；逐對fitness及最終score/wrong/size arrays比對見execution_identity.csv。正式只匯出champion AST，不能由descriptor相同推斷所有中間／最終population AST都相同。

nsys詳細kernel與cudaMalloc呼叫數在profiles.csv；API區間包含等待，不能直接與kernel區間相加或全部歸因為配置成本。新舊profile只作診斷，不混入正式未profile速度。auto與forced1的30筆tiling screen均走同一路徑，未宣稱額外改善。

## 新資料／新種子正式結果

P1024、共同G{freeze['generations']}，{len(freeze['seeds'])}組新配對seeds {freeze['seeds'][0]}–{freeze['seeds'][-1]}；固定選出的9題，不代表全30題品質重現。CPU1T，GPU evaluation和reproduction，16GiB workspace與physical2048/24/phase1024固定，所有方法使用同一新pinned binary。ASGP仍是本地固定comparison adapter，未在本項完成stock論文程式完整重現。

每題fresh dataseed20261018，training最多200 unique、test最多1000 unique；各seed及各方法使用相同task split。test排除新training及舊quality train/test與full-case speed fixture全部已用inputs，實際數量、hash與排除紀錄在dataset_splits.csv。域與oracle沿用本地ASGP資料產生器；input-disjoint不等於分布外泛化。

final成功要求CPU training及test皆wrong=0。所有代champion按training選定，test只在搜尋後稽核；不挑test最好的一代。gen0指已選中的初代champion，不代表窮舉初代是否存在任何其他解。

{aggregate}

{quality_readout}

以上跨題加總僅描述本輪cohort，沒有把task-seed總數視為相同數量的獨立題目。

{chr(10).join(table)}

正式成功GPU搜尋{formal_fidelity['successful_gpu_runs']}次，每次final前16候選另作CPU fitness及wrong稽核，共{formal_fidelity['program_observations']}個候選觀測；canonical ranking fitness差異{formal_fidelity['fitness_mismatches']}、wrong差異{formal_fidelity['wrong_mismatches']}，涉及{formal_fidelity['runs_with_mismatches']}次搜尋。這與程序完成／champion品質判題分開，也不是全population檢查。

逐題固定工作量加速比中位數（同seed ASGP搜尋wall / GPU搜尋wall，含GPU內CPUguard成本）：

|方法|{'|'.join(freeze['tasks'])}|
|---|{'|'.join('---:' for t in freeze['tasks'])}|
{chr(10).join(speeds)}

此倍率是共同P/G/cases的執行時間，不是找出相同品質解所需時間。process_s另含輸入／匯出／posthoc CPU audits，保存在runs.csv；內嵌CPUguard已包含在search_s，離線train/test判題另外列出。

paired_tests.csv逐題報告配對seed的accuracy差、bootstrap95%區間、固定工作速度中位數區間及成功discordance的exact McNemar。每個方法比較對的9題p值使用Holm修正；只有雙邊完整判題者進入paired推論，缺漏數仍保留。成功/計畫seed與Wilson區間是含執行失敗的operational yield。未顯著差異不能當作品質等價證據；區間也沒有推廣到未抽樣的題目族群。

## 完整性、測試與限制

coverage.json明列計畫與實際搜尋／雙split判題狀態；integrity.json核對pinned binary、harness、runtime source、外部ASGP46來源、grammar、初代／split及所有成功搜尋與judge輸出雜湊。所有正式timed jobs區間串行，且未超出原deadline；原始失敗與未執行列留在manifest。completion.json記錄報告產生時間，最終UI檢查另列。

Release全build通過。完整native第一次130/131（251.68s），唯一失敗是附加實驗說明觸發frozen spec hash gate；只移除該附加說明後，gate＋adapter2/2重跑通過（49.56s），tools122（4skip）、repository24通過。最後raw-score／behavior allocation scope變更以pipeline＋adapter2/2通過（49.32s）。未聲稱單次fresh131全通過。新增fixture／runner及統計檢查與最終repository檢查紀錄分別保存。

GPU記憶體為device0全卡NVML約200ms取樣，包含桌面且可能漏短尖峰，不是精確單程序VRAM峰值。正式效能範圍為P1024／本機單GPU，不能把改善倍率推廣到其它population或硬體。品質只有9選定題與本輪有限seeds；不能宣稱原論文重現、ASGP搜尋等價、所有population語意parity或GPU近似已完全受控。本輪新增的site政策是明確演算法變更，必須與execution-only改善分開解讀。

## 檔案

- [逐run結果](runs.csv)、[逐題摘要](task_summary.csv)、[成對倍率](paired_runs.csv)、[配對統計](paired_tests.csv)
- [逐代CPU判題曲線](curves.csv)、[CPU校驗](verification.csv)、[校驗軌跡比對](verification_identity.csv)、[資料切分](dataset_splits.csv)
- [初代評分](initial.csv)、[中後代metrics](checkpoints.csv)、[source同候選比較](evolved_source_comparison.csv)、[算子training](development_training.csv)、[execution AST核對](execution_identity.csv)
- [速度／品質PNG](speed-quality.png)／[PDF](speed-quality.pdf)、[行為多樣性PNG](diagnostic-diversity.png)／[PDF](diagnostic-diversity.pdf)、[CPU校驗成本PNG](verification-cost.png)／[PDF](verification-cost.pdf)
- [互動檢視](dashboard.html)、[完整性](integrity.json)、[覆蓋](coverage.json)、[摘要](summary.json)
'''
    (out/'REPORT.zh-TW.md').write_text(report)
    findings=f'''# 第1至第3項結果

{status}：{coverage['fully_audited']}/{coverage['planned_searches']}個搜尋完成train/test CPU判題；9題、P1024、G{freeze['generations']}、{len(freeze['seeds'])}組新配對seeds。

{aggregate}

{quality_readout}

完成同候選／算子診斷、預設關閉且明列成本的CPU訓練校驗，以及TupleSummary GPU執行優化。新算子選擇為{freeze['operator_selection']}，只依舊training選出。詳細結果及每題差異見[完整報告](REPORT.zh-TW.md)。

限制：品質範圍9選定題；fixed-work speed不等於time-to-solution；部分CPU校驗不保證全population；native grammar-site政策不普遍等於ASGP source邏輯成本；未完成stock ASGP論文完整重現。
'''
    (out/'FINDINGS.zh-TW.md').write_text(findings)
    grouped=defaultdict(list)
    for row in curves:grouped[row['task'],row['variant'],row['generation']].append(row)
    curves_compact=defaultdict(lambda:defaultdict(list))
    for (task,variant,generation),group in sorted(grouped.items()):
        a=[r['train_accuracy'] for r in group];b=[r['test_accuracy'] for r in group]
        curves_compact[task][variant].append([generation,statistics.median(a),statistics.median(b),
            percentile(a,.25),percentile(a,.75),percentile(b,.25),percentile(b,.75),statistics.median(r['elapsed_search_s'] for r in group)])
    payload=dict(tasks=freeze['tasks'],variants=list(freeze['variants']),labels=LABELS,colors=COLORS,
        seeds=len(freeze['seeds']),generations=freeze['generations'],coverage=coverage,details=details,tests=tests,
        curves=curves_compact,operator=freeze['operator_selection'],totals=totals,quality_readout=quality_readout,fidelity=formal_fidelity)
    template=(study.BASE/'dashboard.template.html').read_text()
    (out/'dashboard.html').write_text(template.replace('__DATA__',json.dumps(payload,separators=(',',':')).replace('</','<\\/')))
