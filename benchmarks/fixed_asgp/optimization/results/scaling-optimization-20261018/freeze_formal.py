"""Freeze common formal scale from runtime/provenance only; never launch or score searches."""
from argparse import ArgumentParser
import csv
import fcntl
import json
import os
import shlex
import subprocess
from pathlib import Path
import time
import serial
from development import alive, gpu_lock, source_record


def choose_scale(estimates, now, deadline, reserve):
    available=deadline-now-reserve
    return next((row for row in estimates if row['forecast_s']<=available),None)


def forecast(rows, full, pilot, development, scales):
    historical={(r['task'],int(r['population'])):r for r in rows}
    task_names=list(full['results'])
    source_ratios=[];gpu_ratios=[];audit_ratios=[]
    for task in ('is_sorted','all_equal','house_robber'):
        js={v:pilot['jobs'][f'{task}.p16384.{v}.s0.g8'] for v in ('asgp','gpu_base','gpu_exec')}
        old=historical[task,16384];old_full=full['results'][task]
        source_ratios.append(js['asgp']['search_s']/float(old['asgp_s']))
        gpu_ratios.append(js['gpu_base']['search_s']/float(old['gpu_s']))
        base=max(.001,old_full['process_s']-old_full['evolve_wall_s'])
        audit_ratios.append(max(js[v]['process_s']-js[v]['search_s'] for v in ('gpu_base','gpu_exec'))/base)
    calibration=dict(asgp=max(1,*source_ratios),gpu=max(1,*gpu_ratios),
                     full_audit=max(1,*audit_ratios))
    overhead={}
    for population in (1024,16384):
        observations=[j['process_s']-j['search_s'] for j in development['jobs'].values()
                      if j['status']=='ok' and j['population']==population and j.get('compact_root')==0]
        overhead[population]=max([0,*observations])
    estimates=[]
    for generations,repetitions in scales:
        search_factor={16:2.25,8:1,4:.65}[generations]
        # G16 final populations can also be more expensive to CPU audit than G8.
        full_factor=1.5 if generations==16 else 1
        components=dict(asgp_search_s=0,gpu_original_search_s=0,gpu_candidate_search_s=0,
                        process_overhead_s=0,full_cpu_audit_and_export_s=0,input_hash_s=0)
        cells=[]
        for task in task_names:
            for population in (1024,16384):
                old=historical[task,population];f=full['results'][task]
                source=float(old['asgp_s'])*calibration['asgp']*search_factor
                gpu=float(old['gpu_s'])*calibration['gpu']*search_factor
                source_overhead=max(3,float(old['asgp_process_s'])-float(old['asgp_s']))
                gpu_overhead=max(overhead[population],float(old['gpu_process_s'])-float(old['gpu_s']))
                audit=max(0,f['process_s']-f['evolve_wall_s'])*population/16384*calibration['full_audit']*full_factor
                components['asgp_search_s']+=source*repetitions
                components['gpu_original_search_s']+=gpu*repetitions
                components['gpu_candidate_search_s']+=gpu*repetitions  # Assume no candidate gain.
                components['process_overhead_s']+=(source_overhead+2*gpu_overhead)*repetitions
                components['full_cpu_audit_and_export_s']+=2*audit
                components['input_hash_s']+=3*repetitions
                cells.append(dict(task=task,population=population,asgp_search_s=source,
                                  each_gpu_search_s=gpu,each_gpu_full_audit_extra_s=audit))
        raw=sum(components.values())
        estimates.append(dict(generations=generations,repetitions=repetitions,planned_runs=len(cells)*3*repetitions,
                              components=components,unmargined_s=raw,forecast_s=raw*1.2,cells=cells))
    return calibration,estimates


def dependency_provenance(external=None):
    external=Path(external or '/home/hschi1106/r13921069')
    tracked=subprocess.check_output(['rg','--files',str(external/'src')],
                                    cwd=serial.ROOT,text=True).splitlines()
    sources=[Path(p) for p in tracked if Path(p).suffix in ('.cc','.cpp','.h','.hpp') or Path(p).name=='CMakeLists.txt']
    build=serial.ROOT/'cpp/build/release'
    sources.extend(build/'asgp-list-combine-v1'/p for p in
                   ('lang/grammar.cc','eval/interpreter.cc','lang/printer.cc'))
    link_path=build/'CMakeFiles/gagp_fixed_asgp_bench.dir/link.txt'
    linked=[]
    for token in shlex.split(link_path.read_text()):
        if token.endswith(('.a','.o')):
            path=Path(token)
            linked.append(path if path.is_absolute() else build/path)
    linked.extend((link_path,build/'CMakeCache.txt'))
    allocator=serial.read(serial.BASE/'protocol.json')['settings']['LD_PRELOAD']
    linked.append(Path(allocator))
    revision=subprocess.run(['git','-C',str(external),'rev-parse','HEAD'],
                            cwd=serial.ROOT,text=True,capture_output=True)
    return dict(external_source_sha256={str(p):serial.sha(p) for p in sources},
                linked_input_sha256={str(p):serial.sha(p) for p in linked},
                external_asgp_revision=revision.stdout.strip() if revision.returncode==0 else None,
                external_asgp_source_kind='git_checkout' if revision.returncode==0 else 'source_directory')


def write_new_freeze(target, frozen):
    with target.open('x') as stream:
        stream.write(json.dumps(frozen, indent=2)+'\n')


def main():
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--binary-provenance',type=Path,required=True)
    parser.add_argument('--attempt',type=int,default=0)
    args=parser.parse_args()
    guard=(serial.RAW/'formal-freeze.lock').open('a');fcntl.flock(guard,fcntl.LOCK_EX|fcntl.LOCK_NB)
    target=serial.RAW/'formal-freeze.json';assert not target.exists(),'freeze exists, do not overwrite'
    for p in serial.RAW.glob('*controller.json'):
        c=serial.read(p)
        assert c['pid']==os.getpid() or not alive(c['pid']),f'active controller: {p}'
    with gpu_lock():
        policy=serial.read(serial.BASE/'allocation-policy.json')
        protocol=serial.read(serial.BASE/'protocol.json')
        selection=serial.read(serial.RAW/'compact-root-selection.json')
        assert selection['formal_freeze'] is False
        chosen=bool(selection['selected_compact_root'])
        provenance=serial.read(args.binary_provenance)
        binary=args.binary_provenance.resolve().parent/'bench'
        assert provenance['status']=='ok' and serial.sha(binary)==provenance['binary_sha256']
        assert source_record()==provenance['source_sha256'],'current C++ source does not match selected binary build'
        if chosen:assert provenance['binary_sha256']==selection['binary_sha256'] and selection['valid']
        oldspeed=serial.ROOT/'benchmarks/fixed_asgp/optimization/results/day24-20261017/final/speed.csv'
        oldfull=serial.ROOT/'logs/nodes2048-searches-20261016/manifest.json'
        pilot_path=serial.RAW/'pilot16384/manifest.json'
        dev_path=serial.RAW/'compact-root-development/manifest.json'
        day24_path=serial.ROOT/'logs/day24-20261017/manifest.json'
        rows=list(csv.DictReader(oldspeed.open()))
        full,pilot,dev,day24=map(serial.read,(oldfull,pilot_path,dev_path,day24_path))
        assert len(full['results'])==30 and len(rows)==150
        assert len(pilot['jobs'])==9 and all(j['status']=='ok' for j in pilot['jobs'].values())
        assert len(dev['jobs'])==40 and all(j['status']=='ok' for j in dev['jobs'].values())
        tasks=list(serial.PAPER_TASKS)
        assert set(tasks)==set(full['results']) and len(tasks)==30
        for task in tasks:
            item=day24['inputs'][task]
            assert item['source_sha256']==full['results'][task]['inputs']['source_sha256']
            pairs=[(item['source'],item['source_sha256']),(item['grammar'],item['grammar_sha256'])]
            pairs.extend((item['prefixes'][str(pop)]['prepared'],item['prefixes'][str(pop)]['sha256'])
                         for pop in (1024,16384))
            for path,digest in pairs:assert serial.sha(path)==digest,'frozen input changed: '+path
        calibration,estimates=forecast(rows,full,pilot,dev,policy['candidate_scales_in_priority_order'])
        selected=choose_scale(estimates,time.time(),serial.DEADLINE,policy['reserve_s'])
        forecast_path=serial.RAW/('formal-time-forecast.json' if args.attempt==0 else f'formal-time-forecast-attempt{args.attempt}.json')
        assert not forecast_path.exists(),'forecast attempt exists; preserve it'
        serial.save(forecast_path,dict(calibration=calibration,estimates=estimates,
            selected=selected,computed_epoch=time.time(),deadline_epoch=serial.DEADLINE,
            data_purpose='Historical timings allocation only; every formal denominator is freshly measured'))
        assert selected is not None,'no full matrix fits fixed window and reserve'
        harness=[serial.BASE/name for name in ('formal.py','serial.py','development.py','pilot.py',
                  'freeze_formal.py','formal_pipeline.py','test_freeze_formal.py','test_formal.py','test_serial.py',
                  'protocol.json','allocation-policy.json','compact-root-plan.json')]
        frozen=dict(format='gagp-formal-scaling-freeze-v1',frozen_epoch=time.time(),started_epoch=serial.START,
            deadline_epoch=serial.DEADLINE,tasks=tasks,populations=[1024,16384],cases=1024,seed=0,
            generations=selected['generations'],repetitions=selected['repetitions'],planned_runs=selected['planned_runs'],
            gpu_blocksize=512,compact_root_vm=chosen,binary=str(binary),binary_sha256=provenance['binary_sha256'],
            source_sha256=provenance['source_sha256'],settings=protocol['settings'],inputs=day24['inputs'],
            reserve_s=policy['reserve_s'],timeout_s=5400,forecast_s=selected['forecast_s'],forecast_path=str(forecast_path),
            calibration=calibration,selection_sha256=serial.sha(serial.RAW/'compact-root-selection.json'),
            harness_sha256={str(p):serial.sha(p) for p in harness},
            allocation_inputs_sha256={str(p):serial.sha(p) for p in
                (oldspeed,oldfull,pilot_path,dev_path,day24_path,args.binary_provenance.resolve())},
            historical_denominators_used=False,note='CPU1T,GPU eval+reproduction,16GiB,physical2048/depth24/phase1024; no operator/CPUguard policies')
        frozen.update(dependency_provenance())
        write_new_freeze(target, frozen)
        print(dict(generations=frozen['generations'],repetitions=frozen['repetitions'],
                   compact_root=chosen,planned_runs=frozen['planned_runs'],forecast_h=selected['forecast_s']/3600),flush=True)


if __name__=='__main__':main()

