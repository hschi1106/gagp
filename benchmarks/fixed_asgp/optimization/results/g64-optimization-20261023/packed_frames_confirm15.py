"""Conditional additional OLD TRAINING seed/native confirmation; never heldout."""
from pathlib import Path
import argparse
import fcntl
import math
import os
import select
import time
import traceback

import champion_audit
import serial
import settings_followup_v2
import training_confirm


def main(prior_name,name):
    record=serial.RAW/(name+"-controller.json")
    lease=(serial.RAW/(name+"-controller.lock")).open("a")
    fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
    assert not record.exists()
    protocol=serial.read(serial.BASE/"protocol.json")
    state=dict(pid=os.getpid(),status="waiting_for_actual_prior_exit",prior=prior_name,
        started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
        selection_rule="Complete full30 equality plus >=1.02x incremental GM over fresh selected_control; one global AoS/SoA choice from OLD TRAINING only",
        scope="OLD TRAINING seed0/1/2, training-native65champions each; no independent heldout quality or new formal ASGP result",
        harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/"serial.py",
            serial.BASE/"champion_audit.py",serial.BASE/"training_confirm.py",serial.BASE/"settings_followup_v2.py")})
    serial.save(record,state)
    try:
        prior_path=serial.RAW/(prior_name+"-controller.json")
        prior=serial.read(prior_path)
        if Path(f"/proc/{prior['pid']}").exists():
            fd=os.pidfd_open(prior["pid"])
            try:
                while not select.select([fd],[],[],min(600,max(.1,protocol['timed_cutoff_epoch']-time.time())))[0]:
                    assert time.time()<protocol['timed_cutoff_epoch'],"Original timing cutoff while waiting"
            finally:os.close(fd)
        prior=serial.read(prior_path)
        if prior['status']!='development_complete_only':
            state.update(status="not_run_prerequisite_failed",prior_status=prior['status'],finished_epoch=time.time());return
        result=prior['result']
        winner=max(('packed_aos','packed_soa'),key=lambda v:result['incremental_vs_fresh_selected_control'][v])
        improvement=result['incremental_vs_fresh_selected_control'][winner]
        if improvement<1.02:
            state.update(status="not_run_incremental_gain_below2pct",winner=winner,incremental=improvement,finished_epoch=time.time());return
        assert time.time()+1800<protocol['timed_cutoff_epoch'],"Keep original runtime/report reserve"
        state.update(status="training_confirmation",winner=winner,selected_extra=prior['arms'][winner],
                     binary=prior['binary'],binary_sha256=prior['binary_sha256'],selection_epoch=time.time())
        serial.save(record,state)
        frozen=serial.read(serial.RAW/'final-selection0.json')
        arms={'baseline':{},'selected_control':frozen['candidate_extra'],'candidate':prior['arms'][winner]}
        freeze=dict(reserve_s=14400,variants={v:dict(extra=e) for v,e in arms.items()})
        binaries={'baseline':protocol['baseline_binary'],'selected_control':frozen['binary'],'candidate':prior['binary']}
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            assert serial.sha(prior['binary'])==prior['binary_sha256']
            stages={v:serial.Stage(name+'-'+v,binaries[v],settings=protocol['baseline_settings']) for v in arms}
            audits={v:serial.Stage(name+'-'+v+'-native',binaries[v],settings=protocol['baseline_settings']) for v in arms}
            old=serial.read(serial.ROOT/'logs/day24-20261017/manifest.json')
            source_stages={v:serial.read(serial.RAW/(prior_name+'-'+(winner if v=='candidate' else v))/'manifest.json') for v in arms}
        comparisons=[];native_calls=0
        for seed in (0,1,2):
            for index,task in enumerate(protocol['tasks']):
                assert time.time()<protocol['timed_cutoff_epoch']
                original=protocol['development_inputs'][task]
                if seed==0:
                    entry={'source':original['source'],'prepared':original['prepared']}
                    grammar=original['grammar'];count=original['train_count']
                else:
                    inp=old['inputs'][task];entry=inp['quality'][str(seed)];grammar=inp['grammar'];count=inp['split']['train_count']
                inputs=dict(metadata=dict(train_count=count),grammar=grammar,seeds={str(seed):entry})
                jobs={}
                order=list(arms);offset=(seed+index)%len(order)
                for v in order[offset:]+order[:offset]:
                    if seed==0:
                        jobs[v]=dict(source_stages[v]['jobs'][f'{task}.s0.g64'],variant=v)
                    else:
                        jobs[v]=training_confirm.search(stages[v],task,seed,v,entry,grammar,count,arms[v])
                    audited=champion_audit.run(audits[v],jobs[v],'train',inputs,freeze)
                    assert audited['status']=='ok' and audited.get('native_fidelity_pass'),"Native training mismatch preserved"
                    native_calls+=1
                with serial.GPU_LOCK.open('a') as lock:
                    fcntl.flock(lock,fcntl.LOCK_EX)
                    rows=settings_followup_v2.compare(jobs)
                    for row in rows:row['seed']=seed
                    comparisons.extend(rows);serial.save(serial.RAW/(name+'-comparisons.json'),comparisons)
                    assert all(r[k] for r in rows for k in ('final_descriptors_equal','full_final_ast_equal','champions_equal'))
                state.update(completed_seed=seed,current_task=task,native_calls=native_calls);serial.save(record,state)
        means={v:math.exp(sum(math.log(r['speedup']) for r in comparisons if r['variant']==v)/90) for v in ('selected_control','candidate')}
        state.update(status='development_confirmed_only',finished_epoch=time.time(),tasks=30,seeds=[0,1,2],
                     geometric_means_vs_fresh_baseline=means,incremental_vs_fresh_selected=means['candidate']/means['selected_control'],
                     native_calls=native_calls,independent_heldout=False,formal0_target_still_unmet=True)
        for stage in [*stages.values(),*audits.values()]:stage.m.update(status='requested_phase_complete',finished_epoch=time.time());stage.persist()
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
    finally:serial.save(record,state)


if __name__=='__main__':
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prior',default='packed-integration15')
    parser.add_argument('--name',default='packed-confirm15')
    args=parser.parse_args();main(args.prior,args.name)
