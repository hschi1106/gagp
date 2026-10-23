"""Read complete development cost grids; no fresh data or settings are created."""
import csv
import math
from pathlib import Path
import time

import freeze_rule
import runtime_forecast
import serial


def calculate(training='training9',timing='training-timing4',preparation='preparation-timing2',now=None):
    now=time.time() if now is None else now
    protocol=serial.read(serial.BASE/'protocol.json');tasks=protocol['tasks']
    selection=serial.read(serial.RAW/(training+'-development-selection.json'))
    selected=selection['selected'];family=selected['stage_family'];variant=selected['variant']
    manifests={v:serial.read(serial.RAW/(training+'-'+v)/'manifest.json') for v in ('baseline','candidate')}
    audits={v:serial.read(serial.RAW/(training+'-'+v+'-native')/'manifest.json') for v in ('baseline','candidate')}
    rows=[];evidence={}
    for v in ('baseline','candidate'):
        for task in tasks:
            job=manifests[v]['jobs'][task+'.s0.g64'];audit=audits[v]['jobs'][f'{task}.s0.{v}.train.native']
            assert job['status']==audit['status']=='ok'
            rows.append(dict(job,native_train_s=audit['process_s']))
    quality=runtime_forecast.quality_seed_cost(rows,serial.read(serial.RAW/(timing+'-rows.json')),
                                             serial.read(serial.RAW/(preparation+'-rows.json')),tasks)
    histories={};processes={}
    for p in (1024,16384):
        for v,label in [('baseline','baseline'),(variant,'candidate')]:
            path=serial.RAW/f'{family}-p{p}-{v}/manifest.json'
            manifest=serial.read(path)
            assert manifest['status']=='requested_phase_complete'
            for task in tasks:
                job=manifest['jobs'][f'{task}.p{p}.s0.g8'];assert job['status']=='ok'
                assert serial.sha(job['output'])==job['output_sha256']
                histories[task,p,label]=serial.read(job['output']);processes[task,p,label]=job['process_s']
            evidence[str(path)]=serial.sha(path)
    historical=serial.ROOT/'benchmarks/fixed_asgp/optimization/results/scaling-optimization-20261018/final-v3/runs.csv'
    with historical.open(newline='') as stream:old=list(csv.DictReader(stream))
    speed={};breakdown={}
    for generations in (8,16,32):
        search=2*math.fsum(runtime_forecast.project_search(histories[key],processes[key],generations) for key in histories)
        native=runtime_forecast.full_audit_cost(old,tasks,(1024,16384),generations)
        # Construction, GPU snapshot verification and JSON postprocessing were
        # not included in historical CPU kernel timing. Keep an explicit floor.
        native_overhead=max(600.,native*.15)
        speed[generations]=search+native+native_overhead
        breakdown[generations]=dict(search_process_s=search,fresh_full_native_cpu_forecast_s=native,
            fresh_native_construction_gpu_postprocess_floor_s=native_overhead)
    for suffix in [training+'-development-selection.json',timing+'-rows.json',preparation+'-rows.json']:
        path=serial.RAW/suffix;evidence[str(path)]=serial.sha(path)
    evidence[str(historical)]=serial.sha(historical)
    available=serial.DEADLINE-now-protocol['reserve_s']
    # Reserve real all-target Release/regression work separately from the3h
    # report window and the rule's preparation floor.
    fixed=1800.
    chosen=freeze_rule.choose_scale(available,speed,quality['total_s'],fixed_s=fixed)
    return dict(format='gagp-twofold-runtime-forecast-v1',epoch=now,deadline_epoch=serial.DEADLINE,
        started_epoch=serial.START,report_reserve_s=protocol['reserve_s'],available_s=available,
        training=training,timing=timing,preparation=preparation,selection=selection,
        quality_per_seed=quality,speed_costs_s=speed,speed_cost_breakdown=breakdown,
        final_regression_and_fixed_floor_s=fixed,choice=chosen,cost_evidence_sha256=evidence,
        historical_cost_scope='old CPU costs forecast only; every formal search and native audit must be fresh')


if __name__=='__main__':
    from argparse import ArgumentParser
    import fcntl
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--training',default='training9');parser.add_argument('--timing',default='training-timing4')
    parser.add_argument('--preparation',default='preparation-timing2');parser.add_argument('--out',type=Path,required=True)
    args=parser.parse_args();assert not args.out.exists()
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        serial.save(args.out,calculate(args.training,args.timing,args.preparation))
