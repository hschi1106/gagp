"""Full-case independent CPU/GPU audit of native-selected search champions."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import subprocess

ROOT = Path(__file__).resolve().parents[3]

def main():
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--out', type=Path, required=True)
    parser.add_argument('--base',type=Path,help='Original three-task archive for a new combined analysis')
    parser.add_argument('--aligned-median',type=Path,help='Median exact-objective archive')
    args=parser.parse_args();os.chdir(ROOT);out=args.out.resolve();out.mkdir(parents=True,exist_ok=True)
    if args.base or args.aligned_median:
        assert args.base and args.aligned_median and not (out/'manifest.json').exists()
        base=json.loads((args.base/'manifest.json').read_text());aligned=json.loads((args.aligned_median/'manifest.json').read_text())
        merged=dict(aligned,cells=[],tasks=['sum_of_elements','house_robber','median'],source_manifests=[str((p/'manifest.json').resolve()) for p in (args.base,args.aligned_median)],audit_penalties=dict(sum_of_elements=1000,house_robber=1000,median=1))
        for source,is_aligned in ((base,False),(aligned,True)):
            for c in source['cells']:
                if c['tag'].startswith('smoke.'):continue
                cell=dict(c,binary_sha256=source['binary_sha256'])
                if not is_aligned and c['task']=='median':cell['tag']='legacy_'+cell['tag']
                merged['cells'].append(cell)
        (out/'manifest.json').write_text(json.dumps(merged,indent=2)+'\n')
    dest=out/'audit';dest.mkdir(exist_ok=True)
    m=json.loads((out/'manifest.json').read_text());art=ROOT/'logs/fixed-asgp/artifacts'
    env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')}
    env.update(m['flags'],GAGP_SNAPSHOT_CPU='1')
    summary={}
    def execute(cmd, log):
        with log.open('w') as f:subprocess.run(cmd,env=env,stdout=f,stderr=subprocess.STDOUT,check=True,timeout=300)
    for task in ('sum_of_elements','house_robber','median'):
        selected=[];asts=[];asgp=[];asgp_positions=[];asgp_ids={}
        penalty=m.get('audit_penalties',{}).get(task,1000)
        env['GAGP_SNAPSHOT_PENALTY']=str(penalty)
        cells=[c for c in m['cells'] if c['status']=='ok' and c['task']==task and not c['tag'].startswith('smoke.')]
        for c in cells:
            tag=c['tag'];base=Path(c['output']);result=json.loads(base.read_text());history=json.loads(base.with_suffix('.history.json').read_text())['history']
            final=json.loads(base.with_suffix('.final.json').read_text())
            last=dict(generation=len(history),elapsed_search_ms=result['evolve_wall_ms'],fitness=result['final_best'])
            if c['mode']=='asgp_1t':last['program']=final['best_program']
            else:last['ast']=final['programs'][0]
            for h in history+[last]:
                row=dict(tag=tag,task=task,mode=c['mode'],seed=c['seed'],generation=h['generation'],
                         elapsed_search_ms=h['elapsed_search_ms'],native_fitness=h['fitness'],final=h is last,
                         native_objective_aligned=c['mode']=='asgp_1t' or float(c['flags'].get('GAGP_SEARCH_PENALTY',1000))==penalty)
                selected.append(row)
                if 'program' in h:
                    key=json.dumps(h['program'],sort_keys=True,separators=(',',':'))
                    if key not in asgp_ids:asgp_ids[key]=len(asgp);asgp.append(h['program'])
                    asgp_positions.append((len(asts),asgp_ids[key]));asts.append(None)
                else:asts.append(h['ast'])
        source=json.loads((art/f'{task}.source.json').read_text())
        # New derived artifacts only; frozen parents and grammar are never prepared again.
        sp=dest/f'{task}.asgp.source.json';pp=dest/f'{task}.asgp.prepared.json'
        assert not sp.exists() and not pp.exists()
        sp.write_text(json.dumps(dict(source,population=asgp))+'\n')
        command=[m['binary'],'prepare',str(sp),str(art/f'{task}.grammar.json'),str(len(asgp)),str(pp)]
        execute(command,dest/f'{task}.prepare.log')
        translated=json.loads(pp.read_text())['programs']
        for index,which in asgp_positions:asts[index]=translated[which]
        assert all(a is not None for a in asts)
        unique=[];identity={}
        for row,ast in zip(selected,asts):
            key=json.dumps(ast,sort_keys=True,separators=(',',':'))
            if key not in identity:identity[key]=len(unique);unique.append(ast)
            row['audit_program']=identity[key]
        prepared=json.loads((art/f'{task}.p1024.prepared.json').read_text());prepared['programs']=unique
        prepared_path=dest/f'{task}.champions.prepared.json';prepared_path.write_text(json.dumps(prepared)+'\n')
        score_source=dict(source,population=[source['population'][0]]*len(unique))
        train=dest/f'{task}.train.source.json';train.write_text(json.dumps(score_source)+'\n')
        snap=dest/f'{task}.train.snapshot.json'
        cmd=[m['binary'],'measure',str(train),str(prepared_path),str(art/f'{task}.grammar.json'),str(len(unique)),'snapshot',str(snap)]
        execute(cmd,dest/f'{task}.train.log')
        rows=json.loads(snap.read_text())['programs'];assert len(rows)==len(unique)
        for row in selected:
            score=rows[row['audit_program']];assert score['cases']==1024 and score['unscored']==0
            row.update(common_cpu_fitness=score['cpu_fitness'],common_gpu_fitness=score['fitness'],
                       cpu_gpu_match=score['cpu_fitness']==score['fitness'],
                       native_common_match=score['cpu_fitness']==row['native_fitness'] if row['native_objective_aligned'] else None,nodes=score['nodes'],
                       gpu_errors=score['errors'],gpu_timeouts=score['timeouts'])
        # Held-out selection uses original training objective, never held-out or post-hoc common score.
        champions=[]
        for c in cells:
            candidates=[r for r in selected if r['tag']==c['tag']]
            champions.append(dict(max(candidates,key=lambda r:r['native_fitness']),heldout_selection='run_best'))
            before=[r for r in candidates if r['elapsed_search_ms']<=10000]
            if before:champions.append(dict(max(before,key=lambda r:r['native_fitness']),heldout_selection='10s'))
        hold_prepared=dict(prepared,programs=[unique[r['audit_program']] for r in champions]);hp=dest/f'{task}.heldout.prepared.json';hp.write_text(json.dumps(hold_prepared)+'\n')
        heldout_source=ROOT/f'logs/optimization/native-counted-validation/heldout/{task}.source.json'
        hs=json.loads(heldout_source.read_text());hs['population']=[source['population'][0]]*len(champions)
        hsp=dest/f'{task}.heldout.source.json';hsp.write_text(json.dumps(hs)+'\n');hr=dest/f'{task}.heldout.snapshot.json'
        cmd=[m['binary'],'measure',str(hsp),str(hp),str(art/f'{task}.grammar.json'),str(len(champions)),'snapshot',str(hr)]
        execute(cmd,dest/f'{task}.heldout.log')
        heldrows=json.loads(hr.read_text())['programs']
        held=[]
        for champion,score in zip(champions,heldrows):
            assert score['cases']==1024 and score['unscored']==0
            held.append(dict(champion,heldout_cpu_fitness=score['cpu_fitness'],heldout_gpu_fitness=score['fitness'],heldout_gpu_errors=score['errors'],heldout_gpu_timeouts=score['timeouts']))
        summary[task]=dict(champions=selected,heldout=held,unique_audited_programs=len(unique),
                           heldout_source_sha256=hashlib.sha256(heldout_source.read_bytes()).hexdigest(),
                           case_penalty=penalty, note='Common CPU capped absolute-error sum on full1024cases. Cap1 equals exact mismatch count for admitted Int output / Int targets. Native-selected champions independently translated/admitted. Deduplication only in post-search audit.')
        (dest/'summary.json').write_text(json.dumps(summary,indent=2)+'\n')
        print(task,'records',len(selected),'unique',len(unique),'GPU/CPU mismatches',sum(not r['cpu_gpu_match'] for r in selected),flush=True)

if __name__=='__main__':main()
