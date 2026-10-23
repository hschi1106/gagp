"""Compose admitted adjacency on best complete global recipe; fresh all30/bothP controls."""
import fcntl,os,time,traceback
from pathlib import Path
import best_selection_private_adjacent_recipe,combined7_pilot,serial
name='combined7';record=serial.RAW/(name+'-controller.json');assert not record.exists()
paths=[Path(__file__),serial.BASE/'combined7_pilot.py',serial.BASE/'best_selection_private_adjacent_recipe.py',serial.BASE/'serial.py',serial.BASE/'protocol.json']
state=dict(pid=os.getpid(),status='waiting_for_full30_and_adjacent',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
 harness_sha256={str(p):serial.sha(p) for p in paths},
 rule='native+fixed8 gate first; best COMPLETE60-cell oldTRAINING recipe; global settings; NEW all30/bothP/G8/fresh731 controls; keep all rejected evidence')
serial.save(record,state)
try:
 controls={}
 for component in ('combined6','private-adjacent1'):
  while True:
   path=serial.RAW/(component+'-controller.json');data=serial.read(path) if path.exists() else {}
   proc=Path(f'/proc/{data.get("pid")}/stat');exited=not proc.exists() or proc.read_text().split()[2]=='Z'
   if component=='combined6' and data.get('status')=='stopped':raise RuntimeError('prior valid chain stopped')
   if data.get('status') in ('requested_phase_complete','stopped') and exited:controls[component]=data;break
   if time.time()>serial.DEADLINE-10800:raise RuntimeError('combined7 prerequisites exceeded reserve')
   time.sleep(10)
 with serial.GPU_LOCK.open('a') as lock:
  fcntl.flock(lock,fcntl.LOCK_EX)
  for p,h in state['harness_sha256'].items():assert serial.sha(p)==h
  gate=controls['private-adjacent1'];state['run_full']=False
  if gate['status']=='requested_phase_complete' and gate.get('enabled'):
   choice=serial.read(serial.RAW/'private-adjacent1-development-selection.json')
   assert serial.sha(serial.RAW/'private-adjacent1-coarse-comparisons.json')==choice['comparisons_sha256']
   prior=best_selection_private_adjacent_recipe.select();build=serial.read(serial.RAW/'build-attempt26/manifest.json')
   assert build['status']=='ok' and serial.sha(build['binary'])==build['binary_sha256']
   extra=dict(prior['selected']['extra'],GAGP_GPU_PRIVATE_ADJACENT_CONCAT='1',GAGP_GPU_PRIVATE_CELL_SOA='0',GAGP_GPU_SHORT_EDIT_DISTANCE='0',GAGP_FUSED_COLD_EXECUTABLE='0')
   chosen=dict(extra=extra,binary=build['binary'],binary_sha256=build['binary_sha256'],build_name='build-attempt26',base_selection=prior,adjacent_selection=choice,epoch=time.time(),scope=state['rule'])
   settings=serial.RAW/'combined7-settings.json';assert not settings.exists();serial.save(settings,chosen)
   state.update(status='all30_p1024',run_full=True,selection=chosen,settings=str(settings),settings_sha256=serial.sha(settings))
   protocol=serial.read(serial.BASE/'protocol.json')
  else:state.update(reason='adjacent component unadmitted/failed/rejected; retain prior complete recipes',component_status=gate['status'],component_error=gate.get('error'))
  serial.save(record,state)
 if state['run_full']:
  for p in (1024,16384):
   state['status']=f'all30_p{p}';serial.save(record,state)
   combined7_pilot.run(build['binary'],f'{name}-p{p}',[p],protocol['tasks'],'candidate',pool_mode=int(extra.get('GAGP_GPU_CUDA_POOL','0')))
 state.update(status='requested_phase_complete',finished_epoch=time.time())
except BaseException as error:
 state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time());raise
finally:serial.save(record,state)
