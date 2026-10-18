"""A/B the opt-in TupleSummary carrier with identical full populations."""
from pathlib import Path
import study
from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS

stage=study.Stage("tuple-pilot",study.RAW / "tuple-bounded-bench")
for task in PAPER_TASKS:
    entry,inp=study.old_input(task,0)
    tag=task+".snapshot"
    job=stage.run(tag,["measure",inp["source"],inp["prepared"],entry["grammar"],1024,"snapshot",stage.out/(tag+".json")],
        {"GAGP_BM_CASE_COUNT":str(entry["split"]["train_count"]),"GAGP_SNAPSHOT_NATIVE_OWNER":"1",
         "GAGP_SNAPSHOT_CPU":"1","GAGP_GPU_PRIVATE_TUPLE_SCORE":"1"})
    if job["status"]!="ok": raise RuntimeError(tag+" failed")
    doc=study.read(stage.out/(tag+".json"));rows=doc["programs"]
    assert all(study.canonical(r["fitness"])==study.canonical(r["cpu_fitness"]) and r["wrong"]==r["cpu_wrong"] for r in rows)
    original=study.read(study.RAW / "initial" / (task+".snapshot.json"))["programs"]
    assert all(study.canonical(a["fitness"])==study.canonical(b["fitness"]) and a["wrong"]==b["wrong"] for a,b in zip(rows,original))
for task in ("is_sorted","all_equal"):
    entry,inp=study.old_input(task,0)
    for variant,value in (("off","0"),("tuple","1")):
        job=study.search_job(stage,task,0,variant,inp,entry["grammar"],entry["split"]["train_count"],
            extra={"GAGP_GPU_PRIVATE_TUPLE_SCORE":value,"GAGP_SEARCH_CPU_ALL":"1"},full_export=True)
        if job["status"]!="ok": raise RuntimeError(task+variant+" failed")
    before=study.read(stage.out/f"{task}.s0.off.g64.json")
    after=study.read(stage.out/f"{task}.s0.tuple.g64.json")
    assert before["final_population"]==after["final_population"]
    assert [r["best"] for r in before["generations"]]==[r["best"] for r in after["generations"]]
    assert study.read(stage.out/f"{task}.s0.off.g64.export.json")==study.read(stage.out/f"{task}.s0.tuple.g64.export.json")
stage.m["status"]="complete";stage.persist()
