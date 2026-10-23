"""Stage exact completed-fitness reuse; invoke only under the common lock."""
from pathlib import Path
import hashlib
import json

root=Path.cwd();base=Path(__file__).resolve().parents[1]
changes={}


def replace(path,old,new):
    p=root/path;text=changes.get(p,p.read_text())
    assert text.count(old)==1,(path,old[:80],text.count(old))
    changes[p]=text.replace(old,new)


def apply(dry_run=False):
    previous=json.loads((root/'logs/twofold-optimization-20261022/build-attempt9/manifest.json').read_text())
    if not dry_run:
        assert previous['status']=='ok'
        for path,digest in previous['source_sha256'].items():
            assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,'source differs from pinned9: '+path
    new=root/'cpp/src/evolution/repro/gpu/completed_fitness_reuse.hpp';assert not new.exists()
    changes[new]=(base/'prototypes/completed_fitness_reuse.hpp').read_text()
    replace('cpp/include/gagp/runtime/gpu/device_types_gpu.hpp','  bool shape_result_cache = false;',
        '  bool reuse_completed_fitness = false; // Sealed owner proof plus session revision.\n  bool shape_result_cache = false;')
    replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp','  std::uint64_t physical_program_cases = 0;',
        '  std::uint64_t physical_program_cases = 0;\n  std::uint64_t fitness_reused_programs=0, fitness_reused_program_cases=0;')
    replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp','  // Opt-in diagnostics: evaluated cases, errors, timeouts (subset of errors),',
        '  // Logical diagnostics (including exact completed-score reuse): cases, errors, timeouts (subset of errors),')
    replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp',
        '  FitnessEvalResult eval_owned_programs(const gpu_detail::OwnedGpuPrograms&, bool capture_case_counts) const;',
        '''  std::uint64_t evaluation_revision() const;
  FitnessEvalResult eval_owned_programs(const gpu_detail::OwnedGpuPrograms&, bool capture_case_counts,
      const std::vector<int>* reused_parents=nullptr,const FitnessEvalResult* completed=nullptr) const;''')
    replace('cpp/include/gagp/runtime/gpu/fitness_gpu.hpp',
        '      const std::function<void()>&, const gpu_detail::OwnedGpuPrograms*) const;',
        '''      const std::function<void()>&, const gpu_detail::OwnedGpuPrograms*,
      const std::vector<int>* reused_parents=nullptr,const FitnessEvalResult* completed=nullptr) const;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','#include <algorithm>','#include <algorithm>\n#include <atomic>')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','struct FitnessSessionGpu::Impl {',
        '''namespace {std::atomic<std::uint64_t> next_fitness_revision{1};}
struct FitnessSessionGpu::Impl {
  std::uint64_t evaluation_revision=0;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','bool FitnessSessionGpu::is_ready() const { return impl_ && impl_->ready; }',
        '''bool FitnessSessionGpu::is_ready() const { return impl_ && impl_->ready; }
std::uint64_t FitnessSessionGpu::evaluation_revision() const {
  return impl_ && impl_->ready?impl_->evaluation_revision:0;
}''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','                                                 double penalty, FitnessPolicy policy) {',
        '''                                                 double penalty, FitnessPolicy policy) {
  // Every init attempt invalidates prior owner scores, including failed reinit.
  impl_->evaluation_revision=next_fitness_revision.fetch_add(1,std::memory_order_relaxed);''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu',
        '''    bool capture_case_counts) const {
  return eval_programs_impl(owned.programs(),capture_case_counts,{},&owned);
}''',
        '''    bool capture_case_counts,const std::vector<int>* reused_parents,const FitnessEvalResult* completed) const {
  return eval_programs_impl(owned.programs(),capture_case_counts,{},&owned,reused_parents,completed);
}''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    const gpu_detail::OwnedGpuPrograms* owned) const {',
        '''    const gpu_detail::OwnedGpuPrograms* owned,const std::vector<int>* reused_parents,
    const FitnessEvalResult* completed) const {''')
    # Packed metadata is a fresh per-evaluation copy; never mutate owner tables.
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','  FitnessEvalResult out;\n  out.physical_case_count=',
        '''  std::vector<int> reused(programs.size(),-1);
  std::size_t reused_count=0;
  if(owned && reused_parents && completed && completed->ok &&
      reused_parents->size()==programs.size() && completed->fitness.size()==programs.size() &&
      completed->case_counts.size()==programs.size() &&
      (impl_->policy.metric==FitnessMetric::Legacy || completed->wrong.size()==programs.size())) {
    for(std::size_t index=0;index<programs.size();++index) {
      const auto parent=(*reused_parents)[index];
      if(parent<0 || static_cast<std::size_t>(parent)>=programs.size() ||
          !std::isfinite(completed->fitness[parent]) || completed->case_counts[parent][4] ||
          completed->case_counts[parent][0]!=static_cast<unsigned>(impl_->shared_case_count))continue;
      reused[index]=parent;++reused_count;
    }
  }
  for(std::size_t index=0;index<packed.metas.size();++index)
    packed.metas[index].reuse_completed_fitness=reused[index]>=0;
  FitnessEvalResult out;
  out.fitness_reused_programs=reused_count;
  out.fitness_reused_program_cases=reused_count*impl_->shared_case_count;
  out.physical_case_count=''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu',
        '''  out.physical_program_cases=(programs.size()-shape_result_programs)*out.physical_case_count+
      shape_result_invocations;''',
        '''  for(const auto& meta:packed.metas)if(!meta.reuse_completed_fitness)
    out.physical_program_cases+=meta.shape_result_cache?meta.shape_case_count:out.physical_case_count;''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    const auto copy_t1 = std::chrono::steady_clock::now();',
        '''    for(std::size_t index=0;index<reused.size();++index)if(reused[index]>=0) {
      const auto parent=reused[index];host_fitness[index]=completed->fitness[parent];
      if(capture_case_counts)out.case_counts[index]=completed->case_counts[parent];
      if(!out.wrong.empty())out.wrong[index]=completed->wrong[parent];
    }
    const auto copy_t1 = std::chrono::steady_clock::now();''')
    replace('cpp/src/runtime/gpu/fitness_gpu.cu','    if(impl_->case_reuse_requested)out.execution_profile+=',
        '''    if(reused_count)out.execution_profile+="-completed-fitness-reuse";
    if(impl_->case_reuse_requested)out.execution_profile+=''')
    replace('cpp/src/runtime/gpu/device/kernels.cuh','  const DProgramMeta meta = metas[prog_idx];',
        '''  const DProgramMeta meta = metas[prog_idx];
  if(meta.reuse_completed_fitness) {
    // This is block-uniform. Initial buffers remain zero for skipped rows;
    // exact completed scores/logical counters are restored after copyback.
    if(next_program) {
      __shared__ int next_reused_program;
      if(tid==0)next_reused_program=atomicAdd(next_program,1);
      __syncthreads();logical_idx=static_cast<std::size_t>(next_reused_program);__syncthreads();
    } else {logical_idx+=gridDim.x;__syncthreads();}
    continue;
  }''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','#include "phase_population.hpp"',
        '#include "phase_population.hpp"\n#include "completed_fitness_reuse.hpp"')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        'struct OperatorStats {int parent=-1,crossover_attempted=0,crossover=0,crossover_rejected=0,mutation=0,changed=0,error=0;};',
        'struct OperatorStats {int parent=-1,crossover_attempted=0,crossover=0,crossover_rejected=0,mutation=0,changed=0,error=0,reusable_parent=-1;};')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' const int parent=stats[child].parent;if(parent<0 || parent>=population){stats[child].error=9;return;}\n bool equal=true;',
        ' const int parent=stats[child].parent;if(parent<0 || parent>=population){stats[child].error=9;return;}\n bool equal=true,reusable=true;')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        'for(int i=0;i<a.constant_count && equal;++i){const auto x=a.constants[i],y=b.constants[i];equal=x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i);}',
        '''for(int i=0;i<a.constant_count && equal;++i){const auto x=a.constants[i],y=b.constants[i];
   equal=x.tag==y.tag && (x.tag==ValueTag::Bool?x.b==y.b:x.i==y.i);
   reusable=reusable && x.i==y.i && x.b==y.b;
  }''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' stats[child].changed=!equal;',
        ''' stats[child].changed=!equal;
 stats[child].reusable_parent=equal && reusable?parent:-1;''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' std::size_t peak_transport_bytes=0;',
        ''' CompletedFitnessReuse fitness_reuse{std::getenv("GAGP_GPU_UNCHANGED_FITNESS_REUSE") &&
     std::string(std::getenv("GAGP_GPU_UNCHANGED_FITNESS_REUSE"))=="1"};
 std::size_t peak_transport_bytes=0;''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        ' if(!std::getenv("GAGP_OWNED_PHASE_PACK"))return finish(session.eval_programs(programs(),diagnostics));',
        ''' if(!std::getenv("GAGP_OWNED_PHASE_PACK")) {
  impl_->fitness_reuse.clear();return finish(session.eval_programs(programs(),diagnostics));
 }''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' return finish(session.eval_owned_programs(owned,diagnostics));',
        ''' const auto reuse_begin=Clock::now();
 const auto revision=session.evaluation_revision();
 const auto parents=impl_->fitness_reuse.mapping(revision);
 auto result=finish(session.eval_owned_programs(owned,diagnostics || impl_->fitness_reuse.enabled(),
     parents.empty()?nullptr:&parents,parents.empty()?nullptr:&impl_->fitness_reuse.completed()));
 if(impl_->fitness_reuse.enabled()) {
  impl_->fitness_reuse.record(result,revision);
  if(!diagnostics)result.case_counts.clear();
  result.timing.total_ms=elapsed(reuse_begin);
 }
 return result;''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' impl_->current.swap(offspring);impl_->refresh_programs(&timing);',
        ''' if(impl_->fitness_reuse.enabled()) {
  std::vector<int> parents;parents.reserve(stats.size());
  for(const auto& one:stats)parents.push_back(one.reusable_parent);
  impl_->fitness_reuse.advance(std::move(parents));
 }
 impl_->current.swap(offspring);impl_->refresh_programs(&timing);''')
    # Telemetry separates logical inherited scores from executed root calls.
    replace('cpp/include/gagp/evolution/timing.hpp','  std::uint64_t physical_blocksize = 0;',
        '  std::uint64_t physical_blocksize = 0;\n  std::uint64_t fitness_reused_programs=0,fitness_reused_program_cases=0;')
    replace('cpp/src/evolution/timing.cpp','  total->physical_blocksize=std::max(total->physical_blocksize,value.physical_blocksize);',
        '''  total->physical_blocksize=std::max(total->physical_blocksize,value.physical_blocksize);
  total->fitness_reused_programs+=value.fitness_reused_programs;
  total->fitness_reused_program_cases+=value.fitness_reused_program_cases;''')
    replace('cpp/src/evolution/evolve.cpp','  evaluation.timing.physical_blocksize=fit.physical_blocksize;',
        '''  evaluation.timing.physical_blocksize=fit.physical_blocksize;
  evaluation.timing.fitness_reused_programs=fit.fitness_reused_programs;
  evaluation.timing.fitness_reused_program_cases=fit.fitness_reused_program_cases;''')
    replace('cpp/src/evolution/evolve.cpp','    timing.physical_blocksize=fit.physical_blocksize;',
        '''    timing.physical_blocksize=fit.physical_blocksize;
    timing.fitness_reused_programs=fit.fitness_reused_programs;
    timing.fitness_reused_program_cases=fit.fitness_reused_program_cases;''')
    replace('cpp/src/bench/fixed_asgp/main.cpp','      {"physical_blocksize",number(e.physical_blocksize)},',
        '''      {"physical_blocksize",number(e.physical_blocksize)},
      {"fitness_reused_programs",number(e.fitness_reused_programs)},
      {"fitness_reused_program_cases",number(e.fitness_reused_program_cases)},''')
    for filename in ('test_unchanged_host.patch.py','test_unchanged_gpu.patch.py','unchanged_authority.patch.py'):
        exec((base/'prototypes'/filename).read_text(),dict(root=root,base=base,changes=changes,replace=replace))
    if dry_run:return changes
    record=root/'logs/twofold-optimization-20261022/unchanged-fitness-source-apply.json';assert not record.exists()
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in changes}
    for p,text in changes.items():p.write_text(text)
    record.write_text(json.dumps(dict(status='applied_after_pinned9',source_before=before,
        source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    return list(before)
