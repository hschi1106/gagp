"""Apply the preflight-checked pool to the strict native default-stream path."""
from pathlib import Path
import hashlib
import json

root=Path.cwd();base=Path(__file__).resolve().parents[1];changes={}


def replace(relative,old,new):
    path=root/relative;text=changes.get(path,path.read_text())
    assert text.count(old)==1,(relative,old[:80],text.count(old));changes[path]=text.replace(old,new)


def apply(dry_run=False):
    previous=json.loads((root/'logs/twofold-optimization-20261022/build-attempt13/manifest.json').read_text())
    if not dry_run:
        assert previous['status']=='ok'
        for path,digest in previous['source_sha256'].items():
            assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,'source differs from pinned13: '+path
    for name,relative in [('cuda_allocation_scope.hpp','cpp/src/runtime/gpu/cuda_allocation_scope.hpp'),
            ('allocation_gpu.hpp','cpp/include/gagp/runtime/gpu/allocation_gpu.hpp'),
            ('allocation_pool.cu','cpp/src/runtime/gpu/allocation_pool.cu')]:
        path=root/relative;assert not path.exists();changes[path]=(base/'prototypes'/name).read_text()
    path=root/'cpp/tests/gpu/test_cuda_allocation_pool.cu';assert not path.exists()
    changes[path]=(base/'prototypes/test_cuda_allocation_pool.cu').read_text().replace(
        '#include "cuda_allocation_scope.hpp"','#include "../../src/runtime/gpu/cuda_allocation_scope.hpp"')
    replace('cpp/include/gagp/runtime/gpu/host_pack_gpu.hpp','#include <cuda_runtime.h>',
        '#include <cuda_runtime.h>\n#include "gagp/runtime/gpu/allocation_gpu.hpp"')
    for relative,prefix in [('cpp/include/gagp/runtime/gpu/host_pack_gpu.hpp',''),
            ('cpp/src/runtime/gpu/host_pack_gpu.cu',''),('cpp/src/runtime/gpu/fitness_gpu.cu','gpu_detail::'),
            ('cpp/src/evolution/repro/gpu/phase_population.cu','gpu_detail::')]:
        path=root/relative;text=changes.get(path,path.read_text());assert 'cudaFree(' in text
        changes[path]=text.replace('cudaMalloc(',prefix+'cuda_pool_malloc(').replace('cudaFree(',prefix+'cuda_pool_free(')
    replace('cpp/src/evolution/evolve.cpp','#include "repro/gpu/phase_population.hpp"',
        '#include "repro/gpu/phase_population.hpp"\n#include "../runtime/gpu/cuda_allocation_scope.hpp"')
    replace('cpp/src/evolution/evolve.cpp','  std::unique_ptr<repro::NativePhasePopulation> owner;',
        '  gpu_detail::CudaAllocationScope allocation_scope;\n  std::unique_ptr<repro::NativePhasePopulation> owner;')
    replace('cpp/src/evolution/evolve.cpp',
        '''    result.reproduction_fallback_reason = error.what();
    result.timing.init_population_ms += elapsed(begin);
    return false;''',
        '''    result.reproduction_fallback_reason = error.what();
    result.timing.init_population_ms += elapsed(begin);
    owner.reset();allocation_scope.finish();
    return false;''')
    replace('cpp/src/evolution/evolve.cpp','  result.reproduction_profile = owner->profile_name();',
        '  result.reproduction_profile = owner->profile_name();\n  if(allocation_scope.used())result.reproduction_profile+="-cuda-pool";')
    replace('cpp/src/evolution/evolve.cpp','''    result.timing.final_eval_ms=elapsed(start);
  }
  return true;''','''    result.timing.final_eval_ms=elapsed(start);
  }
  owner.reset();allocation_scope.finish();
  return true;''')
    replace('cpp/CMakeLists.txt','    src/runtime/gpu/fitness_gpu.cu',
        '    src/runtime/gpu/fitness_gpu.cu\n    src/runtime/gpu/allocation_pool.cu')
    replace('cpp/CMakeLists.txt','  add_executable(gagp_test_phase_register_gpu tests/gpu/test_phase_register_gpu.cu)',
        '''  add_executable(gagp_test_cuda_allocation_pool tests/gpu/test_cuda_allocation_pool.cu)
  target_link_libraries(gagp_test_cuda_allocation_pool PRIVATE gagp_gpu)
  add_test(NAME gagp_test_cuda_allocation_pool COMMAND gagp_test_cuda_allocation_pool)
  set_tests_properties(gagp_test_cuda_allocation_pool PROPERTIES LABELS "contract;gpu" SKIP_RETURN_CODE 77)

  add_executable(gagp_test_phase_register_gpu tests/gpu/test_phase_register_gpu.cu)''')
    exec((base/'prototypes/test_pool_evolution.patch.py').read_text(),dict(root=root,base=base,changes=changes,replace=replace))
    for relative,addition in [
      ('docs/design/gpu-reproduction.md','''

## Native GPU allocation scope

`GAGP_GPU_CUDA_POOL=1` enables a dedicated CUDA stream-ordered allocation pool
only during the native phase path, including its cold import. Selection, RNG,
GPU variation, evaluation and physical capacities are unchanged. Generic and
overlap paths keep their existing allocation behavior. The device's current pool
is never changed. Native operations use the existing default stream; this scope
does not promise ordering for arbitrary external streams.

The16GiB release threshold retains unused pool backing between native stages;
it does not reduce or replace the independent16GiB evaluation-workspace budget.
Live genotype and workspace storage can exceed the retention threshold. Scope
completion synchronizes and trims unused backing. Session buffers that outlive
the native owner retain shared ownership; their later matching free releases
the final pool. Pool creation, retirement and cleanup remain inside timed evolve
calls. Unsupported pool devices and allocation errors remain explicit failures.

Allocation ownership lives in `runtime/gpu/allocation_pool.cu`; wrappers are
used by native tree buffers, fitness scratch and host packing. The C++ scope
header is private and CUDA-header-free. `gagp_test_cuda_allocation_pool` checks
stream ordering, dirty reuse, nested scopes, late frees, error forwarding and
unchanged device-pool selection. `gagp_test_gpu_phase_population` compares pooled
and ordinary native searches with full final AST/champion/fitness/wrong/CPU
checks, including private staged-list evolution and an escaped fitness session.
'''),
      ('.agents/skills/gagp-navigation/SKILL.md','''
- Native default-stream allocation scope: `runtime/gpu/cuda_allocation_scope.hpp`
  and `allocation_pool.cu`, C++ caller in `evolution/evolve.cpp`. Pointer ownership
  survives the native scope when an outer session retains workspace; late frees
  release that pool. Public internal wrappers are in
  `include/gagp/runtime/gpu/allocation_gpu.hpp`, called by host packing, fitness
  allocation and native phase buffers. Contract: `docs/design/gpu-reproduction.md`;
  tests `gagp_test_cuda_allocation_pool`, `gagp_test_gpu_phase_population`. This
  does not change physical workspace caps or activate pooling in generic streams.
''')]:
        path=root/relative;changes[path]=changes.get(path,path.read_text())+addition
    if dry_run:return changes
    record=root/'logs/twofold-optimization-20261022/pool-source-apply.json';assert not record.exists()
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in changes}
    for path,text in changes.items():path.write_text(text)
    record.write_text(json.dumps(dict(status='applied_after_pinned13_and_pool_preflight',source_before=before,
        source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    return list(before)
