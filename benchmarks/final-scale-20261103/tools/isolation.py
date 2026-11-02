"""Contention calibration: rerun a subset of finished ASGP-1T (and CPU reference) jobs one
at a time with no other experiment load, and compare with their in-pool times.

usage: isolation.py [MAX_SECONDS]
Picks seed-1 P1024 jobs (spread over the range of in-pool times) plus two P4096 jobs,
runs them serially on one pinned core, writes isolation.json."""
import statistics, sys, time
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c
import run as r

budget = float(sys.argv[1]) if len(sys.argv) > 1 else 3600
rec = c.read(c.ROOT / "record_cpu.json")
done = {k: v for k, v in rec.items() if v.get("code") == 0}
pick = sorted((v for v in done.values() if v["arm"] == "asgp" and v["seed"] == 1 and v["pop"] == 1024), key=lambda v: v["search_s"])
chosen = pick[::max(1, len(pick) // 8)][:8]
chosen += sorted((v for v in done.values() if v["arm"] == "asgp" and v["seed"] == 1 and v["pop"] == 4096 and v["search_s"] < 900),
                 key=lambda v: v["search_s"])[-2:]
chosen += [v for v in done.values() if v["arm"] == "cpuref" and v["search_s"] < 600][:2]
out_path = c.ROOT / "isolation.json"
out = c.read(out_path) if out_path.exists() else {}
t_end = time.time() + budget
for v in chosen:
    name = r.name(v)
    if name in out: continue
    if time.time() + v["search_s"] * 1.2 > t_end: continue
    job = {k: v[k] for k in ("arm", "task", "seed", "pop")}
    job["arm"] = v["arm"]
    # run into a separate directory so the recorded outputs stay untouched
    res = r.run(dict(job, arm=job["arm"]), out_root=c.ROOT / "runs" / "isolation")
    if res["code"] != 0: out[name] = {"error": res.get("error")}; continue
    out[name] = dict(pool_s=v["search_s"], alone_s=res["search_s"], ratio=v["search_s"] / res["search_s"],
                     same_champion=Path(res["export"]).read_bytes() == Path(v["export"]).read_bytes())
    c.save(out_path, out)
    print(name, out[name], flush=True)
ratios = [x["ratio"] for x in out.values() if "ratio" in x and x and not x.get("error")]
if ratios:
    out["_summary"] = dict(n=len(ratios), median_pool_over_alone=statistics.median(ratios), min=min(ratios), max=max(ratios))
    c.save(out_path, out); print(out["_summary"])
