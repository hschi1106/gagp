"""Stop only the verified unstarted old8/9-binary followup chain; preserve it."""
import os
from pathlib import Path
import signal
import time

import serial

path=serial.RAW/'register1-controller.json';before=serial.read(path);pid=before['pid']
proc=Path(f'/proc/{pid}');assert proc.exists()
assert b'/register_pipeline.py\0' in (proc/'cmdline').read_bytes()
assert before['status']=='waiting_for_prior'
assert not list(serial.RAW.glob('register1-p*-comparisons.json'))
record=serial.RAW/'queued-legacy-cancellation0.json';assert not record.exists()
reason='prioritize complete current17/18-binary all30 speed/quality validation within original24h; old8/9 followups are unstarted and superseded, no samples removed'
state=dict(epoch=time.time(),reason=reason,target=before,harness_sha256=serial.sha(Path(__file__)),
    original_controller_sha256=serial.sha(path),dependency_chain=['register1','vm0','training0','reuse2'])
serial.save(record,state)
stopped=False
try:
    os.kill(pid,signal.SIGSTOP);stopped=True
    for _ in range(100):
        if (proc/'stat').read_text().split()[2]=='T':break
        time.sleep(.01)
    assert (proc/'stat').read_text().split()[2]=='T'
    assert not (proc/'task'/str(pid)/'children').read_text().strip(),'unexpected active child; resume and inspect'
    assert serial.read(path)['status']=='waiting_for_prior'
    state['confirmed_cmdline']=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode()
    state['confirmed_start_ticks']=(proc/'stat').read_text().split()[21]
    os.kill(pid,signal.SIGTERM);os.kill(pid,signal.SIGCONT);stopped=False
    for _ in range(500):
        if not proc.exists() or (proc/'stat').read_text().split()[2]=='Z':break
        time.sleep(.01)
    assert not proc.exists() or (proc/'stat').read_text().split()[2]=='Z','controller did not exit'
    after=dict(before,status='stopped',error='intentional_unstarted_queue_cancellation',
        reason=reason,cancellation_record=str(record),finished_epoch=time.time(),scientific_samples=0)
    serial.save(path,after);state.update(status='cancelled_after_verified_no_child',finished_epoch=time.time());serial.save(record,state)
finally:
    if stopped:os.kill(pid,signal.SIGCONT)
