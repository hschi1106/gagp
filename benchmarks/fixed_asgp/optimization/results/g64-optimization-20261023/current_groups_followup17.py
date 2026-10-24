"""Build/test exact current-generation groups, then fresh old-TRAINING G64 controls.

This explicit post-delivery development appendix never changes formal0 or uses
heldout labels. Original hard deadline and timing/report cutoff still apply.
"""
from pathlib import Path
import argparse
import fcntl
import math
import os
import shutil
import signal
import subprocess
import time
import traceback

import development_v2
import serial
import settings_followup_v2


def main(name):
    attempt=serial.RAW/name
    source=serial.read(attempt/"source-manifest.json")
    controller=serial.RAW/(name+"-controller.json")
    lease=(serial.RAW/(name+"-controller.lock")).open("a")
    fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
    assert not controller.exists(), "Preserve prior controller; never implicit resume"
    protocol=serial.read(serial.BASE/"protocol.json")
    cutoff=protocol["timed_cutoff_epoch"]
    state=dict(pid=os.getpid(),status="initializing",started_epoch=time.time(),
               deadline_epoch=serial.DEADLINE,timed_cutoff_epoch=cutoff,
               source_manifest_sha256=serial.sha(attempt/"source-manifest.json"),
               harness_sha256={str(p):serial.sha(p) for p in
                   (Path(__file__),serial.BASE/"serial.py",serial.BASE/"development_v2.py",
                    serial.BASE/"settings_followup_v2.py",serial.BASE/"protocol.json")},
               scope="Resumed user-authorized isolated OLD TRAINING development appendix; no formal0 change, new heldout or new ASGP claim")
    serial.save(controller,state)
    build=attempt/"build"
    selected=serial.read(serial.RAW/"final-selection0.json")
    root=Path(source["root"])

    def identities():
        for relative,digest in source["source_sha256"].items():
            assert serial.sha(root/relative)==digest,relative
        for relative,digest in source["original_source_sha256"].items():
            assert serial.sha(serial.ROOT/relative)==digest,"Official source changed: "+relative
        for p,digest in state["harness_sha256"].items():
            assert serial.sha(p)==digest,p

    def command(label,args,seconds):
        state.update(status=label,current_command=list(map(str,args)))
        log=attempt/(label+".log")
        with log.open("x") as output:
            env={k:v for k,v in os.environ.items() if not k.startswith("GAGP_") and k not in ("LD_PRELOAD","MALLOC_CONF")}
            env.update(GAGP_HOST_THREADS="1",GAGP_CUDA_DEVICE="0")
            child=subprocess.Popen(list(map(str,args)),cwd=serial.ROOT,env=env,
                                   stdout=output,stderr=subprocess.STDOUT,start_new_session=True)
            step=dict(command=list(map(str,args)),status="running",pid=child.pid,started_epoch=time.time(),log=str(log))
            state.setdefault("steps",{})[label]=step
            state["child_pid"]=child.pid;serial.save(controller,state)
            try:
                code=child.wait(timeout=max(.1,min(seconds,cutoff-time.time())))
            except subprocess.TimeoutExpired:
                os.killpg(child.pid,signal.SIGTERM)
                try:child.wait(timeout=3)
                except subprocess.TimeoutExpired:os.killpg(child.pid,signal.SIGKILL);child.wait()
                step.update(status="timeout",finished_epoch=time.time());serial.save(controller,state)
                raise RuntimeError(label+" exceeded original budget")
            step.update(status="ok" if code==0 else "failed",returncode=code,finished_epoch=time.time())
            serial.save(controller,state)
            if code:raise RuntimeError(label+" failed; attempt/log preserved, no implicit retry")

    try:
        assert time.time()+3600<cutoff,"Not enough original time for build/tests/development"
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            identities()
            command("configure",["cmake","-S",root/"cpp","-B",build,"-DCMAKE_BUILD_TYPE=Release",
                    "-DGAGP_ENABLE_CUDA=ON","-DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc",
                    "-DCMAKE_CUDA_ARCHITECTURES=86","-DGAGP_BUILD_BENCHMARKS=ON",
                    "-DGAGP_ASGP_SOURCE_DIR=/home/hschi1106/r13921069","-DGAGP_ASGP_LIST_COMBINE_EXTENSION=ON"],180)
            command("build",["cmake","--build",build,"--target","gagp_fixed_asgp_bench",
                    "gagp_test_extended_payload_gpu","gagp_test_fitness_accumulator_gpu",
                    "gagp_test_gpu_phase_population","gagp_test_fitness_cpu_gpu_parity",
                    "gagp_test_fitness_contract","gagp_test_bounded_region_gpu","-j","2"],3600)
            command("native",["ctest","--test-dir",build,"-R",
                    "^(gagp_test_extended_payload_gpu|gagp_test_fitness_accumulator_gpu|gagp_test_gpu_phase_population|gagp_test_fitness_cpu_gpu_parity|gagp_test_fitness_contract|gagp_test_bounded_region_gpu)$",
                    "--output-on-failure"],1800)
            identities()
            shutil.copy2(build/"gagp_fixed_asgp_bench",attempt/"bench")
            state.update(binary=str(attempt/"bench"),binary_sha256=serial.sha(attempt/"bench"),status="native_pass")
            serial.save(controller,state)

        arms={"baseline":{},"selected_control":dict(selected["candidate_extra"]),
              "ordinary":dict(selected["candidate_extra"],GAGP_GPU_CURRENT_FITNESS_GROUPS="0"),
              "current_groups":dict(selected["candidate_extra"],GAGP_GPU_CURRENT_FITNESS_GROUPS="1")}
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            identities()
            assert serial.sha(selected["binary"])==selected["binary_sha256"]
            assert serial.sha(protocol["baseline_binary"])==protocol["baseline_binary_sha256"]
            stages={variant:serial.Stage(name+"-"+variant,
                     protocol["baseline_binary"] if variant=="baseline" else selected["binary"] if variant=="selected_control" else attempt/"bench",
                     settings=protocol["baseline_settings"]) for variant in arms}
            for stage in stages.values():
                stage.m.update(scope=state["scope"],harness_sha256=state["harness_sha256"]);stage.persist()
        rows=[]
        state.update(status="old_training_full30",arms=arms);serial.save(controller,state)
        for index,task in enumerate(protocol["tasks"]):
            assert time.time()<cutoff,"No late timing/sample resumption"
            variants=list(arms);offset=index%len(variants)
            for variant in variants[offset:]+variants[:offset]:
                development_v2.record_search(stages[variant],task,variant,
                                              protocol["development_inputs"][task],arms[variant])
            with serial.GPU_LOCK.open("a") as lock:
                fcntl.flock(lock,fcntl.LOCK_EX)
                compared=settings_followup_v2.compare({v:s.m["jobs"][f"{task}.s0.g64"] for v,s in stages.items()})
                for row in compared:
                    document=serial.read(stages[row["variant"]].m["jobs"][f"{task}.s0.g64"]["output"])
                    row["grouped_active_generations"]=sum("-current-fitness-groups" in g["execution_profile"] for g in document["generations"])
                rows.extend(compared);serial.save(attempt/"comparisons.json",rows)
                assert all(row[k] for row in compared for k in
                           ("final_descriptors_equal","full_final_ast_equal","champions_equal")),"Preserved execution trajectory mismatch"
                state.update(completed_tasks=index+1,current_task=task);serial.save(controller,state)
        means={variant:math.exp(sum(math.log(row["speedup"]) for row in rows if row["variant"]==variant)/30)
               for variant in arms if variant!="baseline"}
        result=dict(scope=state["scope"],tasks=30,population=1024,generations=64,seed=0,repetitions=1,
                    geometric_means_vs_fresh_baseline=means,
                    incremental_vs_fresh_selected_control={v:means[v]/means["selected_control"] for v in ("ordinary","current_groups")},
                    all_recorded_trajectory_comparisons_equal=True,independent_quality=False,
                    formal0_target_still_unmet=True,source_manifest_sha256=state["source_manifest_sha256"],
                    binary_sha256=state["binary_sha256"])
        serial.save(attempt/"result.json",result)
        for stage in stages.values():stage.m.update(status="requested_phase_complete",finished_epoch=time.time());stage.persist()
        state.update(status="development_complete_only",result=result,finished_epoch=time.time())
    except BaseException as error:
        state.update(status="stopped",error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:
        serial.save(controller,state)


def wait_and_prepare(name):
    import select
    import current_groups_implementation17
    record=serial.RAW/(name+'-wait-controller.json')
    lease=(serial.RAW/(name+'-wait-controller.lock')).open('a')
    fcntl.flock(lease,fcntl.LOCK_EX|fcntl.LOCK_NB)
    assert not record.exists()
    protocol=serial.read(serial.BASE/'protocol.json')
    state=dict(pid=os.getpid(),status='waiting_for_actual_prior_exit',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
               waiting_for=['packed-integration15','packed-confirm15'],
               harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'current_groups_implementation17.py')},
               scope='Independent exact current-generation grouping; official81b8a9c base, no packed-frame source dependency, OLD TRAINING only')
    serial.save(record,state)
    try:
        for prior in state['waiting_for']:
            previous=serial.read(serial.RAW/(prior+'-controller.json'))
            if Path(f"/proc/{previous['pid']}").exists():
                fd=os.pidfd_open(previous['pid'])
                try:
                    while not select.select([fd],[],[],min(600,max(.1,protocol['timed_cutoff_epoch']-3600-time.time())))[0]:
                        if time.time()+3600>=protocol['timed_cutoff_epoch']:
                            state.update(status='budget_not_run',finished_epoch=time.time());serial.save(record,state);raise SystemExit(0)
                finally:os.close(fd)
        if time.time()+3600>=protocol['timed_cutoff_epoch']:
            state.update(status='budget_not_run',finished_epoch=time.time());serial.save(record,state);raise SystemExit(0)
        for p,h in state['harness_sha256'].items():assert serial.sha(p)==h,p
        with serial.GPU_LOCK.open('a') as lock:
            fcntl.flock(lock,fcntl.LOCK_EX)
            current_groups_implementation17.create(name)
        state.update(status='source_ready_dispatching_build',finished_epoch=time.time());serial.save(record,state)
    except Exception as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());serial.save(record,state);raise


if __name__=="__main__":
    parser=argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--name",default="current-groups17")
    name=parser.parse_args().name
    wait_and_prepare(name)
    main(name)
