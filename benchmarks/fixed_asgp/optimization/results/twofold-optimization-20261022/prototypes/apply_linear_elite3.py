"""Integrate a verified O(P) elite_count=1 GPU reduction, exact tie rule."""
from pathlib import Path
import hashlib
import json

root=Path.cwd();base=Path(__file__).resolve().parents[1];changes={}
def replace(relative,old,new):
    path=root/relative;text=changes.get(path,path.read_text())
    assert text.count(old)==1,(relative,old[:80],text.count(old));changes[path]=text.replace(old,new)

def apply():
    prior=json.loads((root/'logs/twofold-optimization-20261022/build-attempt17/manifest.json').read_text())
    assert prior['status']=='ok'
    for path,digest in prior['source_sha256'].items():assert hashlib.sha256((root/path).read_bytes()).hexdigest()==digest,path
    path=root/'cpp/src/evolution/repro/gpu/linear_elite.cuh';assert not path.exists()
    changes[path]=(base/'prototypes/linear_elite.cuh').read_text()
    path=root/'cpp/tests/gpu/test_linear_elite_gpu.cu';assert not path.exists()
    changes[path]=(base/'prototypes/linear_elite_probe.cu').read_text().replace(
        '#include "linear_elite.cuh"','#include "../../src/evolution/repro/gpu/linear_elite.cuh"')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','#include "completed_fitness_reuse.hpp"',
        '#include "completed_fitness_reuse.hpp"\n#include "linear_elite.cuh"')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu','__global__ void annotate_import(',r'''// Exactly the same elite destination and source-index tie rule as rank-based
// preservation. One block finds the winner; slots copy disjoint target rows.
__global__ void preserve_one_elite(TreePointers source,TreePointers target,
    const double* fitness,int population,int slots) {
 const int winner=linear_elite_index(fitness,population);
 for(int k=threadIdx.x;k<slots;k+=blockDim.x)copy_tree(source,winner*slots+k,target,k);
}
__global__ void annotate_import(''')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' const bool source_crossover_gate;',
        ' const bool source_crossover_gate;\n const bool linear_elite=std::getenv("GAGP_GPU_LINEAR_ELITE") && std::string(std::getenv("GAGP_GPU_LINEAR_ELITE"))=="1";')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',' if(impl_->source_crossover_gate)impl_->reproduction_profile+="-source-cross07";',
        ' if(impl_->source_crossover_gate)impl_->reproduction_profile+="-source-cross07";\n if(impl_->linear_elite)impl_->reproduction_profile+="-elite-linear";')
    replace('cpp/src/evolution/repro/gpu/phase_population.cu',
        '  preserve_elites<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),impl_->fitness.data,p,k,elite_count);',
        '  if(impl_->linear_elite && elite_count==1)preserve_one_elite<<<1,256>>>(pointers(*impl_->current),pointers(*offspring),impl_->fitness.data,p,k);\n  else preserve_elites<<<(p+threads-1)/threads,threads>>>(pointers(*impl_->current),pointers(*offspring),impl_->fitness.data,p,k,elite_count);')
    replace('cpp/CMakeLists.txt','  add_executable(gagp_test_cuda_allocation_pool tests/gpu/test_cuda_allocation_pool.cu)',
        '''  add_executable(gagp_test_linear_elite_gpu tests/gpu/test_linear_elite_gpu.cu)
  target_link_libraries(gagp_test_linear_elite_gpu PRIVATE gagp_gpu)
  add_test(NAME gagp_test_linear_elite_gpu COMMAND gagp_test_linear_elite_gpu)
  set_tests_properties(gagp_test_linear_elite_gpu PROPERTIES LABELS "contract;gpu")

  add_executable(gagp_test_cuda_allocation_pool tests/gpu/test_cuda_allocation_pool.cu)''')
    replace('cpp/tests/gpu/test_gpu_phase_population.cu','#include <cmath>','#include <cmath>\n#include <limits>')
    exec((base/'prototypes/test_linear_evolution.patch.py').read_text(),dict(root=root,base=base,changes=changes,replace=replace))
    relative='docs/design/gpu-reproduction.md';path=root/relative
    changes[path]=path.read_text()+'''

## Single-elite GPU reduction

`GAGP_GPU_LINEAR_ELITE=1` is fixed at native population construction. With one
elite, a256-thread GPU reduction selects the greatest fitness and breaks ties by
lowest source index, then copies its phase slots into offspring index0. This
replaces quadratic rank scanning with linear work; selection RNG and variation
are unchanged. No elites and multiple elites retain existing behavior. NaNs are
rejected by the existing reproduction precondition. The ordering oracle covers
98 populations through16384, including signed zero and infinities. Native
generation comparisons cover complete AST/fitness/wrong trajectories, CPU
evaluation, ties, infinities and the zero/multiple-elite fallback.
'''
    record=root/'logs/twofold-optimization-20261022/linear-elite-source-apply.json';assert not record.exists()
    before={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() if p.exists() else None for p in changes}
    for path,text in changes.items():path.write_text(text)
    record.write_text(json.dumps(dict(status='applied_after_pinned17_and_exact_elite_preflight',source_before=before,
        source_after={str(p.relative_to(root)):hashlib.sha256(p.read_bytes()).hexdigest() for p in changes}),indent=2)+'\n')
    return list(before)
