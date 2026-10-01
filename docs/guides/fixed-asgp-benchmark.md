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
- GAGP defaults to one application CPU execution thread in every mode. Set
  `GAGP_HOST_THREADS=2..20` to opt into host workers; unset or `1` is the fair
  ASGP-1T comparison. Invalid settings are rejected. CUDA driver helper threads
  are not GAGP preparation/evaluation/reproduction workers.
- With one host thread, `gpu_overlap` submits GPU evaluation then performs host
  reproduction preparation on the calling thread before waiting for the GPU.
  No second CPU worker is used. All preparation remains in generation timing.
  The optional multiworker setting retains background preparation.
- Historical pre-1T GPU results used host workers and are not 1T controls.

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

The 30× target applies to Sum of Elements and House Robber at 1024×1024. Median
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
`snapshot` and `search`. They write diagnostic JSON and are separate
from the five modes above:

- `snapshot`: identical frozen programs and all cases, per-program fitness/error/
  timeout/fallback counts. `GAGP_SNAPSHOT_CPU=1` also scores every program on CPU.
- `search`: four actual generations, plus separately timed final evaluation and
  top-16 full-case CPU reevaluation. Set `GAGP_GPU_DIAGNOSTICS=1` for per-generation
  counters and `GAGP_BM_SEED=0/1/2` for the short quality screen. Final population
  sizes and exact decoded genome diversity are recorded.
## Native GPU execution profiles

The optimization mainline is GPU evaluation plus GPU reproduction. CPU work for
preparation, decoding and validation may be simplified without replacing the core
reproduction backend with CPU worker threads. The original CPU control remains.
External admission, grammar membership, scope, resource and memory safety remain
required; type correctness alone is not grammar membership.

`GAGP_SEARCH_GENERATIONS` and `GAGP_SEARCH_EXPORT` configure native search
length and final AST export. `benchmarks/fixed_asgp/optimization/round2.py` runs
native `formal`, `quality` and five-mode `scaling` stages on existing frozen
inputs. It records process wall time, flags and input/binary hashes; each output
directory must be new.

Every new generation measurement includes complete evaluation, selection,
variation and preparation of the next evaluable population. Charge initialization,
compilation, verification, collection and fallback at their actual frequency.
Preserve the historical fixed benchmark and frozen ASGP denominator. NCU and
multiple capability kernels are permitted; profiling does not supply headline
speedups. Shared representation never implies shared evaluation or reduced fuel.

`GAGP_WINDOW_EXECUTOR=1` currently tests verified IntList window regions with an
optional carried Int state, two sequence-window requests, one InteriorCut Int
preparation and Int results, without memo or bound operands. The carried state
may be copied or computed by an evolved request-expression phase. Every phase
and original fuel/capacity boundary remains active. Direct typed-phase proof is
required; unsupported populations use the ordinary region executor. Diagnostics
must show the `-window` profile before attributing timings to this experiment.


`GAGP_UNBOXED_WINDOW_FRAMES=1` further stores proved window state/prepared/result
integers without redundant per-slot tags. The list's private view/non-view bit is
retained. It requires the window executor proof and preserves resource limits,
phase order, numeric operations and fuel. Unsupported shapes keep tagged frames.


`GAGP_BOUND_ADD_PHASE=1` recognizes a verified typed phase consisting of two
bound scalar loads, Add and optional Return, with no unused or parameter binding.
It reads proved initialized slots directly, preserving per-instruction fuel and
the existing double-conversion/wrapping arithmetic. Other phases retain the VM.

`GAGP_REUSE_ADMISSION_COMPILE=1` retains scalar-constant bytecode already produced
by native grammar admission in the opaque derivation certificate. Current AST,
input order and fuel are checked before reuse; payload constants and stale proofs
fall back to normal compilation. GPU packing still verifies bytecode. Snapshot
probes exercise this path and report `admission_compile_reuses`. Report cold
admission, complete evolve-call and changing-generation costs alongside fixed
measurements; reuse does not change grammar membership or variation operators.

`GAGP_REUSE_SITE_CONTRACTS=1` is a candidate native host optimization: within
one verified AST analysis, equal nonterminal/template/slot, scope, crossover
group and closure contracts reuse their serialized compatibility key. Multi-
occurrence logical holes retain complete scope intersection/key construction.
It changes neither grammar membership nor operators; it is not a persistent
identity cache. Formal validation status is tracked in the optimization index.

Timing audit: the historical `evolve_call_ms` field is `EvolutionTiming.total_ms`,
an internal timer stopped before function-local destructors. New records also
include `evolve_wall_ms`, measured outside the complete call. This distinction
does not change the fixed generation boundary. Any newly retained run analysis
must be released within the last generation timer; deferred destruction cannot
be claimed as fixed-generation speedup. Cold/process wall and search total remain
separate evidence.

`GAGP_SELECTED_SITES=1` is the native selected-site experiment: CPU1T, one root,
no projected offspring budget, scalar AST constants and no active payload
transaction. Other populations use the full native preparation path. Grammar
membership/lowering and table validation still precede compaction. Logical sites
are enumerated in the existing order; the same RNG shuffle chooses candidates,
then only their full scope/resource/compatibility contracts are materialized.
Repeated holes remain atomic. No site subset is published as a complete public
variation analysis. GPU operators remain unchanged; their two copybacks and AST
rebuilds still occur. Crossover/final children receive canonical execution
admission without eagerly building future sites. Parent continuations own the
exact scalar AST/proof and are bound to the packed source owner and context.

The validated allocator configuration is optional process-local jemalloc
5.2.1-4ubuntu1 (`LD_PRELOAD` with the library path and
`MALLOC_CONF=background_thread:false,narenas:1`). It adds no application workers;
no system allocator change is required. Record the library hash and both
variables with the binary/flags. Default allocator controls and raw repetitions
are retained in `optimization/results/native-allocator-validation.json` under
the fixed benchmark directory. Do not substitute allocator-run ASGP controls
for the frozen headline denominator.

`GAGP_OWNED_PREPARATION=1` is an experimental continuation of selected-site
preparation. A private scalar population copies and admits input, then exposes
only const access to its ASTs and grammar-bound certificates. Site preparation
borrows the witness/type/scope metadata; packing compares the stored identity
instead of reserializing the same owned AST. External pack and admission APIs
retain their validation. Certificates disabled, unsupported payloads, root
requests or budgets retain the existing native path. GPU operators, canonical
membership, RNG and copybacks do not change. This flag has no performance claim
until its paired experiments pass.

`GAGP_CPU_REGION_VIEWS=1` is a separate CPU evaluator prototype. After ordinary
segment verification, the region must use SequenceWindows and forward type-flow
must prove Int/Bool/IntList phases using
only Len/Index/Slice for containers, scalar constants and a scalar region result.
Memoized list-valued states are excluded. Each invocation owns copies of its
registered IntList inputs; intermediate slices become checked offset/length
views. Missing, malformed or oversized payloads use generic execution. Registry
entries for temporary slices are not created; this profile does not preserve
incidental intermediate registry contents/hash-collision side effects. Phase
execution, arithmetic, fuel, lazy capture reads and region scheduling remain
unchanged. No view escapes into root execution. Initialization and conversions
occur inside the ordinary evaluator/generation timer.
