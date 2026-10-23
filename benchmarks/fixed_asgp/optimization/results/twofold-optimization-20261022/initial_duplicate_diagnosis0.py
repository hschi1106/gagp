"""Old-training cold-import duplicates; no searches, settings, or heldout use."""
from collections import Counter
import fcntl
import json
import os
from pathlib import Path
import time
import traceback

import serial


out = serial.RAW / "initial-duplicate-diagnosis0"
out.mkdir()
record = out / "manifest.json"
state = dict(pid=os.getpid(), status="waiting_for_lock", started_epoch=time.time(),
             deadline_epoch=serial.DEADLINE,
             scope="fixed development8/P1024/old TRAINING initial ASTs only; diagnostic, no speed claim")
serial.save(record, state)
try:
    with serial.GPU_LOCK.open("a") as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        protocol = serial.read(serial.BASE / "protocol.json")
        rows = []
        for task in protocol["development"]["tasks"]:
            source = protocol["inputs"][task]["prefixes"]["1024"]
            assert serial.sha(source["prepared"]) == source["sha256"]
            programs = serial.read(source["prepared"])["programs"]
            assert len(programs) == 1024
            # Complete JSON strings, not digest equality; retain metadata,
            # literal encodings, fuel events, names, and order within each AST.
            keys = [json.dumps(p, sort_keys=True, separators=(",", ":")) for p in programs]
            counts = Counter(keys)
            rows.append(dict(task=task, population=1024, unique_initial=len(counts),
                             duplicate_members=1024-len(counts),
                             maximum_identical_copies=max(counts.values()),
                             prepared=source["prepared"], prepared_sha256=source["sha256"]))
            print(rows[-1], flush=True)
        state.update(status="ok", rows=rows, source_sha256=serial.sha(Path(__file__)),
                     finished_epoch=time.time(),
                     caveat="final-generation duplication does not establish cold-import opportunity; no time estimate or cache enabled")
except BaseException as error:
    state.update(status="failed", error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
