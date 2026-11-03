"""Timing repeatability: rerun the seed-1 gagp_clean cells (all P) once more under the GPU lock.

usage: repeat_clean.py   -> runs/repeat/gagp_clean/..., repeat.json"""
import fcntl, sys
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c, run as r

if __name__ == "__main__":
    rec = r.Record(c.ROOT / "repeat.json")
    for p in c.POPS:
        for t in c.TASKS:
            job = dict(arm="gagp_clean", task=t, seed=1, pop=p, timeout=3600)
            if rec.done(job): continue
            with c.LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX); res = r.run(job, c.ROOT / "runs" / "repeat")
            rec.put(res)
    print("queue finished", flush=True)
