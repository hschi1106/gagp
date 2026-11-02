"""Experiment queues. `gpu`: GAGP GPU runs, one at a time under the GPU lock.
`cpu N DEADLINE_EPOCH`: ASGP-1T and GAGP CPU-reference runs on N single-thread
workers; a job starts only if its estimate fits before the deadline.
Records are resumable: finished jobs (code 0) are skipped."""
import json, subprocess, sys, threading, time, fcntl
from concurrent.futures import ThreadPoolExecutor
from pathlib import Path
sys.path.insert(0, str(Path(__file__).resolve().parent)); import common as c

# ASGP-1T P1024 x 200-case G64 seconds (async-island gpu1), used only for ordering/estimates.
ASGP_P1024_200 = {k.split("|")[1]: v["search_s"] for k, v in
                  c.read(c.HERE / "config/asgp_p1024_200_estimates.json").items()
                  if k.startswith("asgp|") and v.get("code") == 0}
MODE = {"gagp": "search_gpu_overlap", "gagp_gen": "search_gpu_overlap", "gagp_g8": "search_gpu_overlap", "cpuref": "search_phase_cpu",
        "cpuref_g8": "search_phase_cpu", "asgp": "search_asgp_1t"}


def name(job): return f"{job['arm']}/{job['task']}.s{job['seed']}.p{job['pop']}"


def estimate(job):
    if job["arm"] == "asgp": return max(ASGP_P1024_200[job["task"]], 10) * job["pop"] / 1024 * 5.12 * 1.5
    return {"cpuref": 3600, "cpuref_g8": 1800}.get(job["arm"], 600) * job["pop"] / 1024 / (8 if job["arm"] == "cpuref_g8" else 1)


def run(job, out_root=None):
    out = (out_root or c.ROOT / "runs") / job["arm"]; out.mkdir(parents=True, exist_ok=True)
    stem = out / f"{job['task']}.s{job['seed']}.p{job['pop']}"
    env = c.base_env(job["task"])
    g8 = job["arm"].endswith("_g8")
    env.update(GAGP_BM_CASE_COUNT=str(c.TRAIN), GAGP_BM_SEED=str(job["seed"]), GAGP_SEARCH_GENERATIONS="8" if g8 else str(c.GENERATIONS),
               GAGP_SEARCH_EXPORT=f"{stem}.export.json", GAGP_SEARCH_EXPORT_BEST_ONLY="1", GAGP_SEARCH_HISTORY_EXPORT=f"{stem}.history.json")
    if job["arm"] != "asgp":
        env.update(c.ENGINE)
        env["GAGP_SEARCH_CONFIG"] = str(c.CONFIG_GEN if job["arm"] == "gagp_gen" else c.CONFIG_G8 if g8 else c.CONFIG)
    cmd = [str(c.BIN), "measure", str(c.source(job["task"], job["seed"])), str(c.prepared(job["task"], job["seed"])),
           str(c.grammar(job["task"])), str(job["pop"]), MODE[job["arm"]], f"{stem}.json"]
    t0 = time.time()
    try:
        with open(f"{stem}.log", "w") as log:
            code = subprocess.run(cmd, env=env, stdout=log, stderr=subprocess.STDOUT, timeout=job.get("timeout", 4 * 3600)).returncode
    except subprocess.TimeoutExpired: code = "timeout"
    res = dict(job, code=code, process_s=time.time() - t0, started=t0)
    if code == 0:
        d = c.read(f"{stem}.json")
        res.update(search_s=d["evolve_wall_ms"] / 1000, final_best=d["final_best"], profile=d.get("reproduction_profile"),
                   export=f"{stem}.export.json", history=f"{stem}.history.json", output=f"{stem}.json")
    else: res["error"] = Path(f"{stem}.log").read_text()[-800:]
    return res


class Record:
    def __init__(self, path): self.path = path; self.lock = threading.Lock(); self.d = c.read(path) if path.exists() else {}
    def done(self, job): return self.d.get(name(job), {}).get("code") == 0
    def put(self, res):
        with self.lock: self.d[name(res)] = res; c.save(self.path, self.d)
        print(time.strftime("%H:%M:%S"), name(res), res["code"], round(res.get("search_s", res["process_s"]), 1), flush=True)


def gpu_jobs(seeds):
    jobs = []
    for s in seeds:
        for p in c.POPS: jobs += [dict(arm="gagp", task=t, seed=s, pop=p, timeout=3600) for t in c.TASKS]
        if s == 1: jobs += [dict(arm="gagp_g8", task=t, seed=1, pop=p, timeout=3600) for p in (4096, 16384) for t in c.TASKS]
    return jobs


def cpu_jobs(seeds):
    lpt = lambda js: sorted(js, key=estimate, reverse=True)
    asgp = lambda s: lpt([dict(arm="asgp", task=t, seed=s, pop=p) for p in c.POPS for t in c.TASKS])
    jobs = asgp(seeds[0])
    jobs += lpt([dict(arm="cpuref", task=t, seed=1, pop=1024, timeout=3 * 3600) for t in c.TASKS] +
                [dict(arm="cpuref_g8", task=t, seed=1, pop=p, timeout=3 * 3600) for p in (4096, 16384) for t in c.TASKS])
    for s in seeds[1:]: jobs += asgp(s)
    return jobs


if __name__ == "__main__":
    kind = sys.argv[1]
    seeds = [int(x) for x in sys.argv[2].split(",")]
    rec = Record(c.ROOT / f"record_{kind}.json")
    if kind in ("gpu", "gpugen"):
        jobs = gpu_jobs(seeds) if kind == "gpu" else [dict(arm="gagp_gen", task=t, seed=s, pop=p, timeout=3600) for s in seeds for p in c.POPS for t in c.TASKS]
        for job in jobs:
            if rec.done(job) or not c.prepared(job["task"], job["seed"]).exists(): continue
            with c.LOCK.open("a") as lock:
                fcntl.flock(lock, fcntl.LOCK_EX); res = run(job)
            rec.put(res)
    else:
        workers, deadline = int(sys.argv[3]), float(sys.argv[4])
        pending = [j for j in cpu_jobs(seeds) if not rec.done(j)]
        qlock = threading.Lock()
        def worker(_):
            while True:
                with qlock:
                    now = time.time()
                    job = next((j for j in pending if now + estimate(j) < deadline and c.prepared(j["task"], j["seed"]).exists()), None)
                    if job is None: return
                    pending.remove(job)
                rec.put(run(job))
        with ThreadPoolExecutor(workers) as ex: list(ex.map(worker, range(workers)))
    print("queue finished", flush=True)
