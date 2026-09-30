from pathlib import Path
import json
import matplotlib
matplotlib.use('Agg')
import matplotlib.pyplot as plt
from matplotlib.patches import Patch
ROOT=Path(__file__).resolve().parent
all_cells=json.loads((ROOT/'analysis.json').read_text())['cells']
cells=[c for c in all_cells if c['mode']=='gpu_repro']
groups=[('Eval kernel','#3b82f6',['eval_kernel']),
        ('Verification / analysis','#ef4444',['verification_analysis']),
        ('Other host reproduction','#f59e0b',['repro_host_other']),
        ('Compilation','#8b5cf6',['compile']),
        ('Host packing','#06b6d4',['packing_host']),
        ('Allocation, transfers, other','#94a3b8',None)]
fig,ax=plt.subplots(figsize=(12,5.4),layout='constrained')
for y,cell in enumerate(cells):
    parts=cell['representative_wall_partition_ms'];left=0
    used=set()
    for label,color,keys in groups:
        if keys is None:value=sum(v for k,v in parts.items() if k not in used)
        else:
            value=sum(parts.get(k,0) for k in keys);used.update(keys)
        ax.barh(y,value,left=left,height=.50,color=color,label=label if y==0 else None)
        if value>50:ax.text(left+value/2,y,f'{value:.1f}',ha='center',va='center',color='white',fontsize=10,fontweight='bold')
        left+=value
    ax.text(left+9,y,f'{left:.1f} ms',va='center',fontsize=10)
    target=cell['asgp_median_ms']/(1 if cell['task']=='median' else 40)
    ax.plot([target,target],[y-.35,y+.35],color='#111827',linewidth=1.5,linestyle='--')
    ax.text(target+7,y-.37,('ASGP 1T' if cell['task']=='median' else '40x budget')+f': {target:.1f} ms',fontsize=9,va='bottom')
ax.set_yticks(range(len(cells)),['Sum of Elements','Median','House Robber'])
ax.invert_yaxis();ax.set_xlim(0,945);ax.set_ylim(2.65,-.65)
ax.set_xlabel('Full generation wall time (ms); exclusive partition, no double counting')
ax.set_title('GAGP profiler baseline: 1024 parents × 1024 cases',loc='left',fontsize=16,pad=24)
ax.text(0,1.03,'GPU evaluation + GPU reproduction, overlap OFF · median-duration traced generation · RTX 3090',transform=ax.transAxes,fontsize=10,color='#475569')
ax.spines[['top','right','left']].set_visible(False);ax.tick_params(axis='y',length=0)
ax.set_axisbelow(True);ax.grid(axis='x',alpha=.15)
ax.legend(handles=[Patch(color=color,label=label) for label,color,_ in groups],loc='upper center',bbox_to_anchor=(.5,-.18),ncol=3,frameon=False,fontsize=9)
fig.savefig(ROOT/'phase-breakdown.svg',metadata={'Date':None})
fig.savefig(ROOT/'phase-breakdown.png',dpi=160)
