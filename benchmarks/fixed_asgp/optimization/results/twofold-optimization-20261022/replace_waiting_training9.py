"""Replace only verified unstarted9 queues with the actual combined candidate chain."""
import os
from pathlib import Path
import signal
import subprocess
import time

import serial

record=serial.RAW/'queued-combined-training-replacement0.json';assert not record.exists()
names=['training9','training-timing4','preparation-timing2']
paths=['training9-baseline','training9-candidate','training9-baseline-native','training9-candidate-native',
       'training-timing4-baseline','training-timing4-candidate','preparation-timing2']
states={};stopped=[]
try:
    assert serial.read(serial.RAW/'build-attempt20/manifest.json')['status']=='ok'
    assert all(not (serial.RAW/path).exists() for path in paths),'a sample stage already exists; do not replace'
    for name in names:
        state=serial.read(serial.RAW/(name+'-controller.json'))
        assert state['status'].startswith('waiting_'),(name,state['status'])
        pid=state['pid'];proc=Path(f'/proc/{pid}')
        command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode()
        assert ('training_best_geometry1.py' in command and '--name training9' in command if name=='training9'
                else ('training_timing4.py' in command if name=='training-timing4' else 'preparation_timing2.py' in command))
        children=subprocess.run(['ps','-o','pid=','--ppid',str(pid)],capture_output=True,text=True).stdout.strip()
        assert not children,(name,children)
        states[name]=dict(state=state,command=command,process_stat=(proc/'stat').read_text())
        os.kill(pid,signal.SIGSTOP);stopped.append(pid)
    assert all(not (serial.RAW/path).exists() for path in paths)
    for name,entry in states.items():
        pid=entry['state']['pid']
        assert Path(f'/proc/{pid}/stat').read_text().split()[2]=='T'
        assert not subprocess.run(['ps','-o','pid=','--ppid',str(pid)],capture_output=True,text=True).stdout.strip()
    serial.save(record,dict(status='verified_unstarted_cancellation',epoch=time.time(),original=states,
        sample_directories_absent=paths,
        reason='include actual admitted20 and composed independent global choices before single G64 training/runtime pilot; no active or completed scientific sample stopped'))
    for pid in stopped:os.kill(pid,signal.SIGTERM);os.kill(pid,signal.SIGCONT)
    for _ in range(100):
        if all(not Path(f'/proc/{pid}/stat').exists() or Path(f'/proc/{pid}/stat').read_text().split()[2]=='Z' for pid in stopped):break
        time.sleep(.05)
    for name,entry in states.items():
        pid=entry['state']['pid'];proc=Path(f'/proc/{pid}/stat')
        assert not proc.exists() or proc.read_text().split()[2]=='Z'
        state=serial.read(serial.RAW/(name+'-controller.json'))
        state.update(status='stopped',finished_epoch=time.time(),intentional_unstarted_queue_cancellation=str(record),
            reason='new combined-global-candidate training11 replaces unstarted9; zero sample stages')
        serial.save(serial.RAW/(name+'-controller.json'),state)
except BaseException:
    for pid in stopped:
        try:os.kill(pid,signal.SIGCONT)
        except ProcessLookupError:pass
    raise
