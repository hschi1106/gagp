"""Read completed19 full30 timing components; no new samples or heldout use."""
import csv
import fcntl
import os
from pathlib import Path
import time
import traceback

import bottlenecks
import serial
import study_statistics

name='bottleneck-analysis0';out=serial.RAW/name;out.mkdir()
record=out/'manifest.json'
state=dict(pid=os.getpid(),status='waiting_for_stage1',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    scope='completed19 full30 OLD TRAINING diagnosis; additive scenarios are unmeasured; no finalcandidate/heldout selection')
serial.save(record,state)
try:
    while True:
        previous=serial.read(serial.RAW/'stage1-controller.json')
        if previous['status']=='stopped':raise RuntimeError('stage19 incomplete, preserve failures')
        if previous['status']=='requested_phase_complete':
            proc=Path(f'/proc/{previous["pid"]}/stat')
            if not proc.exists() or proc.read_text().split()[2]=='Z':break
        if time.time()>serial.DEADLINE-10800:raise RuntimeError('diagnosis prerequisite missed budget')
        time.sleep(10)
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='reading_completed_timings';serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json')
        comparisons=sum([serial.read(serial.RAW/f'stage1-p{p}-comparisons.json') for p in (1024,16384)],[])
        variants=('stage','pool','pool_shape','pool_full128');means={}
        expected={(task,p) for task in protocol['tasks'] for p in (1024,16384)}
        for v in variants:
            group=[row for row in comparisons if row['variant']==v]
            assert len(group)==60 and {(row['task'],row['population']) for row in group}==expected
            assert all(all(row[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for row in group)
            means[v]=study_statistics.geometric_mean(row['speedup'] for row in group)
        candidate=max(means,key=means.get);rows=[];hashes={}
        for p in (1024,16384):
            stages={v:serial.read(serial.RAW/f'stage1-p{p}-{v}/manifest.json') for v in ('baseline',candidate)}
            for task in protocol['tasks']:
                parts={}
                for v,manifest in stages.items():
                    job=manifest['jobs'][f'{task}.p{p}.s0.g8'];assert job['status']=='ok'
                    assert serial.sha(job['output'])==job['output_sha256'];hashes[job['output']]=job['output_sha256']
                    parts[v]=bottlenecks.components(serial.read(job['output']))
                base,current=parts['baseline'],parts[candidate]
                row=dict(task=task,population=p,variant=candidate,
                    measured_search_speedup=base['search_s']/current['search_s'],baseline_search_s=base['search_s'],**current)
                row.update(bottlenecks.scenario(base,current));rows.append(row)
        target=out/'components.csv'
        with target.open('x',newline='') as stream:
            fields=list(dict.fromkeys(key for row in rows for key in row));writer=csv.DictWriter(stream,fieldnames=fields)
            writer.writeheader();writer.writerows(rows)
        state.update(status='ok',stage19_diagnostic_variant=candidate,stage19_geometric_means=means,
            rows=rows,source_sha256={str(p):serial.sha(p) for p in [Path(__file__),serial.BASE/'bottlenecks.py']},
            input_output_sha256=hashes,components_csv=str(target),components_csv_sha256=serial.sha(target),finished_epoch=time.time())
except BaseException as error:
    state.update(status='failed',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
