"""Prepare exact private Levenshtein integration; apply only after real proofs."""
from pathlib import Path


ROOT = Path(__file__).resolve().parents[6]
BASE = Path(__file__).resolve().parent


def apply(dry_run=False):
    changed = {}

    def edit(path, old, new, count=1):
        text = changed.get(path, (ROOT / path).read_text())
        assert text.count(old) == count, (path, old[:120], text.count(old))
        changed[path] = text.replace(old, new)

    copies = {
        'cpp/src/runtime/gpu/short_edit_distance.hpp': 'runtime_short_edit.hpp',
        'cpp/src/runtime/gpu/short_edit_patterns.hpp': 'runtime_short_edit_patterns.hpp',
        'cpp/src/runtime/gpu/device/short_edit_device.cuh': 'runtime_short_edit_device.cuh',
        'cpp/tests/gpu/private_short_edit_checks.cuh': 'runtime_short_edit_checks.cuh',
    }
    for target, source in copies.items():
        assert not (ROOT / target).exists(), target
        changed[target] = (BASE / source).read_text()
    path = 'cpp/include/gagp/runtime/gpu/region_types_gpu.hpp'
    edit(path, 'struct DRegionWorkspace {', '''struct DShortEditPattern;
struct DShortEditEntry;
struct DRegionWorkspace {
  bool short_edit_enabled = false;
  const DShortEditPattern* short_edit_patterns = nullptr;
  const DShortEditEntry* short_edit_entries = nullptr;
  int short_edit_entry_count = 0;''')
    path = 'cpp/src/runtime/gpu/device/resident_score_device.cuh'
    edit(path, '#include "builtins_device.cuh"', '#include "builtins_device.cuh"\n#include "short_edit_device.cuh"')
    edit(path, '    const DPrivateIntListSpan& b,double penalty,FitnessPolicy policy,bool* exact) {',
         '''    const DPrivateIntListSpan& b,double penalty,FitnessPolicy policy,bool* exact,
    bool short_edit=false,const DShortEditPattern* pattern=nullptr,
    const DShortEditEntry* entries=nullptr) {''')
    edit(path, '''  if(fitness_sequence_length_saturates(n,m,penalty,policy))return -fabs(penalty);
  unsigned row[kFitnessMaxSequence+1];''', '''  if(fitness_sequence_length_saturates(n,m,penalty,policy))return -fabs(penalty);
  if(short_edit && (same || !n || !m || pattern)) {
    // Existing maximum-answer and penalty guards run first. Canonical whole
    // sequence equality, empty sequences and one-word patterns are exact;
    // unsupported labels keep the original full DP, without truncation.
    const int distance=same?0:!n?m:!m?n:d_short_edit_distance<CompactA>(a,*pattern,entries);
    const double error=distance;
    return policy.proximity==FitnessProximity::Discounted?-error/(1+error):-fmin(error,fabs(penalty));
  }
  unsigned row[kFitnessMaxSequence+1];''')
    edit(path, '    double penalty,FitnessPolicy policy,const DPayloadTables& tables,const State& state,bool* exact) {',
         '''    double penalty,FitnessPolicy policy,const DPayloadTables& tables,const State& state,bool* exact,
    bool short_edit=false,const DShortEditPattern* pattern=nullptr,
    const DShortEditEntry* entries=nullptr,int entry_count=0) {''')
    edit(path, '''    if(a.integers) {
      if(b.integers)return d_score_private_intlist_spans<true,true>(a,b,penalty,policy,exact);
      return d_score_private_intlist_spans<true,false>(a,b,penalty,policy,exact);
    }
    if(b.integers)return d_score_private_intlist_spans<false,true>(a,b,penalty,policy,exact);
    return d_score_private_intlist_spans<false,false>(a,b,penalty,policy,exact);''', '''    if(pattern && (!short_edit_pattern_matches(*pattern,expected,b.size,entry_count) ||
        (pattern->count && !entries)))pattern=nullptr;
    if(a.integers) {
      if(b.integers)return d_score_private_intlist_spans<true,true>(a,b,penalty,policy,exact,short_edit,pattern,entries);
      return d_score_private_intlist_spans<true,false>(a,b,penalty,policy,exact,short_edit,pattern,entries);
    }
    if(b.integers)return d_score_private_intlist_spans<false,true>(a,b,penalty,policy,exact,short_edit,pattern,entries);
    return d_score_private_intlist_spans<false,false>(a,b,penalty,policy,exact,short_edit,pattern,entries);''')
    path = 'cpp/src/runtime/gpu/device/execute_bytecode_device.cuh'
    edit(path, '*score=d_score_resident(result.value,*expected,penalty,policy,payload_tables,payload_state_storage.ref(),exact);',
         '''*score=d_score_resident(result.value,*expected,penalty,policy,payload_tables,payload_state_storage.ref(),exact,
          workspace.short_edit_enabled,workspace.short_edit_patterns?workspace.short_edit_patterns+local_case:nullptr,
          workspace.short_edit_entries,workspace.short_edit_entry_count);''')
    path = 'cpp/src/runtime/gpu/fitness_gpu.cu'
    edit(path, '#include "shape_partition_cache.hpp"', '#include "shape_partition_cache.hpp"\n#include "short_edit_patterns.hpp"')
    edit(path, '  bool ready = false;\n  int device_id = -1;', '''  bool ready = false;
  bool short_edit_requested = false,short_edit_enabled = false;
  gpu_detail::DShortEditPattern* d_short_edit_patterns = nullptr;
  gpu_detail::DShortEditEntry* d_short_edit_entries = nullptr;
  int short_edit_entry_count = 0;
  int device_id = -1;''')
    edit(path, '    if (d_expected) gpu_detail::cuda_pool_free(d_expected);', '''    if (d_expected) gpu_detail::cuda_pool_free(d_expected);
    if (d_short_edit_patterns) gpu_detail::cuda_pool_free(d_short_edit_patterns);
    if (d_short_edit_entries) gpu_detail::cuda_pool_free(d_short_edit_entries);''')
    edit(path, '  // Every init attempt invalidates prior owner scores, including failed reinit.', '''  impl_->short_edit_enabled=false;
  const char* short_edit_setting=std::getenv("GAGP_GPU_SHORT_EDIT_DISTANCE");
  impl_->short_edit_requested=short_edit_setting && std::string(short_edit_setting)=="1";
  // Every init attempt invalidates prior owner scores, including failed reinit.''')
    edit(path, '  const auto payload_cache_t1 = std::chrono::steady_clock::now();', '''  gpu_detail::HostShortEditPatterns short_edit_plan;
  if(impl_->short_edit_requested && policy.metric==FitnessMetric::Levenshtein) {
    short_edit_plan=gpu_detail::prepare_short_edit_patterns(shared_answer,[&](const Value& answer)->const std::vector<Value>* {
      const auto found=impl_->host_list_payloads.find(HostListPayloadKey{answer.tag,answer.i});
      return found==impl_->host_list_payloads.end()?nullptr:&found->second;
    });
  }
  const auto payload_cache_t1 = std::chrono::steady_clock::now();''')
    edit(path, '  const auto upload_t0 = std::chrono::steady_clock::now();\n  if(impl_->d_shape_representatives){gpu_detail::cuda_pool_free(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}', '''  const auto upload_t0 = std::chrono::steady_clock::now();
  if(impl_->d_short_edit_patterns){gpu_detail::cuda_pool_free(impl_->d_short_edit_patterns);impl_->d_short_edit_patterns=nullptr;}
  if(impl_->d_short_edit_entries){gpu_detail::cuda_pool_free(impl_->d_short_edit_entries);impl_->d_short_edit_entries=nullptr;}
  impl_->short_edit_entry_count=0;
  if(short_edit_plan.eligible) {
    if(!gpu_detail::cuda_alloc_and_copy_in(short_edit_plan.patterns,&impl_->d_short_edit_patterns) ||
        !gpu_detail::cuda_alloc_and_copy_in(short_edit_plan.entries,&impl_->d_short_edit_entries))
      return fitness_init_single_error(ErrCode::Value,"cuda short edit pattern allocation failure");
    impl_->short_edit_entry_count=static_cast<int>(short_edit_plan.entries.size());
  }
  if(impl_->d_shape_representatives){gpu_detail::cuda_pool_free(impl_->d_shape_representatives);impl_->d_shape_representatives=nullptr;}''')
    edit(path, '  impl_->ready = true;\n  impl_->device_id = device_id;', '''  impl_->short_edit_enabled=impl_->short_edit_requested && policy.metric==FitnessMetric::Levenshtein;
  impl_->ready = true;
  impl_->device_id = device_id;''')
    edit(path, '  region_workspace.private_intlists=private_intlists;', '''  region_workspace.private_intlists=private_intlists;
  region_workspace.short_edit_enabled=impl_->short_edit_enabled && private_intlists;
  region_workspace.short_edit_patterns=impl_->d_short_edit_patterns;
  region_workspace.short_edit_entries=impl_->d_short_edit_entries;
  region_workspace.short_edit_entry_count=impl_->short_edit_entry_count;''')
    edit(path, '    if(impl_->shape_cache_requested)out.execution_profile+=shape_result_programs?"-shape-result-cache":"-shape-result-cache-declined";',
         '''    if(impl_->shape_cache_requested)out.execution_profile+=shape_result_programs?"-shape-result-cache":"-shape-result-cache-declined";
    if(impl_->short_edit_enabled)out.execution_profile+=private_intlists?"-short-edit-exact":"-short-edit-declined";''')
    path = 'cpp/tests/gpu/test_extended_payload_gpu.cu'
    edit(path, '#include "private_intlist_offset_checks.cuh"', '#include "private_intlist_offset_checks.cuh"\n#include "private_short_edit_checks.cuh"')
    edit(path, 'int main() {\n  // Session initialization', 'int main() {\n  if(!test_private_short_edit_sessions())return 1;\n  // Session initialization')
    path = 'cpp/tests/gpu/test_gpu_phase_population.cu'
    edit(path, 'void test_private_intlist_generations(bool sequence_windows=false,bool staged=false) {',
         'void test_private_intlist_generations(bool sequence_windows=false,bool staged=false,bool edit_distance=false) {')
    edit(path, ' FitnessPolicy policy;policy.metric=FitnessMetric::Exact;policy.list_allocation_limit=65536;\n const int audit_fuel=sequence_windows?100000:10000;',
         ' FitnessPolicy policy;policy.metric=edit_distance?FitnessMetric::Levenshtein:FitnessMetric::Exact;policy.list_allocation_limit=65536;\n const int audit_fuel=sequence_windows?100000:10000;')
    edit(path, ' test_simple_pattern_refresh();\n test_whole_program_node_budget();', ''' const char* short_prior=std::getenv("GAGP_GPU_SHORT_EDIT_DISTANCE");
 const bool short_present=short_prior!=nullptr;const std::string short_saved=short_prior?short_prior:"";
 setenv("GAGP_GPU_SHORT_EDIT_DISTANCE","1",1);
 test_private_intlist_generations(false,true,true);
 if(short_present)setenv("GAGP_GPU_SHORT_EDIT_DISTANCE",short_saved.c_str(),1);else unsetenv("GAGP_GPU_SHORT_EDIT_DISTANCE");
 test_simple_pattern_refresh();
 test_whole_program_node_budget();''')
    if not dry_run:
        for path, text in changed.items():
            (ROOT / path).parent.mkdir(parents=True, exist_ok=True)
            (ROOT / path).write_text(text)
    return sorted(changed)


if __name__ == '__main__':
    print(apply(dry_run=True))
