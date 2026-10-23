"""Apply next prototype only after build6 source-guard/checks have finished."""
from pathlib import Path

ROOT=Path.cwd()
BASE=Path(__file__).resolve().parents[1]


def replace(path,old,new):
    target=ROOT/path;text=target.read_text()
    assert text.count(old)==1,(path,old[:100],text.count(old))
    target.write_text(text.replace(old,new))


def apply():
    import json
    build=json.loads((ROOT/'logs/twofold-optimization-20261022/build-attempt6/manifest.json').read_text())
    assert build['status'] in ('ok','failed'),'never edit a live build source'
    target=ROOT/'cpp/src/runtime/gpu/shape_result_cache.hpp'
    assert not target.exists()
    target.write_text((BASE/'prototypes/shape_result_cache.hpp').read_text())
    replace('cpp/src/runtime/gpu/case_reuse.hpp','} // namespace gagp::gpu_detail',
        (BASE/'prototypes/shape_case_plan.txt').read_text()+'\n} // namespace gagp::gpu_detail')
    replace('cpp/include/gagp/runtime/gpu/device_types_gpu.hpp','  int region_count = 0;','  int region_count = 0;\n  bool shape_result_cache = false;')
    replace('cpp/include/gagp/runtime/gpu/region_types_gpu.hpp','  bool simple_phases = false;','''  bool simple_phases = false;
  const int* shape_case_groups = nullptr;
  const int* shape_case_representatives = nullptr;
  int shape_result_count = 0;
  std::size_t shape_result_offset = 0;''')
    replace('cpp/include/gagp/runtime/gpu/region_types_gpu.hpp','#include <cstdint>','#include <cstdint>\n#include <cstddef>')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','#include "case_reuse.hpp"','#include "case_reuse.hpp"\n#include "shape_result_cache.hpp"')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  bool case_reuse_requested = false;','''  bool case_reuse_requested = false;
  gpu_detail::ShapeCasePlan shape_case_plan;
  int* d_shape_representatives = nullptr;
  int* d_shape_groups = nullptr;
  bool shape_cache_requested = false;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if (d_reuse_weights) cudaFree(d_reuse_weights);','''    if (d_reuse_weights) cudaFree(d_reuse_weights);
    if (d_shape_representatives) cudaFree(d_shape_representatives);
    if (d_shape_groups) cudaFree(d_shape_groups);''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  impl_->host_case_local_vals = packed_case_local_vals;','''  impl_->host_case_local_vals = packed_case_local_vals;
  const auto* shape_setting=std::getenv("GAGP_GPU_SHAPE_RESULT_CACHE");
  impl_->shape_cache_requested=shape_setting && std::string(shape_setting)=="1";
  impl_->shape_case_plan=impl_->shape_cache_requested?
      gpu_detail::shape_case_plan(packed_case_local_vals,packed_case_local_set):gpu_detail::ShapeCasePlan{};''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  if(impl_->d_reuse_representatives){cudaFree(impl_->d_reuse_representatives);impl_->d_reuse_representatives=nullptr;}','''  if(impl_->d_shape_representatives){cudaFree(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}
  if(impl_->d_shape_groups){cudaFree(impl_->d_shape_groups);impl_->d_shape_groups=nullptr;}
  if(!impl_->shape_case_plan.representatives.empty() &&
      (!gpu_detail::cuda_alloc_and_copy_in(impl_->shape_case_plan.representatives,&impl_->d_shape_representatives) ||
       !gpu_detail::cuda_alloc_and_copy_in(impl_->shape_case_plan.groups,&impl_->d_shape_groups)))
    return fitness_init_single_error(ErrCode::Value,"cuda shape-result cache input allocation failure");
  if(impl_->d_reuse_representatives){cudaFree(impl_->d_reuse_representatives);impl_->d_reuse_representatives=nullptr;}''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  // A tile retains one logical warp; an untiled block retains all logical','''  std::size_t shape_result_programs=0;
  if(direct_root && !impl_->shape_case_plan.representatives.empty() &&
      std::all_of(payload_pack.list_entries.begin(),payload_pack.list_entries.end(),[](const auto& entry) {
        return entry.len>=0 && entry.len<65536 &&
            entry.len==static_cast<int>(static_cast<std::uint64_t>(entry.packed)>>48);
      })) {
    for(auto& meta:packed.metas) {
      meta.shape_result_cache=gpu_detail::shape_result_program_supported(packed,meta);
      shape_result_programs+=meta.shape_result_cache;
    }
    for(int index:generic_indices) {
      if(packed.metas[index].shape_result_cache)--shape_result_programs;
      packed.metas[index].shape_result_cache=false;
    }
    const auto offset=(shared_bytes+alignof(gpu_detail::DResult)-1)&~(alignof(gpu_detail::DResult)-1);
    const auto bytes=offset+sizeof(gpu_detail::DResult)*impl_->shape_case_plan.representatives.size();
    if(shape_result_programs && bytes<=static_cast<std::size_t>(impl_->props.sharedMemPerBlock)) {
      region_workspace.shape_case_groups=impl_->d_shape_groups;
      region_workspace.shape_case_representatives=impl_->d_shape_representatives;
      region_workspace.shape_result_count=impl_->shape_case_plan.representatives.size();
      region_workspace.shape_result_offset=offset;shared_bytes=bytes;
    } else {
      shape_result_programs=0;
      for(auto& meta:packed.metas)meta.shape_result_cache=false;
    }
  }
  // A tile retains one logical warp; an untiled block retains all logical''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  out.physical_case_count=reuse_integer_cases?impl_->integer_case_reuse.representatives.size():impl_->shared_case_count;','''  out.physical_case_count=reuse_integer_cases?impl_->integer_case_reuse.representatives.size():impl_->shared_case_count;
  out.shape_result_programs=shape_result_programs;
  out.shape_result_groups=region_workspace.shape_result_count;
  out.physical_program_cases=(programs.size()-shape_result_programs)*out.physical_case_count+
      shape_result_programs*region_workspace.shape_result_count;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(request_simple)out.execution_profile+=','''    if(impl_->shape_cache_requested)out.execution_profile+=shape_result_programs?"-shape-result-cache":"-shape-result-cache-declined";
    if(request_simple)out.execution_profile+=''')
    replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp','  std::size_t simple_phase_count = 0; // Static admitted phases, not invocation counts.','''  std::size_t simple_phase_count = 0; // Static admitted phases, not invocation counts.
  std::uint64_t physical_program_cases = 0;
  std::uint64_t shape_result_programs = 0, shape_result_groups = 0;''')
    replace('cpp/include/gagp/evolution/timing.hpp','  std::uint64_t simple_phase_count = 0;','  std::uint64_t simple_phase_count = 0;\n  std::uint64_t shape_result_programs = 0, shape_result_groups = 0;')
    replace('cpp/src/evolution/timing.cpp','  total->simple_phase_count += value.simple_phase_count;','''  total->simple_phase_count += value.simple_phase_count;
  total->shape_result_programs += value.shape_result_programs;
  total->shape_result_groups += value.shape_result_groups;''')
    for prefix,pop in [('evaluation.timing.','population.size()'),('timing.','population.size()')]:
        replace('cpp/src/evolution/evolve.cpp',f'{prefix}physical_program_cases=fit.physical_case_count*{pop};',
            f'''{prefix}physical_program_cases=fit.physical_program_cases;
  {prefix}shape_result_programs=fit.shape_result_programs;
  {prefix}shape_result_groups=fit.shape_result_groups;''')
    replace('cpp/src/bench/fixed_asgp/main.cpp','      {"simple_phase_count",number(e.simple_phase_count)},','''      {"simple_phase_count",number(e.simple_phase_count)},
      {"shape_result_programs",number(e.shape_result_programs)},
      {"shape_result_groups",number(e.shape_result_groups)},''')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','  const int lane_begin = tiled_replay ? tile * 32 : 0;','''  DResult* shape_results=nullptr;
  if constexpr(DirectRoot) {
    if(meta.shape_result_cache && workspace.shape_result_count>0) {
      shape_results=reinterpret_cast<DResult*>(shared_bytes+workspace.shape_result_offset);
      for(int group=tid;group<workspace.shape_result_count;group+=blockDim.x) {
        const int input=workspace.shape_case_representatives[group];
        shape_results[group]=d_execute_region_root(meta,shared_code,all_consts,shared_case_local_vals,
            shared_case_local_set,payload_tables,execution_tables,input,fuel,workspace);
      }
      __syncthreads();
    }
  }
  const int lane_begin = tiled_replay ? tile * 32 : 0;''')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','''      result = d_execute_region_root(meta, shared_code, all_consts, shared_case_local_vals,
          shared_case_local_set, payload_tables, execution_tables, input_case, fuel, workspace);''','''      result = shape_results?shape_results[workspace.shape_case_groups[input_case]]:
          d_execute_region_root(meta, shared_code, all_consts, shared_case_local_vals,
              shared_case_local_set, payload_tables, execution_tables, input_case, fuel, workspace);''')


if __name__=='__main__':apply()
