"""Plot seed-resolved long-search trends and archive useful per-generation data."""
import argparse
import csv
import hashlib
import json
from pathlib import Path
import shutil
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
import numpy as np

TASKS=('house_robber','median')
TITLES=('House Robber','Median')
MODES=('asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap')
LABELS=('ASGP 1T','GAGP CPU','GAGP GPU-e','GAGP GPU-er','GAGP GPU-er-o')
COLORS=('#222222','#377EB8','#009E73','#D55E00','#984EA3')
STYLES=('-','--','-.','--',':')
MARKERS=('o','s','^','D','x')


def write_csv(path,rows):
    fields=list(dict.fromkeys(k for r in rows for k in r))
    with path.open('w') as f:
        w=csv.DictWriter(f,fieldnames=fields,lineterminator='\n');w.writeheader();w.writerows(rows)


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    p.add_argument('--report',type=Path,required=True)
    a=p.parse_args();out=a.out.resolve();dest=a.report.resolve();dest.mkdir(parents=True,exist_ok=True)
    m=json.loads((out/'manifest.json').read_text())
    assert 'finished_epoch' in m
    audit=json.loads((out/'checkpoint-audit/summary.json').read_text())
    selected={};allruns=[];raw=[];prefix=[];histories={}
    # Longest successfully finished run per task/mode/seed; never splice runs.
    for c in m['cells']:
        if c['status']!='ok':continue
        r=json.loads(Path(c['output']).read_text());gens=r['generations']
        hs=json.loads(Path(c['output']).with_suffix('.history.json').read_text())['history']
        assert len(hs)==len(gens)
        assert all(h['fitness']==g['best'] for h,g in zip(hs,gens))
        old_path=Path('benchmarks/fixed_asgp/optimization/results/multigen-20261013/generations.csv')
        if old_path.exists():
            with old_path.open() as f:
                old=[x for x in csv.DictReader(f) if x['stage']=='main' and x['task']==c['task'] and x['mode']==c['mode'] and int(x['seed'])==c['seed']]
            if old:
                differences=[i for i,(x,y) in enumerate(zip(old,gens)) if any(float(x[k])!=y[k] for k in ('best','mean','best_nodes'))]
                prefix.append(dict(tag=c['tag'],old_16gen_differences=differences))
        finals=r['final_population'];times=[g['generation_ms'] for g in gens]
        resource=Path(c['output']).with_suffix('.resources').read_text().splitlines()
        row=dict(tag=c['tag'],stage=c['tag'].split('.')[0],task=c['task'],mode=c['mode'],seed=c['seed'],generations=len(gens),
            search_s=r['evolve_wall_ms']/1000,process_s=c['process_wall_s'],
            gen_median_ms=statistics.median(times),early16_ms=statistics.median(times[:16]),
            late16_ms=statistics.median(times[-16:]),late_over_early=statistics.median(times[-16:])/statistics.median(times[:16]),
            best_ever=max([g['best'] for g in gens]+[r['final_best']]),final_best=r['final_best'],
            final_mean_nodes=statistics.mean(v['nodes'] for v in finals),final_unique=r['unique_final_genomes'],
            peak_rss_kib=next(int(s.split('=')[1]) for s in resource if s.startswith('peak_rss_kib=')),
            cpu_audit_mismatches=r.get('cpu_audit_mismatches'),genotype_device_bytes=r.get('genotype_device_bytes'),
            gpu_errors=sum(g.get('errors',0) for g in gens) if c['mode'].startswith('gpu') else None,
            gpu_timeouts=sum(g.get('timeouts',0) for g in gens) if c['mode'].startswith('gpu') else None,
            gpu_fallbacks=sum(g.get('fallbacks',0) for g in gens) if c['mode'].startswith('gpu') else None,
            gpu_unscored=sum(g.get('unscored',0) for g in gens) if c['mode'].startswith('gpu') else None)
        allruns.append(row)
        incumbent=None
        history=[]
        for h,g in zip(hs,gens):
            if incumbent is None or h['fitness']>incumbent['fitness']:incumbent=h
            raw.append(dict(tag=c['tag'],task=c['task'],mode=c['mode'],seed=c['seed'],**g,
                best_so_far=incumbent['fitness']))
            history.append(dict(generation=g['generation'],elapsed_s=g['elapsed_search_ms']/1000,
                best_so_far=incumbent['fitness'],incumbent_generation=incumbent['generation']))
        history.append(dict(generation=len(gens),elapsed_s=r['evolve_wall_ms']/1000,
            best_so_far=max(incumbent['fitness'],r['final_best']),incumbent_generation=None))
        histories[c['tag']]=history
        key=(c['task'],c['mode'],c['seed'])
        if key not in selected or selected[key][0]['generations']<c['generations']:selected[key]=(c,r)
    assert len(selected)==30, 'Incomplete primary matrix: report explicitly before drawing full-comparison plots.'
    aggregates=[]
    for task in TASKS:
        reference=[r for r in allruns if r['stage']=='main' and r['task']==task and r['mode']=='asgp_1t']
        assert len(reference)==3
        baseline=statistics.median(r['search_s'] for r in reference)
        for mode in MODES:
            rs=[r for r in allruns if r['stage']=='main' and r['task']==task and r['mode']==mode]
            assert len(rs)==3
            time=statistics.median(r['search_s'] for r in rs)
            aggregates.append(dict(task=task,mode=mode,generations=rs[0]['generations'],search_median_s=time,
                search_min_s=min(r['search_s'] for r in rs),search_max_s=max(r['search_s'] for r in rs),
                search_speedup_asgp=baseline/time,training_best_by_seed=[r['best_ever'] for r in sorted(rs,key=lambda r:r['seed'])]))
    write_csv(dest/'main_summary.csv',aggregates)
    write_csv(dest/'runs.csv',allruns);write_csv(dest/'generations.csv',raw)
    write_csv(dest/'checkpoints.csv',audit['checkpoints'])
    (dest/'prefix_checks.json').write_text(json.dumps(prefix,indent=2)+'\n')
    shutil.copy2(out/'manifest.json',dest/'manifest.json')
    shutil.copy2(out/'checkpoint-audit/summary.json',dest/'audit-summary.json')
    plt.rcParams.update({'font.family':'DejaVu Sans','font.size':11,'axes.spines.top':False,
                         'axes.spines.right':False,'axes.titleweight':'bold','svg.fonttype':'none'})
    handles=[Line2D([0],[0],color=c,linestyle=s,marker=k,lw=2,label=l)
             for c,s,k,l in zip(COLORS,STYLES,MARKERS,LABELS)]
    def canvas(title,subtitle):
        fig,axes=plt.subplots(2,3,figsize=(16,9.3))
        fig.subplots_adjust(left=.07,right=.98,top=.80,bottom=.14,hspace=.40,wspace=.24)
        fig.suptitle(title,x=.05,ha='left',y=.975,fontsize=21)
        fig.text(.05,.925,subtitle,fontsize=11)
        fig.legend(handles=handles,loc='upper center',bbox_to_anchor=(.5,.90),ncol=5,frameon=False)
        for row,title in enumerate(TITLES):
            for seed in range(3):
                ax=axes[row,seed];ax.set_title(f'{title} | Seed {seed}',loc='left',fontsize=14)
                ax.grid(alpha=.18);ax.set_axisbelow(True)
        return fig,axes
    def save(fig,name):
        fig.savefig(dest/f'{name}.png',dpi=180,facecolor='white')
        svg=dest/f'{name}.svg';fig.savefig(svg,facecolor='white')
        svg.write_text('\n'.join(s.rstrip() for s in svg.read_text().splitlines())+'\n');plt.close(fig)
    def line(ax,x,y,i,step=False):
        ax.plot(x,y,color=COLORS[i],linestyle=STYLES[i],lw=2,marker=MARKERS[i],
                markerfacecolor='white',markersize=4,markevery=(i,max(5,len(x)//8)),
                drawstyle='steps-post' if step else 'default')
    def quality(task,fitness):
        values=np.array(fitness)/1024
        return -values if task=='house_robber' else (1+values)*100
    def ylabel(task):
        return 'Best-so-far error / case (lower is better)' if task=='house_robber' else 'Best-so-far accuracy (%) (higher is better)'
    fig,axes=canvas('Search progress | Every seed, all five modes',
        'P = 1024 | All 1024 training cases | Best-so-far native fitness | No smoothing or seed averaging')
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed];hs=histories[c['tag']]
                line(ax,[h['generation'] for h in hs],quality(task,[h['best_so_far'] for h in hs]),i,True)
                ax.scatter(hs[-1]['generation'],quality(task,[hs[-1]['best_so_far']])[0],color=COLORS[i],marker=MARKERS[i],s=32,zorder=4)
            ax.set_xlabel('Generation (0 = initial population)');ax.set_ylabel(ylabel(task))
            ax.set_xlim(left=0)
            if task=='median' and any(selected[task,mode,seed][0]['generations']>128 for mode in MODES):
                ax.axvline(128,color='#888888',ls=':',lw=1,zorder=0)
                ax.text(128,.98,'128-gen common horizon',transform=ax.get_xaxis_transform(),
                    ha='right',va='top',fontsize=8,color='#666666',bbox=dict(facecolor='white',edgecolor='none',pad=1))
        # Same y limits across seeds to avoid exaggerating differences.
        low=min(ax.get_ylim()[0] for ax in axes[row]);high=max(ax.get_ylim()[1] for ax in axes[row])
        for ax in axes[row]:ax.set_ylim(low,high)
    fig.text(.05,.065,'House: capped absolute error (cap 1000). Median: exact-match accuracy. Final endpoint includes final evaluation; shorter runs are not extended.',fontsize=10)
    fig.text(.05,.03,'All runs start from the same frozen population. Different runtime / variation semantics remain; flat lines show real stagnation, not missing values.',fontsize=10,color='#555555')
    save(fig,'fitness_vs_generation')
    # Common observed time across all five modes, no holding a finished run beyond its measured budget.
    fig,axes=canvas('Search progress at equal time | Every seed, all five modes',
        'Full search clock, including cold start | Each panel ends at the shortest measured run among its five modes')
    time_comparisons=[]
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            limit=min(r['evolve_wall_ms']/1000 for c,r in (selected[task,mode,seed] for mode in MODES))
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed];hs=histories[c['tag']]
                known=[h for h in hs if h['elapsed_s']<=limit]
                if not known:
                    time_comparisons.append(dict(task=task,seed=seed,mode=mode,common_budget_s=limit,
                        best_so_far=None,last_completed_generation=None))
                    ax.text(.02,.05+i*.045,f'{LABELS[i]}: no completed-generation record',transform=ax.transAxes,fontsize=8,color=COLORS[i])
                    continue
                xs=[h['elapsed_s'] for h in known];ys=list(quality(task,[h['best_so_far'] for h in known]))
                # This run is still running at cutoff; its last completed incumbent remains available.
                if xs[-1]<limit:xs.append(limit);ys.append(ys[-1])
                line(ax,xs,ys,i,True)
                time_comparisons.append(dict(task=task,seed=seed,mode=mode,common_budget_s=limit,
                    best_so_far=known[-1]['best_so_far'],last_completed_generation=known[-1]['generation']))
            ax.set_xlim(0,limit);ax.set_xlabel('Elapsed search time (seconds)');ax.set_ylabel(ylabel(task))
            ax.text(.97,.96,f'Common budget: {limit:.1f} s',transform=ax.transAxes,ha='right',va='top',fontsize=9)
        low=min(ax.get_ylim()[0] for ax in axes[row]);high=max(ax.get_ylim()[1] for ax in axes[row])
        for ax in axes[row]:ax.set_ylim(low,high)
    fig.text(.05,.065,'A curve starts only once an evaluated champion is available. Incumbents are recorded at completed-generation boundaries (conservative timestamps).',fontsize=10)
    fig.text(.05,.03,'Cutoffs depend only on run duration, not fitness. Equal-generation and equal-time comparisons answer different questions.',fontsize=10,color='#555555')
    save(fig,'fitness_vs_time');write_csv(dest/'equal_time.csv',time_comparisons)
    fig,axes=canvas('Search progress over measured wall time | Full runs',
        'All five modes, every seed | Logarithmic time axis | Each curve stops at its own measured search endpoint')
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed];hs=histories[c['tag']]
                line(ax,[h['elapsed_s'] for h in hs],quality(task,[h['best_so_far'] for h in hs]),i,True)
                ax.scatter(hs[-1]['elapsed_s'],quality(task,[hs[-1]['best_so_far']])[0],color=COLORS[i],marker=MARKERS[i],s=30,zorder=4)
            ax.set_xscale('log');ax.set_xlabel('Elapsed search time (seconds, log scale)');ax.set_ylabel(ylabel(task))
        low=min(ax.get_ylim()[0] for ax in axes[row]);high=max(ax.get_ylim()[1] for ax in axes[row])
        for ax in axes[row]:ax.set_ylim(low,high)
    fig.text(.05,.065,'Endpoints are different time budgets. Compare at a shared time only where runs are still observed; no claim is made beyond a stopped run.',fontsize=10)
    fig.text(.05,.03,'Search time includes cold start and final evaluation; process-only import, export and post-search audit are reported separately.',fontsize=10,color='#555555')
    save(fig,'fitness_vs_time_full')
    fig,axes=canvas('Complete generation time | Early to late search',
        'P = 1024 | All 1024 cases | Every measured generation, no smoothing | Logarithmic time axis')
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed];gs=r['generations']
                line(ax,[g['generation'] for g in gs],[g['generation_ms'] for g in gs],i)
            ax.set_xlabel('Generation');ax.set_ylabel('Complete generation time (ms, log scale)');ax.set_yscale('log');ax.set_xlim(left=0)
    fig.text(.05,.065,'Includes evaluation, selection, variation and next-generation preparation. One-time startup and final-only evaluation are excluded here.',fontsize=10)
    fig.text(.05,.03,'The same initial population evolves differently across modes: these are system timings, not identical-program executor comparisons.',fontsize=10,color='#555555')
    save(fig,'generation_time')
    fig,axes=canvas('Program size during search | Current-generation champion',
        'Node count of each generation\'s best program | Not population-average size | Raw per-generation values')
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed];gs=r['generations']
                line(ax,[g['generation'] for g in gs],[g['best_nodes'] for g in gs],i)
            ax.set_xlabel('Generation');ax.set_ylabel('Champion nodes (native representation)');ax.set_xlim(left=0);ax.set_ylim(bottom=0)
    fig.text(.05,.065,'ASGP and GAGP count nodes in different representations; absolute sizes are not interchangeable. Track growth within each mode.',fontsize=10)
    fig.text(.05,.03,'The archived GAGP binary does not record per-generation population mean size. Final population mean size and peak RSS are in runs.csv.',fontsize=10,color='#555555')
    save(fig,'champion_size')
    fig,axes=canvas('Independent held-out quality | Training-selected checkpoints',
        '1024 held-out cases | Champions selected only on training fitness | Native ASGP / independently admitted GAGP CPU+GPU')
    for row,task in enumerate(TASKS):
        for seed in range(3):
            ax=axes[row,seed]
            for i,mode in enumerate(MODES):
                c,r=selected[task,mode,seed]
                hs=sorted([v for v in audit['checkpoints'] if v['tag']==c['tag']],key=lambda h:h['completed_generations'])
                ax.plot([h['completed_generations'] for h in hs],quality(task,[h['heldout_fitness'] for h in hs]),
                    color=COLORS[i],linestyle=STYLES[i],marker=MARKERS[i],lw=2,markersize=5,markerfacecolor='white')
            ax.set_xlabel('Completed generations at checkpoint');ax.set_ylabel('Held-out error / case' if task=='house_robber' else 'Held-out accuracy (%)')
        low=min(ax.get_ylim()[0] for ax in axes[row]);high=max(ax.get_ylim()[1] for ax in axes[row])
        for ax in axes[row]:ax.set_ylim(low,high)
    fig.text(.05,.065,'Lower error / higher accuracy is better. Held-out quality can worsen even when training fitness improves; it is never used for selection.',fontsize=10)
    fig.text(.05,.03,'At the final run checkpoint, the actual final evaluation is included; intermediate checkpoints use champions from completed generations only.',fontsize=10,color='#555555')
    save(fig,'heldout_checkpoints')
    report=['# 長世代搜尋結果\n',
        '圖中文字均為英文。House64、Median128主矩陣各15次搜尋；延長run與完成範圍見下表。圖表使用每格最長的完整run，不拼接不同run。\n',
        f"Performance runner elapsed: {m['elapsed_seconds']/60:.2f} min. Checkpoint audit: {audit['elapsed_seconds']:.2f} s.\n",
        '主矩陣同代數的三seed時間中位數與ASGP加速比，另見[main_summary.csv](main_summary.csv)；延長組不能混入該比值。\n',
        '## 完成範圍與時間\n',
        '| Task | Mode | Seed | Generations | Search s | Process s | Early16 ms | Late16 ms | Late/Early | Training best | Held-out | Final mean nodes | Peak RSS MiB |',
        '| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: | ---: |']
    for task in TASKS:
        for mode in MODES:
            for seed in range(3):
                c,_=selected[task,mode,seed];r=next(x for x in allruns if x['tag']==c['tag'])
                h=next(x for x in audit['checkpoints'] if x['tag']==c['tag'] and x['completed_generations']==c['generations'])
                report.append(f"| {task} | {mode} | {seed} | {r['generations']} | {r['search_s']:.2f} | {r['process_s']:.2f} | {r['early16_ms']:.2f} | {r['late16_ms']:.2f} | {r['late_over_early']:.2f} | {r['best_ever']} | {h['heldout_fitness']} | {r['final_mean_nodes']:.1f} | {r['peak_rss_kib']/1024:.1f} |")
    for title,name in [('Fitness vs. generation','fitness_vs_generation'),('同時間品質','fitness_vs_time'),('完整搜尋時間範圍','fitness_vs_time_full'),('每代耗時','generation_time'),('程式大小','champion_size'),('獨立held-out品質','heldout_checkpoints')]:
        report.extend(['',f'## {title}',f'![{title}]({name}.png)'])
    report.extend(['','## 比較限制',
        '- 三個reproduction seeds共用同一初始population；不能當作收斂品質等價證明。Sum初始已有解，本輪不重跑。',
        '- 延長run從初始population重跑，成本包含於performance budget；prefix_checks.json核對之前16代，manifest記錄延長run對128代的前綴。',
        '- GAGP CPU／GPU-e與GPU-er/o保留不同variation規則；ASGP/GAGP原生語意也不同（例如負索引）。只對齊scalar fitness不等於程式語意相同。',
        '- 計時有三種口徑：generation、search（含cold/final eval）、process（另含匯入/audit/export）。不混用。',
        '- GPU error、timeout、fallback、unscored在generations.csv；CPU與ASGP未提供相同細粒度counter，缺值不代表0。changed是operator計數，不代表unique有效offspring。',
        '- 程式大小圖是當代champion，不是整個population平均；不同表示法node數不直接等價。RSS為process峰值，不是逐代memory趨勢。',
        '- 主報告generation endpoint包含final eval；中間checkpoint不額外插入評估。Held-out只用於事後分析。',
        '', '[實驗契約](CONTRACT.zh-TW.md) · [每代資料](generations.csv) · [每次搜尋](runs.csv) · [Checkpoint品質](checkpoints.csv) · [同時間比較](equal_time.csv)',
        '', '重現分析：', '```bash',
        'python3 benchmarks/fixed_asgp/optimization/long_search_audit.py --out logs/optimization/long-search-NEW',
        'python3 benchmarks/fixed_asgp/optimization/long_search_report.py --out logs/optimization/long-search-NEW --report benchmarks/fixed_asgp/optimization/results/long-search-NEW', '```'])
    conclusions=dest/'CONCLUSIONS.zh-TW.md'
    if conclusions.exists():report.insert(1,conclusions.read_text())
    (dest/'REPORT.zh-TW.md').write_text('\n'.join(report)+'\n')
    print(dest/'REPORT.zh-TW.md')

if __name__=='__main__':main()
