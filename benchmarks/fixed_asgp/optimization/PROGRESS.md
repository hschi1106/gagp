# Native 1T optimization checkpoint — 2026-10-02

Active objective: preserve CPU / GPU-e / GPU-er / GPU-er-o architecture, use
CPU 1T + GPU, target fastest native GPU >30× frozen ASGP-1T on Sum/House;
Median remains fixed-overhead workload. Do not resume retired CPU fragment route.

## Active experiment

Candidate I: constexpr descriptor index replaces two linear descriptor scans;
reserved/negative/out-of-range kinds retain rejection. Three focused tests pass.
Quick full-generation Sum407.016 / House422.699 / Median430.516 ms, identical
non-timing fields. Nine-repeat interleaved runs complete with consistent gains;
full snapshots and changing-search comparisons still running before promotion.
Raw binary/repeats/commands: `logs/optimization/native-node-index-validation/`.
No interface/navigation change. Next: avoid constructing unused variation sites
at final GPU mutation admission while preserving full membership/lowering.

## Current fair baseline

Source parent `9ce96a9`; thread-policy checkpoint follows. `GAGP_HOST_THREADS=1`
is now the default; `2..20` is optional host parallelism, excluded from fair
headlines. One-thread overlap runs preparation on the calling thread after GPU
launch, before synchronization. Existing native fast-profile flags below remain.

Exploratory P1024/full1024 baseline (one process, 1 warmup + 3 repeats):
Sum GPU-overlap 724.686 ms, House 853.414 ms, Median 792.708 ms.
These supersede historical multiworker times **for the 1T contract**; they do not
claim a performance improvement. Raw five-mode measurements and binary:
`logs/optimization/native-1t-20261002/`; compact manifest:
[1T baseline](results/native-1t-baseline.json).

Validation: complete Release build; all 120 native tests passed across full run
and focused reruns after adapting two worker-count expectations. Explicit 4-worker
resource/cache/parity tests also passed; tool tests 5, repository tests 24 passed.
A clone trace plus pthread_create interposer/backtrace smoke records three CUDA
driver thread creations, no GAGP host workers. Logs are alongside the baseline.

Candidate A (`GAGP_REUSE_ADMISSION_COMPILE=1`): retain admission's existing
lowering in its opaque native certificate, scalar constants only. Exploratory
Sum640.959 / House736.418 / Median699.902 ms; all non-timing fields equal to 1T
baseline. Compile drops to 12.8–13.9 ms. Tests reject stale AST/constants/input
layout/fuel and decline registry-dependent constants. Needs formal and changing
search validation before promotion. Raw `logs/optimization/native-admission-compile`.

NVTX diagnosis of candidate A, Median: site analysis self178.6 ms, runtime
identity105.9 ms/11024 calls, bytecode+region verification96.7 ms; full profiled
722.7 ms. Nested ranges are not additive. Raw source-only instrumentation and
restored sources at `logs/optimization/native-1t-host-profile/`.

Retained candidate C: 1T decode uses ordered admission in the owning run context;
no discarded worker caches or speculative child warming. Formal 3 processes ×
(1 warmup + 3 repeats), interleaved with 1T controls: Sum552.771 / House637.124 /
Median590.220 ms, reductions22.2% /25.1% /21.1%. All 3072 snapshot program records
match (full cases, GPU counts and CPU fitness); all nine 3-seed×4-generation
search comparisons match non-timing fields and exported ASTs. Quality limits:
short runs only, no convergence/long-run assertion. Raw commands/binaries/repeats:
`logs/optimization/native-checkpoint-validation/`; summary
[validation](results/native-1t-reuse-validation.json).

Rejected B: grammar-wide local-resource eligibility cache. Sum657.988 /
House752.135 /Median697.685 ms versus candidate A640.959 /736.418 /699.902;
no reliable gain. The cache change is reverted, raw data in
`logs/optimization/native-static-charges/`.

Rejected D: compact-before-admission. No performance gain (571.8/656.3/604.5 ms)
and a new negative test caught unused duplicate-name validation being hidden
by compaction. The implementation, flag and proposed spec change are reverted;
the acceptance tests remain and pass on the retained implementation. Raw rejected
patch/timings in `logs/optimization/native-compact-admission/`; no maintained entry.

Candidate E: analysis-local site contract reuse (`GAGP_REUSE_SITE_CONTRACTS=1`).
Same verified scope/nonterminal/template/group/closure reuses the exact key;
repeated-hole intersections retain full construction. Quick Sum505.950 /
House595.256 /Median542.493 ms, unchanged non-timing results. Differential
contract fixtures plus reproduction/bounded variation pass. Formal repeats and
changing-search checks pending. Raw `logs/optimization/native-site-contracts/`.
Detailed Median NVTX: 141886 contract constructions (~94.7 ms self), runtime
identity90.2 ms, root/region bytecode verification91.8 ms. Instrumented timings
are diagnosis only. Next: shared immutable verifier locals between instructions,
copy on Store/merge; preserve traversal and admission/error semantics.

Candidate F: verifier local environments share ownership between unchanged
instruction states; Store and control-flow merge detach before mutation. CFG
order, validation rules and diagnostics unchanged. Quick Sum455.695 /
House498.343 /Median484.025 ms after E; all non-timing fields match. Four focused
tests pass; independent old-source differential checks match all 100000 ordinary
bytecode and 20000 bounded-region results including exact diagnostics and resource
summaries. Full Release build passes. Raw `logs/optimization/native-verifier-locals/`.
E+F formal interleaved controls/snapshots/search underway in
`logs/optimization/native-contract-verifier-validation/`; do not promote until
its comparisons complete.

Rejected G: retaining full sites in native certificates. Fixed generation slowed
~30–47 ms, and 32-generation searches slowed3–4% with RSS+120–140 MB. All short
and32-generation fields/exports matched, but copying/retaining sites lost.
Implementation/flag/exclusive tests removed; raw rejected patch and evidence:
`logs/optimization/native-certified-sites/`. Best runtime unchanged.
Rejected H: retain the run analysis context (also tested a smaller2P capacity).
All short and32-generation results/ASTs matched. After correctly charging final
cache destruction, fixed-gen changes were only ~1–3%, and32-generation process
wall Sum18.78→19.90s, House17.69→17.32s, Median20.53→19.63s. Mixed/small benefit
and Sum regression do not justify this extra lifetime policy. Implementation,
flags, exclusive eligibility/ownership tests and guide removed. Raw corrected
patch/results `logs/optimization/native-run-analysis-accounted/`; earlier timing
notices remain in `native-run-analysis` and `native-run-analysis-2p`.

Timing audit adds `evolve_wall_ms` outside the actual evolve call. Historical
`evolve_call_ms` is an internal timer excluding function-local destructors.
Keep both and original generation boundaries. Fixed benchmark smoke covers it.

Latest best-version NVTX: Median profiled563.5 ms, identity89.4, site build92.7,
key construction9.3, root/region verifier39.8 ms. Separate scopes are nested.
Raw `logs/optimization/native-refreshed-profile/`. Process-local SIGPROF PC samples
(no workers/system settings) in `logs/optimization/native-cpu-pc-profile/` show
allocation/free pressure and repeated linear descriptor lookups; CPU Sum also
spends substantial samples hashing/registering list slices. Full-process samples
include setup, not a precise per-generation attribution. Next independent minimum:
constexpr descriptor index, preserve all invalid/reserved enum checks. Source
prototype pending build after the ongoing sequential GPU experiment.

Current best runtime `ed19fde`, archived binary/flags/commands in
`logs/optimization/native-contract-verifier-validation/manifest.json`.
Enable retained flags plus `GAGP_HOST_THREADS=1 GAGP_REUSE_ADMISSION_COMPILE=1
GAGP_REUSE_SITE_CONTRACTS=1`; round2 runner supplies these.
Formal nine-repeat Sum465.603 /House505.974 /Median499.036 ms versus same-round
controls562.207 /650.451 /590.818. Every snapshot/non-timing search field and
exported AST matches across 3 seeds ×4 generations. All120 native tests pass
across full run and repository rerun (admission contract's freeze hash updated).
[Raw-repeat summary](results/native-contract-verifier-validation.json).
Next: reprofile, then retain certified site analysis across actual generations.
Do not merely persist the compatibility registry: its numeric IDs are local and
its 65536-key bound must not become a long-run cumulative admission restriction.
CPU perf sampling remains blocked by perf_event_paranoid=4; optional request
pending. No system settings changed.

---

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. The maintained mainline is GPU evaluation plus
GPU reproduction. CPU controls remain part of the five-mode fixed benchmark.

## Removed route

CPU fragment evolution/reproduction, its phase-bank predecessor, exclusive
owners/executable certificates, owned packing and capacity buckets, probe modes,
runner options and exclusive tests have been deleted. They are not isolated
alternatives, fallback reproduction backends, or future recommended work.

Historical measurements remain unchanged; their status is recorded in the
[archive notice](results/README.md). The old CPU-fragment timings are withdrawn
from the main target assessment. Original worktree modifications, frozen inputs,
archived binaries and historical logs remain protected. No branch reset or push.

## Retained native settings

Presence enables these flags (including a value of `0`):
```
GAGP_CUDA_DEVICE=0
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
GAGP_DIRECT_PHASE=1 GAGP_DIRECT_ROOT=1 GAGP_WINDOW_EXECUTOR=1
GAGP_UNBOXED_WINDOW_FRAMES=1 GAGP_BOUND_ADD_PHASE=1
```
Direct phase/root, window execution, unboxed frames, bound Add, case sorting,
compact workspaces and native generation-owned analysis handoff remain. The
native bytecode verifier, grammar admission/certificates, donor preparation and
GPU reproduction/decode paths retain their safety contracts and tests.

Most recent nine-repeat native medians at P1024/full1024 cases, before cleanup:
Sum205.448, House207.503, Median178.636 ms. These are historical native values,
not new cleanup measurements; 40× is not achieved. See native rows in
[round2-final.json](results/round2-final.json). Historical control binary
`logs/optimization/final_bench` and frozen inputs must not be overwritten.

## Reproduce maintained paths

Build the existing Release CUDA configuration:
```
cmake --build cpp/build/release -j10
```
The runner supplies the native flags above. Use a new output directory each time:
```
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --generations 32
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench
```
Scaling retains `asgp_1t`, `gagp_cpu`, `gpu_eval`, `gpu_repro`, `gpu_overlap` at
P1024/2048/4096/8192, using the existing frozen P8192 prefixes and all 1024 cases.
`snapshot` and `search` are native diagnostic modes. Never rerun prepare against
original inputs. GPU runs are serial; profile only for diagnosis, not headlines.

## Cleanup validation

Logs and dependency/protection audit:
`logs/optimization/cpu-fragment-removal-20261002/`.
Full Release build and 119 native tests passed. Tools: 89 tests (4 optional skips);
repository: 24 tests passed. Three tasks × five modes × (one warmup + three measured runs)
at P32/full 1024 cases matched every non-timing field in the pre-cleanup archived
native binary. This is smoke/regression evidence, not a new performance baseline.
Removed modes reject with `unknown mode`; no deleted symbols remain in native
libraries and no removed entrypoints remain in maintained source/guides/runners.
All original-file hashes match. [Validation record](results/cpu-fragment-removal-20261002.json).
The cleaned binary is archived at
`logs/optimization/cpu-fragment-removal-20261002/bench`.
No new architecture or optimization experiment is part of this cleanup.
