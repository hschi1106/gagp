path=root/'docs/design/gpu-reproduction.md'
text=changes.get(path,path.read_text())
changes[path]=text+'''

## Completed fitness of exact unchanged offspring

`GAGP_GPU_UNCHANGED_FITNESS_REUSE=1` is fixed when a native GPU phase
population is constructed. Its post-elite device comparison proves every tree
node, origin, constant tag/bits/flag equal to the selected prior parent. Scores
are eligible only from a successful completed GPU evaluation, for the same
immutable fitness-session revision and either that generation or its immediate
successor. Reinitialization (including a failed attempt), another session, a
missing evaluation, or absent exact equality forces ordinary full evaluation.
Selection, RNG, GPU variation and lowering are unchanged. No CPU reproduction,
approximate program equality, output prediction or cached payload result is used.

The GPU skips only certified rows. Host copyback restores their exact scalar
fitness, wrong and logical error/timeout/case counters. `fitness_reused_programs`
and `fitness_reused_program_cases` expose inherited observations; physical root
calls and actual payload replay remain separate. Failed/unscored evaluations
never supply reused scores. Internal complete counters are retained when this
option is active, while callers requesting no diagnostics still receive no
case-counter vector. `gagp_test_gpu_phase_population` covers exact multi-generation
AST/CPU parity, repeated evaluation, elite replacement, reset/fuel/policy changes,
separate sessions and unscored generation gaps; host scope guards are tested in
`gagp_test_fitness_contract`.
'''
path=root/'.agents/skills/gagp-navigation/SKILL.md';text=changes.get(path,path.read_text())
changes[path]=text+'''
- Exact unchanged native-offspring fitness reuse: `repro/gpu/completed_fitness_reuse.hpp`
  owns generation/revision scope; post-elite equality in `phase_population.cu`
  supplies sealed parent maps. `FitnessSessionGpu` validates session-local
  completed counters, and `device/kernels.cuh` skips certified rows. Contract:
  `docs/design/gpu-reproduction.md`; tests `gagp_test_gpu_phase_population` and
  `gagp_test_fitness_contract`. No public reuse factory or serialized proof.
'''
