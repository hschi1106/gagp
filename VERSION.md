# Version

Current release: 3.0.1

## Unreleased

One engine, wider admission, and more of the language on both engines. Search
results of grammars that were already admitted are unchanged (checked on the
`asgp30` tasks `count_odds`, `fibonacci`, `house_robber` and `is_sorted`, with
identical populations, fitness values and champions on both engines).

**Removed**

- The native AST engine and its automatic fallback. A grammar or initial
  population the resident engine cannot admit is an error raised before the
  first generation: `gagp run` prints
  `resident engine cannot admit this grammar or initial population: ...` and
  exits with status 2. There is no host reproduction and no second genotype.
- The `representation` member of `gagp-search-config-v1` and the
  `resident_declined` field of the run result.
- The constant mutation policy (`mutation` in a constant domain) from
  `grammar-definition-v2`. Mutation always replaced whole subtrees, so the
  member never changed a search. A grammar that still carries it fails to
  parse with an error that names it.

**Changed**

- The resident engine admits unit productions, repeated template holes,
  single-program grammars, root-template grammars whose members do not fit the
  region-template layout (they take the whole-program layout), Float ranges,
  more than 4 bounded regions per program (a capacity, rejected per candidate),
  traversals inside bounded region phases and non-default `resource_charge`.
  Every rejection names the limit and the offending value. The limits are
  listed once in the
  [capacity table](spec/search_config.md#resident-capacities). Sequence
  constant domains and a nonterminal reached under two lexical binder
  environments remain
  [language restrictions](spec/search_config.md#language-restrictions).
- A generated initial member that exceeds a per-program capacity is redrawn
  from a derived seed, up to 256 times, identically on both engines. Members
  that fit on the first draw are unchanged.
- With `limits.site_depth` above 0, a generated initial member whose
  derivation as the import reconstructs it (the first, for an ambiguous
  grammar) exceeds the site depth is redrawn, so the `psb1_g3p` grammars run
  under the default search. A grammar with no program within the site depth
  is rejected at admission, naming the depth it needs.
- Admission counts two locals for each `for_range` and `while_stmt` (its bound
  and its counter), as lowering holds them, where it counted one. A grammar
  whose deepest path needs more than 64 locals is now rejected at admission on
  both engines. The CPU engine also refuses an initial executable over 64
  locals, as the GPU engine does, so the two never disagree on a grammar.
- A bounded region plan declares at most 1,024 frames and 4,096 memo cells.
  Plan validation enforces this on both engines, replacing the GPU-only limits
  of 128 frames and 256 cells. Large plans use a per-lane hash index on the GPU.
- `list_allocation_limit` counts list cells and string bytes. Every builtin
  that builds a list or a string charges the length of its result (`append`,
  `prepend`, `reverse`, the list edits, the string conversions, the `strip`
  family and `capitalize` now charge too, `slice` stays free), and string
  inputs are charged like list inputs ([spec/fitness.md](spec/fitness.md)).
  Tasks without the limit are unchanged.
- Physical payload exhaustion is never a score. The CPU engine gives each case
  a 1 GiB overlay (it was 8 MiB and its exhaustion scored as a runtime error)
  and stops the run when a case exceeds it, as the GPU stops when replay cannot
  fit. The message asks for a lower `list_allocation_limit`.
- Every `psb1_g3p` task sets `list_allocation_limit` 65536.

**New**

- Fourteen builtins on the CPU, the GPU and in the grammar catalog: `insert`,
  `remove_at`, `set_at`, `index_mod`, list `contains`, `range`, `strip`,
  `lstrip` and `rstrip` (one and two arguments), `startswith`, `endswith` and
  `capitalize`.
- The `psb1_g3p` benchmark of six PSB1 problems with G3P grammars, and the
  `--suite` option of `benchmarks/run.py`.
- The bounded loop `while_stmt(Int,Bool,Block)->Statement` on both engines. It
  stops quietly after `bound` iterations or when its condition is false, the
  shape of the guarded `while` loops of published G3P grammars
  ([spec/grammar.md](spec/grammar.md#program-grammar)).

## 3.0.1

Faster evaluation; search results are unchanged (65 golden runs, and the 30
P16384 runs below with identical champions and best-of-generation histories).

- The bounded-tuple allocation proof is memoized per fitness session.
- Freed GPU transport buffers are cached and reused by the next evaluation.

On ASGP-shaped searches with population 16384, 3.0.1 is 1.016x faster than
3.0.0 (geometric mean; up to 1.07x on `all_equal` and `is_sorted`) and 1.17x
faster than 2.x.

## 3.0.0

Release 3.0 makes GAGP one program with one search configuration. The
language, bytecode and value semantics are unchanged from 2.0.0: programs,
bytecode and generated populations of 2.0.0 load and run identically, and the
runtime semantic identity stays `gagp-native-2.0.0`.

**New**

- `gagp`, a single executable: `gagp run` (search a task), `gagp eval` (score
  a program) and `gagp grammar` (validate, inspect, resolve, generate).
- Task files (`gagp-task-v1`) and run results (`gagp-run-v1`) with a readable
  program text (`best.text`), saved populations (`gagp-population-v1`) and
  `--init` ([spec/task.md](spec/task.md)).
- One search configuration format (`gagp-search-config-v1`) whose built-in
  default is `configs/search/default.json`: synchronous steady state, tournament
  2, crossover 0.7, mutation 0.3, elitism 1.
- `benchmarks/asgp30`: the 30 tasks of the ASGP paper as ready-to-run tasks,
  with `benchmarks/run.py`.
- Automatic fallback (removed after 3.0.1, see Unreleased): in 3.0.0 a grammar
  the resident GPU engine could not hold ran with host reproduction instead of
  failing, and the result recorded why.

**Removed**

- The earlier executables (`gagp_evolve_cli`, `gagp_generate_cli`,
  `gagp_grammar_cli`, migration tools) and the Python operational tools.
- All engine environment switches; every exact optimization is now always on
  when it applies. Only `GAGP_CUDA_DEVICE`, `GAGP_GPU_DIAGNOSTICS` and
  `GAGP_SEARCH_TRACE` remain.
- Native AST reproduction on the GPU (the resident engine replaces it), the
  fixed-ASGP benchmark harness, host worker threads, CPU fitness verification
  and evaluation checkpoints.
- Release-1 migration (`grammar-config`, `ast-prefix` v1, `grammar-materialized-v2`).
- Library-only projected resource budgets, which `gagp` could not reach: the
  `EvolutionConfig` members `initial_resource_budget`,
  `offspring_resource_budget`, `gpu_projected_phase_profile` and
  `additional_generation_requests`, the `resident-gpu-v3-projected` engine
  profile, and the grammar-layer projection (`ProjectedBudget`,
  `project_derivation_resources`, `joint_resource_frontier`, budgeted donor
  pools). `resource_charge` stays in the grammar format and its hash.

Search results of 3.0 equal those of the last 2.x build for the same inputs
and search settings (checked on 65 golden runs). On ASGP-shaped searches
with population 16384, 3.0 is about 1.15x faster than 2.x.

## 2.0.0

Release 2.0 made compiled grammar definitions (`grammar-definition-v2`) the only
search-space format and introduced the v2 program formats.

| Contract | Release 1 | Release 2 and 3 |
| --- | --- | --- |
| Program AST | `ast-prefix` | `ast-prefix-v2` |
| Bytecode | `bytecode-json`, `bytecode-fixture` | `bytecode-json-v2`, `bytecode-fixture-v2` |
| Grammar | `grammar-config` | `grammar-definition-v2` |
| Generated program and population | `grammar-generated-v1`, `grammar-population-v1` | `grammar-generated-v2`, `grammar-population-v2` |
| Fitness cases | `fitness-cases` | `fitness-cases` (unchanged) |

Release-1 artifacts are rejected; programs must be regenerated or recompiled
from source. The v2 opcode layout assigns 0-24 and 28; 25-27 are permanent
holes. AST node kinds 53-70 are permanent holes for removed specialized nodes.
Decoders never reinterpret either range.
