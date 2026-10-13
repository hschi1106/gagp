"""Keep native ASGP quality separate from translated GAGP semantic diagnostics."""
import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess

ROOT=Path(__file__).resolve().parents[3]

def main():
    p=argparse.ArgumentParser(description=__doc__);p.add_argument('--out',type=Path,required=True);a=p.parse_args()
    out=a.out.resolve();dest=out/'native-asgp-audit';dest.mkdir(exist_ok=False)
    m=json.loads((out/'manifest.json').read_text());path=out/'audit/summary.json';audit=json.loads(path.read_text())
    binary=dest/'bench';shutil.copy2(ROOT/'cpp/build/release/gagp_fixed_asgp_bench',binary)
    (dest/'source.patch').write_text(subprocess.check_output(['git','diff','--','cpp'],cwd=ROOT,text=True))
    env={k:v for k,v in os.environ.items() if not k.startswith('GAGP_') and k not in ('LD_PRELOAD','MALLOC_CONF')};env.update(m['flags'])
    identity=dict(binary=str(binary),sha256=hashlib.sha256(binary.read_bytes()).hexdigest(),commands=[],note='Post-search audit only; formal timed binaries unchanged.')
    for task,data in audit.items():
        source=json.loads((out/f'audit/{task}.asgp.source.json').read_text())
        indices={json.dumps(p,sort_keys=True,separators=(',',':')):i for i,p in enumerate(source['population'])}
        sources={'train':source,'heldout':dict(json.loads((ROOT/f'logs/optimization/native-counted-validation/heldout/{task}.source.json').read_text()),population=source['population'])}
        scored={}
        for split,s in sources.items():
            sp=dest/f'{task}.{split}.source.json';sp.write_text(json.dumps(s)+'\n');result=dest/f'{task}.{split}.json'
            cmd=[str(binary),'measure',str(sp),str(out/f'audit/{task}.asgp.prepared.json'),str(ROOT/f'logs/fixed-asgp/artifacts/{task}.grammar.json'),str(len(source['population'])),'asgp_snapshot',str(result)]
            with (dest/f'{task}.{split}.log').open('w') as log:subprocess.run(cmd,env=env,stdout=log,stderr=subprocess.STDOUT,check=True,timeout=60)
            identity['commands'].append(cmd);scored[split]=json.loads(result.read_text())['programs']
        programs={}
        for c in m['cells']:
            if c['status']!='ok' or c['task']!=task or c['mode']!='asgp_1t':continue
            base=Path(c['output']);hs=json.loads(base.with_suffix('.history.json').read_text())['history']
            for h in hs:programs[c['tag'],h['generation']]=indices[json.dumps(h['program'],sort_keys=True,separators=(',',':'))]
            final=json.loads(base.with_suffix('.final.json').read_text())['best_program'];programs[c['tag'],len(hs)]=indices[json.dumps(final,sort_keys=True,separators=(',',':'))]
        for row in data['champions']:
            row['comparison_fitness']=row['common_cpu_fitness']
            if row['mode']=='asgp_1t':
                native=scored['train'][programs[row['tag'],row['generation']]]
                assert native['cases']==1024 and native['fitness']==row['native_fitness']
                row['comparison_fitness']=native['fitness']
        for row in data['heldout']:
            row['comparison_heldout_fitness']=row['heldout_cpu_fitness']
            if row['mode']=='asgp_1t':
                native=scored['heldout'][programs[row['tag'],row['generation']]];assert native['cases']==1024
                row['comparison_heldout_fitness']=native['fitness']
        print(task,'native ASGP audit exact; translated semantics kept as diagnostics',flush=True)
    # Original audit remains immutable; report explicitly consumes this augmentation.
    (dest/'summary.json').write_text(json.dumps(audit,indent=2)+'\n')
    (dest/'manifest.json').write_text(json.dumps(identity,indent=2)+'\n')

if __name__=='__main__':main()
