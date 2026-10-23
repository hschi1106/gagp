"""Preserve complete shared-case roots across the new nested evolve fixture."""
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_pool
import serial

record=serial.RAW/'pool-corrected3-controller.json';assert not record.exists()
state=dict(pid=os.getpid(),status='waiting_for_lock',started_epoch=time.time(),deadline_epoch=serial.DEADLINE,
    reason='fixture isolation: nested evolve retains_only its case roots; a37-case prefix pruned other cases still used by the original1024-case CPU oracle; keep all1024 roots',
    harness_sha256={str(p):serial.sha(p) for p in (Path(__file__),serial.BASE/'build_pool.py',serial.BASE/'serial.py')})
serial.save(record,state)
try:
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock,fcntl.LOCK_EX)
        for path,digest in state['harness_sha256'].items():assert serial.sha(path)==digest
        prior=serial.read(serial.RAW/'build-attempt16/manifest.json')
        assert prior['status']=='failed' and prior['results']==[0,0,8]
        proc=Path(f'/proc/{prior["pid"]}/stat');assert not proc.exists() or proc.read_text().split()[2]=='Z'
        text=(serial.RAW/'build-attempt16/check2.log').read_text()
        assert 'new GPU list phases differ from full-case independent CPU audit' in text and '1 tests failed out of 7' in text
        for path,digest in prior['source_sha256'].items():assert serial.sha(serial.ROOT/path)==digest,path
        archived=serial.RAW/'build-attempt16/unadmitted-bench';assert not archived.exists()
        shutil.copy2(serial.ROOT/'cpp/build/release/gagp_fixed_asgp_bench',archived)
        path=serial.ROOT/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text();before=serial.sha(path)
        old='  evolution_cases.resize(37); // Partial warp and the1024-element input remain in the pool fixture.\n'
        assert text.count(old)==1;text=text.replace(old,'  // Keep all case payload roots: evolve may prune the global registry.\n')
        old='  require(gpu.fitness==cpu && gpu.wrong==wrong,"new GPU list phases differ from full-case independent CPU audit");'
        assert text.count(old)==1
        diagnostic='''  if(gpu.fitness!=cpu || gpu.wrong!=wrong) {
   std::size_t missing=0;for(const auto& row:cases)for(const auto& binding:row)
    if(binding.value.tag==ValueTag::IntList){std::vector<Value> contents;missing+=!payload::lookup_list(binding.value,&contents);}
   std::cerr<<"private audit generation="<<generation<<" sequence_windows="<<sequence_windows<<" staged="<<staged<<" missing_case_payloads="<<missing<<'\\n';
  }
'''
        path.write_text(text.replace(old,diagnostic+old))
        serial.save(serial.RAW/'pool-payload-fixture-correction.json',dict(reason=state['reason'],epoch=time.time(),
            diagnosis='source-supported retain_only lifetime explanation; must be confirmed by fresh complete native regression; no production runtime correction',
            source_before={str(path.relative_to(serial.ROOT)):before},source_after={str(path.relative_to(serial.ROOT)):serial.sha(path)},
            unadmitted_binary=str(archived),unadmitted_binary_sha256=serial.sha(archived)))
        state['status']='building';serial.save(record,state)
    build_pool.run('build-attempt17')
    build=serial.read(serial.RAW/'build-attempt17/manifest.json');assert build['status']=='ok'
    state.update(status='ok',binary=build['binary'],binary_sha256=build['binary_sha256'],finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped',error=repr(error),traceback=traceback.format_exc(),finished_epoch=time.time())
    raise
finally:serial.save(record,state)
