"""Summarize and independently compare the initial isolated bloat-cost screen."""
import hashlib
import json
import statistics
from pathlib import Path
ROOT = Path(__file__).resolve().parents[5]
RAW = ROOT/'logs/optimization/bloat-cost-20261014'
FIELDS = ('generation','best','mean','best_nodes','changed','unchanged','rejected','contract_rejections','budget_rejections','generation_rejections','operator_fallbacks','cases','expected_program_cases','errors','timeouts','fallbacks','unscored')
def digest(p):
    return hashlib.sha256(p.read_bytes()).hexdigest()
def main():
    result = {'source_checkpoint':'71dc916','frozen_asgp_ms':{'house_robber':27687.368225,'median':5864.915193},'timing':'evolve_wall_ms, complete cold changing search. Per-generation timers are nested diagnostics; never subtract from this wall total.','variants':[]}
    for variant in ('baseline','off','depth','copy','live','all'):
        path=RAW/variant/'manifest.json'; manifest=json.loads(path.read_text())
        for task in ('house_robber','median'):
            cells=[c for c in manifest['cells'] if c['task']==task]
            assert len(cells)==3 and all(c['status']=='ok' for c in cells)
            times=[c['evolve_wall_ms'] for c in cells]; validation=[]; stages=[]
            for cell in cells:
                tag=cell['tag']; current=json.loads(Path(cell['output']).read_text())
                ref_tag=f"{task}.gpu_overlap.s{cell['seed']}.r0"
                reference=json.loads((RAW/'baseline'/f'{ref_tag}.json').read_text())
                assert [{k:g.get(k) for k in FIELDS} for g in current['generations']]==[{k:g.get(k) for k in FIELDS} for g in reference['generations']], (variant,tag,'semantic trace')
                assert current['final_population']==reference['final_population'], (variant,tag,'final fitness population')
                hashes={}
                for kind in ('final','history'):
                    a=RAW/variant/f'{tag}.{kind}.json';b=RAW/'baseline'/f'{ref_tag}.{kind}.json'
                    hashes[kind]=digest(a)
                    if kind == 'final':
                        assert hashes[kind]==digest(b),(variant,tag,kind)
                    else:
                        # History includes elapsed wall time, which is not semantic identity.
                        def semantic_history(p):
                            return [{k:v for k,v in x.items() if k!='elapsed_search_ms'} for x in json.loads(p.read_text())['history']]
                        assert semantic_history(a)==semantic_history(b),(variant,tag,kind)
                validation.append(dict(tag=tag,exact_semantic_trace=True,sha256=hashes))
                stages.append({k:sum(g.get(k,0) for g in current['generations']) for k in ('eval_ms','eval_kernel_ms','repro_ms','repro_kernel_ms','pack_ms','upload_ms')})
            result['variants'].append(dict(variant=variant,task=task,manifest=str(path),binary_sha256=manifest['binary_sha256'],flags=manifest['flags'],raw_ms=times,median_ms=statistics.median(times),range_ms=[min(times),max(times)],search_ratio=result['frozen_asgp_ms'][task]/statistics.median(times),validation=validation,stage_medians_ms={k:statistics.median(s[k] for s in stages) for k in stages[0]}))
    (Path(__file__).parent/'screen.json').write_text(json.dumps(result,indent=2)+'\n')
    print('All 36 searches have exact semantic traces, final AST bytes and history AST/fitness (elapsed time excluded).')
if __name__=='__main__':main()
