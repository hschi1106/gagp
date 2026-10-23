"""Explicit final freeze from complete TRAINING costs, then fresh formal matrices."""
from argparse import ArgumentParser
import fcntl
import os
from pathlib import Path
import time
import traceback

import forecast_shared as forecast
import formal_shared as formal_dedup
import quality_data
import quality_dedup
import serial


def terminal(path):
    state=serial.read(path)
    assert state['status'] in ('requested_phase_complete','ok'),('prerequisite incomplete',str(path),state['status'])
    for key in ('pid','child_pid'):
        pid=state.get(key);proc=Path(f'/proc/{pid}/stat')
        assert not proc.exists() or proc.read_text().split()[2]=='Z',('prerequisite alive',str(path),pid)
    return state


def main(name,training,timing,preparation,additional):
    record=serial.RAW/(name+'-controller.json');assert not record.exists()
    lease=(serial.RAW/(name+'-controller.lock')).open('a')
    fcntl.flock(lease,fcntl.LOCK_EX | fcntl.LOCK_NB)
    state=dict(pid=os.getpid(),status='checking_complete_training',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
    serial.save(record,state)
    try:
        # This command is intentionally explicit, not a second auto-selector.
        # The caller finishes deciding between admitted development families.
        for prior in (training,timing,preparation,*additional):terminal(serial.RAW/(prior+'-controller.json'))
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            protocol=serial.read(serial.BASE/'protocol.json')
            measured=forecast.calculate(training,timing,preparation)
            cost_path=serial.RAW/(name+'-runtime-forecast.json');assert not cost_path.exists();serial.save(cost_path,measured)
            chosen=measured['choice']
            if chosen is None:
                state.update(status='budget_insufficient',forecast=str(cost_path),finished_epoch=time.time())
                serial.save(record,state);return
            selection=measured['selection'];selected=selection['selected']
            build=serial.read(serial.RAW/selected['build_name']/'manifest.json')
            assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']==selected['binary_sha256']
            for field,digest in measured['cost_evidence_sha256'].items():assert serial.sha(field)==digest
            variants={v:dict(extra=selection['variants'][v]['extra'],
                binary=protocol['baseline_binary'] if v=='baseline' else selected['binary'],
                binary_sha256=protocol['baseline_sha256'] if v=='baseline' else selected['binary_sha256'])
                for v in ('baseline','candidate')}
            for spec in variants.values():assert serial.sha(spec['binary'])==spec['binary_sha256']
            paths=[Path(__file__),serial.BASE/'forecast.py',serial.BASE/'forecast_shared.py',serial.BASE/'runtime_forecast.py',serial.BASE/'freeze_rule.py',
                serial.BASE/'formal_dedup.py',serial.BASE/'formal_shared.py',serial.BASE/'full_population_audit_shared.py',serial.BASE/'full_population_audit_shared_timed.py',serial.BASE/'shared_cpu_reference.py',serial.BASE/'quality_dedup.py',serial.BASE/'quality_data.py',
                serial.BASE/'champion_audit_dedup.py',serial.BASE/'champion_audit.py',
                serial.BASE/'full_population_audit.py',serial.BASE/'full_population_audit_v2.py',
                serial.BASE/'serial.py',serial.BASE/'protocol.json',quality_data.HELPER]
            hashes={str(p):serial.sha(p) for p in paths}
            common=dict(started_epoch=serial.START,deadline_epoch=serial.DEADLINE,frozen_epoch=time.time(),
                settings=protocol['settings'],inputs=protocol['inputs'],tasks=list(serial.PAPER_TASKS),
                variants=variants,resources=protocol['resources'],reserve_s=protocol['reserve_s'],
                harness_sha256=hashes,candidate_selection=selection,runtime_forecast=str(cost_path),
                runtime_forecast_sha256=serial.sha(cost_path),scale_selection=chosen,
                candidate_source_sha256=build['source_sha256'],candidate_source_snapshot=str(Path(build['binary']).parent/'source-delta'))
            speed=dict(common,format='gagp-twofold-speed-freeze-v3',cpu_reference_sharing='exact fresh paired CPU reference; fresh GPU snapshot for every method',populations=[1024,16384],
                generations=chosen['speed_generations'],repetitions=chosen['speed_repetitions'],cases=1024,timeout_s=3600)
            quality=dict(common,format='gagp-twofold-quality-freeze-v1',population=1024,generations=64,
                seeds=chosen['quality_seeds'],data_seed=20261022,train_limit=200,test_limit=1000,
                first_heldout_use='after immutable candidate/scale freeze')
            speed_path=serial.RAW/(name+'-speed-freeze.json');quality_path=serial.RAW/(name+'-quality-freeze.json')
            assert not speed_path.exists() and not quality_path.exists()
            serial.save(speed_path,speed);serial.save(quality_path,quality)
            state.update(status='frozen',speed_freeze=str(speed_path),quality_freeze=str(quality_path),
                speed_freeze_sha256=serial.sha(speed_path),quality_freeze_sha256=serial.sha(quality_path),forecast=str(cost_path))
            serial.save(record,state)
            entry=f"\n- {time.strftime('%Y-%m-%d %H:%M:%S UTC',time.gmtime())}：正式前凍結 {selected['family']}/{selected['variant']}，binary {selected['binary_sha256']}；速度全30兩種P、G{chosen['speed_generations']}／R2，品質全30 P1024／G64、seeds{quality['seeds'][0]}–{quality['seeds'][-1]}。僅用舊TRAINING／runtime／parity選擇；含新CPU核對與品質準備的保守預估{chosen['estimated_total_s']/3600:.3f}h，可用{chosen['available_s']/3600:.3f}h，另留3h報告。詳細{cost_path.relative_to(serial.ROOT)}；新資料尚未建立。\n"
            progress=serial.ROOT/'benchmarks/fixed_asgp/optimization/PROGRESS.md'
            text=progress.read_text();progress.write_text(entry+'\n'+text)
        state['status']='formal_speed';serial.save(record,state)
        formal_dedup.main(speed_path,name+'-speed')
        state['status']='fresh_quality';serial.save(record,state)
        quality_dedup.main(quality_path,name+'-quality')
        state.update(status='requested_phase_complete',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=ArgumentParser(description=__doc__)
    parser.add_argument('--name',required=True);parser.add_argument('--training',default='training11')
    parser.add_argument('--timing',default='training-timing6');parser.add_argument('--preparation',default='preparation-timing4')
    parser.add_argument('--additional-controller',action='append',default=[])
    args=parser.parse_args();main(args.name,args.training,args.timing,args.preparation,args.additional_controller)
