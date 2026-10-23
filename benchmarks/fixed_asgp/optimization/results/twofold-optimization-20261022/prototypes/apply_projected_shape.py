"""Integrate staged optimizations only after the specified base build is pinned successfully."""
from pathlib import Path
import hashlib
import json

root=Path.cwd();base=Path(__file__).resolve().parents[1]
changes={}


def replace(path,old,new):
    p=root/path;text=changes.get(p,p.read_text())
    assert text.count(old)==1,(path,old[:70],text.count(old))
    changes[p]=text.replace(old,new)


def apply(dry_run=False,previous_build='build-attempt8'):
    build=json.loads((root/'logs/twofold-optimization-20261022'/previous_build/'manifest.json').read_text())
    if not dry_run:
        assert build['status']=='ok','failed/active candidate must be diagnosed before this followup'
        for path,digest in build['source_sha256'].items():
            assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,'source differs from pinned base: '+path
    target=root/'cpp/src/runtime/gpu/shape_partition_cache.hpp';assert not target.exists()
    changes[target]=(base/'prototypes/shape_partition_cache.hpp').read_text()
    changes[root/'cpp/src/runtime/gpu/shape_result_cache.hpp']=(base/'prototypes/projected_shape_result_cache.hpp').read_text()
    replace('cpp/src/runtime/gpu/case_reuse.hpp',
        '''inline ShapeCasePlan shape_case_plan(const std::vector<Value>& values,
    const std::vector<unsigned char>& assigned) {''',
        '''inline ShapeCasePlan shape_case_plan(const std::vector<Value>& values,
    const std::vector<unsigned char>& assigned,std::uint64_t inputs=~std::uint64_t{0}) {''')
    path=root/'cpp/src/runtime/gpu/case_reuse.hpp';text=changes[path]
    split=text.index('inline ShapeCasePlan shape_case_plan(')
    old="""    key.append(reinterpret_cast<const char*>(assigned.data()+row*64),64);
    for(std::size_t local=0;local<64;++local)if(assigned[row*64+local]) {"""
    new="""    for(std::size_t local=0;local<64;++local)if(inputs & (std::uint64_t{1}<<local)) {
      key.push_back(assigned[row*64+local]?1:0);
      if(!assigned[row*64+local])continue;"""
    assert text[split:].count(old)==1
    changes[path]=text[:split]+text[split:].replace(old,new)
    # Exact weighted reuse still keys every effective binding AND its label.
    replace('cpp/src/runtime/gpu/case_reuse.hpp','#include <algorithm>','#include <algorithm>\n#include <cstdint>')
    replace('cpp/include/gagp/runtime/gpu/device_types_gpu.hpp','  bool shape_result_cache = false;',
        '  bool shape_result_cache = false;\n  int shape_case_group_offset=0, shape_representative_offset=0, shape_case_count=0;')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','#include "shape_result_cache.hpp"',
        '#include "shape_result_cache.hpp"\n#include "shape_partition_cache.hpp"')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  gpu_detail::ShapeCasePlan shape_case_plan;',
        '  gpu_detail::ShapePartitionCache shape_partitions;\n  std::vector<unsigned char> host_case_local_set;\n  bool project_shape_inputs=false;')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu',
        '''  impl_->shape_case_plan=impl_->shape_cache_requested?
      gpu_detail::shape_case_plan(packed_case_local_vals,packed_case_local_set):gpu_detail::ShapeCasePlan{};''',
        '''  const auto* project_setting=std::getenv("GAGP_GPU_PROJECT_SHAPE_INPUTS");
  impl_->project_shape_inputs=project_setting && std::string(project_setting)=="1";
  impl_->shape_partitions.clear();
  impl_->host_case_local_set=packed_case_local_set;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu',
        '''  if(!impl_->shape_case_plan.representatives.empty() &&
      (!gpu_detail::cuda_alloc_and_copy_in(impl_->shape_case_plan.representatives,&impl_->d_shape_representatives) ||
       !gpu_detail::cuda_alloc_and_copy_in(impl_->shape_case_plan.groups,&impl_->d_shape_groups)))
    return fitness_init_single_error(ErrCode::Value,"cuda shape-result cache input allocation failure");''','')
    path=root/'cpp/src/runtime/gpu/fitness_gpu.cu';text=changes[path]
    begin=text.index('  std::size_t shape_result_programs=0;')
    end=text.index('  // A tile retains one logical warp;',begin)
    replacement='''  std::size_t shape_result_programs=0,shape_result_invocations=0,max_shape_groups=0;
  if(direct_root && impl_->shape_cache_requested &&
      std::all_of(payload_pack.list_entries.begin(),payload_pack.list_entries.end(),[](const auto& entry) {
        return entry.len>=0 && entry.len<65536 &&
            entry.len==static_cast<int>(static_cast<std::uint64_t>(entry.packed)>>48);
      })) {
    const auto base_cases=reuse_integer_cases?impl_->integer_case_reuse.representatives.size():impl_->shared_case_count;
    for(auto& meta:packed.metas) {
      meta.shape_result_cache=false;meta.shape_case_count=0;
      std::uint64_t inputs=0;
      if(!gpu_detail::shape_result_program_supported(packed,meta,&inputs))continue;
      if(!impl_->project_shape_inputs)inputs=~std::uint64_t{0};
      const auto* partition=impl_->shape_partitions.find_or_add(impl_->host_case_local_vals,impl_->host_case_local_set,inputs);
      if(!partition || static_cast<std::size_t>(partition->count)*4>static_cast<std::size_t>(base_cases))continue;
      meta.shape_result_cache=true;meta.shape_case_count=partition->count;
      meta.shape_case_group_offset=partition->group_offset;
      meta.shape_representative_offset=partition->representative_offset;
    }
    for(int index:generic_indices){packed.metas[index].shape_result_cache=false;packed.metas[index].shape_case_count=0;}
    for(const auto& meta:packed.metas)if(meta.shape_result_cache) {
      ++shape_result_programs;shape_result_invocations+=meta.shape_case_count;
      max_shape_groups=std::max(max_shape_groups,static_cast<std::size_t>(meta.shape_case_count));
    }
    const auto offset=(shared_bytes+alignof(gpu_detail::DResult)-1)&~(alignof(gpu_detail::DResult)-1);
    const auto bytes=offset+sizeof(gpu_detail::DResult)*max_shape_groups;
    if(shape_result_programs && bytes<=static_cast<std::size_t>(impl_->props.sharedMemPerBlock)) {
      region_workspace.shape_result_count=max_shape_groups;
      region_workspace.shape_result_offset=offset;shared_bytes=bytes;
    } else {
      shape_result_programs=shape_result_invocations=max_shape_groups=0;
      for(auto& meta:packed.metas){meta.shape_result_cache=false;meta.shape_case_count=0;}
    }
  }
'''
    changes[path]=text[:begin]+replacement+text[end:]
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','      shape_result_programs*region_workspace.shape_result_count;',
        '      shape_result_invocations;')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','''    const auto upload_t0 = std::chrono::steady_clock::now();
    if (cudaMalloc''','''    const auto upload_t0 = std::chrono::steady_clock::now();
    if(shape_result_programs) {
      if(impl_->shape_partitions.dirty) {
        if(impl_->d_shape_representatives){cudaFree(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}
        if(impl_->d_shape_groups){cudaFree(impl_->d_shape_groups);impl_->d_shape_groups=nullptr;}
        if(!gpu_detail::cuda_alloc_and_copy_in(impl_->shape_partitions.representatives,&impl_->d_shape_representatives) ||
            !gpu_detail::cuda_alloc_and_copy_in(impl_->shape_partitions.groups,&impl_->d_shape_groups))
          return fitness_eval_single_error(ErrCode::Value,"cuda projected shape partition upload failure");
        impl_->shape_partitions.dirty=false;
      }
      region_workspace.shape_case_groups=impl_->d_shape_groups;
      region_workspace.shape_case_representatives=impl_->d_shape_representatives;
    }
    if (cudaMalloc''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(impl_->shape_cache_requested)out.execution_profile+=',
        '''    if(impl_->project_shape_inputs)out.execution_profile+=shape_result_programs?"-project-shape-inputs":"-project-shape-inputs-declined";
    if(impl_->shape_cache_requested)out.execution_profile+=''')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','group<workspace.shape_result_count;', 'group<meta.shape_case_count;')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','workspace.shape_case_representatives[group]',
        'workspace.shape_case_representatives[meta.shape_representative_offset+group]')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','workspace.shape_case_groups[input_case]',
        'workspace.shape_case_groups[meta.shape_case_group_offset+input_case]')
    # Tests are authored separately alongside this script and copied exactly.
    for name in ('test_projected_host.patch.py','test_projected_gpu.patch.py','register_phase.patch.py','virtual_view_lanes.patch.py','authority.patch.py'):
        scope={'root':root,'base':base,'changes':changes,'replace':replace}
        exec((base/'prototypes'/name).read_text(),scope)
    if dry_run:return changes
    record=root/'logs/twofold-optimization-20261022/projected-shape-source-apply.json';assert not record.exists()
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in changes}
    for p,text in changes.items():p.write_text(text)
    record.write_text(json.dumps(dict(status='applied_after_pinned_base',previous_build=previous_build,source_before=before,
        source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    return len(changes)


if __name__=='__main__':raise RuntimeError('invoke from guarded followup controller under the common GPUlock')
