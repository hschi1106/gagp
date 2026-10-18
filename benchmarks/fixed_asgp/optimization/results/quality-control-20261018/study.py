"""Serial bounded study utilities and initial same-candidate audit."""
from argparse import ArgumentParser
from datetime import datetime, timezone
import fcntl
import ctypes
import hashlib
import json
import math
import os
from pathlib import Path
import random
import shutil
import signal
import subprocess
import time

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, process_group_rss

ROOT = Path(__file__).resolve().parents[5]
BASE = Path(__file__).resolve().parent
RAW = ROOT / "logs/quality-control-20261018"
START = datetime(2026, 10, 18, 1, 17, 45, tzinfo=timezone.utc).timestamp()
DEADLINE = START + 86400


def read(path):
    return json.loads(Path(path).read_text())


def save(path, data):
    path = Path(path)
    temporary = path.with_suffix(path.suffix + ".tmp")
    temporary.write_text(json.dumps(data, indent=2) + "\n")
    temporary.replace(path)


def sha(path):
    with Path(path).open("rb") as stream:
        digest = hashlib.sha256()
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            digest.update(block)
        return digest.hexdigest()


class Memory(ctypes.Structure):
    _fields_ = [(name, ctypes.c_ulonglong) for name in ("total", "free", "used")]


class GpuMemory:
    def __init__(self):
        self.library=ctypes.CDLL("libnvidia-ml.so.1")
        assert self.library.nvmlInit_v2()==0
        self.device=ctypes.c_void_p()
        assert self.library.nvmlDeviceGetHandleByIndex_v2(0,ctypes.byref(self.device))==0

    def sample(self):
        memory=Memory()
        if self.library.nvmlDeviceGetMemoryInfo(self.device,ctypes.byref(memory))!=0:return None
        return int(memory.used)


class Stage:
    def __init__(self, name, binary, settings=None, strict=False):
        self.strict=strict
        self.memory=GpuMemory() if strict else None
        self.out = RAW / name
        self.out.mkdir(exist_ok=True)
        self.lock = (RAW / (name + ".lock")).open("a")
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        manifest = self.out / "manifest.json"
        if manifest.exists():
            self.m = read(manifest)
            assert sha(self.out / "bench") == self.m["binary_sha256"]
            if strict:
                assert sha(binary)==self.m["binary_sha256"], "resume binary differs"
                assert settings is not None and settings==self.m["settings"], "resume settings differ"
            for job in self.m["jobs"].values():
                if job["status"] == "running":
                    assert not Path(f"/proc/{job['pid']}").exists(), "previous child is alive"
                    job["status"] = "interrupted"
        else:
            shutil.copy2(binary, self.out / "bench")
            self.m = dict(stage=name, status="running", started_epoch=START,
                deadline_epoch=DEADLINE, jobs={}, binary_sha256=sha(self.out / "bench"),
                settings=settings if settings is not None else read(RAW / "baseline-settings.json"),
                source_head=subprocess.check_output(["git", "rev-parse", "HEAD"],cwd=ROOT,text=True).strip())
        self.binary = self.out / "bench"
        self.m["controller_pid"] = os.getpid()
        self.persist()

    def persist(self):
        self.m["elapsed_h"] = (time.time() - START) / 3600
        save(self.out / "manifest.json", self.m)

    def run(self, tag, args, extra=None, timeout=600, prefix=(), reserve=3600):
        command=[*map(str,prefix),str(self.binary),*map(str,args)]
        hashes={}
        if self.strict:
            positions={"measure":(1,2,3),"prepare":(1,2),"quality-source":(1,2,3)}.get(str(args[0]),())
            paths=[args[i] for i in positions]
            if str(args[0])=="quality-source" and len(args)>5:paths.append(args[5])
            hashes={str(path):sha(path) for path in paths}
        job=self.m["jobs"].get(tag)
        if job is not None:
            if self.strict:
                assert job["command"]==command and job["extra"]==(extra or {}), "resume command differs"
                assert job["input_sha256"]==hashes, "resume input differs"
            if not (self.strict and job["status"]=="waiting" and "pid" not in job):return job
            job.setdefault("waiting_resumed_epoch",[]).append(time.time())
        else:
            job=dict(status="waiting",command=command,extra=extra or {})
            if self.strict:job["input_sha256"]=hashes
            self.m["jobs"][tag]=job
        self.persist()
        if time.time() >= DEADLINE - reserve:
            job["status"] = "budget_not_run"
            self.persist()
            return job
        with open("/tmp/gagp-support-20261015-gpu.lock", "a") as lock:
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.time() >= DEADLINE - reserve:
                        job["status"] = "budget_not_run"; self.persist(); return job
                    time.sleep(1)
            env = {k:v for k,v in os.environ.items()
                   if not k.startswith("GAGP_") and k not in ("LD_PRELOAD", "MALLOC_CONF")}
            env.update(self.m["settings"]); env.update(extra or {})
            start = time.time(); end = min(start + timeout, DEADLINE - reserve)
            job.update(status="running", started_epoch=start, log=str(self.out / (tag + ".log")))
            with Path(job["log"]).open("w") as log:
                child = subprocess.Popen(job["command"], cwd=ROOT, env=env, stdout=log,
                                         stderr=subprocess.STDOUT, start_new_session=True)
                job["pid"] = child.pid; self.persist()
                peak = 0; reason = None; last = 0; card_peak=0; memory_samples=0; memory_errors=0
                while child.poll() is None:
                    peak = max(peak, process_group_rss(child.pid))
                    if self.memory:
                        sample=self.memory.sample()
                        if sample is None:memory_errors+=1
                        else:card_peak=max(card_peak,sample);memory_samples+=1
                    if peak > 32 * 1024**3: reason = "host_memory_limit"
                    if time.time() >= end: reason = "timeout"
                    if reason:
                        os.killpg(child.pid, signal.SIGTERM)
                        try: child.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            os.killpg(child.pid, signal.SIGKILL); child.wait()
                        break
                    if time.time() - last > 10:
                        job.update(process_s=time.time()-start, peak_rss_bytes=peak)
                        self.persist(); last=time.time()
                    time.sleep(.2)
            job.update(status=reason or ("ok" if child.returncode == 0 else "failed"),
                returncode=child.returncode, process_s=time.time()-start, peak_rss_bytes=peak)
            if self.memory:job.update(sampled_card_peak_bytes=card_peak,memory_samples=memory_samples,memory_sampling_errors=memory_errors,
                memory_sampling="whole-card NVML device0 every~200ms; includes desktop, may miss short peaks")
        self.persist()
        print(tag,job["status"],round(job["process_s"],2),flush=True)
        return job


def canonical(value):
    if not math.isfinite(value) or value == 0:
        return value
    mantissa, exponent = math.frexp(value)
    scaled = math.ldexp(mantissa, 40)
    rounded = math.floor(scaled + .5) if scaled >= 0 else math.ceil(scaled - .5)
    return math.ldexp(float(rounded), exponent - 40)


def audit_initial(binary):
    stage = Stage("initial", binary)
    old = read(ROOT / "logs/day24-20261017/manifest.json")
    for task in PAPER_TASKS:
        e = old["inputs"][task]; inp = e["quality"]["0"]
        for mode in ("asgp_snapshot", "snapshot"):
            output = stage.out / f"{task}.{mode}.json"
            extra = dict(GAGP_BM_CASE_COUNT=str(e["split"]["train_count"]))
            if mode == "snapshot":
                extra.update(GAGP_SNAPSHOT_CPU="1", GAGP_SNAPSHOT_NATIVE_OWNER="1")
            result = stage.run(task+"."+mode,
                ["measure",inp["source"],inp["prepared"],e["grammar"],1024,mode,output],extra)
            if result["status"] != "ok":
                raise RuntimeError("initial scoring failed: "+task+"."+mode)
    summaries = []
    for task in PAPER_TASKS:
        a = read(stage.out / f"{task}.asgp_snapshot.json")["programs"]
        doc = read(stage.out / f"{task}.snapshot.json"); b = doc["programs"]
        assert len(a) == len(b) == 1024
        assert doc["diagnostic_owner"].startswith("native-gpu-phase-")
        assert all(r["cases"] == doc["cases"] and r["unscored"] == 0 for r in b)
        key_source = [(-canonical(r["fitness"]),r["wrong"],r["nodes"]) for r in a]
        key_cpu = [(-canonical(r["cpu_fitness"]),r["cpu_wrong"],r["nodes"]) for r in b]
        key_gpu = [(-canonical(r["fitness"]),r["wrong"],r["nodes"]) for r in b]
        rng = random.Random(20261018)
        pairs = [(rng.randrange(1024),rng.randrange(1024)) for _ in range(16384)]
        preference = lambda keys,i,j: (keys[i]>keys[j])-(keys[i]<keys[j])
        summaries.append(dict(task=task,programs=1024,cases=doc["cases"],
            source_cpu_fitness_mismatches=sum(abs(x["fitness"]-y["cpu_fitness"])>1e-8 for x,y in zip(a,b)),
            source_cpu_wrong_mismatches=sum(x["wrong"]!=y["cpu_wrong"] for x,y in zip(a,b)),
            cpu_gpu_fitness_mismatches=sum(canonical(x["fitness"])!=canonical(x["cpu_fitness"]) for x in b),
            cpu_gpu_wrong_mismatches=sum(x["wrong"]!=x["cpu_wrong"] for x in b),
            source_cpu_max_abs_error=max(abs(x["fitness"]-y["cpu_fitness"]) for x,y in zip(a,b)),
            cpu_gpu_max_abs_error=max(abs(x["fitness"]-x["cpu_fitness"]) for x in b),
            pair_count=len(pairs),
            source_native_preference_disagreements=sum(preference(key_source,i,j)!=preference(key_cpu,i,j) for i,j in pairs),
            cpu_gpu_preference_disagreements=sum(preference(key_cpu,i,j)!=preference(key_gpu,i,j) for i,j in pairs)))
    save(stage.out / "summary.json",dict(rows=summaries,
        interpretation="scoring/ranking parity on the same initial candidates; source/native node tie-break differences are not evaluator errors"))
    stage.m["status"] = "complete"; stage.persist()
    print(json.dumps(summaries,indent=2))


TASKS = read(BASE / "protocol.json")["diagnostic_tasks"]
VARIANTS = {
    "baseline": {},
    "phase": {"GAGP_GPU_UNIFORM_PHASE_SITES": "1"},
    "gate": {"GAGP_GPU_SOURCE_CROSSOVER_GATE": "1"},
    "phase_gate": {"GAGP_GPU_UNIFORM_PHASE_SITES": "1", "GAGP_GPU_SOURCE_CROSSOVER_GATE": "1"},
}


def old_input(task, seed):
    old = read(ROOT / "logs/day24-20261017/manifest.json")
    entry = old["inputs"][task]
    return entry, entry["quality"][str(seed)]


def search_job(stage, task, seed, variant, inp, grammar, count, generations=64,
               mode="gpu_overlap", extra=None, checkpoints=False, prefix=(), full_export=False):
    tag = f"{task}.s{seed}.{variant}.g{generations}"
    output = stage.out / (tag + ".json")
    env = dict(GAGP_BM_CASE_COUNT=str(count), GAGP_BM_SEED=str(seed),
               GAGP_SEARCH_GENERATIONS=str(generations),
               GAGP_SEARCH_EXPORT=str(stage.out / (tag + ".export.json")),
               GAGP_SEARCH_HISTORY_EXPORT=str(stage.out / (tag + ".history.json")),
               GAGP_SEARCH_EXPORT_BEST_ONLY="1")
    env.update(extra or {})
    if full_export: env.pop("GAGP_SEARCH_EXPORT_BEST_ONLY",None)
    if checkpoints:
        env.update(GAGP_SEARCH_CHECKPOINT_DIR=str(stage.out / (tag + ".checkpoints")),
                   GAGP_SEARCH_CHECKPOINTS="0,8,32,"+str(generations))
    job = stage.run(tag, ["measure", inp["source"], inp["prepared"], grammar,
                            1024, "search_"+mode, output], env, timeout=1800, prefix=prefix)
    job.update(task=task, seed=seed, variant=variant, generations=generations,
               output=str(output), cases=count, input_hashes={k:sha(inp[k]) for k in ("source","prepared")},
               grammar_sha256=sha(grammar))
    if job["status"] == "ok":
        result = read(output)
        if mode == "gpu_overlap":
            assert result["reproduction_profile"].startswith("native-gpu-phase-")
            assert result["reproduction_fallback_reason"] == ""
            assert all(r["unscored"] == 0 and r["cases"] == 1024 * count for r in result["generations"])
        assert result.get("cpu_audit_mismatches",0) == 0
        assert result.get("cpu_audit_wrong_mismatches",0) == 0
    stage.persist()
    return job


def development(binary):
    stage = Stage("development", binary)
    # Factorial order rotates by seed/task, without observing held-out results.
    for seed in range(5):
        for index,task in enumerate(TASKS):
            entry,inp = old_input(task, seed)
            variants = list(VARIANTS)
            shift=(seed+index)%len(variants)
            for variant in variants[shift:]+variants[:shift]:
                search_job(stage,task,seed,variant,inp,entry["grammar"],entry["split"]["train_count"],
                           extra=VARIANTS[variant],checkpoints=seed==0 and variant=="baseline")
            if seed == 0:
                search_job(stage,task,seed,"asgp",inp,entry["grammar"],entry["split"]["train_count"],
                           mode="asgp_1t",checkpoints=True)
    stage.m["status"]="complete";stage.persist()


def checkpoint_audit(binary):
    stage=Stage("checkpoint-audit",binary)
    dev=RAW / "development"
    for task in TASKS:
        entry,inp=old_input(task,0)
        original=read(inp["source"])
        for mode in ("baseline","asgp"):
            folder=dev / f"{task}.s0.{mode}.g64.checkpoints"
            for generation in (8,32,64):
                tag=f"{task}.{mode}.g{generation}"
                source=stage.out/(tag+".source.json")
                prepared=folder/f"gen{generation}.native.json"
                if mode=="asgp":
                    checkpoint=read(folder/f"gen{generation}.source.json")
                    document=dict(original,population=checkpoint["population"])
                    save(source,document)
                    prepared=stage.out/(tag+".prepared.json")
                    job=stage.run(tag+".prepare",["prepare",source,entry["grammar"],1024,prepared],
                                  {"GAGP_PREPARE_CLASSIFY_REJECTIONS":"1"})
                    if job["status"]!="ok":continue
                    accepted=read(prepared)
                    document["population"]=[checkpoint["population"][i] for i in accepted["source_indices"]]
                    save(stage.out/(tag+".admission.json"),dict(requested=1024,
                        accepted=len(document["population"]),rejections=accepted["admission_rejections"]))
                    save(source,document)
                else: save(source,original)
                population=len(read(prepared)["programs"])
                if population==0:continue
                env=dict(GAGP_BM_CASE_COUNT=str(entry["split"]["train_count"]),
                         GAGP_SNAPSHOT_CPU="1",GAGP_SNAPSHOT_NATIVE_OWNER="1")
                for snapshot_mode in (("asgp_snapshot","snapshot") if mode=="asgp" else ("snapshot",)):
                    stage.run(tag+"."+snapshot_mode,["measure",source,prepared,entry["grammar"],population,
                        snapshot_mode,stage.out/(tag+"."+snapshot_mode+".json")],env)
    stage.m["status"]="complete";stage.persist()


def profile(binary):
    stage=Stage("profile-baseline",binary)
    for task in ("is_sorted","all_equal"):
        entry,inp=old_input(task,0)
        prefix=["nsys","profile","--trace=cuda,nvtx","--sample=none","--cpuctxsw=none",
                "-o",str(stage.out/task)]
        search_job(stage,task,0,"profile",inp,entry["grammar"],entry["split"]["train_count"],prefix=prefix)
    stage.m["status"]="complete";stage.persist()

def main():
    parser=ArgumentParser(description=__doc__)
    parser.add_argument("stage",choices=("initial","development","checkpoint-audit","profile"))
    parser.add_argument("--binary",type=Path,required=True)
    args=parser.parse_args()
    {"initial":audit_initial,"development":development,"checkpoint-audit":checkpoint_audit,"profile":profile}[args.stage](args.binary.resolve())


if __name__ == "__main__":
    main()
