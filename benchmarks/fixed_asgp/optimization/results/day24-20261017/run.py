"""Serial, resumable 24-hour ASGP/GAGP experiment. Run from repository root."""
from argparse import ArgumentParser
import ctypes
from datetime import datetime, timezone
import fcntl
import json
import math
import os
from pathlib import Path
import signal
import subprocess
import shutil
import threading
import time

from gagp_tools.experiments.fixed_asgp.low_budget import PAPER_TASKS, read, save, sha as file_sha, process_group_rss
from quality_data import split_pool

ROOT = Path.cwd()
POPS = (1024, 2048, 4096, 8192, 16384)
MODES = ("asgp_1t", "gpu_overlap")
LOCK = "/tmp/gagp-support-20261015-gpu.lock"
STARTED = datetime(2026, 10, 17, 13, 16, 49, tzinfo=timezone.utc).timestamp()
RESULT = Path(__file__).resolve().parent


def sha(path):
    return file_sha(Path(path))


def validate(data, population, generations, mode, cases):
    assert data["population"] == population and data["cases"] == cases
    assert len(data["generations"]) == generations and len(data["final_population"]) == population
    assert math.isfinite(data["final_best"])
    assert data["final_best"] + 1e-8 >= max(r["best"] for r in data["generations"])
    assert data["evolve_wall_ms"] > 0
    assert all(r["expected_program_cases"] == population * cases for r in data["generations"])
    if mode == "gpu_overlap":
        assert data["reproduction_profile"].startswith("native-gpu-phase-")
        assert not data["reproduction_fallback_reason"]
        assert data["cpu_audit_program_count"] == 16 and data["cpu_audit_mismatches"] == 0
        assert all(r["cases"] == population * cases and r["case_counters_available"] == 1
                   and r["unscored"] == 0 and r["eval_kernel_ms"] > 0
                   and r["repro_kernel_ms"] > 0 for r in data["generations"])
        assert max(r["nodes"] for r in data["final_population"]) <= 2048


class Memory(ctypes.Structure):
    _fields_ = [(k, ctypes.c_ulonglong) for k in ("total", "free", "used")]


class Experiment:
    def __init__(self, out, resume):
        self.out = out.resolve()
        self.controller_lock = (out.parent / (out.name + ".lock")).open("a")
        fcntl.flock(self.controller_lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        if resume:
            self.m = read(self.out / "manifest.json")
            assert sha(self.out / "bench") == self.m["binary_sha256"]
            for cell in self.m["jobs"].values():
                if cell["status"] == "running":
                    pid = cell.get("pid")
                    if pid and Path(f"/proc/{pid}").exists():
                        raise RuntimeError(f"previous child is still running: {pid}; do not duplicate")
                    cell["status"] = "interrupted"
        else:
            self.out.mkdir(parents=True, exist_ok=False)
            shutil.copy2(ROOT / "cpp/build/release/gagp_fixed_asgp_bench", self.out / "bench")
            self.m = dict(format="asgp-day24-v1", started_epoch=STARTED,
                deadline_epoch=STARTED + 86400, stage="preparation", jobs={}, inputs={},
                speed={}, quality={}, pilots={}, selection=None,
                source_head=subprocess.check_output(["git", "rev-parse", "HEAD"], text=True).strip(),
                runner_sha256=sha(Path(__file__)), fixture_helper_sha256=sha(RESULT / "quality_data.py"),
                binary_sha256=sha(self.out / "bench"), protocol=read(RESULT / "protocol.json"))
            flags = read(ROOT / "benchmarks/fixed_asgp/optimization/results/list-5x-20261015/settings-workspace-pool16.json")
            flags.pop("GAGP_BM_COMMON_FITNESS", None)
            flags.update(GAGP_HOST_THREADS="1", GAGP_CUDA_DEVICE="0", OMP_NUM_THREADS="1",
                OPENBLAS_NUM_THREADS="1", MKL_NUM_THREADS="1", GAGP_BM_ALIGNED_FITNESS="1")
            self.m["settings"] = flags
            self.m["runtime_source_sha256"] = {str(p): sha(p) for p in (
                Path("cpp/src/evolution/evolve.cpp"),
                Path("cpp/src/evolution/repro/gpu/phase_layout.hpp"),
                Path("cpp/src/evolution/repro/gpu/phase_population.cu"))}
            save(self.out / "environment.json", dict(
                cpu=next(l.split(":",1)[1].strip() for l in Path("/proc/cpuinfo").read_text().splitlines() if l.startswith("model name")),
                gpu=subprocess.check_output(["nvidia-smi","--query-gpu=index,name,memory.total,driver_version,uuid","--format=csv"],text=True),
                source_head=self.m["source_head"], binary_sha256=self.m["binary_sha256"],
                host_threads=1, workspace_mib=16384))
        self.binary = self.out / "bench"
        self.env = {k:v for k,v in os.environ.items()
                    if not k.startswith("GAGP_") and k not in ("LD_PRELOAD", "MALLOC_CONF")}
        self.env.update(self.m["settings"])
        self.active = None
        self.stop = threading.Event()
        self.nvml = ctypes.CDLL("libnvidia-ml.so.1")
        assert self.nvml.nvmlInit_v2() == 0
        self.device = ctypes.c_void_p()
        assert self.nvml.nvmlDeviceGetHandleByIndex_v2(0, ctypes.byref(self.device)) == 0
        self.thread = threading.Thread(target=self.sample, daemon=True)
        self.thread.start()
        self.m["controller_pid"] = os.getpid()
        self.persist()

    def sample(self):
        with (self.out / "vram.csv").open("a") as stream:
            while not self.stop.is_set():
                memory = Memory()
                if self.nvml.nvmlDeviceGetMemoryInfo(self.device, ctypes.byref(memory)) == 0:
                    tag = self.active
                    stream.write(f"{time.time_ns()},{memory.used},{memory.total},{tag or ''}\n")
                    stream.flush()
                    if tag:
                        cell = self.m["jobs"][tag]
                        cell["sampled_device_peak_bytes"] = max(cell.get("sampled_device_peak_bytes", 0), memory.used)
                self.stop.wait(.2)

    def persist(self):
        self.m["elapsed_s"] = time.time() - self.m["started_epoch"]
        self.m["remaining_s"] = max(0, self.m["deadline_epoch"] - time.time())
        save(self.out / "manifest.json", self.m)

    def run(self, tag, command, extra=None, timeout=5400, reserve=3600):
        previous = self.m["jobs"].get(tag)
        if previous:
            return previous
        left = self.m["deadline_epoch"] - time.time() - reserve
        cell = dict(command=list(map(str, command)), extra_settings=extra or {}, status="waiting")
        self.m["jobs"][tag] = cell
        self.persist()
        if left <= 0:
            cell["status"] = "budget_not_run"
            self.persist()
            return cell
        with open(LOCK, "a") as lock:
            while True:
                try:
                    fcntl.flock(lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
                    break
                except BlockingIOError:
                    if time.time() >= self.m["deadline_epoch"] - reserve:
                        cell["status"] = "budget_not_run"; self.persist(); return cell
                    time.sleep(1)
            start = time.time()
            if start >= self.m["deadline_epoch"] - reserve:
                cell["status"]="budget_not_run";self.persist();return cell
            end = min(start + timeout, self.m["deadline_epoch"] - reserve)
            log = self.out / (tag + ".log")
            cell.update(status="running", started_epoch=start, log=str(log))
            self.active = tag
            self.persist()
            with log.open("w") as stream:
                env = dict(self.env); env.update(extra or {})
                process = subprocess.Popen(cell["command"], cwd=ROOT, env=env,
                    stdout=stream, stderr=subprocess.STDOUT, start_new_session=True)
                cell["pid"] = process.pid; self.persist()
                last = 0; peak = 0; reason = None
                while process.poll() is None:
                    peak = max(peak, process_group_rss(process.pid))
                    if peak > 32 * 1024**3: reason = "host_memory_limit"
                    if time.time() >= end: reason = "timeout"
                    if reason:
                        os.killpg(process.pid, signal.SIGTERM)
                        try: process.wait(timeout=3)
                        except subprocess.TimeoutExpired:
                            os.killpg(process.pid, signal.SIGKILL); process.wait()
                        break
                    if time.time() - last > 10:
                        cell.update(process_s=time.time()-start, peak_rss_bytes=peak)
                        self.persist(); last = time.time()
                    time.sleep(.5)
                cell.update(status=reason or ("ok" if process.returncode == 0 else "failed"),
                    returncode=process.returncode, process_s=time.time()-start, peak_rss_bytes=peak)
            self.active = None
        self.persist()
        print(tag, cell["status"], round(cell["process_s"], 2), flush=True)
        return cell

    def prepare(self):
        old_path = ROOT / "logs/nodes2048-20261016/manifest.json"
        old = read(old_path)
        self.m["preparation_manifest"] = str(old_path)
        self.m["preparation_manifest_sha256"] = sha(old_path)
        folder = self.out / "inputs"; folder.mkdir(exist_ok=True)
        for task in PAPER_TASKS:
            if task in self.m["inputs"]: continue
            entry = old["catalog"][task]
            assert entry["admission_status"] == "ok"
            assert entry["native_limits"] == dict(max_nodes=2048, max_depth=24)
            for field in ("source", "prepared", "grammar"):
                assert sha(entry[field]) == entry[field+"_sha256"]
            data = read(Path(entry["source"]))
            prepared = read(Path(entry["prepared"]))
            taskdir = folder / task; taskdir.mkdir(exist_ok=True)
            prefixes = {}
            for p in POPS:
                pp = taskdir / f"p{p}.prepared.json"
                if not pp.exists(): save(pp, dict(prepared, programs=prepared["programs"][:p]))
                prefixes[str(p)] = dict(prepared=str(pp), sha256=sha(pp))
            poolpath = taskdir / "quality.pool.json"
            result = self.run(task+".dataset", [self.binary,"dataset",task,poolpath], timeout=180)
            if result["status"] != "ok": raise RuntimeError("dataset failed: "+task)
            split = split_pool(read(poolpath))
            save(taskdir / "split.json", split["metadata"])
            qinputs = {}
            for seed in range(5):
                sp = taskdir / f"s{seed}.train.source.json"
                qp = taskdir / f"s{seed}.prepared.json"
                if seed == 0:
                    source = dict(data, population=data["population"][:1024])
                    qp = Path(prefixes["1024"]["prepared"])
                else:
                    frozen = taskdir / f"s{seed}.frozen.source.json"
                    status = self.run(task+f".freeze.s{seed}",
                        [self.binary,"freeze",task,frozen],
                        dict(GAGP_FREEZE_POPULATION="1024",GAGP_FREEZE_POPULATION_SEED=str(seed)),timeout=180)
                    if status["status"] != "ok": raise RuntimeError("freeze failed: "+task)
                    source = read(frozen)
                source.update(cases=split["train"], data_seed=20261017, quality_split="train", quality_data_seed=20261017)
                save(sp,source)
                if seed:
                    status = self.run(task+f".prepare.s{seed}",
                        [self.binary,"prepare",sp,entry["grammar"],1024,qp],timeout=300)
                    if status["status"] != "ok": raise RuntimeError("initial admission failed: "+task)
                qinputs[str(seed)] = dict(source=str(sp), source_sha256=sha(sp),
                    prepared=str(qp), prepared_sha256=sha(qp))
            tp = taskdir / "test.source.json"
            save(tp, dict(data, population=data["population"][:1], cases=split["test"],data_seed=20261017,
                          quality_split="test",quality_data_seed=20261017))
            self.m["inputs"][task] = dict(source=entry["source"],source_sha256=entry["source_sha256"],
                grammar=entry["grammar"],grammar_sha256=entry["grammar_sha256"],prefixes=prefixes,
                quality_pool=str(poolpath),quality_pool_sha256=sha(poolpath),
                quality=qinputs,test_source=str(tp),test_source_sha256=sha(tp),split=split["metadata"])
            self.persist()

    def measure(self, suite, task, p, mode, seed, g):
        tag = f"{suite}.{task}.p{p}.{mode}.s{seed}.g{g}"
        record = self.m[suite].get(tag)
        if record: return record
        inputs = self.m["inputs"][task]
        quality = suite == "quality"
        cases = inputs["split"]["train_count"] if quality else 1024
        source = inputs["quality"][str(seed)]["source"] if quality else inputs["source"]
        prepared = inputs["quality"][str(seed)]["prepared"] if quality else inputs["prefixes"][str(p)]["prepared"]
        output = self.out / (tag + ".json")
        export = self.out / (tag + ".final.json")
        history = self.out / (tag + ".history.json")
        extra = dict(GAGP_BM_SEED=str(seed),GAGP_SEARCH_GENERATIONS=str(g),GAGP_SEARCH_TRACE="1",
            GAGP_BM_CASE_COUNT=str(cases),GAGP_SEARCH_HISTORY_EXPORT=str(history))
        if quality: extra.update(GAGP_SEARCH_EXPORT=str(export),GAGP_SEARCH_EXPORT_BEST_ONLY="1")
        cell = self.run(tag,[self.binary,"measure",source,prepared,inputs["grammar"],p,"search_"+mode,output],
                        extra,timeout=5400 if not quality else 1800,
                        reserve=6*3600 if not quality and self.m["selection"] is not None else 3600)
        record = dict(status=cell["status"],task=task,population=p,mode=mode,seed=seed,generations=g,
            cases=cases,output=str(output),history=str(history),export=str(export) if quality else None,
            process_s=cell.get("process_s"),sampled_device_peak_bytes=cell.get("sampled_device_peak_bytes"),
            peak_rss_bytes=cell.get("peak_rss_bytes"),job=tag,source=source,prepared=prepared)
        if cell["status"] == "ok":
            try:
                data = read(output); validate(data,p,g,mode,cases)
                assert len(read(history)["history"]) == g
                record.update(evolve_wall_s=data["evolve_wall_ms"]/1000,
                    final_best=data["final_best"],cpu_audit_mismatches=data.get("cpu_audit_mismatches"),
                    reproduction_profile=data["reproduction_profile"],
                    final_max_nodes=max(r["nodes"] for r in data["final_population"]))
            except (AssertionError,KeyError,ValueError) as error:
                record.update(status="validation_failed", reason=str(error))
        self.m[suite][tag] = record
        self.persist()
        return record

    def forecast(self, g, q):
        import csv
        with (ROOT/"benchmarks/fixed_asgp/optimization/results/scaling-all30-20261016/final-same16/cells.csv").open() as f:
            old = {(r["task"],int(r["population"])):r for r in csv.DictReader(f)}
        newest = read(ROOT/"benchmarks/fixed_asgp/optimization/results/nodes2048-20261016/final/summary.json")
        newer = {r["task"]:r for r in newest["rows"]}
        def baseline(task,p,mode):
            row = old[task,p]
            if mode == "asgp_1t": return float(row["asgp_wall_s"])
            if p == 16384: return newer[task]["gpu_wall_s"]
            return float(row["gpu_wall_s"])
        speed_factors = []
        for r in self.m["speed"].values():
            if r["status"] == "ok":
                reference = baseline(r["task"],r["population"],r["mode"])*(r["generations"]+1)/5+3
                speed_factors.append(r["process_s"]/reference)
        speed_factor = max([1.10]+[f*1.1 for f in speed_factors])
        speed = sum(baseline(t,p,m)*(g+1)/5+3 for t in PAPER_TASKS for p in POPS for m in MODES)*speed_factor
        def quality_reference(task,mode,gens):
            row = old[task,1024]
            d = read(Path(row["asgp_output"] if mode == "asgp_1t" else row["gpu_output"]))
            evaluation = sum(r["eval_ms"] for r in d["generations"])+d["final_eval_ms"]
            other = max(0,d["evolve_wall_ms"]-evaluation)
            n = self.m["inputs"][task]["split"]["train_count"]
            return (evaluation*n/1024+other)*(gens+1)/5/1000+3
        qfactors=[]
        for r in self.m["quality"].values():
            if r["status"]=="ok":
                qfactors.append(r["process_s"]/quality_reference(r["task"],r["mode"],r["generations"]))
        qfactor=max([1.20]+[f*1.1 for f in qfactors])
        quality=sum(quality_reference(t,m,q) for t in PAPER_TASKS for m in MODES)*5*qfactor
        return dict(speed_s=speed,quality_s=quality,speed_factor=speed_factor,quality_factor=qfactor)

    def pilot(self):
        self.m["stage"]="pilot";self.persist()
        for task in ("double_letters","is_sorted","house_robber"):
            for mode in MODES:
                r=self.measure("speed",task,16384,mode,0,16)
                if r["status"]!="ok":raise RuntimeError("speed pilot failed: "+r["job"])
        for task in ("double_letters","is_sorted","house_robber","fibonacci_array","median","filter_odd"):
            for mode in MODES:
                r=self.measure("quality",task,1024,mode,0,128)
                if r["status"]!="ok":raise RuntimeError("quality pilot failed: "+r["job"])
        if any(r["status"]!="ok" for s in ("speed","quality") for r in self.m[s].values()):
            raise RuntimeError("pilot failed; formal matrix not admitted")
        standard=self.forecast(16,128);compact=self.forecast(8,64)
        left=self.m["deadline_epoch"]-time.time()
        qualified=(standard["speed_s"]<=12*3600 and standard["quality_s"]<=5*3600
                   and standard["speed_s"]+standard["quality_s"]<=left-5*3600)
        g,q=(16,128) if qualified else (8,64)
        self.m["selection"]=dict(speed_generations=g,quality_generations=q,
            standard_forecast=standard,compact_forecast=compact,remaining_s=left,
            reason="standard passed12h/5h and five-hour reserve" if qualified
                   else "global compact preset selected before formal matrix; pilotG16/G128 retained separately",
            selected_epoch=time.time())
        self.persist()
        print("SELECTED",json.dumps(self.m["selection"]),flush=True)

    def searches(self):
        g=self.m["selection"]["speed_generations"]
        q=self.m["selection"]["quality_generations"]
        self.m["stage"]="speed";self.persist()
        for pi,p in enumerate(POPS):
            for ti,task in enumerate(PAPER_TASKS):
                order=MODES if (pi+ti)%2==0 else MODES[::-1]
                for mode in order:self.measure("speed",task,p,mode,0,g)
        self.m["stage"]="quality";self.persist()
        for seed in range(5):
            for ti,task in enumerate(PAPER_TASKS):
                order=MODES if (seed+ti)%2==0 else MODES[::-1]
                for mode in order:self.measure("quality",task,1024,mode,seed,q)
        self.m["stage"]="audits";self.persist()
        for record in list(self.m["quality"].values()):
            if record["generations"]!=q or record["status"]!="ok":continue
            task=record["task"];inputs=self.m["inputs"][task]
            for split in ("train","test"):
                tag=record["job"]+"."+split+".judge"
                source=record["source"] if split=="train" else inputs["test_source"]
                count=inputs["split"]["train_count" if split=="train" else "test_count"]
                output=self.out/(tag+".json")
                result=self.run(tag,[self.binary,"quality-source",source,record["export"],
                    inputs["grammar"],output,record["history"]],
                    dict(GAGP_BM_CASE_COUNT=str(count)),timeout=300,reserve=600)
                record[split+"_audit"]=dict(status=result["status"],output=str(output))
                if result["status"]=="ok":
                    d=read(output)
                    assert d["cases"]==count and len(d["history"])==q
                    assert d["final"]["cases"]==count
                    record[split+"_audit"].update(final=d["final"],unique_programs=d["unique_audited_programs"])
                    # Per-case rows are archived in the raw audit; keep the manifest compact.
                    record[split+"_audit"]["final"].pop("case_results",None)
                self.persist()
        self.m["stage"]="complete";self.m["requested_phase_complete"]=True;self.persist()


def main():
    parser=ArgumentParser(description=__doc__)
    parser.add_argument("--out",type=Path,required=True)
    parser.add_argument("--resume",action="store_true")
    args=parser.parse_args()
    experiment=Experiment(args.out,args.resume)
    try:
        if not experiment.m["inputs"] or len(experiment.m["inputs"])!=30:experiment.prepare()
        if experiment.m["selection"] is None:experiment.pilot()
        experiment.searches()
    except Exception as error:
        experiment.m.update(stage="stopped",stop_reason=repr(error))
        experiment.persist()
        raise
    finally:
        experiment.stop.set();experiment.thread.join();experiment.persist()


if __name__=="__main__":
    main()
