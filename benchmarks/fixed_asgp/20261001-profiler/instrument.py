"""Create an isolated NVTX-only copy; never alter production source files."""
from pathlib import Path
import difflib, hashlib, json, shutil
ROOT=Path.cwd()
OUT=ROOT/'logs/profiler-baseline-20261001'
SRC=OUT/'source/cpp'
shutil.copytree(ROOT/'cpp',SRC,ignore=shutil.ignore_patterns('build*','__pycache__'),dirs_exist_ok=True)
modified={}
def edit(rel, pairs):
    path=SRC/rel
    before=path.read_text()
    value=before
    for old,new in pairs:
        assert value.count(old)==1,(rel,old,value.count(old))
        value=value.replace(old,new)
    value='#include "gagp/profile_probe.hpp"\n'+value
    path.write_text(value)
    modified[rel]=(before,value)
def insert(old,extra): return old,old+'\n'+extra
header='''#pragma once
#include <nvtx3/nvToolsExt.h>
namespace gagp::profile_probe {
class Range {
  bool active_ = true;
 public:
  explicit Range(const char* name) { nvtxRangePushA(name); }
  ~Range() { finish(); }
  void finish() { if (active_) { nvtxRangePop(); active_ = false; } }
  void next(const char* name) { finish(); nvtxRangePushA(name); active_ = true; }
};
}
#define GAGP_PROBE_JOIN_(a,b) a##b
#define GAGP_PROBE_JOIN(a,b) GAGP_PROBE_JOIN_(a,b)
#define GAGP_PROBE(name) gagp::profile_probe::Range GAGP_PROBE_JOIN(gagp_probe_,__LINE__)(name)
'''
(SRC/'include/gagp/profile_probe.hpp').write_text(header)
modified['include/gagp/profile_probe.hpp']=('',header)
edit('src/bench/fixed_asgp/main.cpp',[
 insert('  for (int rep=-1; rep<3; ++rep) {','    const auto rep_name = std::string("rep/") + std::to_string(rep);\n    GAGP_PROBE(rep_name.c_str());'),
])
edit('src/evolution/evolve.cpp',[
 insert('                                      CompileCache* compile_cache, int fuel, bool parallel_allowed = false) {','  GAGP_PROBE("compile.total");'),
 ('    bool sort_output) {\n  for (double& value', '    bool sort_output) {\n  GAGP_PROBE("selection.rank_host");\n  for (double& value'),
 insert('    const auto gen_t0 = std::chrono::steady_clock::now();','    gagp::profile_probe::Range generation_range("generation");\n    gagp::profile_probe::Range generation_stage("eval.stage");'),
 insert('    const auto repro_t0 = std::chrono::steady_clock::now();','    generation_stage.next("repro.stage");'),
 ('    const auto gen_t1 = std::chrono::steady_clock::now();','    generation_stage.finish();\n    generation_range.finish();\n    const auto gen_t1 = std::chrono::steady_clock::now();'),
])
edit('src/evolution/lifecycle.cpp',[
 insert('    const std::vector<ScoredGenome>* final_population) const {','  GAGP_PROBE("lifecycle.retain");'),
])
edit('src/evolution/repro/gpu.cpp',[
 insert('std::vector<ProgramGenome> compact_prepared_population(const std::vector<ProgramGenome>& population) {','  GAGP_PROBE("repro.compact");'),
 insert('                                                      CompiledVariationPass pass = CompiledVariationPass::Crossover) {','  GAGP_PROBE(pass == CompiledVariationPass::Crossover ? "repro.prepare.crossover" : "repro.prepare.mutation");\n  gagp::profile_probe::Range stage("repro.prepare_inputs");'),
 insert('  const auto prep_t0 = std::chrono::steady_clock::now();','  stage.next("repro.preprocess");'),
 insert('  const auto pack_t0 = std::chrono::steady_clock::now();','  stage.next("repro.pack");'),
 insert('    const EvolutionConfig& cfg, const GpuReproPreparedData& prepared) {','  GAGP_PROBE("verification.prepared_identity");'),
 insert('  const auto run_pass = [&](const GpuReproPreparedData& pass, const std::vector<double>& fitness) {','    GAGP_PROBE(pass.config.compiled_pass == CompiledVariationPass::Crossover ? "repro.pass.crossover" : "repro.pass.mutation");\n    gagp::profile_probe::Range pass_stage("repro.setup");'),
 ('    if (!upload_gpu_repro_inputs(pass.packed, &cache.arena, &out.stats, &message) ||','    pass_stage.next("repro.upload_and_kernels");\n    if (!upload_gpu_repro_inputs(pass.packed, &cache.arena, &out.stats, &message) ||'),
 insert('    GpuReproChildView view;','    pass_stage.next("repro.copyback");'),
 insert('    const auto decode_start = std::chrono::steady_clock::now();','    pass_stage.next("repro.decode_and_verify");'),
])
edit('src/evolution/grammar/variation_cache.cpp',[
 insert('void VariationAnalysisCache::warm_population(const std::vector<ProgramGenome>& population,\n    const std::vector<GenerationRequest>& requests, std::size_t max_workers,\n    std::vector<WarmPopulationMember>* handoff) {','  GAGP_PROBE("verification.parent_analysis");'),
 insert('    const std::vector<GenerationRequest>& requests, std::size_t max_workers) {','  GAGP_PROBE("verification.candidate_analysis");'),
])
edit('src/evolution/grammar/variation.cpp',[
 insert('    VariationContext& context, std::optional<std::uint32_t> owned_parent_root) {','  GAGP_PROBE("verification.accept_worker");'),
])
edit('src/evolution/repro/compiled_decode.cpp',[
 insert('    if (i % analysis_batch == 0) {','      gagp::profile_probe::Range batch_range("repro.decode_batch");'),
 ('      admitted.clear(); admitted.resize(count);','      batch_range.next("verification.admission_batch");\n      admitted.clear(); admitted.resize(count);'),
])
edit('src/evolution/repro/backend.cpp',[
 insert('ReproductionResult run_cpu_backend_impl(const std::vector<ScoredGenomeRef>& scored,\n                                        const EvolutionConfig& cfg,\n                                        std::mt19937_64& rng) {','  GAGP_PROBE("repro.cpu");\n  gagp::profile_probe::Range stage("repro.cpu_preprocess");'),
 insert('  const auto selection_t0 = std::chrono::steady_clock::now();','  stage.next("selection.cpu_tournament");'),
 ('  std::uniform_real_distribution<double> probability(0.0, 1.0);','  stage.next("repro.cpu_variation");\n  std::uniform_real_distribution<double> probability(0.0, 1.0);'),
])
edit('src/runtime/gpu/fitness_gpu.cu',[
 insert('FitnessEvalResult FitnessSessionGpu::eval_programs(const std::vector<BytecodeProgram>& programs) const {','  GAGP_PROBE("eval.call");\n  gagp::profile_probe::Range stage("eval.pack");'),
 insert('  const auto launch_prep_t0 = std::chrono::steady_clock::now();','  stage.next("eval.launch_prep");'),
 insert('    const auto upload_t0 = std::chrono::steady_clock::now();','    stage.next("eval.alloc_upload");'),
 insert('    const auto kernel_t0 = std::chrono::steady_clock::now();','    stage.next("eval.launch_wait");'),
 insert('    const auto copy_t0 = std::chrono::steady_clock::now();','    stage.next("eval.copyback");'),
 insert('    teardown_t0 = std::chrono::steady_clock::now();','    stage.next("eval.teardown");'),
])
edit('src/runtime/gpu/host_pack_gpu.cu',[
 insert('void verify_bounded_program_or_throw(const BytecodeProgram& prog) {','  GAGP_PROBE("verification.bytecode_pack");'),
])
cmake=SRC/'CMakeLists.txt'
before=cmake.read_text()
after=before.replace('enable_testing()','enable_testing()\ninclude_directories("/usr/local/cuda-12.6/include")\nlink_libraries(${CMAKE_DL_LIBS})',1)
cmake.write_text(after)
modified['CMakeLists.txt']=(before,after)
patch=''.join(''.join(difflib.unified_diff(a.splitlines(True),b.splitlines(True),fromfile='a/cpp/'+p,tofile='b/cpp/'+p)) for p,(a,b) in sorted(modified.items()))
(OUT/'instrumentation.patch').write_text(patch)
def fingerprint(root):
    records={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in sorted(root.rglob('*')) if p.is_file() and 'build' not in str(p.relative_to(root)).split('/')[0] and '__pycache__' not in p.parts}
    return {'sha256':hashlib.sha256(json.dumps(records,sort_keys=True).encode()).hexdigest(),'files':records}
(OUT/'source_identity.json').write_text(json.dumps({'original':fingerprint(ROOT/'cpp'),'instrumented':fingerprint(SRC),'modified_paths':list(modified)},indent=2)+'\n')
print('NVTX-only source copy:',SRC)
print('Changed files:',len(modified))
