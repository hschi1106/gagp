# Research Mainline: Exact GPU Grammar-Guided GP

This page summarizes the research system: what it claims, what "exact" means
and how it is checked, what was measured, and which designs were tried and
retired. The implementation contract is [gpu-reproduction.md](gpu-reproduction.md).

## Claims

GAGP is a grammar-guided genetic programming (GGGP) system for program
synthesis. It makes four claims, in this order:

1. **The whole loop runs on the GPU.** Selection, crossover, mutation, lowering
   of changed children to bytecode and fitness evaluation all run on the
   device, including for programs that work on strings and lists. The host
   ranks the fitness vector and records history.
2. **User-defined grammars run.** The search space is a compiled grammar
   definition ([grammar_definition.md](../../spec/grammar_definition.md)), not a
   fixed instruction set. Two very different families are evidence: the
   algorithm-structured templates of ASGP (`benchmarks/asgp30`) and statement
   grammars in the style of published G3P work (`benchmarks/psb1_g3p`).
3. **It is fast.** Measured against ASGP on one CPU thread and against GAGP's
   own CPU reference engine.
4. **Quality does not get worse.** The speed is not bought with search quality.

The design principle behind all four is exactness, which is not a claim. A
plain C++ reference engine runs the same operators and random draws on the CPU.
For the same inputs and seed the GPU engine publishes bit-identical genotypes,
fitness values, champions and final populations, so every speedup is measured
on the same search and an optimization can never silently change it. Anything
that changes which programs are explored, such as the steady-state schedule, is
declared as a member of the search configuration
([search_config.md](../../spec/search_config.md)), implemented identically by
both engines.

GAGP does not claim that its search is better than ASGP's, that it matches
ASGP, that it reaches the state of the art on PSB1, that it reproduced the
numbers of the ASGP paper, or that steady-state scheduling is its invention.

## Representation

Every search runs on the resident phase population. Reproduction runs on the
GPU (`--engine gpu`) or in the CPU reference engine (`--engine cpu`).
The resident engine admits whole-program grammars over all eight value types,
with statements, `let` binders, traversals and bounded regions, and fixed
region templates with up to 8 varied phases
([admission](gpu-reproduction.md#genome-layouts)). A grammar outside its
[capacities](../../spec/search_config.md#resident-capacities), or one that uses a
construct it does not yet hold, is rejected with an error before the search
starts ([admission](../../spec/search_config.md#resident-admission)).

## Default search

The default search ([`configs/search/default.json`](../../configs/search/default.json))
aligns every parameter ASGP also has (tournament 2, crossover 0.7, mutation
0.3, one phase per operator, phase depth 7, elitism 1, fuel 2,000,000) and
uses a synchronous steady state: each round creates `P` children with the
ordinary operators, evaluates them in one launch, and places each into the
population by replacing the loser of an inverse tournament of 2, never the
best member. With one round per generation-equivalent the GPU launch shape is
that of a generational run while the population dynamics are steady state.

## Exactness and how it is checked

"Exact" means that the GPU engine publishes the same values as the CPU
reference for the same task, search configuration, initial population and
seed.

- `gagp_test_phase_population_cpu_reference`: CPU reference vs GPU engine over
  several search policies and both schedules. Final populations, fitness,
  wrong counts and champions must be identical.
- `gagp_test_fitness_cpu_gpu_parity`: CPU and GPU evaluator parity.
- `benchmarks/run.py report` checks that GPU and CPU runs of the same
  configuration found the same best program with the same fitness history, for
  both suites.
- Engine changes are also checked against golden runs: exported champions and
  histories compared byte for byte with the previous build.

## Results

Measured on one RTX 3090 with the default search. Details in
[benchmarks/README.md](../../benchmarks/README.md#recorded-results).

- **GPU vs the CPU reference.** The same searches run 5x to 185x faster on
  the GPU than on the single-threaded CPU reference engine (geometric mean
  30x) with identical results.
- **GPU vs ASGP.** On frozen ASGP-initialized populations of 16384 members
  (30 tasks, seed 1), single-threaded ASGP took 95x longer than GAGP
  (geometric mean. About 74x after correcting for machine contention during
  the ASGP runs). ASGP is not part of this repository. This comparison is
  thesis evidence, not a benchmark the repository reproduces. It compares
  speed on the same inputs, not search quality, and GAGP's search is not
  claimed to beat ASGP's.
- **Quality.** On the 30 ASGP tasks (`benchmarks/asgp30`, P1024, 64
  generations, 5 seeds) GAGP solves 56 of 150 runs. The benchmark report prints
  the published ASGP counts beside them as context, not as a matched baseline.

Where the GPU time goes: evaluation is about three quarters of search time
and reproduction a few percent. The long tail of evaluation is single-lane
recursive list work replayed for the few cases whose intermediate lists
overflow the per-lane workspace ([payload.md](payload.md)).

## Retired designs

These were implemented, measured and removed. Git history keeps the code.

| Design | Outcome |
| --- | --- |
| Islands with ring migration | CPU-reference simulation, 300-run protocol: 8 islands +3.0 pp (not significant), 32 islands failed non-inferiority, no simulated speed gain |
| Asynchronous (barrier-free) evolution | Same quality at equal children; GPU speedup bound about 1.08-1.3x. Loses determinism and therefore reference parity. Its saving comes from steady-state replacement, which the synchronous steady state keeps |
| Small steady-state batches (`batch < P`) | Each evaluation launch has a 25-30 ms floor. `batch = 256` ran at 0.65x the speed of `batch = P` |
| Population-wide phase sharing | 70-84% of phase fuel belongs to phases used by a single program |
| GPU reproduction of native ASTs | Slower than the resident engine on the same grammars. Removed in 3.0 |
| Host native AST population and automatic fallback | A second genotype with its own operators duplicated the search and hid admission failures. An inadmissible grammar is an error |
| CPU worker reproduction, CPU fragment and phase banks | Not GPU speedups. Removed |
| Engine environment switches | Every exact optimization became unconditional once it passed the reference checks. Removed in 3.0 |
