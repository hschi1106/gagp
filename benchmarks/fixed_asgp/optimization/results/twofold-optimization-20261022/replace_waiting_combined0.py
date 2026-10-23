"""Replace only verified unstarted9 queues with the actual combined candidate chain."""
import os
from pathlib import Path
import signal
import subprocess
import time

import serial

record=serial.RAW/'queued-coarse-recipe-replacement0.json';assert not record.exists()
names=['combined0','training11','training-timing6','preparation-timing4']
paths=['combined0-settings.json','training11-baseline','training11-candidate','training11-baseline-native','training11-candidate-native','training-timing6-baseline','training-timing6-candidate','preparation-timing4']
states={};stopped=[]
try:
    assert serial.read(serial.RAW/'build-attempt20/manifest.json')['status']=='ok'
    assert all(not (serial.RAW/path).exists() for path in paths),'a sample stage already exists; do not replace'
    for name in names:
        state=serial.read(serial.RAW/(name+'-controller.json'))
        assert state['status'].startswith('waiting_'),(name,state['status'])
        pid=state['pid'];proc=Path(f'/proc/{pid}')
        command=(proc/'cmdline').read_bytes().replace(b'\0',b' ').decode()
        expected={'combined0':'combined_pipeline0.py','training11':'training_best_combined.py','training-timing6':'training_timing6.py','preparation-timing4':'preparation_timing4.py'}
        assert expected[name] in command
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
        reason='coarse checkpoint composition replaces unstarted individual-tail recipe; preserve full30 composed/formal and independent quality; zero scientific samples stopped'))
    for pid in stopped:os.kill(pid,signal.SIGTERM);os.kill(pid,signal.SIGCONT)
    for _ in range(100):
        if all(not Path(f'/proc/{pid}/stat').exists() or Path(f'/proc/{pid}/stat').read_text().split()[2]=='Z' for pid in stopped):break
        time.sleep(.05)
    for name,entry in states.items():
        pid=entry['state']['pid'];proc=Path(f'/proc/{pid}/stat')
        assert not proc.exists() or proc.read_text().split()[2]=='Z'
        state=serial.read(serial.RAW/(name+'-controller.json'))
        state.update(status='stopped',finished_epoch=time.time(),intentional_unstarted_queue_cancellation=str(record),
            reason='coarse-recipe training12 replaces unstarted11; zero sample stages')
        serial.save(serial.RAW/(name+'-controller.json'),state)
except BaseException:
    for pid in stopped:
        try:os.kill(pid,signal.SIGCONT)
        except ProcessLookupError:pass
    raise
