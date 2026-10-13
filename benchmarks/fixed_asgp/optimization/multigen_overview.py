"""Focused ASGP versus one fixed GAGP mode; render from archived CSV only."""
import argparse
import csv
from pathlib import Path
import statistics

import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.lines import Line2D
from matplotlib.ticker import MaxNLocator
import numpy as np

TASKS=('sum_of_elements','house_robber','median')
TITLES=('Sum of Elements','House Robber','Median')
MODES=('asgp_1t','gpu_overlap')
LABELS=('ASGP 1T','GAGP GPU-er-o')
COLORS=('#2463AA','#D96524')

def read(path):
    with path.open() as f:return list(csv.DictReader(f))

def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--results',type=Path,required=True)
    args=p.parse_args();out=args.results
    summary=read(out/'summary.csv');q16=read(out/'quality_16gen.csv');q10=read(out/'quality_10s.csv');gens=read(out/'generations.csv')
    plt.rcParams.update({'font.family':'Noto Sans CJK JP','font.size':12,
                         'axes.spines.top':False,'axes.spines.right':False,
                         'axes.labelcolor':'#344054','text.color':'#172B43',
                         'axes.titleweight':'bold','svg.fonttype':'none'})
    def record(task,mode):return next(r for r in summary if r['task']==task and r['mode']==mode)
    def save(fig,name):
        fig.savefig(out/f'{name}.png',dpi=170,facecolor='white')
        path=out/f'{name}.svg';fig.savefig(path,facecolor='white');plt.close(fig)
        path.write_text('\n'.join(line.rstrip() for line in path.read_text().splitlines())+'\n')
    # One endpoint, two implementations, direct seconds and ratios.
    fig,axes=plt.subplots(1,3,figsize=(13.5,4.7))
    fig.subplots_adjust(left=.13,right=.98,top=.68,bottom=.19,wspace=.58)
    fig.suptitle('ASGP vs GAGP：完成同樣 16 代，需要多久？',fontsize=21,x=.05,ha='left',y=.97)
    fig.text(.05,.875,'P = 1024 · 完整 1024 cases · CPU 1T + GPU · 3 seeds 中位數 · 越短越快',fontsize=12)
    for ax,task,title in zip(axes,TASKS,TITLES):
        values=[float(record(task,m)['search_median_ms'])/1000 for m in MODES]
        ratio=values[0]/values[1]
        verdict=f'GAGP 快 {ratio:.2f} 倍' if ratio>=1 else f'ASGP 快 {1/ratio:.2f} 倍'
        ax.set_title(title+'\n'+verdict,loc='left',fontsize=15,pad=18)
        ax.barh([1,0],values,color=COLORS,height=.44)
        ax.set_yticks([1,0],LABELS,fontsize=11);ax.set_ylim(-.65,1.65)
        ax.set_xlim(0,max(values)*1.42);ax.xaxis.set_major_locator(MaxNLocator(3))
        ax.set_xlabel('完整搜尋時間（秒）');ax.spines['left'].set_visible(False)
        ax.tick_params(axis='y',length=0);ax.grid(axis='x',alpha=.15);ax.set_axisbelow(True)
        for y,value in zip([1,0],values):ax.text(value+max(values)*.035,y,f'{value:.3f} s',va='center',fontsize=12,fontweight='bold')
    fig.text(.05,.035,'包含冷啟動、16 次完整 generation 與 final evaluation；不含事後封存／audit。各題橫軸尺度不同。',fontsize=10,color='#667085')
    save(fig,'overview_speed')
    # Show seed observations, not overlapping uncertainty ribbons. Evaluate
    # native-selected champions on held-out cases, never select on held-out.
    fig,axes=plt.subplots(2,2,figsize=(12.8,9))
    fig.subplots_adjust(left=.10,right=.95,top=.79,bottom=.16,hspace=.65,wspace=.37)
    fig.suptitle('品質比較：每一個 seed 都攤開看',fontsize=22,x=.055,ha='left',y=.97)
    fig.text(.055,.91,'獨立 1024 held-out cases；選解只看 training。圓點 = seed 0 / 1 / 2；粗橫線 = 中位數。',fontsize=12)
    fig.text(.055,.862,'Sum：五模式都是 0 誤差，但初始族群已有正確解，因此不拿它宣稱搜尋能力提升。',fontsize=11,color='#475467')
    for row,(data,field,budget) in enumerate(((q16,'heldout_fitness','同樣 16 代'),(q10,'comparison_heldout_fitness','同樣 10 秒'))):
        for col,(task,title) in enumerate((('house_robber','House Robber'),('median','Median'))):
            ax=axes[row,col];values=[]
            for mode in MODES:
                records=sorted([r for r in data if r['task']==task and r['mode']==mode],key=lambda r:int(r['seed']))
                assert len(records)==3
                if row:assert all(r['covered_10s']=='True' for r in records)
                loss=np.array([-float(r[field]) for r in records])
                values.append(loss/1024 if task=='house_robber' else (1-loss/1024)*100)
            for x,(ys,color) in enumerate(zip(values,COLORS)):
                med=statistics.median(ys)
                ax.hlines(med,x-.23,x+.23,color=color,linewidth=5,zorder=2)
                offsets=(-.13,0,.13)
                for seed,(offset,y) in enumerate(zip(offsets,ys)):
                    ax.scatter(x+offset,y,s=80,color=color,edgecolor='white',linewidth=1.2,zorder=3)
                    ax.annotate(str(seed),(x+offset,y),xytext=(0,9),textcoords='offset points',ha='center',fontsize=10,color=color,zorder=5,
                                bbox=dict(facecolor='white',edgecolor='none',pad=.2))
                label=f'{med:.2f}' if task=='house_robber' else f'{med:.1f}%'
                ax.text(x, -.27, f'中位數 {label}',transform=ax.get_xaxis_transform(),ha='center',fontweight='bold',color=color,fontsize=12)
            ax.set_title(f'{budget}  |  {title}',loc='left',fontsize=15,pad=13)
            ax.set_xticks([0,1],LABELS);ax.tick_params(axis='x',length=0,pad=10)
            ax.set_xlim(-.5,1.5);ax.set_ylim((0,10) if task=='house_robber' else (0,60))
            ax.set_ylabel('每 case 平均 capped 誤差 ↓' if task=='house_robber' else '正確率（%）↑ 越高越好',fontsize=11)
            ax.grid(axis='y',alpha=.18);ax.set_axisbelow(True)
    fig.text(.055,.052,'House：同時間的 GAGP 優勢明顯；同代數沒有優勢。Median：中位數相同，個別 seed 波動大。',fontsize=12,fontweight='bold')
    fig.text(.055,.018,'House 每 case 誤差上限 1000。ASGP 原生評分；GAGP 獨立 CPU 重評。語言／operator 仍有差異；3 seeds 不代表收斂保證。',fontsize=9,color='#667085')
    save(fig,'overview_quality')
    # Only two lines, common labels, and a direct link to late-search caveat.
    fig,axes=plt.subplots(1,3,figsize=(13.5,4.8))
    fig.subplots_adjust(left=.07,right=.98,top=.70,bottom=.20,wspace=.31)
    fig.suptitle('單代速度：演化過程中的 ASGP vs GAGP',fontsize=21,x=.055,ha='left',y=.97)
    fig.text(.055,.88,'前 16 代 · 每一代取 3 seeds 中位數 · 完整 generation · 越低越快',fontsize=12)
    fig.legend(handles=[Line2D([0],[0],color=c,lw=3,label=l) for c,l in zip(COLORS,LABELS)],loc='upper right',bbox_to_anchor=(.98,.865),ncol=2,frameon=False,fontsize=11)
    for ax,task,title in zip(axes,TASKS,TITLES):
        for mode,color in zip(MODES,COLORS):
            selected=[r for r in gens if r['stage']=='main' and r['task']==task and r['mode']==mode]
            med=[statistics.median(float(r['generation_ms']) for r in selected if int(r['generation'])==g) for g in range(16)]
            ax.plot(range(16),med,color=color,lw=2.8,marker='o',markersize=3)
        ax.set_title(title,fontsize=14,loc='left');ax.set_xlabel('Generation');ax.set_ylabel('毫秒 / generation')
        ax.set_xlim(0,15);ax.set_xticks([0,5,10,15]);ax.set_ylim(bottom=0);ax.grid(alpha=.18)
    fig.text(.055,.035,'長跑仍會變慢：House 的一個 512 代 GAGP run，前 4 代 35.56 ms → 後 4 代 409.27 ms（11.51 倍）。',fontsize=10,color='#667085')
    save(fig,'overview_generation')
    # Compact system-level comparison: same GAGP mode across all tasks.
    wall=[]
    for task,title in zip(TASKS,TITLES):
        vals=[float(record(task,m)['search_median_ms'])/1000 for m in ('asgp_1t','gagp_cpu','gpu_eval','gpu_repro','gpu_overlap')]
        wall.append('| '+title+' | '+' | '.join(f'{x:.3f}' for x in vals)+' |')
    document='''# ASGP vs GAGP：總體比較

主比較固定 **ASGP 1T vs GAGP GPU-er-o**，三題不更換 GAGP 模式、不挑最好 seed。
P=1024、完整1024cases，三個reproduction seeds、相同frozen來源初始族群。

**結論：Sum／House 跑完16代約快10倍；House能把速度轉成同時間更好的解；Median尚無穩定優勢。**

## 1. 速度

![同樣16代的完整搜尋時間](overview_speed.png)

- Sum：31.317 → 3.142秒，GAGP快 **9.97倍**。
- House：13.782 → 1.279秒，GAGP快 **10.78倍**。
- Median：0.776 → 1.028秒，**ASGP快1.32倍**；這個短搜尋的GAGP尚未攤平冷啟動等成本。

這些是cold + 16次完整generation + final evaluation的搜尋wall，不是kernel時間。輸入／admission及事後audit另列process wall，不含於此處search wall。
先前frozen-parent的30×不能當作本輪整趟搜尋的加速比。未跨題平均不同工作量／品質的倍數。

## 2. 搜尋品質：同代數與同時間分開

![同代數與同時間的held-out品質](overview_quality.png)

| 題目 | 同樣16代的held-out中位數 | 同樣10秒的held-out中位數 | 判讀 |
| --- | --- | --- | --- |
| Sum | ASGP / GAGP均0誤差 | 均0誤差 | 初始已有正確解，不能判定哪個更會搜尋 |
| House | 平均誤差7.71 → 8.20 | 平均誤差8.20 → 4.58，降低44.1% | 每代搜尋品質沒有優勢，但較快的演化換得更好的同時間結果 |
| Median | 正確率33.4% → 33.4% | 正確率33.4% → 33.4% | 沒有穩定的品質優勢，不能只挑有改善的seed |

箭頭均為ASGP → GAGP GPU-er-o。House同時間三個seed的held-out誤差都較低；
Median同時間一個seed變好、一個略差、一個更差。圖中保留全部觀察，避免只看中位數掩蓋差異。
held-out不參與挑選程式；挑選依training best-so-far，同分採最早完成的champion。

## 3. 單代速度與後期成本

![兩種實作的每代時間](overview_generation.png)

前16代，GPU-er-o的每代時間中位數為Sum143.13ms、House42.13ms、Median18.05ms；
ASGP分別1845.30ms、778.96ms、42.24ms。單代較快不代表短搜尋的整體wall一定較快。

長跑也不維持固定成本：House seed0的512代GAGP run，前4代中位數35.56ms，末4代409.27ms。
這是一個run的觀察，不是三個seed的總體估計；後期populations不同，也不能當作相同程式的VM退化倍數。

## 4. 四種GAGP模式全貌

單位：秒。16代搜尋wall，3 seeds中位數；包含cold與final evaluation。

| 題目 | ASGP 1T | GAGP CPU | GPU-e | GPU-er | GPU-er-o |
| --- | ---: | ---: | ---: | ---: | ---: |
'''+ '\n'.join(wall)+'''

CPU版仍明顯落後ASGP；只加GPU evaluation也不夠。主要收益出現在GPU evaluation + GPU reproduction。
ER和ER-o的數字接近，這個resident profile使用相同依賴排程；不能把小幅時間差宣稱為overlap的獨立收益。
Median的GPU-er略快於ER-o，但仍慢於ASGP的16代完整搜尋，因此不改變總體結論。

## 5. 比較界線與資料

- ASGP Median採exact，GAGP以Int輸出的cap1對齊；Sum／House用cap1000的absolute error。
- 各自使用原生執行語意：ASGP負索引報錯，GAGP採Python負索引。原生ASGP的品質不能拿翻譯後GAGP分數代替。
- Operator、tie-breaking、語言支援仍有差異；這是系統比較，不是單一RNG變更的因果實驗。
- 3個seeds共用一份初始族群；不能推論收斂等價或普遍泛化優勢。
- 圖表全部來自既有CSV，沒有重跑實驗或更動測量結果。

[完整逐seed報告與限制](REPORT.zh-TW.md) · [每次搜尋](runs.csv) · [逐代資料](generations.csv) · [十秒品質](quality_10s.csv)

重畫命令：

```bash
python3 benchmarks/fixed_asgp/optimization/multigen_overview.py --results benchmarks/fixed_asgp/optimization/results/multigen-20261013
```
'''
    (out/'OVERVIEW.zh-TW.md').write_text(document)
    print(out/'OVERVIEW.zh-TW.md')

if __name__=='__main__':main()
