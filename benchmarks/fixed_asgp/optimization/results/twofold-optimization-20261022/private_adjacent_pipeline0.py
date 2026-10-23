"""Actual25-native gate, then fixed8/bothP three-arm development gate, no fresh heldout."""
import fcntl,os,time,traceback
from pathlib import Path
import private_adjacent_pilot as helper
import serial,study_statistics
name='private-adjacent0';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'private_adjacent_pilot.py',serial.BASE/'stage_pilot.py',serial.BASE/'vm_pilot.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_actual25_admission',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
 harness_sha256={str(p):serial.sha(p) for p in paths},
 rule='fixed8/bothP/G8/seed0 TRAINING; 24OFF/25OFF/25ON; >=2% vs BOTH OFF controls, exact population/champion/CPU16 fidelity; no fresh heldout')
serial.save(record,state)
try:
 while True:
  candidate=serial.read(serial.RAW/'private-adjacent-candidate0-controller.json')
  if candidate['status']=='stopped':
   state.update(status='requested_phase_complete',enabled=False,component_failed=True,reason='native25 failed; preserve all evidence, retain earlier recipes',component_error=candidate.get('error'),finished_epoch=time.time());serial.save(record,state);break
  path=serial.RAW/'build-attempt25/manifest.json';build=serial.read(path) if path.exists() else {}
  if candidate['status']=='ok' and build.get('status')=='ok':break
  if time.time()>serial.DEADLINE-10800:raise RuntimeError('adjacent gate exceeded original reserve')
  time.sleep(10)
 if not state.get('component_failed'):
  with serial.GPU_LOCK.open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX)
   for p,h in state['harness_sha256'].items():assert serial.sha(p)==h
   assert serial.sha(build['binary'])==build['binary_sha256']==candidate['binary_sha256']
   protocol=serial.read(serial.BASE/'protocol.json');state['status']='coarse';serial.save(record,state)
  helper.run(build['binary'],name+'-coarse',[1024,16384],protocol['development']['tasks'])
  with serial.GPU_LOCK.open('a') as lock:
   fcntl.flock(lock,fcntl.LOCK_EX)
   path=serial.RAW/(name+'-coarse-comparisons.json');rows=serial.read(path)
   expected={(t,p) for t in protocol['development']['tasks'] for p in (1024,16384)};means={}
   for variant in helper.VARIANTS[1:]:
    group=[r for r in rows if r['variant']==variant]
    assert len(group)==16 and {(r['task'],r['population']) for r in group}==expected
    assert all(all(r[k] for k in ('final_descriptors_equal','full_final_ast_equal','champion_ast_fitness_equal')) for r in group)
    means[variant]=study_statistics.geometric_mean(r['speedup'] for r in group)
   jobs=serial.read(serial.RAW/(name+'-coarse-adjacent/manifest.json'))['jobs']
   assert len(jobs)==16 and all(j['status']=='ok' for j in jobs.values())
   active=sum(j['adjacent_active_generations'] for j in jobs.values());assert active>0
   enabled=means['adjacent']>=max(1.,means['ordinary'])*1.02
   choice=dict(selected='adjacent' if enabled else 'baseline',enabled=enabled,geometric_means=means,
    epoch=time.time(),scope=state['rule'],extra=helper.execution_settings('adjacent'),binary_sha256=build['binary_sha256'],
    comparisons_sha256=serial.sha(path),admitted_generations=active)
   serial.save(serial.RAW/(name+'-development-selection.json'),choice)
   state.update(status='requested_phase_complete',enabled=enabled,selection=choice,finished_epoch=time.time())
except BaseException as error:
 state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
