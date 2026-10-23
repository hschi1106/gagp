"""Add a bounded512-thread view specialization without changing logical lanes."""
from pathlib import Path
import fcntl
import hashlib
import json
import time

root=Path.cwd(); raw=root/'logs/twofold-optimization-20261022'
changes={}

def replace(path,old,new):
    p=root/path;text=changes.get(p,p.read_text())
    assert text.count(old)==1,(path,old[:70],text.count(old))
    changes[p]=text.replace(old,new)

with Path('/tmp/gagp-support-20261015-gpu.lock').open('a') as lock:
    fcntl.flock(lock,fcntl.LOCK_EX)
    build=json.loads((raw/'build-attempt7/manifest.json').read_text())
    assert build['status']=='waiting','source must be frozen once build7 starts'
    record=raw/'relaxed-view-source-apply.json';assert not record.exists()
    replace('cpp/src/runtime/gpu/device/kernels.cuh',
        'template <DPayloadFlavor Flavor, bool EnableRegions = false, bool DirectRoot = false>',
        '''template <DPayloadFlavor Flavor, bool EnableRegions = false, bool DirectRoot = false,
    int LaunchThreads = (Flavor == DPayloadFlavor::ArenaMixed || Flavor == DPayloadFlavor::IndexedArena) ? 32 : 1024>''')
    replace('cpp/src/runtime/gpu/device/kernels.cuh',
        '''__global__ __launch_bounds__((Flavor == DPayloadFlavor::ArenaMixed ||
    Flavor == DPayloadFlavor::IndexedArena) ? 32 : 1024) void evaluate_fitness_programs_impl(''',
        '''__global__ __launch_bounds__(LaunchThreads) void evaluate_fitness_programs_impl(''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  const auto pack_t1 = std::chrono::steady_clock::now();',
        '''  const auto* relaxed_setting=std::getenv("GAGP_GPU_RELAXED_VIEW_REGISTERS");
  const bool request_relaxed=relaxed_setting && std::string(relaxed_setting)=="1";
  const bool relaxed_view_registers=request_relaxed && direct_root && execution_threads<=512;
  const auto pack_t1 = std::chrono::steady_clock::now();''')
    p=root/'cpp/src/runtime/gpu/fitness_gpu.cu';s=changes[p]
    begin=s.index('    } else if (direct_root) {')
    end=s.index('    } else if (direct_phases) {',begin)
    body=s[begin:end];original=body[len('    } else if (direct_root) {'):]
    relaxed=original.replace('DPayloadFlavor::BoundIntListViews, true, true>',
                            'DPayloadFlavor::BoundIntListViews, true, true,512>')
    assert relaxed!=original
    changes[p]=s[:begin]+'''    } else if (direct_root) {
      if(relaxed_view_registers) {'''+relaxed+'''      } else {'''+original+'''      }
'''+s[end:]
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(impl_->shape_cache_requested)out.execution_profile+=',
        '''    if(request_relaxed)out.execution_profile+=relaxed_view_registers?"-view-reg512":"-view-reg512-declined";
    if(impl_->shape_cache_requested)out.execution_profile+=''')
    replace('cpp/tests/gpu/test_gpu_phase_population.cu',
        'setenv("GAGP_REUSE_IMPORTED_EXECUTABLE","1",1);setenv("GAGP_REUSE_PHASE_GRAMMAR","1",1);',
        'setenv("GAGP_GPU_RELAXED_VIEW_REGISTERS","1",1);setenv("GAGP_REUSE_IMPORTED_EXECUTABLE","1",1);setenv("GAGP_REUSE_PHASE_GRAMMAR","1",1);')
    p=root/'cpp/tests/gpu/test_bounded_region_gpu.cpp';s=p.read_text()
    start=s.index('bool test_shape_result_cache()');stop=s.index('\nint main()',start)
    part=s[start:stop]
    part=part.replace('"GAGP_SORT_CASES","GAGP_MEASURE_PHASE_CACHE","GAGP_GPU_SHAPE_RESULT_CACHE"',
                      '"GAGP_SORT_CASES","GAGP_MEASURE_PHASE_CACHE","GAGP_GPU_SHAPE_RESULT_CACHE","GAGP_GPU_RELAXED_VIEW_REGISTERS"')
    part=part.replace('saved[11];bool present[11]', 'saved[12];bool present[12]').replace('i<11;', 'i<12;')
    part=part.replace('  const auto excluded=saturated.eval_programs({shape},true);',
        '''  const auto excluded=saturated.eval_programs({shape},true);
  // Requests outside the512-thread specialization retain the original kernel.
  FitnessSessionGpu wide;
  if(!wide.init(cases,answers,1,1024,7,policy).ok)return false;
  const auto fallback=wide.eval_programs({shape},true);
  if(!check(fallback.ok && fallback.execution_profile.find("-view-reg512-declined")!=std::string::npos &&
      fallback.fitness==eval_fitness_cpu({shape},cases,answers,1,7,1024,policy),
      "relaxed-register kernel lost1024-thread fallback"))return false;''')
    changes[p]=s[:start]+part+s[stop:]
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}
    for p,text in changes.items():p.write_text(text)
    record.write_text(json.dumps(dict(status='applied_before_build7',epoch=time.time(),
        source_before=before,source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    print('applied relaxed view-register specialization',len(changes),'files')
