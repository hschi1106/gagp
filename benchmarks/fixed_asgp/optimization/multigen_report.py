"""Summarize completed multi-generation runs and render standalone figures."""
import argparse
import csv
import json
from pathlib import Path
import statistics
import numpy as np
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D

ROOT=Path(__file__).resolve().parents[3]
TASKS=('sum_of_elements','house_robber','median')
NAMES=dict(zip(TASKS,('Sum of Elements','House Robber','Median')))
MODES=('asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap')
LABELS=dict(zip(MODES,('ASGP 1T','GAGP CPU','GPU-e','GPU-er','GPU-er-o')))
COLORS=dict(zip(MODES,plt.get_cmap('tab10').colors[:5]))

def median(x):return statistics.median(x)
def table(head,rows):return '\n'.join(['| '+' | '.join(head)+' |','| '+' | '.join(['---']*len(head))+' |']+['| '+' | '.join(map(str,row))+' |' for row in rows])+'\n'
def write_csv(path,rows):
    fields=list(dict.fromkeys(k for r in rows for k in r))
    with path.open('w') as f:
        w=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');w.writeheader();w.writerows(rows)
def incumbent(records):
    best=None;result=[]
    for r in records:
        if best is None or r['native_fitness']>best['native_fitness']:best=r
        result.append(-best['comparison_fitness']/1024)
    return np.array(result)

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);p.add_argument('--report',type=Path,required=True);args=p.parse_args()
    out=args.out.resolve();dest=args.report.resolve();dest.mkdir(parents=True,exist_ok=True)
    m=json.loads((out/'manifest.json').read_text());aud=json.loads((out/'native-asgp-audit/summary.json').read_text())
    cells={c['tag']:c for c in m['cells'] if c['status']=='ok' and not c['tag'].startswith('smoke.')}
    reports={tag:json.loads(Path(c['output']).read_text()) for tag,c in cells.items()}
    groups={}
    for t in TASKS:
        for row in aud[t]['champions']:groups.setdefault(row['tag'],[]).append(row)
    for rows in groups.values():rows.sort(key=lambda r:r['generation'])
    raw=[];runs=[];prefix=[]
    for tag,c in cells.items():
        r=reports[tag];g=r['generations'];wall=json.loads(Path(c['output']+'.wall.json').read_text())['input_through_report_ms']
        best=max(groups[tag],key=lambda x:x['native_fitness'])
        held=next(x for x in aud[c['task']]['heldout'] if x['tag']==tag)
        run=dict(tag=tag,stage=tag.split('.')[0],task=c['task'],mode=c['mode'],seed=c['seed'],generations=len(g),
                 search_ms=r['evolve_wall_ms'],input_through_report_ms=wall,process_ms=c['process_wall_s']*1000,
                 init_ms=r['initial_admission_ms'],final_eval_ms=r['final_eval_ms'],audit_ms=r.get('cpu_audit_ms',0),
                 gen_median_ms=median(x['generation_ms'] for x in g),early4_ms=median(x['generation_ms'] for x in g[:4]),
                 late4_ms=median(x['generation_ms'] for x in g[-4:]),native_best_ever=best['native_fitness'],
                 comparison_training_fitness=best['comparison_fitness'],heldout_fitness=held['comparison_heldout_fitness'],
                 final_nodes_mean=statistics.mean(x['nodes'] for x in r['final_population']),
                 final_unique=r['unique_final_genomes'],
                 final_unique_fitness=len({x['fitness'] for x in r['final_population']}),
                 final_fitness_mean=statistics.mean(x['fitness'] for x in r['final_population']),
                 final_fitness_median=statistics.median(x['fitness'] for x in r['final_population']),
                 reproduction_profile=r['reproduction_profile'],
                 cpu_audit_mismatches=r.get('cpu_audit_mismatches'),
                 genotype_device_bytes=r.get('genotype_device_bytes'),
                 gpu_fallback_pairs=sum(x.get('fallbacks',0) for x in g) if c['mode'].startswith('gpu') else None,
                 gpu_error_pairs=sum(x.get('errors',0) for x in g) if c['mode'].startswith('gpu') else None,
                 gpu_timeout_pairs=sum(x.get('timeouts',0) for x in g) if c['mode'].startswith('gpu') else None,
                 gpu_unscored_pairs=sum(x.get('unscored',0) for x in g) if c['mode'].startswith('gpu') else None)
        resource=Path(c['output']).with_suffix('.resources').read_text().splitlines()
        run['peak_rss_kib']=next(int(v.split('=')[1]) for v in resource if v.startswith('peak_rss_kib='))
        run['late_over_early']=run['late4_ms']/run['early4_ms'];runs.append(run)
        for i,row in enumerate(g):
            a=groups[tag][i];entry=dict(stage=run['stage'],task=c['task'],mode=c['mode'],seed=c['seed'],**row,
                comparison_champion_fitness=a['comparison_fitness'],common_champion_nodes=a['nodes'],common_gpu_match=a['cpu_gpu_match'])
            if not row.get('case_counters_available'):
                for key in ('cases','errors','timeouts','fallbacks','unscored'):entry[key]=None
            raw.append(entry)
        if run['stage']=='extended':
            original=reports[f'main.{c["task"]}.{c["mode"]}.s{c["seed"]}']['generations']
            diffs=[i for i,(a,b) in enumerate(zip(original,g)) if any(a[k]!=b[k] for k in ('best','mean','best_nodes'))]
            prefix.append(dict(tag=tag,different_primary_generations=diffs))
    write_csv(dest/'runs.csv',runs);write_csv(dest/'generations.csv',raw)
    primary=[r for r in runs if r['stage']=='main'];assert len(primary)==45
    summary=[];wall_quality=[];quality_main=[]
    for t in TASKS:
        for mode in MODES:
            rs=[r for r in primary if r['task']==t and r['mode']==mode];assert len(rs)==3
            baseline=median(r['search_ms'] for r in primary if r['task']==t and r['mode']=='asgp_1t')
            s=dict(task=t,mode=mode,search_median_ms=median(r['search_ms'] for r in rs),
                search_range_ms=[min(r['search_ms'] for r in rs),max(r['search_ms'] for r in rs)],
                process_median_ms=median(r['process_ms'] for r in rs),input_report_median_ms=median(r['input_through_report_ms'] for r in rs),
                gen_median_ms=median(r['gen_median_ms'] for r in rs),early4_ms=median(r['early4_ms'] for r in rs),
                late4_ms=median(r['late4_ms'] for r in rs),late_over_early=median(r['late_over_early'] for r in rs),
                final_nodes_mean=median(r['final_nodes_mean'] for r in rs),final_unique=median(r['final_unique'] for r in rs))
            s['search_speedup_asgp']=baseline/s['search_median_ms']
            s['process_speedup_asgp']=median(r['process_ms'] for r in primary if r['task']==t and r['mode']=='asgp_1t')/s['process_median_ms']
            summary.append(s)
            for seed in range(3):
                key=f'extended.{t}.{mode}.s{seed}'
                if key not in groups:key=f'main.{t}.{mode}.s{seed}'
                rows=groups[key];covered=rows[-1]['elapsed_search_ms']>=10000
                eligible=[r for r in rows if r['elapsed_search_ms']<=10000]
                best=max(eligible,key=lambda r:r['native_fitness']) if eligible else None
                held10=next((r for r in aud[t]['heldout'] if r['tag']==key and r.get('heldout_selection')=='10s'),None)
                wall_quality.append(dict(task=t,mode=mode,seed=seed,tag=key,covered_10s=covered,
                    available_before_10s=len(eligible),selected_generation=best['generation'] if best else None,
                    native_fitness=best['native_fitness'] if best else None,
                    comparison_fitness=best['comparison_fitness'] if best else None,
                    comparison_heldout_fitness=held10['comparison_heldout_fitness'] if held10 else None))
                r=next(r for r in rs if r['seed']==seed);quality_main.append({k:r[k] for k in ('task','mode','seed','native_best_ever','comparison_training_fitness','heldout_fitness')})
    write_csv(dest/'summary.csv',summary);write_csv(dest/'quality_10s.csv',wall_quality);write_csv(dest/'quality_16gen.csv',quality_main)
    plt.rcParams.update({'font.size':10,'axes.grid':True,'grid.alpha':.25})
    def save(fig,name):
        fig.tight_layout();fig.savefig(dest/f'{name}.png',dpi=170);fig.savefig(dest/f'{name}.svg');plt.close(fig)
        svg=dest/f'{name}.svg';svg.write_text('\n'.join(line.rstrip() for line in svg.read_text().splitlines())+'\n')
    # Aligned metric, each system native execution; translations are diagnostics only.
    fig,axes=plt.subplots(1,3,figsize=(16,4.6))
    for ax,t in zip(axes,TASKS):
        for mode in MODES:
            values=np.array([[-r['comparison_fitness']/1024 for r in groups[f'main.{t}.{mode}.s{s}']] for s in range(3)])
            x=np.arange(values.shape[1]);ax.plot(x,np.median(values,axis=0),label=LABELS[mode],color=COLORS[mode],linestyle={'gagp_cpu':'--','gpu_eval':':','gpu_repro':'--'}.get(mode,'-'));ax.fill_between(x,values.min(0),values.max(0),color=COLORS[mode],alpha=.10)
        ax.set(title=NAMES[t],xlabel='Generation (16 = final evaluation)',ylabel='Wrong-case fraction (lower is better)' if t=='median' else 'Native capped L1 / case (lower is better)');ax.set_ylim(bottom=0);ax.ticklabel_format(axis='y',style='plain',useOffset=False)
        if t=='sum_of_elements':ax.set_ylim(0,1)
    axes[0].legend(fontsize=8);save(fig,'quality_by_generation')
    fig,axes=plt.subplots(1,3,figsize=(16,4.6))
    for ax,t in zip(axes,TASKS):
        for mode in MODES:
            xs=[];ys=[]
            for seed in range(3):
                tag=f'extended.{t}.{mode}.s{seed}'
                if tag not in groups:tag=f'main.{t}.{mode}.s{seed}'
                rows=groups[tag];xs.append(np.array([r['elapsed_search_ms']/1000 for r in rows]));ys.append(incumbent(rows))
            lo=max(x[0] for x in xs);hi=min(10,min(x[-1] for x in xs))
            if lo<=hi:
                grid=np.unique(np.concatenate(([lo,hi],*[x[(x>=lo)&(x<=hi)] for x in xs])))
                values=np.array([y[np.searchsorted(x,grid,side='right')-1] for x,y in zip(xs,ys)])
                ax.step(grid,np.median(values,0),where='post',label=LABELS[mode],color=COLORS[mode]);ax.fill_between(grid,values.min(0),values.max(0),step='post',color=COLORS[mode],alpha=.1)
        ax.set(title=NAMES[t],xlabel='Completed search time including cold start (s)',ylabel='Wrong-case fraction of incumbent' if t=='median' else 'Capped L1 / case of incumbent');ax.set_ylim(bottom=0);ax.ticklabel_format(axis='y',style='plain',useOffset=False)
        if t=='sum_of_elements':ax.set_ylim(0,1)
        ax.set_xlim(0,10)
    axes[0].legend(handles=[Line2D([0],[0],color=COLORS[x],label=LABELS[x]) for x in MODES],fontsize=8);save(fig,'quality_by_time')
    fig,axes=plt.subplots(1,3,figsize=(16,4.6))
    for ax,t in zip(axes,TASKS):
        for mode in MODES:
            v=np.array([[r['generation_ms'] for r in reports[f'main.{t}.{mode}.s{s}']['generations']] for s in range(3)])
            ax.plot(np.arange(16),np.median(v,0),color=COLORS[mode],label=LABELS[mode]);ax.fill_between(np.arange(16),v.min(0),v.max(0),color=COLORS[mode],alpha=.1)
        ax.set(title=NAMES[t],xlabel='Generation',ylabel='Complete generation (ms)');ax.set_yscale('log')
    axes[0].legend(fontsize=8);save(fig,'generation_time')
    fig,axes=plt.subplots(1,3,figsize=(16,4.6))
    for ax,t in zip(axes,TASKS):
        vals=[next(s for s in summary if s['task']==t and s['mode']==mode) for mode in MODES]
        x=np.arange(5);ax.bar(x-.18,[r['search_median_ms']/1000 for r in vals],.36,label='Search incl. cold + final eval');ax.bar(x+.18,[r['process_median_ms']/1000 for r in vals],.36,label='Process incl. import/audit/report');ax.set_xticks(x,[LABELS[m] for m in MODES],rotation=25);ax.set(title=NAMES[t],ylabel='16-generation wall time (s)');ax.set_yscale('log')
    axes[0].legend(fontsize=8);save(fig,'end_to_end')
    fig,axes=plt.subplots(3,2,figsize=(13,12))
    for i,t in enumerate(TASKS):
        for col,modes in enumerate((('asgp_1t',),MODES[1:])):
            ax=axes[i,col]
            for mode in modes:
                for field,style in [('best','-'),('mean','--')]:
                    v=np.array([[-r[field]/1024 for r in reports[f'main.{t}.{mode}.s{s}']['generations']] for s in range(3)])
                    ax.plot(range(16),np.median(v,0),style,color=COLORS[mode],label=LABELS[mode]+' '+field)
            ax.set(title=NAMES[t]+(' ASGP native objective' if col==0 else 'GAGP native objective'),xlabel='Generation',ylabel='Wrong-case fraction' if t=='median' else 'Native capped L1 / case');ax.set_yscale('symlog',linthresh=.01 if t=='median' else 1);ax.legend(fontsize=7)
    save(fig,'native_fitness')
    fig,axes=plt.subplots(1,3,figsize=(16,4.6))
    for ax,t in zip(axes,TASKS):
        for mode in MODES:
            for seed in range(3):
                tag=f'extended.{t}.{mode}.s{seed}'
                if tag not in reports:continue
                g=reports[tag]['generations'];ax.plot([r['generation'] for r in g],[r['generation_ms'] for r in g],color=COLORS[mode],alpha=.6,label=LABELS[mode] if seed==0 else None)
        ax.set(title=NAMES[t],xlabel='Generation (extension lengths differ)',ylabel='Complete generation (ms)');ax.set_yscale('log')
    axes[0].legend(handles=[Line2D([0],[0],color=COLORS[x],label=LABELS[x]) for x in MODES if any(c['mode']==x and tag.startswith('extended.') for tag,c in cells.items())],fontsize=8);save(fig,'extended_generation_time')
    parity=[]
    for t in TASKS:
        for seed in range(3):
            for a,b in [('gagp_cpu','gpu_eval'),('gpu_repro','gpu_overlap')]:
                ga=reports[f'main.{t}.{a}.s{seed}']['generations'];gb=reports[f'main.{t}.{b}.s{seed}']['generations']
                parity.append(dict(task=t,seed=seed,modes=[a,b],different_best_generations=sum(x['best']!=y['best'] for x,y in zip(ga,gb)),different_mean_generations=sum(x['mean']!=y['mean'] for x,y in zip(ga,gb))))
    summary_json=dict(source_manifest=str(out/'manifest.json'),binary_sha256=m['binary_sha256'],primary_runs=len(primary),
         summary=summary,quality_10s=wall_quality,quality_16gen=quality_main,prefix_checks=prefix,mode_trajectory_checks=parity,
         audit_mismatches={t:sum(not r['cpu_gpu_match'] for r in aud[t]['champions']) for t in TASKS},
         native_common_mismatches={t:sum(r.get('native_common_match') is False for r in aud[t]['champions']) for t in TASKS},
         source_manifests=m.get('source_manifests',[]),
         incomplete_cells=[c for c in m['cells'] if c['status']!='ok'],
         note='3 seeds; shaded areas show min/max, not confidence intervals. Frozen initial population is identical across seeds; only reproduction RNG varies. Common scoring uses native-selected champions, not rescore-and-reselect of every population.')
    (dest/'summary.json').write_text(json.dumps(summary_json,indent=2)+'\n')
    parts=['# 多世代演化比較：CPU 1T＋GPU\n',
      '[全部五模式的六張比較圖（英文）](all-mode-comparison/README.md)\n' if (dest/'all-mode-comparison/README.md').exists() else '',
      '**先看 [ASGP vs GAGP 總體比較](OVERVIEW.zh-TW.md)**：固定同一 GAGP 模式，分開呈現速度、同代數品質與同時間品質。\n' if (dest/'OVERVIEW.zh-TW.md').exists() else '',
      '實驗日期：2026-10-13。主矩陣為 P=1024、完整 1024 cases、三題、五模式、seeds 0/1/2、16 次完整 generation 加 final evaluation，比較矩陣共 45 次搜尋。另保留先執行的15次未對齊 Median 比較（12次GAGP L1、3次ASGP exact）作對照；延長組30次，總共90次搜尋。以下是本輪重新量測，沒有沿用 frozen single-generation 分母。\n',
      '## 實驗設計與解讀界線\n',
      '- 三題沿用相同 frozen 初始 population 與 cases；seed 改變 reproduction RNG，不改變初始族群。資料是 ASGP 生成資料，不是官方 PSB dataset；Median 是三個整數中位數。\n- ASGP 保留既定 elite1、crossover0.7、mutation0.3、phase depth7。GAGP 保留其 grammar budgets、crossover→mutation0.3、subtree probability1；GPU-er/o 啟用最佳新 profile。所有模式均 CPU1T，無 early-stop 或漏評。ASGP 保留 error→wrong-case count→nodes 的同分排序；GAGP CPU/e 保留 program-key tie-break，新 GPU profile 在 tournament 同分時保留先抽到者。對齊 scalar fitness 不等於把這些 selection 規則一併改成 ASGP。\n- 五模式 fitness 目標已對齊：Sum／House 為每 case 上限1000的 absolute error；Median 為錯誤 case 數。Median 的 GAGP search penalty 設1：在 admitted Int 輸出／Int 目標條件下，min(abs(error),1) 等價 exact mismatch；不宣稱這個技巧適用浮點輸出。ASGP 原生分數越低越好，紀錄統一取負值、越高越好。正式品質使用各系統原生執行的分數，ASGP held-out 也用 ASGP 重評。GAGP champions 用獨立 CPU／GPU 完整重評。ASGP→GAGP 翻譯後重評另列語意診斷，不能代替 ASGP 原生品質。\n- 主矩陣固定16代；時間延長組只按實測速度選代數，供十秒品質與後期速度觀察。未覆蓋時間不外推。重評、匯出與報告另外計時；主搜尋每個 program/case pair 均實際評估。\n- 搜尋 wall 包含冷啟動與 final evaluation；process wall 再包含輸入／admission、audit、輸出。每代 trace 寫出成本含於 search wall、在單代計時之外。ASGP 每代最佳程式編碼位於其 generation 計時內；GAGP 每代 history-best materialization 亦在其 generation 內，但事後 JSON export 在搜尋外。\n- 圖中線為3 seeds 中位數、陰影為 min/max，不是信賴區間。初始 population 只有一份，結論不可外推為隨機初始化族群的收斂保證。\n',
      '## 1. 固定 generation 的品質演變\n',
      '![同一評分公式下各系統的原生 champion](quality_by_generation.png)\n',
      '橫軸16為 final evaluation；縱軸 Sum／House 是各系統原生 capped L1 除以1024；Median 是錯誤 case 比例，均越低越好。每代 native champion 可能退步：GAGP 沒有 ASGP 的 elite1 保證。Sum 初代已有正確解，這題主要觀察保留解與成本，不能證明新方法更會找解。\n',
      '![各系統原始 best／mean](native_fitness.png)\n',
      '實線為當代 best、虛線為 population mean。Median 左右均為錯誤 case 比例；Sum／House 均為 capped L1 / case。\n',
      '16代內（含 final）依原 native objective 選出的 best-so-far 程式，各自原生 training／held-out fitness 如下；均越接近0越好。同分取最早完成的 champion，held-out 不參與選擇。\n']
    qrows=[]
    for t in TASKS:
        for mode in MODES:
            q=[r for r in quality_main if r['task']==t and r['mode']==mode]
            qrows.append([NAMES[t],LABELS[mode],'/'.join(str(int(r['comparison_training_fitness'])) for r in q),'/'.join(str(int(r['heldout_fitness'])) for r in q)])
    parts +=[table(['題目','模式','Training seeds0/1/2','Held-out seeds0/1/2'],qrows),
      '\nHeld-out 沿用已封存的獨立1024 cases，排除與 training 完全相同輸入；與原 task 範圍相同，不是域外泛化證明。ASGP held-out 直接由 ASGP 評分。翻譯／admission 與 GAGP 重評結果保留為獨立診斷。\n',
      '## 2. 相同十秒搜尋預算\n','![包含 cold start 的品質時間曲線](quality_by_time.png)\n',
      '縱軸為依已對齊的 task objective 保留的 best-so-far champion，以其原生執行語意評分；GAGP 另經獨立 CPU 重評。曲線只畫三個 seeds 都已有完成紀錄的時間範圍，不把未完成 generation 算進成果。\n']
    parts.append(table(['題目','模式','十秒training fitness：seeds0/1/2','十秒held-out fitness','完整覆蓋10秒'],[[NAMES[t],LABELS[mode],'/'.join(str(int(r['comparison_fitness'])) if r['comparison_fitness'] is not None else '無完成代' for r in wall_quality if r['task']==t and r['mode']==mode),'/'.join(str(int(r['comparison_heldout_fitness'])) if r['comparison_heldout_fitness'] is not None else '無完成代' for r in wall_quality if r['task']==t and r['mode']==mode),str(sum(r['covered_10s'] for r in wall_quality if r['task']==t and r['mode']==mode))+'/3'] for t in TASKS for mode in MODES]))
    parts +=['\n完整選中 generation、來源 run 與覆蓋標記見 [quality_10s.csv](quality_10s.csv)。\n',
      '## 3. 端到端時間\n','![16代搜尋與完整process](end_to_end.png)\n',
      '以下為3 seeds 中位數。加速比為同輪 ASGP 的16代搜尋 wall 中位數除以對應模式中位數；包含 cold／final eval，不等於先前 frozen-parent single-gen headline。Process 欄額外包含實驗封存／獨立 audit：GAGP 匯出完整 final AST population並重評top16，ASGP匯出其champion；兩者封存工作不完全相同，應以 Search 作主要搜尋速度比較，Process 用來揭露實驗實際成本。\n',
      table(['題目','模式','Search（s）','Process（s）','ASGP/Search'],[[NAMES[r['task']],LABELS[r['mode']],f"{r['search_median_ms']/1000:.3f}",f"{r['process_median_ms']/1000:.3f}",f"{r['search_speedup_asgp']:.2f}×"] for r in summary]),
      '\n## 4. 前後期 generation 速度\n','![主矩陣每代時間](generation_time.png)\n',
      'Early／late 分別取每個 seed 的前4代／後4代中位數，再跨 seeds 取中位數；late/early 是各 seed 比值的中位數。各模式後期 populations 不同，這些是實际演化成本，不是 identical-workload evaluator speedup。\n',
      table(['題目','模式','全程gen中位數（ms）','Early4（ms）','Late4（ms）','Late/Early','Final平均nodes'],[[NAMES[r['task']],LABELS[r['mode']],f"{r['gen_median_ms']:.3f}",f"{r['early4_ms']:.3f}",f"{r['late4_ms']:.3f}",f"{r['late_over_early']:.2f}×",f"{r['final_nodes_mean']:.1f}"] for r in summary]),
      '\nASGP node 數是 phases 內的 nodes，GAGP 是完整 AST，不能直接比較絕對大小。\n',
      '![延長組逐代時間](extended_generation_time.png)\n',
      '延長組每條線是一個 seed；代數依性能分配，不同長度的總成本不可直接相除。詳見 [runs.csv](runs.csv) 中 extended rows 的 cold、final、RSS、final nodes 與 early/late。\n',
      '## 5. 正確性、可重現性與限制\n',
      f"- Primary completed: {len(primary)}/45；翻譯後 GAGP CPU／GPU 重評不一致的 champion 紀錄數：{summary_json['audit_mismatches']}。這是記錄數，重複 champion 可能多次出現；詳細 row 保留於 audit。\n",
      f"- 已對齊 objective 的 native fitness 與共同 CPU 重評不一致紀錄：{summary_json['native_common_mismatches']}；原 L1 Median 不混入此檢查。\n",
      f"- 延長組前16代 best／mean／best_nodes 與原run不一致：{sum(bool(r['different_primary_generations']) for r in prefix)}/{len(prefix)} runs。這只驗證已記錄欄位，不等同完整 population identity 證明。\n",
      '- CPU／ASGP 目前未提供每對 case 的 error／timeout counters；CSV 留空，不將缺值當零。GPU counters、rejection、operator fallback、changed／unchanged 等保留原始定義；不能把 attempted operator changes 當成最終 unique offspring。\n- 本輪沒有更改 runtime 數值、fuel、grammar 或 operators；擴充 benchmark search入口、明確設定其 penalty 與 opt-in progress trace。Median 改 fitness 目標是本輪揭露的唯一搜尋設定變更，原 L1 紀錄另保留。前一輪已知的 lossy／grammar profile 差異仍存在。\n- 3 seeds 只做有限品質篩查；雖已對齊題目評分，ASGP與GAGP的elitism、mutation粒度及搜尋空間仍不同，不能把品質差異單獨歸因GPU或RNG。\n',
      '## 6. 原始資料與重現\n',
      '- [summary.json](summary.json)：彙整、十秒品質、延長組prefix與模式間trajectory檢查。\n- [runs.csv](runs.csv)：每個獨立 process 的完整時間、記憶體與品質。\n- [generations.csv](generations.csv)：每代時間、fitness、原生／翻譯診斷、錯誤與operators計数。\n- [quality_16gen.csv](quality_16gen.csv)、[quality_10s.csv](quality_10s.csv)：逐seed品質。\n',
      f'本機 raw JSON、逐代 trace、history AST、final populations、RSS、commands、flags 與 frozen SHA 由下列目錄的manifest索引；原始檔保留於base／exact來源archives： `{out}`。Binary SHA-256：`{m["binary_sha256"]}`。每張圖另有同名 SVG。\n',
      '```bash\npython3 benchmarks/fixed_asgp/optimization/multigen.py --out logs/optimization/multigen-NEW-base --stage main --budget-seconds 2700\npython3 benchmarks/fixed_asgp/optimization/multigen.py --out logs/optimization/multigen-NEW-exact --stage main --tasks median --case-penalty 1 --budget-seconds 300\npython3 benchmarks/fixed_asgp/optimization/multigen.py --out logs/optimization/multigen-NEW-exact --stage extended --tasks median --case-penalty 1 --budget-seconds 600\npython3 benchmarks/fixed_asgp/optimization/multigen.py --out logs/optimization/multigen-NEW-base --stage extended --tasks sum_of_elements house_robber --budget-seconds 600\npython3 benchmarks/fixed_asgp/optimization/multigen.py --out logs/optimization/multigen-NEW-base --stage extended --tasks sum_of_elements house_robber --extension-observed-timing --budget-seconds 450\npython3 benchmarks/fixed_asgp/optimization/multigen_audit.py --out logs/optimization/multigen-NEW-analysis --base logs/optimization/multigen-NEW-base --aligned-median logs/optimization/multigen-NEW-exact\npython3 benchmarks/fixed_asgp/optimization/multigen_native_audit.py --out logs/optimization/multigen-NEW-analysis\npython3 benchmarks/fixed_asgp/optimization/multigen_report.py --out logs/optimization/multigen-NEW-analysis --report benchmarks/fixed_asgp/optimization/results/multigen-NEW\n```\n',
      '執行前先 build 此版 `gagp_fixed_asgp_bench`；需要既有 frozen artifacts 與本機封存 jemalloc。使用新輸出目錄，不對原始輸入重跑 prepare。整體一小時預算需自行合併各階段的實際時間，不是每階段各可跑一小時。\n']
    parts += ['## 7. Fitness 設計對照與可客製化範圍\n',
      '本輪實作的是 benchmark search／snapshot 的 penalty 設定，尚未新增通用 custom-fitness API。Median 採 Int 專用的等價實作完成 exact 目標對齊；不能據此宣稱支援任意輸出型別的 exact metric。\n',
      '原 Median L1 與新 exact 各跑16代，使用同一批初始程式與同一組 seed。下表均把各 run 依自己目標選出的 best-so-far 程式，用共同 exact metric 重評；比較的是更換選擇壓力後的結果，不是相同演化軌跡。\n']
    ablation=[]
    for mode in MODES[1:]:
        for stage in ('legacy_main','main'):
            rs=sorted([r for r in runs if r['task']=='median' and r['mode']==mode and r['stage']==stage],key=lambda r:r['seed'])
            if rs:ablation.append([LABELS[mode],'L1 cap1000' if stage=='legacy_main' else 'Exact (Int cap1)',
                  '/'.join(str(int(-r['comparison_training_fitness'])) for r in rs),
                  '/'.join(str(int(-r['heldout_fitness'])) for r in rs)])
    parts.append(table(['模式','搜尋目標','Training 錯誤cases，seeds0/1/2','Held-out 錯誤cases'],ablation))
    parts += ['\n建議後續以小型、可驗證的 fitness descriptor 指定 metric、distance cap、error penalty、type compatibility 與 aggregation；CPU/GPU 共用同一份設定並做 parity 測試。Secondary ranking（例如 ASGP 的 wrong count／size）應另外明確配置；它會影響 bloat 與 RNG tie 軌跡，不能只換 metric 就宣稱完整複製 ASGP selection。保持現有預設 spec，新增模式明確 opt-in。完整 cases 的評估與錯誤懲罰不能因客製化而略過。避免每 case 呼叫 Python／host callback，否則增加資料搬移與同步，也讓 GPU 成本難以比較。這是下一步介面建議，本輪沒有變更 runtime fitness contract。\n',
      '## 8. 錯誤、族群多樣性與記憶體\n']
    erows=[]
    for t in TASKS:
        for mode in MODES:
            rs=[r for r in primary if r['task']==t and r['mode']==mode]
            pairs=3*16*1024*1024
            errors=f"{sum(r['gpu_error_pairs'] for r in rs)/pairs*100:.3f}%" if mode.startswith('gpu') else '未提供'
            timeouts=str(sum(r['gpu_timeout_pairs'] for r in rs)) if mode.startswith('gpu') else '未提供'
            erows.append([NAMES[t],LABELS[mode],errors,timeouts,
                          '/'.join(str(r['final_unique']) for r in rs),
                          f"{max(r['peak_rss_kib'] for r in rs)/1024:.1f}"])
    parts.append(table(['題目','模式','16代 error pairs','Timeout pairs','Final unique seeds0/1/2','Peak RSS max (MiB)'],erows))
    parts += ['\n錯誤是已評估後的實際懲罰結果，不是跳過的 pairs。CPU／ASGP counters 未提供；GPU fallback／unscored 與完整原始 counters 在 CSV。Final unique 是序列化 genotype identity 數，不能當成行為多樣性或有效新 offspring 數；runs.csv 另有 final distinct fitness、mean／median，仍不是行為等價分類。本輪沒有每代全族群 unique／behavior census，也沒有逐代 RSS/device memory 採樣；peak RSS 含輸出與事後 audit，不能用來證明記憶體無洩漏。resident genotype 配置量另存 runs.csv。\n']
    thresholds=dict(sum_of_elements=0,house_robber=-8000,median=-512)
    threshold_rows=[]
    for t in TASKS:
        for mode in MODES:
            q=[r for r in quality_main if r['task']==t and r['mode']==mode]
            w=[r for r in wall_quality if r['task']==t and r['mode']==mode]
            threshold_rows.append([NAMES[t],LABELS[mode],thresholds[t],
              f"{sum(r['comparison_training_fitness']>=thresholds[t] for r in q)}/3",
              f"{sum(r['heldout_fitness']>=thresholds[t] for r in q)}/3",
              f"{sum(r['covered_10s'] and r['comparison_fitness'] is not None and r['comparison_fitness']>=thresholds[t] for r in w)}/{sum(r['covered_10s'] for r in w)}"])
    parts += ['## 9. 品質門檻\n',
      '主矩陣前定義 Sum=0、House>=-8000；Median 改 exact 後、執行 aligned runs 前定義>=-512（至少一半 cases 正確）。這些是本輪觀察門檻，不是三題難度等價的標準。Held-out 不參與選擇。十秒欄只計完整覆蓋十秒的 runs；0/0 表示沒有可用觀察。\n',
      table(['題目','模式','Fitness門檻','16代training','16代held-out','十秒training'],threshold_rows)]
    if (dest/'CONCLUSIONS.zh-TW.md').exists():parts.insert(1,(dest/'CONCLUSIONS.zh-TW.md').read_text())
    (dest/'REPORT.zh-TW.md').write_text('\n'.join(parts).replace('實际','實際').replace('計数','計數'))
    print('Report:',dest/'REPORT.zh-TW.md')
    for r in summary:print(r['task'],r['mode'],round(r['search_median_ms']/1000,3),round(r['search_speedup_asgp'],2),round(r['late_over_early'],2))

if __name__=='__main__':main()
