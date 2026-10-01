# Fixed ASGP/GAGP benchmark

This document owns `fixed-asgp-v1`, the fast fixed-parent system comparison.
It complements the broader [experiment protocol](experiment-protocol.md).
It measures scoring plus reproduction, not time-to-solution or equivalent
algorithm execution. Numerical differences and different reproduction outcomes
are accepted and disclosed. Runtime semantics remain owned by `spec/`.

## Workloads and matrix

| Task | Frozen input distribution | Execution path |
| --- | --- | --- |
| `sum_of_elements` | ASGP generator, list length 1–50, values 0–100 | DC, list slicing, phase execution |
| `median` | ASGP generator, exactly three integers in −100–100 | DC base size 3: no recursive subdivision; small evaluation and fixed overhead |
| `house_robber` | ASGP generator, list length 1–20, values 0–20 | 1D DP with n−1/n−2 dependencies and memoization |

Every task has 1024 actual evaluated training cases. The generator includes its
native edge cases; natural duplicates are retained and counted, not padded by
duplicating a small fixture. Data seed is 20260626. Test cases and simplification
do not participate in timing.

Each task freezes 8192 original ASGP parents, population seed 0, ramped phase
depths 2–7 with alternating grow/full construction. Populations are prefixes of
this artifact. The adapter maps source operators without screening or replacing
parents. GAGP imports the same source expressions into a task-specific grammar.
Translated ASTs are prepared once per required prefix size and reused.

| Mode | Evaluation | Reproduction | Overlap |
| --- | --- | --- | --- |
| `asgp_1t` | original ASGP Score, sequential | original ASGP operators | none |
| `gagp_cpu` | GAGP CPU | GAGP CPU | off |
| `gpu_eval` | GAGP GPU | GAGP CPU | off |
| `gpu_repro` | GAGP GPU | GAGP GPU | off |
| `gpu_overlap` | GAGP GPU | GAGP GPU | on |

The daily suite is three tasks × P=1024 × five modes: 15 cells. Each cell is an
isolated process with one warmup and three measured repetitions of the same
fixed-parent generation. Mode order rotates across tasks. Warmup and measured
samples are retained separately. This does not assert cross-process persistent
session reuse or a continuous multi-generation steady state.

The scaling suite uses all three tasks at P=1024/2048/4096/8192: 60 cells. It is
run at optimization milestones, especially after allocation, batching or cache
changes. It does not replace the daily suite.

Daily wall-time target is 180 seconds, budget 300 seconds, excluding build and
one-time artifact preparation. Setup, process startup, input reading and reporting
belong to daily wall time even when outside generation timing. The runner finishes
the requested matrix if the budget is exceeded and reports the overrun; it does
not silently reduce cases or repetitions. A per-cell process timeout is reported
as failure (default 300 seconds for daily, 1800 for scaling, adjustable via
`--cell-timeout`). Incomplete cells never contribute a speedup.

## Timing and configuration

The timed interval starts with in-memory, unscored parents and ends when the
next population has been produced. GAGP uses native `generation_total_ms`, with
compilation, evaluation, ranking, reproduction, transfers, decoding, validation
and in-generation payload retention included. ASGP uses a wall timer around
Score, tournament selection, elite copying and native crossover/mutation.
Offspring are not evaluated again. Overlapping phase times are not added together.

Offline source generation, adapter translation, grammar compilation, initial
parent admission/copy and GPU session initialization are excluded from generation
time. GAGP's wider evolve-call time and GPU initialization time are also retained.
These scopes follow the [native timing reference](../reference/timing.md).

- Release build; fixed visible CUDA device; record CPU/GPU, build settings,
  binary/source hashes, artifacts and exact commands. Run without competing work.
- No early stop, simplification, or fitness reuse across repetitions. Native
  within-call compilation/preparation behavior remains in effect.
- ASGP: tournament 2, crossover 0.7, mutation 0.3, one-phase variation, depth 7,
  elite 1, N−1 varied children. Score's fitness uses the task's native metric.
- GAGP: tournament 2, native crossover on each parent pair, mutation 0.3,
  subtree probability 1, N children, physical nodes 1024/depth 48. Literal ranges
  and phase visibility follow the task; mutation terminal/bound weights are 4,
  other production weights 1. Offspring distributions are not claimed equivalent.
- GAGP blocksize 512 in all GPU modes. DC frames 50; DP frames/cells 21. These
  cover the declared input ranges. ASGP recursion limit is 127 (root depth zero).
- Both execution budget numbers are 2,000,000, penalty/case-error cap 1000;
  budget units and fitness definitions are not equivalent. Integer remainder,
  large-integer numeric paths and bounded GPU payload behavior can differ.
- GAGP GPU preparation/compilation can use up to 20 CPU workers. Comparisons
  are CPU+GPU system speedups versus ASGP-1T, not isolated GPU hardware speedups.

Report per-task median/min/max generation milliseconds, ASGP-1T/GAGP-mode,
GAGP-CPU/GAGP-mode and synchronous/overlapped reproduction ratios. Do not compare
un-normalized native fitness values across systems. Variation counters classify
operator outputs, not final-population counts. The main timing modes leave optional per-case diagnostics disabled. Error/timeout
counts come from the separate probes below, never inferred from aggregate fitness.

Known hand-authored solutions are checked against every frozen case in ASGP and
GAGP CPU before timing. GPU aggregate fitness is recorded, including nonzero
error from its accepted payload approximation. No arbitrary evolved-program
parity gate is imposed. Crashes and missing results are failures, not losses.
Short evolution/held-out quality experiments are separate from this timing suite.

The 40× target applies to Sum of Elements and House Robber at 1024×1024. Median
exposes fixed overhead. The user clarified that the earlier term `medium` meant Median.

## Build and run

The optional native adapter requires the external ASGP C++20 source tree. It
links original ASGP sources without editing them. Example on the RTX 3090 host:

```bash
cmake -S cpp -B cpp/build/release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=86 -DGAGP_BUILD_BENCHMARKS=ON \
  -DGAGP_ASGP_SOURCE_DIR="$HOME/r13921069"
cmake --build cpp/build/release --target gagp_fixed_asgp_bench -j10
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp prepare
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp run \
  --out logs/fixed-asgp/daily-001
```

`--artifacts` selects the reusable frozen directory. Use a new directory if the
ASGP source, adapter or grammar changes; hash mismatches are rejected. A changed
GAGP implementation can reuse compatible artifacts. `run` requires a fresh output
directory and never overwrites previous results. For scaling, first prepare with
`--suite scaling`, then run with the same option. It prepares all 8192 translated
parents and evaluates prefixes; it never filters a parent that fails admission.

Output includes `manifest.json` with commands and all samples, `environment.json`,
per-cell logs/JSONL, `summary.json`, `summary.csv`, and `report.md`. Retain raw data
under ignored `logs/`; copy reviewed compact evidence to `benchmarks/fixed_asgp/`.

Ownership: `cpp/src/bench/fixed_asgp/` handles the native adapter and timing;
`tools/gagp_tools/experiments/fixed_asgp/` handles task grammar authoring, immutable
artifact preparation, matrix execution and reports. Neither defines product
runtime semantics. Validate runner aggregation/failures with
`python3 -m unittest discover -s tools/tests -p test_fixed_asgp.py -v`.
The optional `gagp_test_fixed_asgp_bench` CTest checks all three known solutions,
population translation and five modes using eight parents and all 1024 cases.


## Optimization and diagnostic probes

The current opt-in runtime profiles and eligibility rules are owned by
[payload design](../design/payload.md#proven-integerlist-execution-profile).
For the tested combination, prefix ordinary fixed runs with:

```bash
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1 \
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1 \
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp run \
  --suite scaling --out logs/fixed-asgp/optimized-scaling
```

The runner records all `GAGP_*` environment settings alongside binary/source
identity. For populations beyond the analysis-cache capacity, the optional
`GAGP_POPULATION_HANDOFF=1` [generation-owned continuation](../design/gpu-reproduction.md)
trades additional host memory for avoiding repeated analysis. These flags neither alter population/case counts nor bypass unsupported
programs. The continuing optimization record, controls, rejected experiments and
resume commands are in [the progress index](../../benchmarks/fixed_asgp/optimization/PROGRESS.md).

The native `measure SOURCE PREPARED GRAMMAR POP MODE OUT` entry also supports
`snapshot`, `search` and `phase_bank`. They write diagnostic JSON and are separate
from the five modes above:

- `snapshot`: identical frozen programs and all cases, per-program fitness/error/
  timeout/fallback counts. `GAGP_SNAPSHOT_CPU=1` also scores every program on CPU.
- `search`: four actual generations, plus separately timed final evaluation and
  top-16 full-case CPU reevaluation. Set `GAGP_GPU_DIAGNOSTICS=1` for per-generation
  counters and `GAGP_BM_SEED=0/1/2` for the short quality screen. Final population
  sizes and exact decoded genome diversity are recorded.
- `phase_bank`: experimental finite independent-phase profile. Capability
  detection receives grammar/program structure, not task identifiers. It requires
  one fixed bounded-region skeleton and independent compatible template holes;
  unsupported structures fail explicitly. Initial compilation, membership/site
  analysis, skeleton proof and phase interning are timed as `bank_init_ms`, added
  to the first complete generation in `first_generation_with_bank_ms`. Subsequent
  generations materialize bytecode references, fully verify/pack and evaluate all
  cases. No fitness cache is used.

Phase-bank reproduction uses tournament 2, elite 1, one whole-phase crossover and
0.3 mutation by sampling an admitted source phase. It cannot invent new phase
shapes/constants and is **not the full grammar search space**. Fuel, precision,
case count and execution limits remain unchanged. Phase identities are interned
before reporting changed children/diversity. Conservative grammar/node/depth
bounds cover combinations. Every final child is independently exported and
re-admitted to the original grammar; exported AST and handle fitness/counters
must match. Export, final evaluation and top-16 CPU checks are separately timed.
`GAGP_BANK_GENERATIONS` controls only this probe (default 4, maximum 256).
Report cold first generation and subsequent generation times separately; do not
substitute amortized bank timings for the original fixed benchmark.

Setting `GAGP_BANK_FRESH_MUTATION=1` replaces finite-bank mutation with fresh
whole-phase donors from the authored grammar. Each donor is fully admitted,
compiled and interned during the generation; failures and effective changes are
reported. This variant requires registry-independent constant domains and limits
the probe to 16 generations because it retains complete proof sources. It still
fixes the region skeleton and varies only independent whole phases. Three variation seeds with the same frozen parents
matched or improved the observed House best and preserved Sum, but Median remained at its initial
best: it is not a substitute for native variation quality. Both bank variants
remain separate from the primary fixed benchmark.

Short searches can expose obvious degradation; they do not establish convergence
or time-to-solution equivalence. The phase-bank prototype is isolated in the
optional benchmark executable and does not change native CLI evolution.

## Architecture experiments (2026-10-01, second round)

Internal immutable executables and retained derivations may replace per-generation
AST materialization, content identity reconstruction and repeated compilation.
External input admission remains mandatory. Type safety alone is not grammar
membership: nonterminal, scope, constant domain, linked holes, root and resources
must be certified or explicitly fall back. A controlled internal object cannot
be publicly marked trusted. Changed admission/resource or operator semantics must
be named, tested and reported separately from legacy fixed-parent comparisons.
RNG draw order and CPU/GPU trajectories are not universal architecture contracts;
legacy parity tests continue to cover their explicitly selected legacy modes.

Every new generation measurement includes complete evaluation, selection,
variation and preparation of the next evaluable population. Charge initialization,
compilation, verification, collection and fallback at their actual frequency.
Preserve the historical fixed benchmark and frozen ASGP denominator. NCU and
multiple capability kernels are permitted; profiling does not supply headline
speedups. Shared representation never implies shared evaluation or reduced fuel.

### Owned fragment prototype

The `fragments` probe uses a native `ExecutableFragments` owner with immutable
phase sources, certified local sites and compiled phases. Handles cannot be
forged or reused across owners; imported metadata is not authority. Only scalar
constant domains, one root region, a fixed admitted skeleton and independent
whole-phase grammar holes are supported initially. Nested lexical/traversal/region
phase syntax and cross-phase coupled holes require legacy evolution; the probe
reports unsupported inputs explicitly. Repeated holes entirely inside a phase
are replaced atomically from retained grammar sites. Type equivalence is never
used as a membership proof.

This mode changes operators: tournament-2, one elite, then each child receives
one site crossover (70%) or fresh site mutation (30%). It samples a phase and a
local logical site; this is not legacy pair-uniform crossover. Every new phase
still has full admission and compilation in a temporary exemplar; only the phase
is retained. Unchanged executable phases and proofs are shared. Current packing
still materializes bytecode programs and verifies them each evaluation; these
costs are measured and are candidates for a later immutable pack boundary.
Root combination budgets are conservative; unsupported combinations reject.
No numeric/fuel/case limits change. This is a restricted architecture experiment,
not a replacement for the fixed benchmark or a grammar-wide backend.

`GAGP_FRAGMENT_GENERATIONS` sets 1–65536 generations (default 4). Last-owner
release reclaims each retired phase within generation timing; no source bank,
unbounded interning table or 16-generation lifetime limit exists. Init and cold
GPU cost are recorded separately and included in `search_total_ms`. Final full
export/admission, AST/handle GPU equality and CPU top-16 audit are separate.
`GAGP_SEARCH_GENERATIONS` and `GAGP_SEARCH_EXPORT` allow native search controls
and expression representability checks; ordinary defaults remain unchanged.

`GAGP_LOCAL_FRAGMENT_ADMISSION=1` replaces temporary full-exemplar admission
with exact membership reconstruction of the changed phase in its declared closed
frame, followed by phase-only compilation. No ordinary implicit captures or
nested scope metadata are admitted by this local path. Incoming template-hole
identity, enclosing template budget and all repeated internal occurrences remain
explicit. Physical replacement limits use the conservative exemplar hole budget;
this can differ from reconstructing a whole child's canonical resource allowance.
The local profile therefore has its own reproducible operator trajectory and is
reported separately. Export still independently re-admits the complete program.


`GAGP_OWNED_EXECUTABLE=1` is an experimental compositional bytecode certificate
boundary for the fragment probe. Admission copies and verifies an immutable
single-region layout; each replacement phase is verified once in its exact plan
and ordinal. Composition requires the same live layout owner and slot. This is
bytecode safety, **not grammar membership**; the fragment owner separately proves
membership. Registry-dependent constants and oversized GPU region capacities are
rejected. External raw bytecode still uses full verification. Evaluation assembles
and destroys ordinary packed bytecode inside timing, but skips repeated whole
bytecode verification. Capability/type-flow detection and GPU packing still run.
The added phase verification and cold owner creation are measured. Initial
same-binary probes suggest a few milliseconds saved, pending repeat validation.


Round-two reproduction: `benchmarks/fixed_asgp/optimization/round2.py` consumes
existing frozen files and records binary hashes, input hashes, flags, command and
process wall time in a fresh output directory. Stages `formal`, `quality`, and
`scaling` execute serially. `fragments_repeat` performs one warmup trajectory and
three measured fresh-owner trajectories per process; compare the same generation
index across repeats, never different evolved populations. `formal` runs three
processes per configuration. Input reading through reporting is separately saved
in `.wall.json`; process startup/exit is additionally included by the driver.
`search_total_ms` covers probe setup and evolution, excluding external input decode
and final independent audit/export. Full process wall time includes those costs.
`GAGP_FRAGMENT_EXPORT` writes independently admitted final ASTs for external audit.


`GAGP_FRAGMENT_PARSIMONY=1` is a separate search-rule experiment: tournament
fitness ties and elite fitness ties prefer fewer retained physical AST nodes;
remaining ties preserve the existing choice order. Fitness, numerical semantics,
cases and resource limits are unchanged. Its populations and quality must be
compared separately, including the cost of obtaining current size summaries.
This targets neutral program growth observed after dozens of generations; it is
not an evaluator optimization or an identical-workload speedup.


`GAGP_WINDOW_EXECUTOR=1` currently tests verified IntList window regions with an
optional carried Int state, two sequence-window requests, one InteriorCut Int
preparation and Int results, without memo or bound operands. The carried state
may be copied or computed by an evolved request-expression phase. Every phase
and original fuel/capacity boundary remains active. Direct typed-phase proof is
required; unsupported populations use the ordinary region executor. Diagnostics
must show the `-window` profile before attributing timings to this experiment.
