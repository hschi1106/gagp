"""Quality search/judge API with a cutoff check before champion export reads."""
import fcntl
import time

import quality
import serial


search = quality.search


def judge(stage, run, split, inputs, reserve_s):
    if run["status"] == "ok" and time.time() >= serial.DEADLINE - reserve_s:
        with serial.GPU_LOCK.open("a") as lock:
            fcntl.flock(lock, fcntl.LOCK_EX)
            return quality.missing(stage, f'{run["task"]}.s{run["seed"]}.{run["variant"]}.{split}',
                                   "budget_not_run", dict(task=run["task"], seed=run["seed"], variant=run["variant"],
                                                          split=split, inputs_not_read=True))
    return quality.judge(stage, run, split, inputs, reserve_s)
