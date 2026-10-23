"""Correct the conflicting native test, preserving failed25 and unchanged production sources."""
import fcntl
import os
from pathlib import Path
import shutil
import time
import traceback

import build_cell_soa
import serial

record = serial.RAW / 'private-adjacent-candidate1-controller.json'
assert not record.exists()
state = dict(pid=os.getpid(), status='waiting_for_gpu_lock', started_epoch=time.time(), deadline_epoch=serial.DEADLINE,
             helper_sha256=serial.sha(Path(__file__)), reason='test25 inherited SoA=1 but incorrectly demanded an enabled contiguous-only route; split admitted and declined cases; preserve failed attempt')
serial.save(record, state)
try:
    previous = serial.read(serial.RAW / 'build-attempt25/manifest.json')
    assert previous['status'] == 'failed' and previous['results'] == [0, 0, 8]
    proc = Path(f'/proc/{previous["pid"]}/stat')
    assert not proc.exists() or proc.read_text().split()[2] == 'Z'
    with serial.GPU_LOCK.open('a') as lock:
        fcntl.flock(lock, fcntl.LOCK_EX)
        assert time.time() < serial.DEADLINE - 10800
        for path, digest in previous['source_sha256'].items():
            assert serial.sha(serial.ROOT / path) == digest
        backup = serial.RAW / 'failed25-artifacts0'
        backup.mkdir()
        binaries = {}
        for filename in ('gagp_fixed_asgp_bench', 'gagp_test_gpu_phase_population'):
            target = backup / filename
            shutil.copy2(serial.ROOT / 'cpp/build/release' / filename, target)
            binaries[str(target)] = serial.sha(target)
        serial.save(backup / 'manifest.json', dict(status='preserved_failed_build', binaries=binaries,
                    source_build='build-attempt25', native_status='14/15 passed; phase test failed; not admitted', epoch=time.time()))
        path = serial.ROOT / 'cpp/tests/gpu/test_gpu_phase_population.cu'
        original = path.read_text()
        start = original.index('  const char* adjacent_prior=')
        end = original.index('  const char* launch_prior=', start)
        replacement = '''  const char* adjacent_prior=std::getenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  const bool adjacent_present=adjacent_prior!=nullptr;
  const std::string adjacent_saved=adjacent_prior?adjacent_prior:"";
  const char* soa_prior=std::getenv("GAGP_GPU_PRIVATE_CELL_SOA");
  const bool soa_present=soa_prior!=nullptr;
  const std::string soa_saved=soa_prior?soa_prior:"";
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","0",1);
  const auto adjacent_off=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","1",1);
  const auto adjacent_current=owner.evaluate(session,true);
  require(adjacent_current.ok && adjacent_off.ok && adjacent_current.fitness==adjacent_off.fitness &&
      adjacent_current.wrong==adjacent_off.wrong && adjacent_current.case_counts==adjacent_off.case_counts &&
      adjacent_current.fitness==gpu.fitness && adjacent_current.wrong==gpu.wrong,
      "private adjacent request changed results with existing layout");
  require(adjacent_current.execution_profile.find("-private-adjacent-concat")!=std::string::npos &&
      (adjacent_current.execution_profile.find("-private-adjacent-concat-declined")!=std::string::npos)==(soa_saved=="1"),
      "private adjacent admission must decline the existing interleaved layout");
  setenv("GAGP_GPU_PRIVATE_CELL_SOA","0",1);
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","0",1);
  const auto adjacent_contiguous_off=owner.evaluate(session,true);
  setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT","1",1);
  const auto adjacent_contiguous_on=owner.evaluate(session,true);
  if(soa_present)setenv("GAGP_GPU_PRIVATE_CELL_SOA",soa_saved.c_str(),1);
  else unsetenv("GAGP_GPU_PRIVATE_CELL_SOA");
  if(adjacent_present)setenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT",adjacent_saved.c_str(),1);
  else unsetenv("GAGP_GPU_PRIVATE_ADJACENT_CONCAT");
  require(adjacent_contiguous_on.ok && adjacent_contiguous_off.ok &&
      adjacent_contiguous_on.fitness==adjacent_contiguous_off.fitness &&
      adjacent_contiguous_on.wrong==adjacent_contiguous_off.wrong &&
      adjacent_contiguous_on.case_counts==adjacent_contiguous_off.case_counts &&
      adjacent_contiguous_on.fitness==gpu.fitness && adjacent_contiguous_on.wrong==gpu.wrong &&
      adjacent_contiguous_on.execution_profile.find("-private-adjacent-concat")!=std::string::npos &&
      adjacent_contiguous_on.execution_profile.find("-private-adjacent-concat-declined")==std::string::npos,
      "private adjacent contiguous backing changed full-case cross-generation results");
'''
        path.write_text(original[:start] + replacement + original[end:])
        for name, digest in previous['source_sha256'].items():
            if name != 'cpp/tests/gpu/test_gpu_phase_population.cu':
                assert serial.sha(serial.ROOT / name) == digest, 'production source changed in test correction'
        state.update(status='test_only_correction_applied', preserved_binaries=binaries,
                     changed_paths=['cpp/tests/gpu/test_gpu_phase_population.cu'])
        serial.save(record, state)
    build_cell_soa.run('build-attempt26')
    build = serial.read(serial.RAW / 'build-attempt26/manifest.json')
    assert build['status'] == 'ok' and build['results'] == [0, 0, 0]
    state.update(status='ok', binary=build['binary'], binary_sha256=build['binary_sha256'],
                 same_benchmark_as_failed25=build['binary_sha256'] == binaries[str(backup / 'gagp_fixed_asgp_bench')],
                 finished_epoch=time.time())
except BaseException as error:
    state.update(status='stopped', error=repr(error), traceback=traceback.format_exc(), finished_epoch=time.time())
    raise
finally:
    serial.save(record, state)
