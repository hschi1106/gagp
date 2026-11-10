# GPU Reproduction: the Resident Engine

The resident engine is how GAGP runs a whole genetic programming search on the
GPU. This document is its implementation contract: what it admits, how it
represents and varies programs, and how the CPU reference engine keeps it
honest. The semantics it must implement are in
[search_config.md](../../spec/search_config.md); the overview of the research
system is [research-mainline.md](research-mainline.md).

## Overview

With `representation: resident` (the default of `gagp run`), the population
lives on the device for the whole run as fixed-shape *phase genes*.
Tournament selection, typed subtree crossover, subtree mutation, elitism and
lowering of changed programs to bytecode all run as GPU kernels, and the GPU
evaluator reads the lowered programs in place. Only the fitness vector and
each generation's champion return to the host.

| Owner | File | Engine |
| --- | --- | --- |
| `NativePhasePopulation` | `cpp/src/evolution/repro/gpu/phase_population.cu` | GPU genotype, GPU operators, GPU evaluation |
| `HostPhasePopulation` | `cpp/src/evolution/repro/gpu/phase_population_cpu.cpp` | The CPU reference: same operators on the host, CPU evaluation, no CUDA |
| shared operators | `cpp/src/evolution/repro/gpu/phase_operators.cuh` | `__host__ __device__` bodies of every per-child step |
| phase lowering | `cpp/src/evolution/repro/gpu/device/phase_compile.cuh` | Phase tree to bytecode, mirroring the native compiler |
| layout and admission | `phase_layout.hpp`, `phase_grammar.hpp` | Which grammars and genomes fit, and how they are laid out |
| generation loops | `cpp/src/evolution/evolve.cpp` (`run_phase_population`, `run_phase_population_steady`) | Ranking, selection fitness, history and export, common to both owners |

A grammar the engine cannot hold is not an error: `evolve_population` reruns
the same search with `native_fallback_config` (typed AST population, host
reproduction, the same evaluation engine) and records the reason in
`EvolutionResult::resident_declined`, which `gagp run` prints and writes to
`result.json`. Native AST reproduction lives in
`cpp/src/evolution/repro/backend.cpp`. Only admission declines fall back;
invalid search policies still fail.

## Genome layouts

A genome is one of two layouts, chosen from the grammar:

- **Whole program.** A grammar without templates is admitted as one phase
  holding the whole program. An expression entry is lowered and completed by
  the native program terminator (`patch_whole_program`); a Program entry
  lowers its statements (`assign`, `return`, `if_stmt`, `for_range`, blocks,
  up to 16 grammar locals) exactly as the native compiler does, including its
  first-use local order. Statement programs are limited to 512 nodes.
- **Region template.** A grammar whose genomes share one fixed root template
  with exactly one bounded region, and no `let` or traversal primitive, keeps
  the template fixed and varies up to 8 independent phases (the holes of the
  template).

Phase expressions may use all eight value types, any catalog operation of
matching arity, grammar inputs, finite constant domains of any type and Int
ranges. Whole-program phases may also contain:

- **`let` binders**, numbered statically: every nonterminal is entered under
  exactly one binder environment, so generation never renumbers. A grammar that
  reaches a nonterminal under two environments, or needs 16 or more binder
  numbers, is declined.
- **Traversals** (`traverse`, `traverse_reverse`, `traverse_range`,
  `traverse_range_reverse`), lowered to the native prefix exactly, including
  its aliasing of stored sources and its length facts.
- **Bounded regions** (at most 8 operands plus phases, 8 captures, 4 regions
  per program). Each region phase is a separate program whose locals start
  with its bindings, and variation inside a phase uses the phase's own scope.

Templates and holes are zero-node wrappers: a site on a template production
passes to its first physical node, so production roots must resolve to
distinct nodes. A hole repeated in its template body is admitted only when its
fill is fixed. A grammar whose only member is one fixed program uses the
native fallback, and so does a genome whose fixed template structure carries a
`resource_charge` other than the default (more than one node or depth level,
or a depth reset).

## Capacities

Admission bounds every resource over every program the grammar can derive
within its depth budget, so variation never produces a program the engine
cannot lower or hold:

| Resource | Limit |
| --- | --- |
| Population | 16384 members |
| Whole-program grammar budget | 2048 nodes, depth 64 |
| One variable phase | 1024 nodes |
| Open traversals / lets / loops / analysed value `if`s on one path | 8 / 16 / 16 / 16 |
| Locals | 64 |

A child that reads a local before native definite assignment would define it
is rejected: crossover tries its next site, mutation keeps the crossed child,
and initialization draws again. Sites of a nonterminal with
`"variation": false` count toward site depth but are never chosen.

## Operators and the CPU reference engine

Per-child selection and variation are `__host__ __device__` functions in
`phase_operators.cuh`: tournament, site draw, crossover with its gate and
retries, subtree mutation (generation, splice, copy), offspring comparison and
phase lowering. The GPU kernels are thin wrappers around them, and
`HostPhasePopulation` calls the same functions serially. Both owners admit the
same input, use the same per-child seeds and the same operation order
(crossover, mutation, elites, comparison, swap, lowering), and reuse a
completed fitness result only for a genotype identical to a parent. The
search policy (`PhaseVariationPolicy`) is a constructor argument of both.

`gagp_test_phase_population_cpu_reference` requires identical final
populations, fitness values, wrong counts and champions from both owners for
several policies and both schedules. Every engine optimization is accepted
only if this test, the golden comparison of exported champions and histories,
and the parity tests still pass.

Subtree generation on the device selects productions with host-built
threshold tables (`phase_choice_tables.hpp`): for each nonterminal, depth and
budget the eligible set changes only at sorted minimum costs, and the serial
FP64 draw is monotone in the 53-bit random value, so integer thresholds pick
exactly the production of the serial loop.

## Schedules

Both owners implement both schedules and publish identical results.

- **Generational**: each generation creates `P` children, keeps `elitism`
  elites, evaluates every changed child, and ends with a final evaluation.
- **Synchronous steady state** (`schedule.mode: steady_state`, the default
  search): each round creates `batch` children with the same per-child
  operators, evaluates them together, then places them one by one; each
  replaces the loser of an inverse tournament, never the current best. Each
  owner implements `begin_steady`, `make_batch`, `evaluate_batch`,
  `batch_nodes` and `commit_batch`. The GPU owner lowers and evaluates a batch
  through a second packed program set and copies placed rows into the
  population. The default `batch` is the population size, because every
  evaluation launch has a latency floor that makes smaller batches slower.

## Site depth and tie breaks

`limits.site_depth` (1-15, 0 disables it) caps the nesting of independently
mutable grammar invocations within a phase; physical node and depth budgets
remain separate checks. Cold import checks every initial member, crossover
checks the spliced site's depth, and mutation samples from a joint
node/depth/invocation-depth feasibility table. The engine profile reported by
the run is `resident-gpu-v4-site-depthN`.

`ranking.tie_break: grammar_sites` breaks fitness and wrong-count ties by the
number of mutable grammar invocations instead of AST nodes. It changes
selection and requires a task metric with wrong counts.

## Distribution notes

The resident engine is a GP system of its own, not a replay of native AST
reproduction: tournaments sample with replacement; sites are expression
nonterminal occurrences within phases; crossover tries at most eight
compatible sites; mutation uses the remaining whole-program node and phase
depth budget; fixed template structure is never a site; ties keep source
order. Results of the two representations are therefore not comparable member
by member, and every result records its representation.
