"""Completed whole30 counter diagnosis, including the older admitted fallback.

Replaces a failed-to-start output-path collision; no searches are repeated.
Scenarios are explicitly unmeasured and never enter headline ratios.
"""
import csv
import fcntl
import os
from pathlib import Path
import time
import traceback

import bottlenecks
import serial
import study_statistics

name='bottleneck-after-stage1-0';out=serial.RAW/name;out.mkdir()
record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_stage1',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
           scope='full30/bothP old TRAINING only; recorded counters, unmeasured scenarios; no new samples')
serial.save(record,state)
try:
    for family in ('stage1','shape1'):
        while True:
            d=serial.read(serial.RAW/(family+'-controller.json'))
            if d['status']=='stopped':raise RuntimeError('incomplete family: '+family)
            proc=Path(f'/proc/{d["pid"]}/stat')
            if d['status']=='requested_phase_complete' and (not proc.exists() or proc.read_text().split()[2]=='Z'):break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('diagnosis prerequisite missed budget')
            time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='analyzing';serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json')
        expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
        choices=[]
        for family,variants in [('stage1',('stage','pool','pool_shape','pool_full128')),('shape1',('shape',))]:
            comparisons=sum([serial.read(serial.RAW/f'{family}-p{p}-comparisons.json') for p in (1024,16384)],[])
            for variant in variants:
                group=[r for r in comparisons if r['variant']==variant]
                assert len(group)==60 and {(r['task'],r['population']) for r in group}==expected
                assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal',
                                             'champion_ast_fitness_equal')) for r in group)
                choices.append(dict(family=family,variant=variant,
                                    geomean=study_statistics.geometric_mean(r['speedup'] for r in group)))
        chosen=max(choices,key=lambda r:r['geomean']);rows=[];hashes={}
        for population in (1024,16384):
            manifests={v:serial.read(serial.RAW/f'{chosen["family"]}-p{population}-{v}/manifest.json')
                       for v in ('baseline',chosen['variant'])}
            for task in protocol['tasks']:
                parts={}
                for variant,manifest in manifests.items():
                    job=manifest['jobs'][f'{task}.p{population}.s0.g8'];assert job['status']=='ok'
                    assert serial.sha(job['output'])==job['output_sha256']
                    hashes[job['output']]=job['output_sha256']
                    parts[variant]=bottlenecks.components(serial.read(job['output']))
                base,current=parts['baseline'],parts[chosen['variant']]
                row=dict(task=task,population=population,variant=chosen['variant'],family=chosen['family'],
                         baseline_search_s=base['search_s'],**current)
                for component in ('initial_admission_s','gpu_init_s','eval_s','repro_s','final_eval_export_s'):
                    value=current[component];wall=current['search_s']
                    row[component+'_share']=value/wall
                    row[component+'_halved_scenario_speedup']=(base['search_s']/(wall-value/2)
                        if 0<=value<=wall else None)
                rows.append(row)
        with (out/'components.csv').open('x',newline='') as stream:
            fields=list(dict.fromkeys(k for r in rows for k in r));writer=csv.DictWriter(stream,fieldnames=fields)
            writer.writeheader();writer.writerows(rows)
        sensitivity={}
        for component in ('initial_admission_s','gpu_init_s','eval_s','repro_s','final_eval_export_s'):
            values=[r[component+'_halved_scenario_speedup'] for r in rows]
            sensitivity[component]=dict(mean_share_per_cell=sum(r[component+'_share'] for r in rows)/60,
                halved_component_scenario_geomean=study_statistics.geometric_mean(values)
                if all(v is not None for v in values) else None,
                scope='unmeasured additive scenario; no hard bound, no actual optimization gain')
        state.update(status='ok',chosen=chosen,considered=choices,rows=rows,sensitivity=sensitivity,
                     input_sha256=hashes,components_csv_sha256=serial.sha(out/'components.csv'),
                     source_sha256={str(p):serial.sha(p) for p in [Path(__file__),serial.BASE/'bottlenecks.py']},
                     finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
