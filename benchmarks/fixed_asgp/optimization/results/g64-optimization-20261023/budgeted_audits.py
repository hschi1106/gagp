"""Reserve-aware audit entrypoints; never parse full AST exports after cutoff."""
import fcntl
import time

import asgp_audit
import champion_audit
import common_audit
import full_population_audit_timed
import serial


def stopped(stage, tag, job, freeze, *, split=None, diagnostic):
    if job["status"] != "ok":
        return None  # The original audit records the precise search failure.
    if time.time() < serial.DEADLINE - freeze["reserve_s"]:
        return None
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert tag not in stage.m["jobs"], "No implicit overwrite or retry"
        result = dict(status="budget_not_run", task=job["task"], seed=job["seed"], variant=job["variant"],
                      population=job["population"], inputs_not_read=True, diagnostic=diagnostic,
                      reason="Timed cutoff before audit source/export parsing")
        if "repetition" in job:
            result["repetition"] = job["repetition"]
        if split is not None:
            result["split"] = split
        stage.m["jobs"][tag] = result
        stage.persist()
        return result


def run(stage, job, split, inputs, freeze):
    """Native GAGP champion API used by the sharded quality runner."""
    tag = f'{job["task"]}.s{job["seed"]}.{job["variant"]}.{split}.native'
    result = stopped(stage, tag, job, freeze, split=split, diagnostic="native champion")
    return result if result is not None else champion_audit.run(stage, job, split, inputs, freeze)


def asgp(stage, job, split, inputs, freeze):
    tag = f'{job["task"]}.s{job["seed"]}.r{job["repetition"]}.{job["variant"]}.{split}.native'
    result = stopped(stage, tag, job, freeze, split=split, diagnostic="ASGP native champion")
    return result if result is not None else asgp_audit.run(stage, job, split, inputs, freeze)


def common(stage, job, split, inputs, freeze):
    tag = f'{job["task"]}.s{job["seed"]}.r{job["repetition"]}.{job["variant"]}.{split}.common'
    result = stopped(stage, tag, job, freeze, split=split, diagnostic="common output champion")
    return result if result is not None else common_audit.run(stage, job, split, inputs, freeze)


def full(stage, job, inputs, freeze):
    tag = f'{job["task"]}.p{job["population"]}.{job["variant"]}.final.full-native'
    result = stopped(stage, tag, job, freeze, diagnostic="full final population")
    return result if result is not None else full_population_audit_timed.run(stage, job, inputs, freeze)
