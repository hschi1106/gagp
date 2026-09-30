"""Derive per-generation NVTX wall ranges and real CUDA activity from Nsight SQLite.

The serial-mode wall partition is additive: CUDA device activity has priority,
then main-thread CUDA API overhead, then the most deeply nested named NVTX stage.
Raw CUDA synchronization waits are reported separately because they overlap work.
Parallel worker accept durations are thread-time, never additive generation time.
"""
from pathlib import Path
from collections import defaultdict,Counter
import csv,json,sqlite3,statistics,sys
OUT=Path(sys.argv[1]).resolve() if len(sys.argv)>1 else Path(__file__).resolve().parent

def union(spans):
    result=[]
    for a,b in sorted(spans):
        if b<=a:continue
        if result and a<=result[-1][1]: result[-1]=(result[-1][0],max(b,result[-1][1]))
        else:result.append((a,b))
    return result

def length(spans):return sum(b-a for a,b in union(spans))/1e6

def intersection(left,right):
    a=union(left);b=union(right);i=j=0;out=[]
    while i<len(a) and j<len(b):
        lo=max(a[i][0],b[j][0]);hi=min(a[i][1],b[j][1])
        if hi>lo:out.append((lo,hi))
        if a[i][1]<b[j][1]:i+=1
        else:j+=1
    return out

def category(name):
    if name.startswith('verification.'):return 'verification_analysis'
    if name=='compile.total':return 'compile'
    if name in ('eval.pack','repro.pack'):return 'packing_host'
    if name.startswith('selection.'):return 'selection_host'
    if name in ('eval.alloc_upload','eval.teardown','repro.setup'):return 'allocation_setup'
    if name=='eval.copyback':return 'transfer_host_overhead'
    if name=='eval.launch_wait':return 'launch_or_wait_overhead'
    if name.startswith('repro.'):return 'repro_host_other'
    if name.startswith('lifecycle.'):return 'lifecycle'
    if name.startswith('eval.'):return 'eval_host_other'
    return None

def cuda_api_category(name):
    if 'Synchronize' in name:return 'sync_exposed'
    if 'Memcpy' in name:return 'transfer_host_overhead'
    if any(x in name for x in ('Malloc','Free','HostAlloc','HostRegister','HostUnregister')):return 'allocation_setup'
    if 'Launch' in name:return 'launch_or_wait_overhead'
    return None

def kernel_category(name):
    if 'evaluate_fitness' in name:return 'eval_kernel'
    if 'tournament_select' in name:return 'selection_gpu'
    return 'repro_gpu'

def inspect(database, native):
    db=sqlite3.connect(database);db.row_factory=sqlite3.Row
    strings=dict(db.execute('select id,value from StringIds'))
    def table(name):
        try:return [dict(row) for row in db.execute('select * from '+name)]
        except sqlite3.OperationalError:return []
    nvtx=table('NVTX_EVENTS')
    for r in nvtx:r['name']=r['text'] or strings.get(r['textId'],'')
    nvtx=[r for r in nvtx if r['end'] is not None and r['end']>=r['start']]
    kernels=table('CUPTI_ACTIVITY_KIND_KERNEL')
    for r in kernels:r['name']=strings[r['shortName']]
    copies=table('CUPTI_ACTIVITY_KIND_MEMCPY');memsets=table('CUPTI_ACTIVITY_KIND_MEMSET')
    apis=table('CUPTI_ACTIVITY_KIND_RUNTIME')
    for r in apis:r['name']=strings[r['nameId']]
    osrt=table('OSRT_API')
    for r in osrt:r['name']=strings[r['nameId']]
    allocs=table('CUDA_GPU_MEMORY_USAGE_EVENTS')
    gens=sorted([r for r in nvtx if r['name']=='generation'],key=lambda r:r['start'])
    assert len(gens)==4,(database,len(gens))
    output=[]
    for index,g in enumerate(gens):
        lo,hi=g['start'],g['end'];main=g['globalTid']
        def inside(rows):return [r for r in rows if r['start']<hi and r['end']>lo]
        def spans(rows):return [(max(lo,r['start']),min(hi,r['end'])) for r in rows]
        local=inside(nvtx);ka=inside(kernels);ma=inside(copies);sa=inside(memsets);api=inside(apis)
        rep=[r for r in local if r['name'].startswith('rep/') and r['start']<=lo and r['end']>=hi]
        assert len(rep)==1 and int(rep[0]['name'].split('/')[1])==index-1
        item={'rep':index-1,'generation_ms':(hi-lo)/1e6,'native_generation_ms':native[index]['generation_ms']}
        assert abs(item['generation_ms']-item['native_generation_ms'])<0.25,(database,item)
        ranges=defaultdict(list)
        for r in local:ranges[r['name']].append((max(lo,r['start']),min(hi,r['end'])))
        item['range_union_ms']={k:length(v) for k,v in sorted(ranges.items()) if not k.startswith('rep/')}
        item['accept_worker_thread_ms']=sum(b-a for a,b in ranges['verification.accept_worker'])/1e6
        item['accept_worker_union_ms']=length(ranges['verification.accept_worker'])
        devices=spans(ka+ma+sa)
        item['gpu_active_ms']=length(devices)
        item['gpu_idle_ms']=item['generation_ms']-item['gpu_active_ms']
        item['gpu_kernels']=[{k:r[k] for k in ('name','gridX','gridY','gridZ','blockX','blockY','blockZ','registersPerThread','dynamicSharedMemory','localMemoryPerThread','localMemoryTotal')} | {'duration_ms':(r['end']-r['start'])/1e6} for r in ka]
        for name,kind in (('eval_kernel',lambda r:'evaluate_fitness' in r['name']),('selection_gpu',lambda r:'tournament_select' in r['name']),('repro_gpu',lambda r:'evaluate_fitness' not in r['name'] and 'tournament_select' not in r['name'])):
            item[name+'_ms']=length(spans([r for r in ka if kind(r)]))
        for name,kind in (('h2d',1),('d2h',2)):
            selected=[r for r in ma if r['copyKind']==kind]
            item[name+'_device_ms']=length(spans(selected));item[name+'_bytes']=sum(r['bytes'] for r in selected);item[name+'_copies']=len(selected)
        sync=[r for r in api if 'Synchronize' in r['name']]
        item['cuda_sync_wait_ms']=length(spans(sync))
        item['cuda_sync_device_overlap_ms']=length(intersection(spans(sync),devices))
        item['cuda_sync_exposed_ms']=item['cuda_sync_wait_ms']-item['cuda_sync_device_overlap_ms']
        item['cuda_sync_calls']=len(sync)
        item['cuda_api_ms']={name:length(spans([r for r in api if r['name']==name])) for name in sorted(set(r['name'] for r in api))}
        item['cuda_api_counts']=dict(Counter(r['name'] for r in api))
        allocations=[r for r in allocs if lo<=r['start']<hi]
        item['cuda_memory_events']={f'op_{kind}':{'count':sum(r['memoryOperationType']==kind for r in allocations),'bytes':sum(r['bytes'] for r in allocations if r['memoryOperationType']==kind)} for kind in sorted(set(r['memoryOperationType'] for r in allocations))}
        waits=[r for r in inside(osrt) if r['globalTid']==main and any(n in r['name'] for n in ('cond_wait','cond_timedwait','join','futex'))]
        item['main_osrt_wait_ms']=length(spans(waits))
        prep=ranges['repro.prepare.crossover']
        eval_stage=ranges['eval.stage']
        item['crossover_prepare_ms']=length(prep)
        item['crossover_prep_overlapped_with_eval_ms']=length(intersection(prep,eval_stage))
        item['crossover_prep_overlapped_with_gpu_ms']=length(intersection(prep,spans(ka)))
        # Exact partition of the main-thread timeline. GPU activity takes priority
        # to remove double counting with blocking CUDA APIs. Worker work is only
        # reported as a separate union, not charged twice to a waiting main thread.
        labelled=[]
        for r in local:
            cat=category(r['name'])
            if r['globalTid']==main and cat:
                labelled.append((max(lo,r['start']),min(hi,r['end']),10,cat))
        for r in api:
            cat=cuda_api_category(r['name'])
            if r['globalTid']==main and cat:
                labelled.append((max(lo,r['start']),min(hi,r['end']),20,cat))
        for r in ka:labelled.append((max(lo,r['start']),min(hi,r['end']),30,kernel_category(r['name'])))
        for r in ma:labelled.append((max(lo,r['start']),min(hi,r['end']),30,{1:'h2d_device',2:'d2h_device'}.get(r['copyKind'],'other_device_copy')))
        for r in sa:labelled.append((max(lo,r['start']),min(hi,r['end']),30,'device_memset'))
        events=[]
        for i,(a,b,priority,cat) in enumerate(labelled):
            events.extend(((a,1,i),(b,0,i)))
        events.sort();active=set();last=lo;charged=defaultdict(int)
        for timestamp,end_or_start,i in events+[(hi,0,-1)]:
            if timestamp>last:
                if active:
                    winner=max(active,key=lambda k:(labelled[k][2],labelled[k][0],-labelled[k][1]))
                    cat=labelled[winner][3]
                else:cat='unattributed_host'
                charged[cat]+=timestamp-last;last=timestamp
            if i!=-1:
                if end_or_start:active.add(i)
                else:active.discard(i)
        item['wall_partition_ms']={k:v/1e6 for k,v in sorted(charged.items())}
        assert abs(sum(item['wall_partition_ms'].values())-item['generation_ms'])<1e-7
        output.append(item)
    db.close()
    return output

def analyze():
    manifest=json.loads((OUT/'manifest.json').read_text())
    assert manifest['complete']
    lookup={(c['task'],c['mode'],c['kind']):c for c in manifest['cells']}
    result={'method':__doc__,'cases':1024,'population':1024,'cells':[]}
    for cell in manifest['cells']:
        if cell['kind']!='trace':continue
        task,mode=cell['task'],cell['mode']
        rows=inspect(OUT/'runs'/f'{task}.{mode}.trace.sqlite',cell['rows'])
        measured=rows[1:]
        med=lambda xs:statistics.median(xs)
        base=lookup[task,mode,'original'];probe=lookup[task,mode,'probe'];asgp=lookup[task,'asgp_1t','original']
        summary={'task':task,'mode':mode,'rows':rows,'original_median_ms':med(r['generation_ms'] for r in base['rows'][1:]),
                 'probe_median_ms':med(r['generation_ms'] for r in probe['rows'][1:]),'asgp_median_ms':med(r['generation_ms'] for r in asgp['rows'][1:])}
        summary['medians']={k:med(r[k] for r in measured) for k,v in measured[0].items() if isinstance(v,(int,float)) and k!='rep'}
        # A single representative trace row is additive; independent phase medians
        # need not sum to the median generation. Retain both with explicit names.
        representative=sorted(measured,key=lambda r:r['generation_ms'])[1]
        summary['representative_rep']=representative['rep']
        summary['representative_wall_partition_ms']=representative['wall_partition_ms']
        summary['representative_range_union_ms']=representative['range_union_ms']
        summary['range_medians_ms']={k:med(r['range_union_ms'].get(k,0) for r in measured) for k in set().union(*(r['range_union_ms'] for r in measured))}
        summary['traced_over_original']=summary['medians']['generation_ms']/summary['original_median_ms']
        summary['probe_over_original']=summary['probe_median_ms']/summary['original_median_ms']
        summary['speedup_asgp']=summary['asgp_median_ms']/summary['original_median_ms']
        result['cells'].append(summary)
    (OUT/'analysis.json').write_text(json.dumps(result,indent=2,allow_nan=False)+'\n')
    columns=['task','mode','original_median_ms','probe_median_ms','traced_median_ms','speedup_asgp','traced_over_original','eval_kernel_ms','h2d_device_ms','d2h_device_ms','selection_gpu_ms','repro_gpu_ms','cuda_sync_wait_ms','cuda_sync_exposed_ms','crossover_prep_overlapped_with_eval_ms']
    with (OUT/'summary.csv').open('w') as f:
        writer=csv.DictWriter(f,fieldnames=columns);writer.writeheader()
        for cell in result['cells']:
            row={k:cell.get(k,cell['medians'].get(k)) for k in columns}
            row['traced_median_ms']=cell['medians']['generation_ms'];writer.writerow(row)
    for cell in result['cells']:
        print(cell['task'],cell['mode'],'original',round(cell['original_median_ms'],3),'trace',round(cell['medians']['generation_ms'],3))
        if cell['mode']=='gpu_repro':print(cell['representative_wall_partition_ms'])

if __name__=='__main__':analyze()
