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
RAW = ROOT / "logs/scaling-optimization-20261018"
START = datetime(2026, 10, 18, 11, 18, 51, tzinfo=timezone.utc).timestamp()
DEADLINE = START + 86400
GPU_LOCK = Path("/tmp/gagp-support-20261015-gpu.lock")


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
    def __init__(self, name, binary, settings=None, strict=True, resume=False):
        self.strict=strict
        self.memory=GpuMemory() if strict else None
        self.out = RAW / name
        if self.out.exists() and not resume:
            raise RuntimeError("stage already exists; explicit --resume required")
        self.out.mkdir(exist_ok=True)
        self.lock = (RAW / (name + ".lock")).open("a")
        fcntl.flock(self.lock, fcntl.LOCK_EX | fcntl.LOCK_NB)
        manifest = self.out / "manifest.json"
        if resume and not manifest.exists():
            raise RuntimeError("resume requires an existing manifest")
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
            if any(self.out.iterdir()):
                raise RuntimeError("partial stage files must be preserved; choose a new attempt directory")
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
        with GPU_LOCK.open("a") as lock:
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

