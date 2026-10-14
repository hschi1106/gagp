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
segment verification, the region must use SequenceWindows with a constant-false
base predicate (so snapshot setup is reserved for recursive work), and forward type-flow
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

The isolated `gagp_test_gpu_phase_compile` target tests bounded GPU lowering of
prefix phase expressions against the existing native compiler. Its optional
arguments are an existing prepared JSON and its grammar JSON; it reads them and
independently checks source membership, without running prepare. The phase IR
supports Int/Bool constants, typed local reads, forward conditional/boolean
expressions and the admitted scalar/list observation primitives. Capacity and
unsupported/type cases return explicit status. Lowering is not a grammar
membership certificate. This prototype does not change native generation paths;
its kernel timings are diagnostic only. Raw cold/transfer/repeat/memcheck records
are indexed by `optimization/results/native-gpu-phase-compile.json` in the fixed
benchmark directory.

The isolated `gagp_test_gpu_phase_generate [GRAMMAR_JSON]` prototype constructs
fresh phase donors on the GPU from tables compiled from the actual grammar.
The test exercises construction independently; the opt-in native integration
described below uses the same constructor. Standalone kernel time is not a
generation speedup claim.
It supports Int/Bool/IntList expression nonterminals, up to eight formal lexical
bindings, scalar Int/Bool domains, and the phase-lowering primitive set. Every
reachable alternative is checked; templates/repeated holes, local/input names,
inner binders/regions, root aliases, sampling-domain overrides and other payloads
decline that requested profile explicitly. Different nonterminals remain distinct
regardless of type. Reference scope permutations and finite/range constant domains
are retained. Construction uses mutation-stage productions and depth-indexed
minimum costs; a bounded lane owns each output. A maximum of 1024 physical nodes,
64 expression levels and 8192 expansion steps is a prototype capacity, not an
implicit reduction of native budgets. Callers must fall back when inapplicable.

Production sampling uses the existing SplitMix generator with a new stream and
budget allocation order, so generated donors are not seed-for-seed native donor
replays. This has not yet been adopted as a search rule. Tests export each donor
with its fuel annotations and use independent CPU grammar membership and lowering
in its lexical frame. This construction proof is separate from typed phase
lowering. Kernel timings exclude grammar setup, upload, output reconstruction and
validation; they cannot serve as complete-generation benchmark results.

`GAGP_GPU_DONORS=1` is the opt-in native integration experiment for that
constructor. It requires the owned selected-site 1T path and only changes subtree
donor construction during the existing GPU mutation pass. Selection, crossover,
mutation application, complete evaluation, final canonical offspring admission
and both GPU copybacks remain. Donors for unsupported nonterminals/scopes,
mutation-entry overrides, projected budgets or transport capacities use the
native donor generator. No reproduction moves to CPU workers. Grammar tables
are compiled/uploaded lazily during timed preparation, retained by the run's
grammar owner and freed with it. First-generation setup is included; repeated
search generations reuse those tables. Diagnostic rows expose generated/fallback
donor counts, table setup time and donor-owned device bytes. This experiment is
not in the best-version flags until full-generation and quality measurements pass.

Constructed donor origins are explicitly construction metadata, separate from
canonical witness/certificate objects. Template or repeated-hole construction
is not supported by this constructor; existing native atomic replacement at a
certified destination remains responsible for coupled occurrences. Independent
canonical admission still validates each changed final offspring. The new donor
RNG trajectory/allocation policy is an experimental search-rule difference;
identical frozen-parent evaluator work can be compared, but later populations
must be evaluated as different searches.

`GAGP_SINGLE_EVAL_IDENTITY=1` combines native evaluation-cache identity and
sealed-executable lookup in one synchronous read. The helper computes the key
itself; callers cannot supply an unchecked identity. Mutable imports still need
identity validation, and registry constants keep their existing path. It does
not change grammar admission, operator choices or evaluator semantics.

`GAGP_OWNED_CROSSOVER_HANDOFF=1` retains the private immutable scalar population
produced by the fully checked GPU crossover decoder. Mutation preparation uses
that same owner instead of copying and certifying it again. The constructor runs
the decoder itself; it does not accept a caller's vector with a claimed proof.
Source/context/grammar and population size must match, registry-dependent
constants and external mutable populations keep ordinary validation. Both GPU
operator passes, copybacks, canonical changed-child admission and RNG stay the
same. The owner is released after the mutation pass and never retains history.


GPU phase variation (`repro/gpu/device/phase_variation.cuh`) and cold root-layout
import/export (`repro/gpu/phase_layout.hpp`) now back the resident profile below.
They retain nonterminal/scope information per distinct construction occurrence;
this internal metadata is not an external admission certificate. New subtree
mutation uses the GPU grammar constructor, rather than a finite expression bank.

Experimental `GAGP_GPU_PHASE_POPULATION=1` uses the existing GPU reproduction
modes with a resident GPU genotype. Its contract is in
[GPU reproduction](../design/gpu-reproduction.md#experimental-native-gpu-phase-population).
Reports identify `reproduction_profile` and explicit fallback reasons; compare
this altered operator distribution separately from identical-workload evaluator
results. `initial_admission_ms` includes cold genotype import; use
`evolve_wall_ms` and complete search results to assess amortization. All four GAGP
modes and the ASGP 1T control remain available. No host reproduction workers are
introduced. It remains opt-in. The current validated settings, exact binary/input
identities, raw repeats and remaining limitations are indexed in
[optimization progress](../../benchmarks/fixed_asgp/optimization/PROGRESS.md) and
[checked results](../../benchmarks/fixed_asgp/optimization/results/native-counted-validation.json).
`GAGP_GPU_INPLACE_MUTATION=1` reduces native GPU genotype workspace without
changing operators or RNG. Mutation reads the crossed population, generates its
donor in the existing per-child scratch, and splices into a separate per-child
temporary. Only a successful splice is copied back to its crossed phase; there
is no aliased splice. The temporary has population rows instead of population ×
evolving-phase rows. Current/crossed owners exchange only after variation checks;
errors invalidate the owner as before. This saves memory for multi-phase programs
and eliminates the unconditional second population copy. Publishing the changed
phase adds a copy, so kernel speedup is not guaranteed; report memory, cold-start,
and end-to-end timings separately. The 32-generation owner test compares exact
exported ASTs, operator counters and full-case fitness against the original path.

`GAGP_CPU_REGION_VIEWS=1` is also part of the checked single-thread settings: it
uses invocation-local views only for structurally proven recursive windows, with
generic CPU fallback. It does not add CPU reproduction workers.

Additional independent CPU evaluator opt-ins (all stay CPU 1T):
- `GAGP_CPU_BOUND_PHASE=1`: immutable-session local-to-binding tables avoid
  presets/type-vector/locals copies for read-only phases. Phases containing
  `STORE` retain the ordinary interpreter. Absent or wrong-type captures still
  fail only at an executed `LOAD`, after its fuel charge.
- `GAGP_CPU_COORDINATE_VIEWS=1` together with `GAGP_CPU_REGION_VIEWS=1`:
  extend the same validated scalar-returning list-view capability to coordinate
  regions with captured IntLists. Each invocation snapshots payload contents;
  unsupported or missing payloads fall back to the original values/interpreter.
- `GAGP_CPU_SCALAR_VIEWS=1` together with `GAGP_CPU_REGION_VIEWS=1`:
  admit proven scalar-returning windows with arbitrary base predicates, including
  terminal-at-root invocations. This extends an old performance heuristic; the
  same non-escaping-list proof and invocation snapshot remain mandatory. It is
  useful when a larger phase can amortize conversion through typed execution.
- `GAGP_CPU_TYPED_PHASE=1` (implies bound phases): existing forward type-flow
  proof admits Int/Bool and invocation-local IntList operations with stack<=64.
  A compact int64 stack executes the evolved bytecode, including every original
  fuel charge. Unsupported code or runtime values use the ordinary interpreter.
  Numeric arithmetic preserves the reference double round-trip. If a round-trip
  approaches an out-of-range signed conversion, the pure phase restores fuel
  and replays generically; no registry writes, allocation, memo updates or
  externally visible locals occur before that replay.
- `GAGP_CPU_COORDINATE_DRIVER=1` with bound/typed phases: one Int coordinate,
  Int results, at most two fixed-offset requests and no preparations use compact
  persistent frames. One transient binding frame feeds the same phase executor.
  Dynamic coordinate domains are resolved and validated by the existing entry
  boundary. Tagged child values, captures, memo keys/capacity, every fuel charge
  and error order are preserved; other verified region shapes retain the generic
  driver. This does not change reproduction or introduce CPU workers.
- `GAGP_CPU_DENSE_MEMO=1`: one Int state coordinate and a domain of at most 4096
  cells use direct memo slots; other keys/domains retain the generic hash map.
  All state-key components, lazy top-down order, fuel and logical cell capacity
  remain unchanged. Allocation and clearing are included in evaluation time.

These are execution capabilities, not grammar membership certificates or changes
in grammar/operator/RNG semantics. Their one-time capability construction and
per-invocation storage are inside measured CPU fitness execution. Tests include
lazy captures, STORE fallback, signed/numeric boundaries, fuel, memo capacity,
full multi-axis keys and session lifetime. Candidate experiment evidence lives
under `benchmarks/fixed_asgp/optimization/results/cpu-evaluator-20261013.json`.

Within that profile, `GAGP_OWNED_PHASE_PACK=1` reuses private executable proofs.
Independent evaluator flag `GAGP_COORDINATE_EXECUTOR=1` selects a verified
single-coordinate region executor; unsupported structures use the existing
executor. Record both flags with binary identity, cold initialization and raw
unprofiled generation repeats. Historical default controls do not silently gain
these opt-in settings.

`GAGP_OWNED_PHASE_CAPS=1` also reuses owner-bound typed-view capability after
matching actual case types. CLI JSON/stdout report the reproduction profile,
fallback reason and resident genotype bytes.


Search diagnostics record each completed generation's `elapsed_search_ms` from
actual search entry, including initialization, plus its evaluator execution
profile. `GAGP_SEARCH_HISTORY_EXPORT=PATH` archives the already-retained history
ASTs after the timed search. This permits equal-budget comparisons of completed
prefixes of longer runs without changing operators or terminating evaluations.
Archive serialization/independent CPU audits are separate from search time;
required history construction and final population materialization remain timed.
Search JSON also exposes the existing GPU fitness-session `gpu_init_ms` timer (zero for CPU mode),
separately from `initial_admission_ms` and `final_eval_ms`. It is included in
`evolve_wall_ms`; this diagnostic field does not move initialization outside
the benchmark boundary. Earlier GPU reproduction device preflight is also
inside search wall but outside this narrower fitness-session timer. Older
artifacts omit this field.

`GAGP_COUNTED_SITE_DRAW=1` selects the GPU profile
`native-gpu-phase-v2-counted`: site counting plus one uniform rank draw replaces
per-node reservoir RNG. Report this setting because trajectories differ from
`native-gpu-phase-v1`; grammar admission, full cases and execution semantics remain.

Re-run the checked native candidate with the existing frozen inputs and a new
output directory (one warmup plus three measurements per task/mode):

```bash
python3 benchmarks/fixed_asgp/optimization/native_candidate.py daily --out logs/optimization/native-daily-check
```

The thin runner invokes the existing native benchmark's five modes serially,
records binary/input hashes and the full environment, and never runs `prepare`.
It requires the locally archived jemalloc dependency recorded in the optimization
index. `paired` performs three independent processes per GPU setting; `scaling`
runs all four population sizes. An existing output manifest resumes completed
cells only after binary/settings identity checks. Interrupted scaling attempts
remain on disk; a retry uses a fresh output filename.


## Multi-generation comparison and fitness configuration

`gagp_fixed_asgp_bench measure` accepts `search_asgp_1t`, `search_gagp_cpu`,
`search_gpu_eval`, `search_gpu_repro`, and `search_gpu_overlap`. The original
five frozen-parent modes are unchanged; `search` remains the GPU-overlap alias.
`GAGP_SEARCH_GENERATIONS` and `GAGP_BM_SEED` select search length and reproduction
seed. `GAGP_SEARCH_TRACE=1` emits completed-generation progress on stderr. This
I/O is included in search wall time, outside each generation timer.

`GAGP_SEARCH_PENALTY` configures the existing GAGP numeric error cap and runtime
error penalty for these search entries (positive finite value, default1000).
`GAGP_SNAPSHOT_PENALTY` configures the independent snapshot audit, with the same
validation/default. Neither changes the original frozen-parent benchmark.
These are benchmark configuration options, not a general custom-scoring API.

ASGP's Median task uses exact-error count; Sum/House use capped absolute error.
For the admitted integer-output Median grammar and integer targets, penalty1
makes the GAGP numeric score equal negative wrong-case count. This equivalence
does not apply to arbitrary Float results or GAGP's type-selected exact scoring
for nonnumeric values. Runtime/type failures contribute negative penalty.
Record the chosen objective, cap, error penalty and direction with each run;
changing an objective changes selection, even when programs/operators are fixed.

The serial runner `benchmarks/fixed_asgp/optimization/multigen.py` archives a
binary, source patch, frozen hashes, full settings, per-generation logs and final
populations. Main runs use P1024/C1024, seeds0/1/2 and16 complete generations plus
final evaluation. `--tasks` selects tasks and `--case-penalty` sets the search
option. `--stage extended` chooses lengths using timing only, to cover10seconds;
inspect coverage instead of extrapolating. If bloat invalidates a generation16
projection, `--extension-observed-timing` uses already completed longer runs
of the same task/mode to estimate time-to14s instead; it never uses fitness.
Completed runs are retained, and retries use separate attempt filenames.
Use new output directories. CPU1T
and the best native GPU profile come from the checked candidate manifest.

`multigen_audit.py` translates/admit-checks ASGP champions, independently scores
all recorded champions on full training cases, and checks selected best-so-far
champions on existing independent held-out cases. Deduplication is only for
post-search audits. Its optional `--base` and `--aligned-median` arguments merge
separate archives without overwriting them; earlier Median L1 runs are indexed
as `legacy_main`. `multigen_report.py` produces CSV, JSON, PNG/SVG and a Chinese
report. Search wall, input-through-report and process wall are distinct. Never
mix their denominators with frozen-parent single-generation claims.

A future general fitness configuration should select a named metric independently
of expected-value type (`absolute`, `exact`, and explicitly supported sequence
metrics), separate distance cap from execution-error penalty, and define numeric
tolerance/type compatibility. CPU and GPU must implement the same validated
configuration. Default behavior stays under `spec/fitness.md`; new scoring
profiles need explicit opt-in and parity tests. Arbitrary Python/host callbacks
per program/case would force transfers/synchronization and are not an appropriate
GPU fitness extension point. This interface is a proposal, not implemented here.

`asgp_snapshot` independently evaluates imported ASGP programs on all supplied
cases without reproduction. `multigen_native_audit.py` uses it to check ASGP
training records and held-out quality in ASGP's own runtime. Its audit binary is
archived separately from timed binaries. Reports consume this augmentation:
ASGP-to-GAGP translation is not an execution-equivalence proof (for example,
ASGP rejects negative indices whereas GAGP normalizes them). Ported scores remain
explicit diagnostics and never replace native ASGP search quality. See the
[ASGP vs GAGP overview](../../benchmarks/fixed_asgp/optimization/results/multigen-20261013/OVERVIEW.zh-TW.md)
and [2026-10-13 detailed report](../../benchmarks/fixed_asgp/optimization/results/multigen-20261013/REPORT.zh-TW.md).

Long-search follow-up uses `long_search.py` with an archived binary and a new
output directory. `long_search_audit.py` independently admits and scores training-
selected checkpoint champions after all performance runs finish;
`long_search_report.py` renders seed-resolved generation/time/size and held-out
plots. See the [bounded long-search contract](../../benchmarks/fixed_asgp/optimization/results/long-search-20261013/CONTRACT.zh-TW.md).

### Short changing-population optimization check

`benchmarks/fixed_asgp/optimization/short_search.py` compares fresh processes on
House Robber (32 generations, seed 0) and Median (64 generations, seed 2) by
default. `--tasks sum_of_elements house_robber median` additionally runs Sum of
Elements (32 generations, seed 0). All tasks use
1024 programs and all 1024 cases. It verifies frozen input hashes, archives the
binary, pins one host thread, records explicit settings and raw generation data,
and serializes runs using a shared file lock. `--seeds 0 1 2` expands quality
screening; `--generations 4` is only a focused diagnostic, not the main target.
Primary search-wall includes cold evolution initialization and final evaluation;
process wall additionally includes import and post-search audit/export. Neither
metric should be mixed with frozen-parent single-generation ratios.
Native CPU reproduction can opt into `GAGP_CPU_LAZY_ANALYSIS=1`: every input
still receives canonical grammar admission before selection, and every changed
child receives membership/lowering/resource admission. Future variation sites
are built only when an operator needs them, rather than during every child
acceptance. `GAGP_CPU_SELECTED_PARENTS=1` additionally keeps immutable scalar
parent ASTs and their certified site analysis for one reproduction call. It
copies/admit-checks selected parents once, declines registry-dependent constants,
and releases owners before the generation ends. It does not add CPU workers or
change GPU reproduction. Both settings preserve native CPU operator/RNG behavior;
CPU ablation modes retain their ordinary preparation. External mutable parents
are always admitted, including unselected invalid parents. Focused regression:
`gagp_test_grammar_reproduction` plus crossover, mutation, membership and variation
contract/cache/region tests. Archived benchmark manifests must record their use.

`GAGP_CPU_SELECTED_MUTATION=1` also materializes just the chosen logical site
for a subtree mutation without projected offspring budgets. The same RNG draw
selects the same logical group and preserves all repeated occurrences; complete
parent/child admission still runs. Constant mutation and projected-budget donor
preparation keep their existing path. This setting does not sample fitness cases
or change the mutation distribution.

Coordinate ring memo: `GAGP_COORDINATE_RING_MEMO=1` requires the typed coordinate
executor, memoization, first offset -1, other offsets -1..-4, and a statically
proved lower-interval base predicate. Four scalar values replace the linear
lookup; original entry/base/fuel/frame/insertion-capacity behavior remains.
Unsupported shapes keep generic lookup; workspace allocation is unchanged.
Six same-binary search repeats per setting show about2% House32 search-wall
and7% diagnostic kernel improvement. Exact final AST/trace comparison,
independent proof review and exhaustive focused fuel/capacity tests pass.
Evidence: `benchmarks/fixed_asgp/optimization/results/parallel-20261013/ring-memo.json`.
`GAGP_CPU_COUNTED_PAIRS=1` is a separate native CPU crossover RNG profile.
It groups donor sites by their exact compatibility contract, counts every pair
passing both directional resource checks, and draws one unbiased uniform rank.
The ordinary path instead makes one reservoir draw per feasible pair. These
have the same uniform conditional pair distribution but different seeded
trajectories; report multi-seed fitness and equal-wall quality separately from
fixed-parent execution cost. No sites, cases, operators, or admission checks
are removed. Nonlocal projected resource charges without complete local
allowances retain the original trial-child/reservoir path. Exact-key equality
resolves hash collisions; total pair counts are checked for overflow.

This flag changes the existing CPU reproduction in `gagp_cpu` and `gpu_eval`;
GPU reproduction and CPU thread count are unchanged. It has no persistent
population/grammar cache. Tests cover enumerated feasible-rank selection,
rejection accounting, deterministic replay, repeated holes, scopes, and
changing populations in `gagp_test_grammar_reproduction`; run
`GAGP_CPU_COUNTED_PAIRS=1 cpp/build/release/gagp_test_derivation_resources`
for local/nonlocal projected-resource coverage.

The archived native validator also accepts `--flag GAGP_NAME=VALUE`, `--tasks`
and `--populations` to reproduce a checked candidate through the existing fixed
warmup/three-repeat and scaling modes. Defaults and timing boundaries are
unchanged; explicit flags are recorded for controls and candidate, CPU1T is
required, and inputs remain the frozen artifacts. Run it under the shared
benchmark lock when other worktrees are active.

`GAGP_CPU_ALL_FIT_PAIRS=1` is an exact optimization inside the counted-pair
profile. Each exact compatibility group summarizes maximum donor node/depth/
template requirements and minimum destination allowances. When both directional
resource conditions hold for the entire group, its row count and selected donor
index are computed directly. Otherwise the original pair scan runs. Projected
allowances always keep the scan, and nonlocal projected budgets still use the
legacy fallback. Group order, RNG draws, feasible-pair distribution, offspring,
and rejection counters remain identical to counted pairs without this setting.
These summaries certify only resource fits; scope/domain/membership contracts
still come from admitted variation sites and final canonical child admission.
The setting has no effect on GPU reproduction or CPU worker count.

`GAGP_CPU_FUSED_ADMISSION=1` optionally fuses materialized grammar checks into
canonical witness construction in `grammar/membership.cpp`. At each nonterminal,
exact local node, type, name, bound-variable, constant-domain, fuel-profile and
structured-plan checks prove a unique production head, or defer to ordinary
recursive matching. All selected descendants and repeated-hole equalities are
checked; this is not type-only admission. Aliases, ambiguous heads and more than
64 nested head wrappers use the original matcher. Native AST verification,
lowering and canonical resource projection still run.

Despite the CPU experiment name, this switch affects every unframed single-root
`reconstruct_derivation` call, including public reconstruction and GPU import.
Strict `require_membership`, framed reconstruction and multiple-root admission
retain the original matcher. The switch changes the accounting of speculative
matching work: its matching-step/frame guards cover recursive fallback work,
while the unchanged witness logical-step/frame/template limits cover complete
reconstruction. It does not claim identical admission at the old speculative
work boundary. AST limits, execution fuel, canonical choices and RNG are
unchanged. Benchmark it separately and keep an independent strict admission
check for exported populations. Differential reconstruction coverage lives in
`gagp_test_grammar_membership`; native CPU reproduction and authored resources
are covered by `gagp_test_grammar_reproduction` and
`gagp_test_derivation_resources`.

`GAGP_CPU_COMPACT_ADMISSION=1` removes a second reconstruction when native CPU
splicing leaves unused scalar constants. The private `admit_and_compact_execution`
continuation owns a fresh single-root AST, fully verifies and lowers its original
uncompacted contents (including unused entries), then compacts before publishing
its certificate. Stable constant-only compaction preserves decoded referenced
values, node positions, canonical choices, exact scopes and resource charges;
the retained bytecode owns its values. The certificate binds the final compacted
AST identity. A changed name table triggers ordinary reconstruction after
compaction; registry-backed constants, multiple roots and inputs with attached
provenance keep the ordinary path. This flag changes no admission work limits,
grammar, RNG, resource or execution semantics. It is independent of fused
admission. Unlike the historical rejected GPU compaction experiment, CPU splice
outputs measurably contain dead constants; the diagnostic counts and separate
CPU/GPU-e controls must establish that premise. Membership regressions cover
invalid unused entries, stale proofs, name fallback, nontrivial constant-index
remapping and retained-versus-independent bytecode outputs/errors/fuel limits.

The Sum25/House25/Median5 validation uses the unchanged P1024/C1024 changing
searches (Sum32 seed0, House32 seed0, Median64 seed2). Its frozen denominators and
complete starting settings are in
[`sum25-20261014/contract.json`](../../benchmarks/fixed_asgp/optimization/results/sum25-20261014/contract.json).
The evaluator-only options and fallback contracts are documented in
[`gpu-reproduction.md`](../design/gpu-reproduction.md#experimental-pure-measure-evaluation-reuse).
They do not change native CPU/GPU modes, reproduction operators or CPU1T.

Reproduce each stage into a new directory from the experiment worktree:

```bash
python3 benchmarks/fixed_asgp/optimization/results/sum25-20261014/run_validation.py timing --out logs/optimization/sum25-rerun/timing
```

The same entry point accepts `quality` (three seeds × 128 generations, archived
before and current after), `other-modes`, `snapshot`, and `scaling`. It uses the
existing runners and serial lock, never prepares or rewrites frozen inputs.
Timing records nine fresh cold searches per task plus interleaved controls;
scaling separately retains the original fixed-parent warmup/three-sample
contract. These are different workloads and their speedup denominators must
remain separate. The adjacent `summarize.py` refuses incomplete validation by
default and writes only to a new report directory.

## Expanded capability and multi-generation study (2026-10-14)

The opt-in study under `benchmarks/fixed_asgp/optimization/results/expanded-20261014/`
adds Count Odds and Last Index of Zero to the original five native modes.
`run.py` stages `prepare`, `pilot`, `formal`, `quality`, and `native-controls`
use new output directories, full P1024/C1024, CPU1T, and serialized GPU jobs.
Original default TASKS and frozen artifacts remain unchanged. The contract is
`contract.json`; `summarize.py` requires all30 formal and30 quality cells.

`GAGP_BM_COMMON_FITNESS=1` is benchmark-only: numeric tasks use capped L1;
Last Index's original ASGP discounted L1 is separately controlled. The list
adapter supports exact-case scoring (ASGP negative error count, GAGP correct
count), but Negative to Zero is excluded from formal timing: GPU known-solution
validation fails and CPU random P8 evolution reached95.36GiB RSS. Stock With
Cooldown's exact clamped memo transitions fail the current validator. Neither
is represented by a substitute pipeline or a qualified speedup.

The native `quality TASK EXPORT GRAMMAR OUT [HISTORY]` command independently
admits GAGP exports and CPU-evaluates final and initial/middle/last champions
on1024 separately seeded local ASGP-domain cases. These are not official PSB
heldout datasets, and natural overlap remains. Three seeds screen quality;
they do not prove convergence equivalence. Known solutions are audit-only.

Validation: `ctest -R gagp_test_fixed_asgp_bench`, operational
`test_fixed_asgp*.py`, and the study's `test_summarize.py`. The list GPU
correctness assertion and ASGP/GAGP overflowing-integer parity assertion are
explicit expected failures; unsafe random list evolution is explicitly skipped,
not reported as supported. Full initial snapshots quantify existing numerical
differences separately from known-solution and final-top16 audit qualification.
CLI process timings use GNU `time` resource files (10ms resolution); the
watchdog monitor duration includes polling delay and is retained separately.
