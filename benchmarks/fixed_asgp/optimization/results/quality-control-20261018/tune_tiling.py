"""Fixed candidates, full cases; serial shared GPU lock; no held-out data."""
import study

stage=study.Stage("tiling-screen",study.RAW / "diagnostic-bench")
for repetition in range(3):
    for task in ("is_sorted","all_equal","count_odds","filter_odd","double_letters"):
        entry,inp=study.old_input(task,0)
        for value in (("auto","1") if repetition % 2==0 else ("1","auto")):
            tag=f"{task}.r{repetition}.tile{value}"
            stage.run(tag,["measure",inp["source"],inp["prepared"],entry["grammar"],1024,"snapshot",stage.out/(tag+".json")],
                {"GAGP_BM_CASE_COUNT":str(entry["split"]["train_count"]),"GAGP_SNAPSHOT_NATIVE_OWNER":"1",
                 "GAGP_GPU_TILED_INITIAL_PAYLOAD":value})
stage.m["status"]="complete";stage.persist()
