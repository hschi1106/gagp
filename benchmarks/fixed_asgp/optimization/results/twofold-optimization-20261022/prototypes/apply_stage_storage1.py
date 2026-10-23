"""Integrate sequential16GiB storage after immutable18 and allocator preflight."""
from pathlib import Path
import hashlib
import json

root=Path.cwd();base=Path(__file__).resolve().parents[1];changes={}
def replace(relative,old,new):
    path=root/relative;text=changes.get(path,path.read_text())
    assert text.count(old)==1,(relative,old[:90],text.count(old));changes[path]=text.replace(old,new)

def apply(dry_run=False):
    changes.clear()
    prior=json.loads((root/'logs/twofold-optimization-20261022/build-attempt18/manifest.json').read_text())
    assert prior['status']=='ok'
    for path,digest in prior['source_sha256'].items():assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
    for original,target in [('cuda_stage_storage.hpp','cpp/src/runtime/gpu/cuda_stage_storage.hpp'),
            ('stage_storage_gpu.hpp','cpp/src/runtime/gpu/stage_storage_gpu.hpp'),
            ('stage_storage1.cu','cpp/src/runtime/gpu/stage_storage.cu')]:
        path=root/target;assert not path.exists();changes[path]=(base/'prototypes'/original).read_text()
    path=root/'cpp/tests/gpu/test_cuda_stage_storage.cu';assert not path.exists()
    text=(base/'prototypes/test_stage_storage1.cu').read_text()
    for header in ('cuda_stage_storage.hpp','stage_storage_gpu.hpp','cuda_allocation_scope.hpp'):
        text=text.replace('#include "'+header+'"','#include "../../src/runtime/gpu/'+header+'"')
    changes[path]=text
    replace('cpp/src/runtime/gpu/allocation_pool.cu','#include "cuda_allocation_scope.hpp"',
        '#include "cuda_allocation_scope.hpp"\n#include "stage_storage_gpu.hpp"')
    replace('cpp/src/runtime/gpu/allocation_pool.cu','cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes) {',
        'cudaError_t cuda_pool_malloc(void** pointer,std::size_t bytes) {\n  if(cuda_stage_reproduction_active())return cuda_stage_workspace_malloc(pointer,bytes);')
    replace('cpp/src/runtime/gpu/allocation_pool.cu','cudaError_t cuda_pool_free(void* pointer) {',
        'cudaError_t cuda_pool_free(void* pointer) {\n  cudaError_t stage_status;\n  if(cuda_stage_free_if_owned(pointer,&stage_status))return stage_status;')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','#include "private_tuple_policy.hpp"',
        '#include "private_tuple_policy.hpp"\n#include "stage_storage_gpu.hpp"')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu',
        'gpu_detail::cuda_pool_malloc(&data,bytes)!=cudaSuccess',
        'gpu_detail::cuda_stage_workspace_malloc(&data,bytes)!=cudaSuccess')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','if(gpu_detail::cuda_pool_malloc(p,bytes)!=cudaSuccess)return false;',
        'if(gpu_detail::cuda_stage_workspace_malloc(p,bytes)!=cudaSuccess)return false;')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','#include "completed_fitness_reuse.hpp"',
        '#include "completed_fitness_reuse.hpp"\n#include "../../../runtime/gpu/cuda_stage_storage.hpp"')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','__global__ void annotate_import(',r'''__global__ void copy_current_population(TreePointers source,TreePointers target,int rows) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;
 if(row<rows)copy_tree(source,row,target,row);
}
__global__ void annotate_import(''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' std::vector<std::unique_ptr<UploadedProfile>> profiles;Buffer<DeviceSlot> slots;',
        ' std::unique_ptr<gpu_detail::CudaStageStorage> stage_storage;\n std::vector<std::unique_ptr<UploadedProfile>> profiles;Buffer<DeviceSlot> slots;')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' phase_layout_require(site_depth>=0 && site_depth<=15',
        ' gpu_detail::CudaStageAllocationScope construction_storage(nullptr,false);\n phase_layout_require(site_depth>=0 && site_depth<=15')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' // Persistent buffers are bounded by population',r''' const auto* storage_flag=std::getenv("GAGP_GPU_STAGE_STORAGE");
 if(storage_flag)phase_layout_require(std::string(storage_flag)=="0" || std::string(storage_flag)=="1","stage storage flag must be0 or1");
 const bool standalone_storage=storage_flag && std::string(storage_flag)=="1";
 const auto* pool_flag=std::getenv("GAGP_GPU_CUDA_POOL");
 const bool requested_storage=standalone_storage || (pool_flag && std::string(pool_flag)=="1");
 if(requested_storage && impl_->staged_buffers && scratch_budget) {
  impl_->stage_storage=std::make_unique<gpu_detail::CudaStageStorage>(scratch_budget);
  impl_->reproduction_profile+="-stage-storage";
 } else if(standalone_storage)impl_->reproduction_profile+="-stage-storage-declined";
 // Persistent buffers are bounded by population''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        'FitnessEvalResult NativePhasePopulation::evaluate(FitnessSessionGpu& session,bool diagnostics)const {\n impl_->require_device();',
        'FitnessEvalResult NativePhasePopulation::evaluate(FitnessSessionGpu& session,bool diagnostics)const {\n impl_->require_device();\n gpu_detail::CudaStageAllocationScope evaluation_storage(impl_->stage_storage.get(),false);')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','   result.payload_workspace_retained_bytes=0;',
        '   result.payload_workspace_retained_bytes=0;\n   if(impl_->stage_storage && impl_->stage_storage->live_allocations())throw std::runtime_error("evaluation left live stage scratch");')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");',
        ' for(double value:completed_fitness)phase_layout_require(!std::isnan(value),"uncanonical fitness");\n gpu_detail::CudaStageAllocationScope reproduction_storage(impl_->stage_storage.get(),true);')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' compare_offspring<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),p,k,impl_->stats.data);checked(cudaGetLastError());',
        ' compare_offspring<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),p,k,impl_->stats.data);checked(cudaGetLastError());\n if(impl_->stage_storage){copy_current_population<<<(p*k+127)/128,128>>>(pointers(*offspring),pointers(*impl_->current),p*k);checked(cudaGetLastError());}')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' impl_->current.swap(offspring);impl_->refresh_programs(&timing);',
        ' if(!impl_->stage_storage)impl_->current.swap(offspring);impl_->refresh_programs(&timing);')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' impl_->healthy=true;return timing;',
        ' if(impl_->stage_storage && impl_->stage_storage->live_allocations())throw std::runtime_error("reproduction left live stage scratch");\n impl_->healthy=true;return timing;')
    replace('cpp/src/evolution/repro/gpu/phase_population.hpp','  std::size_t device_bytes() const;',
        '  std::size_t device_bytes() const;\n  std::size_t stage_storage_reserved_bytes() const;\n  std::size_t stage_storage_peak_bytes() const;')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','std::vector<std::array<unsigned,2>> NativePhasePopulation::site_metrics()const {',
        'std::size_t NativePhasePopulation::stage_storage_reserved_bytes()const{return impl_->stage_storage?impl_->stage_storage->reserved_bytes():0;}\nstd::size_t NativePhasePopulation::stage_storage_peak_bytes()const{return impl_->stage_storage?impl_->stage_storage->peak_bytes():0;}\nstd::vector<std::array<unsigned,2>> NativePhasePopulation::site_metrics()const {')
    replace('cpp/include/gagp/evolution/evolve.hpp','  std::uint64_t genotype_device_bytes = 0;',
        '  std::uint64_t genotype_device_bytes = 0;\n  std::uint64_t gpu_stage_storage_reserved_bytes = 0;\n  std::uint64_t gpu_stage_storage_peak_bytes = 0;')
    replace('cpp/src/evolution/evolve.cpp','  result.reproduction_profile = owner->profile_name();',
        '  result.reproduction_profile = owner->profile_name();\n  result.gpu_stage_storage_reserved_bytes=owner->stage_storage_reserved_bytes();')
    path=root/'cpp/src/evolution/evolve.cpp';text=changes[path]
    old='result.genotype_device_bytes=std::max<std::uint64_t>(result.genotype_device_bytes,owner->device_bytes());'
    assert text.count(old)==2
    changes[path]=text.replace(old,old+'\n    result.gpu_stage_storage_peak_bytes=owner->stage_storage_peak_bytes();')
    path=root/'cpp/src/bench/fixed_asgp/main.cpp';text=path.read_text();old='{"genotype_device_bytes",number(result.genotype_device_bytes)}';assert text.count(old)==2
    changes[path]=text.replace(old,old+', {"gpu_stage_storage_reserved_bytes",number(result.gpu_stage_storage_reserved_bytes)}, {"gpu_stage_storage_peak_bytes",number(result.gpu_stage_storage_peak_bytes)}')
    replace('cpp/CMakeLists.txt','    src/runtime/gpu/allocation_pool.cu','    src/runtime/gpu/allocation_pool.cu\n    src/runtime/gpu/stage_storage.cu')
    replace('cpp/CMakeLists.txt','  add_executable(gagp_test_cuda_allocation_pool tests/gpu/test_cuda_allocation_pool.cu)',
        '''  add_executable(gagp_test_cuda_stage_storage tests/gpu/test_cuda_stage_storage.cu)
  target_link_libraries(gagp_test_cuda_stage_storage PRIVATE gagp_gpu)
  add_test(NAME gagp_test_cuda_stage_storage COMMAND gagp_test_cuda_stage_storage)
  set_tests_properties(gagp_test_cuda_stage_storage PROPERTIES LABELS "contract;gpu")

  add_executable(gagp_test_cuda_allocation_pool tests/gpu/test_cuda_allocation_pool.cu)''')
    # Existing pool fixture now exercises automatic storage for staged owners;
    # also test its interaction with exact completed-fitness reuse.
    path=root/'cpp/tests/gpu/test_gpu_phase_population.cu';text=path.read_text()
    text=text.replace('#include "../../src/runtime/gpu/cuda_allocation_scope.hpp"','#include "../../src/runtime/gpu/cuda_allocation_scope.hpp"\n#include "../../src/runtime/gpu/cuda_stage_storage.hpp"')
    start=text.index('void check_pool_evolution(');end=text.index('void check_pool_session_lifetime(',start);fixture=text[start:end]
    fixture=fixture.replace('"GAGP_GPU_PHASE_POPULATION"};','"GAGP_GPU_PHASE_POPULATION","GAGP_GPU_UNCHANGED_FITNESS_REUSE","GAGP_GPU_STAGE_STORAGE","GAGP_GPU_GLOBAL_PAYLOAD","GAGP_GPU_PAYLOAD_WORKSPACE_MIB"};')
    fixture=fixture.replace('saved[3];bool present[3]','saved[7];bool present[7]').replace('i<3','i<7')
    fixture=fixture.replace(' setenv(flags[2],"1",1);',' setenv(flags[2],"1",1);setenv(flags[4],"0",1);setenv(flags[5],"1",1);setenv(flags[6],"16384",1);')
    fixture=fixture.replace('setenv(flags[0],"0",1);','setenv(flags[0],"0",1);setenv(flags[3],"0",1);')
    fixture=fixture.replace('const auto initial=gpu_detail::cuda_pool_debug_stats();setenv(flags[0],"1",1);',
        'const auto initial=gpu_detail::cuda_pool_debug_stats();const auto stage_initial=gpu_detail::cuda_stage_debug_stats();setenv(flags[0],"1",1);setenv(flags[3],"1",1);')
    fixture=fixture.replace('  const auto final=gpu_detail::cuda_pool_debug_stats();',r'''  require((after.gpu_stage_storage_reserved_bytes>0)==staged &&
      (!staged || (after.gpu_stage_storage_reserved_bytes==UINT64_C(16384)*1024*1024+64*1024 &&
                   after.gpu_stage_storage_peak_bytes>0 && after.gpu_stage_storage_peak_bytes<=after.gpu_stage_storage_reserved_bytes)),
      "staged storage not active or requested workspace capacity changed");
  const auto stage_final=gpu_detail::cuda_stage_debug_stats();
  require(stage_final.banks_alive==stage_initial.banks_alive && stage_final.live_allocations==stage_initial.live_allocations &&
      stage_final.cleanup_errors==stage_initial.cleanup_errors && stage_final.failed_allocations==stage_initial.failed_allocations &&
      (!staged || stage_final.allocations>stage_initial.allocations),"shared stages leaked, failed or did not allocate");
  const auto final=gpu_detail::cuda_pool_debug_stats();''')
    changes[path]=text[:start]+fixture+text[end:]
    path=root/'docs/design/gpu-reproduction.md';changes[path]=path.read_text()+'''

## Shared storage for sequential GPU stages

The staged native path uses one backing for reproduction scratch and evaluation
workspace. Pooled staged owners activate it automatically; `GAGP_GPU_STAGE_STORAGE=1`
also enables it with the metadata pool disabled. The configured evaluation cap
remains16GiB. Another64KiB covers allocator alignment only. At most256 transient
allocations are tracked; unsupported capacity or lifetime overlap is an error.

The current population stays outside this backing. After variation and exact
parent comparison, a GPU kernel copies active offspring nodes, origins, sites
and constants into that fixed current storage. It is inside reproduction kernel
timing. All scratch retires before the next stage. This prevents surviving
offspring from fragmenting a freed16GiB workspace allocation. Default-stream
ordering, RNG, operators, AST/depth/phase limits, scoring and replay are unchanged.
Physical reserved/peak stage bytes are reported separately from logical genotype
allocation sizes and the whole-card sampled VRAM peak.
'''
    if dry_run:return changes
    record=root/'logs/twofold-optimization-20261022/stage-storage-source-apply.json';assert not record.exists()
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in changes}
    for path,text in changes.items():path.write_text(text)
    record.write_text(json.dumps(dict(status='applied_after_pinned18_and_stage_preflight1',source_before=before,
        source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    return list(before)
