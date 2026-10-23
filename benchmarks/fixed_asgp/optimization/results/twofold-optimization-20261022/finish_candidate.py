"""Recover the recorded test-source snapshot race only after all checks pass."""
import os
from pathlib import Path
import time
import traceback

import build_candidate
import serial


def main():
    record=serial.RAW/'finish-candidate-controller.json'
    if record.exists():raise RuntimeError('recovery controller already recorded')
    state=dict(pid=os.getpid(),status='waiting_for_build3',started_epoch=time.time(),deadline_epoch=serial.DEADLINE)
    serial.save(record,state)
    try:
        while True:
            previous=serial.read(serial.RAW/'build-attempt3/manifest.json')
            if previous['status'] in ('ok','failed'):
                proc=Path(f'/proc/{previous["pid"]}/stat')
                if not proc.exists() or proc.read_text().split()[2]=='Z':break
            if time.time()>serial.DEADLINE-10800:raise RuntimeError('recovery budget exhausted')
            time.sleep(10)
        # A failed test needs diagnosis, never an automatic identical rerun.
        assert previous.get('results')==[0,0,0], 'build3 had an actual build/test failure; inspect it'
        assert previous['status']=='ok' or 'source changed during build/checks' in previous.get('error',''), 'unexpected prior failure'
        state['status']='rebuild_with_complete_source_snapshot';serial.save(record,state)
        build_candidate.run('build-attempt4')
        state.update(status='ok',finished_epoch=time.time())
    except BaseException as error:
        state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
        raise
    finally:
        serial.save(record,state)


if __name__=='__main__':main()
