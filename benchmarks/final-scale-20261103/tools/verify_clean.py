"""Rerun GAGP GPU seed-1 P1024 jobs with the cleaned binary (engine profile defaults, no
engine switch set, no diagnostics) and compare exports/histories with the recorded runs.

usage: verify_clean.py MODE (search_gpu_overlap | search_phase_cpu)"""
import fcntl, json, os, subprocess, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c
from analyze_helpers import strip

mode = sys.argv[1]
BIN = c.ROOT / "bins/bench-clean"
rec = c.read(c.ROOT / "record_gpu.json")
s = c.read(c.ROOT / "settings.json")["settings"]
out_dir = c.ROOT / "runs" / f"clean_{mode}"; out_dir.mkdir(parents=True, exist_ok=True)
result_path = c.ROOT / f"verify_clean_{mode}.json"
result = c.read(result_path) if result_path.exists() else {}
for t in c.TASKS:
    ref = rec.get(f"gagp/{t}.s1.p1024")
    if not ref or ref.get("code") != 0 or t in result: continue
    env = {k: v for k, v in os.environ.items() if not k.startswith("GAGP_")}
    env.update(LD_PRELOAD=s["LD_PRELOAD"], MALLOC_CONF=s["MALLOC_CONF"], GAGP_HOST_THREADS="1",
               GAGP_BM_ALIGNED_FITNESS="1", GAGP_BM_GPU_BLOCKSIZE="512", GAGP_BM_SEED="1",
               GAGP_BM_CASE_COUNT=str(c.TRAIN), GAGP_SEARCH_GENERATIONS=str(c.GENERATIONS),
               GAGP_SEARCH_CONFIG=str(c.ROOT / "gagp_final_asgp.json"),
               GAGP_SEARCH_EXPORT=str(out_dir / f"{t}.export.json"), GAGP_SEARCH_EXPORT_BEST_ONLY="1",
               GAGP_SEARCH_HISTORY_EXPORT=str(out_dir / f"{t}.history.json"))
    cmd = [str(BIN), "measure", str(c.source(t, 1)), str(c.prepared(t, 1)), str(c.grammar(t)), "1024", mode, str(out_dir / f"{t}.json")]
    def go():
        with open(out_dir / f"{t}.log", "w") as log:
            return subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=7200).returncode
    if mode == "search_gpu_overlap":
        with c.LOCK.open("a") as lock: fcntl.flock(lock, fcntl.LOCK_EX); code = go()
    else: code = go()
    if code: result[t] = {"code": code}; c.save(result_path, result); print(t, "code", code, flush=True); continue
    same_export = (out_dir / f"{t}.export.json").read_bytes() == Path(ref["export"]).read_bytes()
    same_history = strip(c.read(out_dir / f"{t}.history.json")) == strip(c.read(ref["history"]))
    search_s = c.read(out_dir / f"{t}.json")["evolve_wall_ms"] / 1000
    result[t] = dict(code=0, same_export=same_export, same_history=same_history, search_s=search_s, recorded_s=ref["search_s"])
    c.save(result_path, result); print(t, result[t], flush=True)
ok = [t for t, v in result.items() if v.get("same_export") and v.get("same_history")]
print(f"{mode}: identical {len(ok)}/{len(result)}")
