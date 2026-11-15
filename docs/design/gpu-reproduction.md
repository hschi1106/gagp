# GPU Reproduction: the Resident Engine

The resident engine is how GAGP runs a whole genetic programming search on the
GPU. This document is its implementation contract: what it admits, how it
represents and varies programs, and how the CPU reference engine keeps it
honest. The semantics it must implement are in
[search_config.md](../../spec/search_config.md); the overview of the research
system is [research-mainline.md](research-mainline.md).

## Overview

The population lives on the device for the whole run as fixed-shape *phase genes*.
Tournament selection, typed subtree crossover, subtree mutation, elitism and
lowering of changed programs to bytecode all run as GPU kernels, and the GPU
evaluator reads the lowered programs in place. Only the fitness vector returns
to the host each generation. A champion is exported only when a newly placed
child becomes the best, and otherwise the history repeats the previous one.

| Owner | File | Engine |
| --- | --- | --- |
| `NativePhasePopulation` | `cpp/src/evolution/repro/gpu/phase_population.cu` | GPU genotype, GPU operators, GPU evaluation |
| `HostPhasePopulation` | `cpp/src/evolution/repro/gpu/phase_population_cpu.cpp` | The CPU reference: same operators on the host, CPU evaluation, no CUDA |
| shared operators | `cpp/src/evolution/repro/gpu/phase_operators.cuh` | `__host__ __device__` bodies of every per-child step |
| phase lowering | `cpp/src/evolution/repro/gpu/device/phase_compile.cuh` | Phase tree to bytecode, mirroring the native compiler |
| layout and admission | `phase_layout.hpp`, `phase_grammar.hpp` | Which grammars and genomes fit, and how they are laid out |
| generation loops | `cpp/src/evolution/evolve.cpp` (`run_phase_population`, `run_phase_population_steady`) | Ranking, selection fitness, history and export, common to both owners |

A grammar or initial genome the engine cannot hold is an error. Owner
construction rejects it, and `evolve_population` raises `invalid_argument`
with a message that begins `resident engine cannot admit this grammar or
initial population:` and names the construct. `gagp run` prints it and exits
with status 2 without writing a result. No other engine runs instead.

GPU construction admits every member on the host (import, phase grammar
tables, bytecode verification and the packed evaluator form) before its first
CUDA call. `evolve_population` meanwhile initializes the evaluation session,
whose cost is mostly CUDA context creation, on a second thread, and the owner
waits for it right before that first call. Neither thread evaluates or
reproduces programs. A failed session is still reported before any admission
error.

## Genome layouts

A genome is one of two layouts, chosen by `import_native_phase_population`
(`phase_layout.hpp`) from the grammar and the initial population. The rule is
in [Resident admission](../../spec/search_config.md#resident-admission).

- **Region template.** A candidate grammar (entry with one template
  production, no `let` or traversal primitive) keeps the template fixed and
  varies up to 8 independent phases, the holes of its one bounded region. The
  population first imports exactly as each member's own import chooses; it
  keeps this layout only when every member takes it with one fixed skeleton.
  A phase grammar with Float, String or nonempty list constant domains never
  takes it, because variation would draw constants the layout does not hold.
- **Whole program.** Every other population, including a candidate's whose
  members do not all fit the region template, is imported again as one phase
  holding the whole program. An expression entry is lowered and completed by
  the native program terminator (`patch_whole_program`); a Program entry
  lowers its statements (`assign`, `return`, `if_stmt`, `for_range`,
  `while_stmt`, blocks, up to 16 grammar locals) exactly as the native
  compiler does, including its first-use local order.

Initial population generation (`generate_phase_initial_population`) applies
the same rule to its prototype program.

Phase expressions may use all eight value types, any catalog operation of
matching arity, grammar inputs, finite constant domains of any type, Int
ranges, Float ranges (quantized or not, sampled by `numeric_sampling.hpp`
exactly as native construction) and `sample_from` samplers. `sequence`
constant domains are not held yet. Whole-program phases may also contain:

- **`let` binders**, numbered statically: every nonterminal is entered under
  exactly one binder environment, so generation never renumbers. A grammar that
  reaches a nonterminal under two environments, or needs 16 or more binder
  numbers, is rejected.
- **Traversals** (`traverse`, `traverse_reverse`, `traverse_range`,
  `traverse_range_reverse`), lowered to the native prefix exactly, including
  its aliasing of stored sources and its length facts.
- **Bounded regions** (at most 8 operands plus phases, 8 captures, 4 regions
  per program). Each region phase is a separate program whose locals start
  with its bindings, and variation inside a phase uses the phase's own scope.

Templates and holes are zero-node wrappers: a site on a template production
passes to its first physical node, and site annotation recovers a production
from that node's expression id. Phase grammar construction
(`compile_phase_grammar`, shared by both owners) therefore inlines every unit
production: an alternative that resolves through wrappers to a reference is
replaced by the referenced alternatives with multiplied weights, alias-cycle
alternatives are dropped and diamonds are merged (the rules are in
[Resident variation](../../spec/search_config.md#resident-variation)). Every
production root then resolves to a node, and the alternatives of one
nonterminal to distinct nodes. An inlined chain carries the outermost
nonterminal's site only, so `variation: false` on an inner nonterminal of the
chain has no effect at that node, and an inner variable nonterminal under a
fixed outer one is not a separate site.

A hole repeated in its template body is one shared derivation
(`PhaseSharedHole`). Fills are compiled separately per occurrence, in
post-order, so the first fill's expressions (up to references) occupy a
contiguous id range and each later fill is the same shape shifted by a
constant; construction verifies this. The genotype holds every occurrence:
`share_phase_tree` walks a tree, records the first occurrence in prefix order
and makes each later one an exact copy, with its own fill's expression ids,
binder numbers and region entries, the first's choices below references and
constant values, and fixed sites. Generation, crossover and mutation run
unchanged and then rebuild the copies through the child's scratch row; a
rebuilt program over the node, depth or site-depth budget is rejected. Import
renames the copies of an imported member (whose copies carry the first
occurrence's ids) the same way, and annotation checks that every copy is
exact. A grammar whose only member is one fixed program is admitted with no
variable site. A `resource_charge` is ignored: it never changes a layout or a
search.

## Capacities

The capacities are the language's limits on the resident engine, listed with
their values in [Resident capacities](../../spec/search_config.md#resident-capacities).
Admission checks them in three places, each message naming the limit and the
offending value:

- `compile_phase_grammar` (`phase_grammar.hpp`): every bound over the
  programs the grammar can derive within its depth budget (scopes, bindings,
  binder numbers, inputs, grammar locals, region shapes, constant domains,
  tables, unit inlining, and the lowering bound on locals, open traversals,
  lets, loops and value `if`s), so variation never produces a program the
  engine cannot lower or hold.
- `import_native_phase_layout` (`phase_layout.hpp`): `search_limits`, the
  nodes and bounded regions of each imported phase and the statement program
  size. Generation and variation reject a candidate with more than 4 bounded
  regions (`phase_region_count`), as they reject one over the node budget.
- The owners' constructors: population size, program inputs, phase count and,
  on the GPU, the genotype workspace and the evaluator's local capacity for
  the first member.

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
node/depth/invocation-depth feasibility table. The GPU owner names its profile
`resident-gpu-v4-site-depthN` (`EvolutionResult::reproduction_profile`, not a
`result.json` member).

`ranking.tie_break: grammar_sites` breaks fitness and wrong-count ties by the
number of mutable grammar invocations instead of AST nodes. It changes
selection and requires a task metric with wrong counts.

## Distribution notes

The resident engine defines its own operator distributions. Tournaments
sample with replacement. Sites are expression nonterminal occurrences within
phases. Crossover tries at most `crossover.attempts` compatible site pairs.
Mutation uses the remaining whole-program node and phase depth budget. Fixed
template structure is never a site, and ties keep source order.
