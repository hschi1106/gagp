"""Read recorded development counters under the lock; no new search/test data."""
import csv
import fcntl
import os
import time
import traceback

import serial

out=serial.RAW/'bottleneck-analysis0';out.mkdir();record=out/'manifest.json'
state=dict(status='waiting_for_lock',pid=os.getpid(),started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        state['status']='analyzing';serial.save(record,state)
        protocol=serial.read(serial.BASE/'protocol.json');rows=[];input_hashes={}
        for population in (1024,16384):
            manifest=serial.read(serial.RAW/f'simple1-p{population}-baseline/manifest.json')
            assert manifest['binary_sha256']==protocol['baseline_sha256']
            for task in protocol['tasks']:
                job=manifest['jobs'][f'{task}.p{population}.s0.g8'];assert job['status']=='ok'
                path=job['output'];input_hashes[path]=serial.sha(path);assert input_hashes[path]==job['output_sha256']
                data=serial.read(path);generations=data['generations'];wall=data['evolve_wall_ms']/1000
                total=lambda key:sum(g.get(key,0) for g in generations)/1000
                evaluation=total('eval_ms')+data['final_eval_ms']/1000
                row=dict(task=task,population=population,generations=8,search_s=wall,
                    generation_eval_s=total('eval_ms'),final_eval_s=data['final_eval_ms']/1000,
                    generation_eval_kernel_s=total('eval_kernel_ms'),generation_repro_s=total('repro_ms'),
                    generation_repro_kernel_s=total('repro_kernel_ms'),repro_decode_s=total('repro_decode_ms'),
                    pack_s=total('pack_ms'),upload_s=total('upload_ms'),
                    initial_admission_s=data['initial_admission_ms']/1000,gpu_init_s=data['gpu_init_ms']/1000,
                    unchanged_fraction=sum(g['unchanged'] for g in generations)/(population*len(generations)),
                    generation_eval_fraction=total('eval_ms')/wall,generation_repro_fraction=total('repro_ms')/wall,
                    zero_evaluation_counter_estimate=wall/(wall-evaluation) if 0<evaluation<wall else None,
                    profile=generations[0]['execution_profile'],process_s=job['process_s'],cpu_audit_s=data['cpu_audit_ms']/1000)
                rows.append(row)
        serial.save(out/'rows.json',rows)
        with (out/'rows.csv').open('x',newline='') as stream:
            writer=csv.DictWriter(stream,fieldnames=list(rows[0]),lineterminator='\n');writer.writeheader();writer.writerows(rows)
        state.update(status='ok',rows=len(rows),input_sha256=input_hashes,finished_epoch=time.time(),
            scope='old TRAINING, baseline development counters; zero-evaluation ratio is an illustrative counter-based ceiling estimate, not a measured speedup or a formal bound under overlap')
        serial.save(record,state)
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state)
    raise
