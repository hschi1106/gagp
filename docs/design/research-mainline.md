# Research Mainline: Exact GPU Grammar-Guided GP

This page summarizes the research system: what it claims, what "exact" means
and how it is checked, what was measured, and which designs were tried and
retired. The implementation contract is [gpu-reproduction.md](gpu-reproduction.md).

## Claim

GAGP is a grammar-guided genetic programming (GGGP) system for program
synthesis whose whole search loop runs on the GPU:

- **User-defined grammars.** The search space is a compiled grammar definition
  ([grammar_definition.md](../../spec/grammar_definition.md)), not a fixed
  instruction set. Algorithm schemes such as ASGP's divide-and-conquer and
  dynamic-programming templates are ordinary grammars; so is a three-line
  arithmetic grammar.
- **GPU evaluation and GPU reproduction.** Selection, crossover, mutation,
  lowering of changed children to bytecode and fitness evaluation all run on
  the device. The host ranks the fitness vector and records history.
- **Exactness.** A plain C++ reference engine runs the same operators and
  random draws on the CPU. For the same inputs and seed the GPU engine
  publishes bit-identical genotypes, fitness values, champions and final
  populations, so a speed optimization can never silently change the search.
- **Changes of search dynamics are declared.** Anything that changes which
  programs are explored, such as the steady-state schedule, is a member of the
  search configuration ([search_config.md](../../spec/search_config.md)),
  implemented identically by both engines.

## Representations

| Representation | Used for | Reproduction |
| --- | --- | --- |
| `resident` (default) | Every grammar the resident engine admits | GPU (`--engine gpu`) or the CPU reference (`--engine cpu`) |
| `native_ast` | Grammars the resident engine declines (automatic), or an explicit choice | Host; evaluation on the selected engine |

The resident engine admits whole-program grammars over all eight value types,
with statements, `let` binders, traversals and bounded regions, and fixed
region templates with up to 8 varied phases
([admission](gpu-reproduction.md#genome-layouts)). The `native_ast`
representation exists so that any valid grammar runs; it is not the fast path.

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
  several search policies and both schedules; final populations, fitness,
  wrong counts and champions must be identical.
- `gagp_test_fitness_cpu_gpu_parity`, `gagp_test_evolution_cpu_gpu_parity`:
  evaluator and native-AST parity.
- `benchmarks/run.py report` checks that GPU and CPU runs of the same
  configuration found the same best program with the same fitness history.
- Engine changes are also checked against golden runs: exported champions and
  histories compared byte for byte with the previous build.

## Results

Measured on one RTX 3090 with the default search; details in
[benchmarks/README.md](../../benchmarks/README.md#recorded-results).

- **Quality.** On the 30 ASGP tasks (`benchmarks/asgp30`, P1024, 64
  generations, 5 seeds) GAGP solves 56 of 150 runs.
- **GPU vs the CPU reference.** The same searches run 5x to 185x faster on
  the GPU than on the single-threaded CPU reference engine (geometric mean
  30x) with identical results.
- **GPU vs ASGP.** On frozen ASGP-initialized populations of 16384 members
  (30 tasks, seed 1), single-threaded ASGP took 95x longer than GAGP
  (geometric mean; about 74x after correcting for machine contention during
  the ASGP runs). ASGP is not part of this repository; this comparison is
  thesis evidence, not a benchmark the repository reproduces. It compares
  speed on the same inputs, not search quality, and GAGP's search is not
  claimed to beat ASGP's.

Where the GPU time goes: evaluation is about three quarters of search time
and reproduction a few percent. The long tail of evaluation is single-lane
recursive list work replayed for the few cases whose intermediate lists
overflow the per-lane workspace ([payload.md](payload.md)).

## Retired designs

These were implemented, measured and removed; git history keeps the code.

| Design | Outcome |
| --- | --- |
| Islands with ring migration | CPU-reference simulation, 300-run protocol: 8 islands +3.0 pp (not significant), 32 islands failed non-inferiority, no simulated speed gain |
| Asynchronous (barrier-free) evolution | Same quality at equal children; GPU speedup bound about 1.08-1.3x; loses determinism and therefore reference parity. Its saving comes from steady-state replacement, which the synchronous steady state keeps |
| Small steady-state batches (`batch < P`) | Each evaluation launch has a 25-30 ms floor; `batch = 256` ran at 0.65x the speed of `batch = P` |
| Population-wide phase sharing | 70-84% of phase fuel belongs to phases used by a single program |
| GPU reproduction of native ASTs | Slower than the resident engine on the same grammars; removed in 3.0 |
| CPU worker reproduction, CPU fragment and phase banks | Not GPU speedups; removed |
| Engine environment switches | Every exact optimization became unconditional once it passed the reference checks; removed in 3.0 |
