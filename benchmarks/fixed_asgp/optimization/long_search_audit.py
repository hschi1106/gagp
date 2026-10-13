"""Audit selected long-search checkpoints in each implementation's native runtime."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess
import time

ROOT = Path(__file__).resolve().parents[3]


def main():
    p=argparse.ArgumentParser(description=__doc__)
    p.add_argument('--out',type=Path,required=True)
    args=p.parse_args(); out=args.out.resolve()
    manifest=json.loads((out/'manifest.json').read_text())
    assert 'finished_epoch' in manifest, 'Run audits after performance experiments, never concurrently.'
    dest=out/'checkpoint-audit'; dest.mkdir(exist_ok=False)
    ref=json.loads((ROOT/'benchmarks/fixed_asgp/optimization/results/multigen-20261013/native-asgp-audit-manifest.json').read_text())
    binary=Path(ref['binary']); assert hashlib.sha256(binary.read_bytes()).hexdigest()==ref['sha256']
    env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
    env.update(manifest['flags'],GAGP_SNAPSHOT_CPU='1')
    art=ROOT/'logs/fixed-asgp/artifacts'
    summary=dict(binary=str(binary),binary_sha256=ref['sha256'],commands=[],checkpoints=[],started_epoch=time.time())
    # Audit each main and extended run independently, including prefix/checkpoint champions.
    for task in ('house_robber','median'):
        source=json.loads((art/f'{task}.source.json').read_text())
        heldpath=ROOT/f'logs/optimization/native-counted-validation/heldout/{task}.source.json'
        held=json.loads(heldpath.read_text())
        summary.setdefault('heldout_sha256',{})[task]=hashlib.sha256(heldpath.read_bytes()).hexdigest()
        env['GAGP_SNAPSHOT_PENALTY']=str(manifest['audit_penalties'][task])
        for native in (True,False):
            selected=[];unique=[];ids={}
            cells=[c for c in manifest['cells'] if c['status']=='ok' and c['task']==task and (c['mode']=='asgp_1t')==native]
            for c in cells:
                path=Path(c['output']); result=json.loads(path.read_text())
                history=json.loads(path.with_suffix('.history.json').read_text())['history']
                final=json.loads(path.with_suffix('.final.json').read_text())
                last=dict(generation=len(history),elapsed_search_ms=result['evolve_wall_ms'],fitness=result['final_best'])
                last['program' if native else 'ast']=final['best_program'] if native else final['programs'][0]
                allrows=history+[last]
                for count in sorted({v for v in (16,32,64,128,256,len(history)) if v<=len(history)}):
                    # At intermediate checkpoints only completed-generation history is available.
                    # At run end include the actually performed final evaluation.
                    eligible=history[:count] if count<len(history) else allrows
                    best=max(eligible,key=lambda r:r['fitness'])
                    payload=best['program' if native else 'ast']
                    key=json.dumps(payload,sort_keys=True,separators=(',',':'))
                    if key not in ids: ids[key]=len(unique);unique.append(payload)
                    selected.append(dict(tag=c['tag'],task=task,mode=c['mode'],seed=c['seed'],
                        completed_generations=count,includes_final_eval=count==len(history),
                        selected_generation=best['generation'],training_recorded=best['fitness'],
                        program_index=ids[key],selection='training best-so-far, earliest tie'))
            if not unique:continue
            prefix=f'{task}.asgp' if native else f'{task}.gagp'
            prepared=json.loads((art/f'{task}.p1024.prepared.json').read_text())
            if not native:prepared['programs']=unique
            pp=dest/f'{prefix}.prepared.json'; pp.write_text(json.dumps(prepared)+'\n')
            scores={}
            for split,s in [('train',source),('heldout',held)]:
                s=dict(s,population=unique if native else [source['population'][0]]*len(unique))
                sp=dest/f'{prefix}.{split}.source.json';sp.write_text(json.dumps(s)+'\n')
                target=dest/f'{prefix}.{split}.json'
                cmd=[str(binary),'measure',str(sp),str(pp),str(art/f'{task}.grammar.json'),str(len(unique)),
                     'asgp_snapshot' if native else 'snapshot',str(target)]
                begin=time.monotonic()
                with (dest/f'{prefix}.{split}.log').open('w') as f:
                    subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=180)
                summary['commands'].append(dict(command=cmd,seconds=time.monotonic()-begin))
                scores[split]=json.loads(target.read_text())['programs']
                assert len(scores[split])==len(unique)
            for r in selected:
                a=scores['train'][r['program_index']];b=scores['heldout'][r['program_index']]
                assert a['cases']==b['cases']==1024
                assert a['fitness']==r['training_recorded'],(r,a)
                r.update(training_rechecked=a['fitness'],heldout_fitness=b['fitness'],nodes=a['nodes'])
                if not native:
                    assert a['unscored']==b['unscored']==0
                    assert a['fitness']==a['cpu_fitness'] and b['fitness']==b['cpu_fitness'],(r,a,b)
                    r.update(cpu_gpu_match=True,heldout_errors=b['errors'],heldout_timeouts=b['timeouts'])
                summary['checkpoints'].append(r)
            summary.setdefault('unique_programs',{})[prefix]=len(unique)
            print(prefix,'checkpoints',len(selected),'unique',len(unique),'passed',flush=True)
            (dest/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
    summary['elapsed_seconds']=time.time()-summary['started_epoch']
    (dest/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')

if __name__=='__main__':main()
