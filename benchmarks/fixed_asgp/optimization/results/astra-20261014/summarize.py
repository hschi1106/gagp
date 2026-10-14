"""Summarize completed Astra manifests; retain raw search and generation times."""
import csv
import json
from pathlib import Path
import statistics as st

ROOT = Path(__file__).resolve().parents[5]
OUT = Path(__file__).resolve().parent
RAW = ROOT/'logs/optimization/astra-20261014'
FROZEN = {'house_robber': 27687.368225, 'median': 5864.915193}
MODES = ['asgp_1t', 'gagp_cpu', 'gpu_eval', 'gpu_repro', 'gpu_overlap']
manifests = [RAW/'final-controls/manifest.json', RAW/'final/gpu-three/manifest.json',
             RAW/'final/gpu-nine/manifest.json']
cells = []
for path in manifests:
    m = json.loads(path.read_text())
    for c in m['cells']:
        assert c['status'] == 'ok'
        if path == manifests[0] and c['mode'] == 'gpu_eval':
            continue  # CPU/ASGP unchanged; GPU-e rerun with final view setting.
        cells.append(dict(c, manifest=str(path), binary_sha256=m['binary_sha256']))
summary = {'frozen_search_asgp_ms': FROZEN, 'rows': [], 'timing':
           'Unprofiled complete search, cold initialization retained. Process time additionally includes parsing/audit/serialization.'}
generation_rows = []
for task in FROZEN:
    for mode in MODES:
        group = [c for c in cells if c['task'] == task and c['mode'] == mode]
        expected = 9 if mode == 'gpu_overlap' else 3
        assert len(group) == expected, (task, mode, len(group))
        samples, stage_rows = [], []
        for c in group:
            data = json.loads(Path(c['output']).read_text())
            gs = data['generations']
            q = max(1, len(gs)//4)
            stages = {k: sum(g[k] for g in gs) for k in
                      ('generation_ms', 'eval_ms', 'repro_ms', 'eval_kernel_ms',
                       'repro_kernel_ms', 'repro_decode_ms', 'pack_ms', 'upload_ms')
                      if all(k in g for g in gs)}
            stages.update({k: data[k] for k in ('initial_admission_ms', 'gpu_init_ms', 'final_eval_ms') if k in data})
            stages.update(early_gen_ms=st.median(g['generation_ms'] for g in gs[:q]),
                          late_gen_ms=st.median(g['generation_ms'] for g in gs[-q:]))
            stage_rows.append(stages)
            samples.append({k:c[k] for k in ('tag','evolve_wall_ms','process_wall_s','final_best','manifest','binary_sha256')})
            for g in gs:
                generation_rows.append(dict(task=task, mode=mode, repeat=c['tag'].split('.r')[-1], **g))
        vals = [x['evolve_wall_ms'] for x in samples]
        summary['rows'].append(dict(task=task, mode=mode, repeats=samples, median_ms=st.median(vals),
            range_ms=[min(vals), max(vals)], process_median_s=st.median(x['process_wall_s'] for x in samples),
            frozen_asgp_ratio=FROZEN[task]/st.median(vals),
            diagnostic_stage_medians_ms={k:st.median(s[k] for s in stage_rows) for k in stage_rows[0]}))
with (OUT/'generation-times.csv').open('w', newline='') as f:
    columns = ['task','mode','repeat','generation'] + sorted(set().union(*(r.keys() for r in generation_rows))-{'task','mode','repeat','generation'})
    w=csv.DictWriter(f, fieldnames=columns); w.writeheader(); w.writerows(generation_rows)
scaling = json.loads((RAW/'final/scaling/manifest.json').read_text())
assert len(scaling['cells']) == 40
summary['scaling'] = [{k:c[k] for k in ('task','population','mode','median_ms','output')} for c in scaling['cells']]
summary['settings_manifest'] = str(RAW/'final/gpu-nine/manifest.json')
final_manifest = json.loads(Path(summary['settings_manifest']).read_text())
summary.update({k:final_manifest[k] for k in ('binary','binary_sha256','source_head','flags','frozen_sha256')})
summary['cpu_control_reuse'] = 'Same archived control binary/settings and unchanged CPU/ASGP implementation; final addition touches GPU view preparation only. GPU-e rerun. Scaling controls all rerun.'
(OUT/'final.json').write_text(json.dumps(summary, indent=2)+'\n')
for r in summary['rows']:
    print(r['task'], r['mode'], round(r['median_ms'],3), round(r['frozen_asgp_ratio'],3))
