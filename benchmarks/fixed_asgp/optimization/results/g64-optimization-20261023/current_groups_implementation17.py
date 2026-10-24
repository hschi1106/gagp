"""Isolate exact within-generation score grouping; no formal0 source edits."""
from pathlib import Path
import difflib
import json
import shutil
import subprocess
import time

import serial


def create(name):
    attempt=serial.RAW/name;attempt.mkdir();root=attempt/'source';root.mkdir()
    paths=subprocess.check_output(['git','ls-files','cpp'],cwd=serial.ROOT,text=True).splitlines()
    originals={}
    for relative in paths:
        p=serial.ROOT/relative;target=root/relative;target.parent.mkdir(parents=True,exist_ok=True)
        shutil.copy2(p,target);originals[relative]=p.read_bytes()
    for name in ('spec','configs','data','docs','tools','tests','ref','VERSION.md','AGENTS.md'):
        p=serial.ROOT/name
        if p.exists():(root/name).symlink_to(p,target_is_directory=p.is_dir())
    def edit(relative,before,after,count=1):
        p=root/relative;s=p.read_text();assert s.count(before)==count,(relative,before,s.count(before));p.write_text(s.replace(before,after))
    header='cpp/src/runtime/gpu/owned_programs.hpp'
    edit(header,'  const PackResult* prepacked() const { return prepacked_; }',
         '  const std::vector<int>* current_groups() const { return current_groups_; }\n  const PackResult* prepacked() const { return prepacked_; }')
    edit(header,'      const std::array<ValueTag,64>* private_input_types=nullptr)',
         '      const std::array<ValueTag,64>* private_input_types=nullptr, const std::vector<int>* current_groups=nullptr)')
    edit(header,'materialize_(std::move(materialize)),private_input_types_(private_input_types) {}',
         'materialize_(std::move(materialize)),private_input_types_(private_input_types),current_groups_(current_groups) {}')
    edit(header,'  const std::array<ValueTag,64>* private_input_types_;',
         '  const std::array<ValueTag,64>* private_input_types_;\n  const std::vector<int>* current_groups_;')
    phase='cpp/src/evolution/repro/gpu/phase_population.cu'
    kernel='''// Only exact current genotypes group; hashes select candidates, never prove equality.
__global__ void find_current_fitness_groups(TreePointers trees,int population,int slots,
    const std::uint64_t* hashes,const int* table,int mask,int* groups) {
 const int row=blockIdx.x*blockDim.x+threadIdx.x;if(row>=population)return;
 const auto hash=hashes[row];
 const int representative=parent_cache::find_parent(trees,trees,row,slots,hashes,table,mask,hash);
 groups[row]=representative<0?row:representative;
}
'''
    edit(phase,'template<bool SharedNodes, bool CompactFrames = false>\n__global__ void compile_population',kernel+'template<bool SharedNodes, bool CompactFrames = false>\n__global__ void compile_population')
    edit(phase,' Buffer<std::uint64_t> parent_hashes;Buffer<int> parent_table;int parent_table_size=0;',
         ' const bool current_fitness_groups=std::getenv("GAGP_GPU_CURRENT_FITNESS_GROUPS") &&\n     std::string(std::getenv("GAGP_GPU_CURRENT_FITNESS_GROUPS"))=="1";\n'
         ' std::vector<int> current_representatives;Buffer<int> group_indices;\n'
         ' Buffer<std::uint64_t> parent_hashes;Buffer<int> parent_table;int parent_table_size=0;')
    edit(phase,'+parent_hashes.bytes()+parent_table.bytes();','+parent_hashes.bytes()+parent_table.bytes()+group_indices.bytes();')
    # Both constructor paths reserve these persistent indices once, outside staged scratch.
    edit(phase,' if(impl_->parent_fitness_cache) {\n  phase_layout_require',
         ' if(impl_->parent_fitness_cache || impl_->current_fitness_groups) {\n  phase_layout_require')
    edit(phase,'  impl_->reproduction_profile+="-parent-fitness-cache";',
         '  if(impl_->parent_fitness_cache)impl_->reproduction_profile+="-parent-fitness-cache";')
    edit(phase,' if(impl_->uniform_phase_sites)impl_->reproduction_profile+="-uniform-phase";',
         ' if(impl_->current_fitness_groups){impl_->group_indices.reserve(impl_->population);impl_->reproduction_profile+="-current-fitness-groups";}\n'
         ' if(impl_->uniform_phase_sites)impl_->reproduction_profile+="-uniform-phase";')
    initial=''' if(impl_->current_fitness_groups) {
  checked(cudaMemset(impl_->parent_table.data,0xff,impl_->parent_table.bytes()));
  index_fitness_parents<<<(impl_->population+63)/64,64>>>(pointers(*impl_->current),impl_->population,impl_->slot_count,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1);checked(cudaGetLastError());
  find_current_fitness_groups<<<(impl_->population+63)/64,64>>>(pointers(*impl_->current),impl_->population,impl_->slot_count,
      impl_->parent_hashes.data,impl_->parent_table.data,impl_->parent_table_size-1,impl_->group_indices.data);checked(cudaGetLastError());
  impl_->current_representatives=impl_->group_indices.read(impl_->population);
 }
'''
    edit(phase,' if(!impl_->staged_buffers)impl_->reserve_reproduction_buffers();',initial+' if(!impl_->staged_buffers)impl_->reserve_reproduction_buffers();')
    edit(phase,'     impl_->private_intlists_proven?&impl_->input_types:nullptr);',
         '     impl_->private_intlists_proven?&impl_->input_types:nullptr,\n     impl_->current_fitness_groups?&impl_->current_representatives:nullptr);')
    edit(phase,'diagnostics || impl_->fitness_reuse.enabled(),','diagnostics || impl_->fitness_reuse.enabled() || impl_->current_fitness_groups,')
    edit(phase,'  if(!diagnostics)result.case_counts.clear();','  if(!diagnostics)result.case_counts.clear();')
    edit(phase,' return result;\n}\nconst char* NativePhasePopulation::profile_name()',
         ' if(!diagnostics && impl_->current_fitness_groups)result.case_counts.clear();\n return result;\n}\nconst char* NativePhasePopulation::profile_name()')
    new_group=''' if(impl_->current_fitness_groups) {
  checked(cudaMemset(impl_->parent_table.data,0xff,impl_->parent_table.bytes()));
  index_fitness_parents<<<(p+63)/64,64>>>(pointers(*offspring),p,k,impl_->parent_hashes.data,
      impl_->parent_table.data,impl_->parent_table_size-1);checked(cudaGetLastError());
  find_current_fitness_groups<<<(p+63)/64,64>>>(pointers(*offspring),p,k,impl_->parent_hashes.data,
      impl_->parent_table.data,impl_->parent_table_size-1,impl_->group_indices.data);checked(cudaGetLastError());
 }
'''
    edit(phase,' if(impl_->stage_storage){copy_current_population',new_group+' if(impl_->stage_storage){copy_current_population')
    edit(phase,' const auto stats=impl_->stats.read(p);',' const auto stats=impl_->stats.read(p);\n if(impl_->current_fitness_groups)impl_->current_representatives=impl_->group_indices.read(p);')
    fitness='cpp/src/runtime/gpu/fitness_gpu.cu'
    aliases='''  std::vector<int> current_groups;
  std::size_t grouped_count=0;
  if(owned && owned->current_groups()) {
    current_groups=*owned->current_groups();
    if(current_groups.size()!=programs.size())return fitness_eval_single_error(ErrCode::Value,"current fitness group size mismatch");
    for(std::size_t i=0;i<current_groups.size();++i) {
      const int representative=current_groups[i];
      if(representative<0 || static_cast<std::size_t>(representative)>=programs.size() ||
          current_groups[representative]!=representative)
        return fitness_eval_single_error(ErrCode::Value,"current fitness group is not a direct representative");
      if(static_cast<std::size_t>(representative)!=i && reused[i]<0)++grouped_count;
    }
  }
'''
    edit(fitness,'  for(std::size_t index=0;index<packed.metas.size();++index)\n    packed.metas[index].reuse_completed_fitness=reused[index]>=0;',
         aliases+'  for(std::size_t index=0;index<packed.metas.size();++index)\n    packed.metas[index].reuse_completed_fitness=reused[index]>=0 || (!current_groups.empty() && current_groups[index]!=static_cast<int>(index));')
    restore='''    for(std::size_t index=0;index<current_groups.size();++index) {
      const auto representative=current_groups[index];
      if(static_cast<std::size_t>(representative)==index)continue;
      host_fitness[index]=host_fitness[representative];
      if(capture_case_counts)out.case_counts[index]=out.case_counts[representative];
      if(!out.wrong.empty())out.wrong[index]=out.wrong[representative];
    }
'''
    edit(fitness,'    const auto copy_t1 = std::chrono::steady_clock::now();',restore+'    const auto copy_t1 = std::chrono::steady_clock::now();')
    edit(fitness,'    if(reused_count)out.execution_profile+="-completed-fitness-reuse";',
         '    if(grouped_count)out.execution_profile+="-current-fitness-groups"+std::to_string(grouped_count);\n'
         '    if(reused_count)out.execution_profile+="-completed-fitness-reuse";')
    test='cpp/tests/gpu/test_gpu_phase_population.cu'
    test_code='''void check_current_groups(const std::shared_ptr<const grammar::CompiledGrammar>& grammar,
    const std::vector<ProgramGenome>& parents,const std::vector<std::string>& names,
    const std::vector<CaseBindings>& cases,const std::vector<Value>& answers) {
 const char* flag="GAGP_GPU_CURRENT_FITNESS_GROUPS";const auto* prior=std::getenv(flag);
 const bool present=prior;const std::string saved=prior?prior:"";
 const std::vector<ProgramGenome> duplicates(parents.size(),parents.front());
 setenv(flag,"0",1);repro::NativePhasePopulation reference(grammar,duplicates,names);
 setenv(flag,"1",1);repro::NativePhasePopulation grouped(grammar,duplicates,names);
 if(present)setenv(flag,saved.c_str(),1);else unsetenv(flag);
 FitnessSessionGpu session;const FitnessPolicy policy{FitnessMetric::Exact};
 require(session.init(cases,answers,10000,512,1000,policy).ok,"current-group session");
 for(int generation=0;generation<=8;++generation) {
  const auto a=reference.evaluate(session,true),b=grouped.evaluate(session,true),dirty=grouped.evaluate(session,true);
  require(a.ok && b.ok && dirty.ok && a.fitness==b.fitness && dirty.fitness==b.fitness &&
      a.wrong==b.wrong && dirty.wrong==b.wrong && a.case_counts==b.case_counts && dirty.case_counts==b.case_counts,
      "current-group reuse changed scores, wrong, fuel/errors or dirty storage");
  if(generation==0)require(b.physical_program_cases<a.physical_program_cases &&
      b.execution_profile.find("-current-fitness-groups")!=std::string::npos,"duplicate fixture never grouped new scores");
  const auto x=reference.export_population(),y=grouped.export_population();std::vector<BytecodeProgram> programs;
  for(std::size_t i=0;i<x.size();++i){require(ast_cache_key(x[i].ast)==ast_cache_key(y[i].ast),"current-group changed RNG/AST");programs.push_back(compile_for_eval(y[i],names));}
  std::vector<unsigned> wrong;
  require(eval_fitness_cpu(programs,cases,answers,10000,1000,512,policy,&wrong)==b.fitness && wrong==b.wrong,"current-group differs from full CPU reference");
  if(generation<8){const auto seed=UINT64_C(0xf3a9201)+generation;reference.reproduce(a.fitness,2,.5,seed,1);grouped.reproduce(b.fitness,2,.5,seed,1);}
 }
 for(int fuel:{0,1,10000}) {
  auto labels=answers;for(auto& v:labels)v=Value::from_int(v.i+19);
  require(session.init(cases,labels,fuel,128,7,policy).ok,"current-group changed context");
  const auto a=reference.evaluate(session,true),b=grouped.evaluate(session,true);
  require(a.ok && b.ok && a.fitness==b.fitness && a.wrong==b.wrong && a.case_counts==b.case_counts,"current-group retained another context's scores");
 }
}

'''
    edit(test,'void check_unchanged_fitness_reuse(',test_code+'void check_unchanged_fitness_reuse(')
    edit(test,' check_parent_cache_keys();',' check_parent_cache_keys();\n check_current_groups(grammar,parents,names,cases,answers);')
    edit(test,' test_private_intlist_generations();',
         ' {const char* flag="GAGP_GPU_CURRENT_FITNESS_GROUPS";const auto* p=std::getenv(flag);const bool have=p;const std::string old=p?p:"";setenv(flag,"1",1);test_private_intlist_generations(true,true);if(have)setenv(flag,old.c_str(),1);else unsetenv(flag);}\n test_private_intlist_generations();')
    patch=[];changed={}
    for relative,before in originals.items():
        after=(root/relative).read_bytes()
        if after!=before:
            changed[relative]=serial.sha(root/relative)
            patch.extend(difflib.unified_diff(before.decode().splitlines(True),after.decode().splitlines(True),fromfile='a/'+relative,tofile='b/'+relative))
    (attempt/'implementation.patch').write_text(''.join(patch));shutil.copy2(Path(__file__),attempt/'producer.py')
    serial.save(attempt/'source-manifest.json',dict(status='isolated_source_ready',created_epoch=time.time(),deadline_epoch=serial.DEADLINE,
        original_commit=subprocess.check_output(['git','rev-parse','HEAD'],cwd=serial.ROOT,text=True).strip(),root=str(root),
        original_source_sha256={p:serial.sha(serial.ROOT/p) for p in paths},source_sha256={p:serial.sha(root/p) for p in paths},
        changed_source_sha256=changed,implementation_patch_sha256=serial.sha(attempt/'implementation.patch'),
        scope='Exact current-generation score groups; isolated OLD TRAINING only, no packed-frame dependency or formal0 mutation'))


if __name__=='__main__':
    import argparse
    parser=argparse.ArgumentParser(description=__doc__);parser.add_argument('--name',default='current-groups17')
    create(parser.parse_args().name)
