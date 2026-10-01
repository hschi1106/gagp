# Optimization checkpoint — round two

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. Best validated runtime checkpoint **9fcb8c3** (proof objects cannot be copied or
assigned; grammar is snapshotted at admission). Archived candidate
`logs/optimization/R2-release/bench`;
its source, binary hash, flags and frozen input hashes are recorded in
`R2-release/formal/manifest.json`. Do not replace this binary or prepare frozen inputs.

Previous best: `logs/optimization/final_bench`, six flags, [results](results/final.json).
Original baseline: `edfcf0b`; original worktree and 41 user-modified files protected.
Old experiment narrative remains in this file at commit `1b4f4bb`; raw logs remain
under `logs/optimization/`. Historical `best_bench` is not the final candidate.

## Current candidate and commands

Native flags (presence enables each flag, including a value of `0`):
```
GAGP_CUDA_DEVICE=0
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
GAGP_DIRECT_PHASE=1 GAGP_DIRECT_ROOT=1 GAGP_WINDOW_EXECUTOR=1
GAGP_UNBOXED_WINDOW_FRAMES=1 GAGP_BOUND_ADD_PHASE=1
```
Fragment probe additionally enables `GAGP_LOCAL_FRAGMENT_ADMISSION=1` and
`GAGP_OWNED_EXECUTABLE=1`. `GAGP_FRAGMENT_PARSIMONY=1` is a separately reported
search-rule variant, not the native backend. Support/semantics are authoritative
in [the benchmark guide](../../../docs/guides/fixed-asgp-benchmark.md#owned-fragment-prototype).

```
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary logs/optimization/R2-release/bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary logs/optimization/R2-release/bench --native-generations 32 --generations 128
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary logs/optimization/R2-release/bench --only fragments --parsimony --generations 256
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary logs/optimization/R2-release/bench
```
Run serially; every output directory must be new. No performance runs alongside
builds or other GPU tests. Build: Release CUDA arch 86, `-lineinfo --ptxas-options=-v`.

## Formal common-configuration result

P=1024, all 1024 cases; three processes, one warmup and three samples each.
Unprofiled complete-generation medians, ms:

| Path | Sum | House | Median |
|---|---:|---:|---:|
| Same-round previous-best control | 250.679 | 216.210 | 176.267 |
| New native fixed-parent | 205.448 | 207.503 | 178.636 |
| Owned fragments, first generation | 71.089 | 63.521 | 32.278 |

Native and fragment operator contracts differ. Later fragment generations contain
different programs and are never identical-workload speedups. Full raw nine
samples, cold owner/GPU setup and search totals: `R2-release/formal-summary.json`,
`R2-release/formal-repeats.csv`, `R2-release/formal/`. Targets Sum <46.029 and House
<32.050 ms are **not met**. Native Median has not improved; fragment Median meets
the <60 ms stage target with explicit support/search restrictions.

## Experiments retained / rejected

| Direction | Evidence and decision |
|---|---|
| 512 launch bound vs 1024 at block 512 | Same 64 registers; different spill/stack allocation, no full-generation gain. Rejected (`R2-A`). |
| Direct typed phases/root | Removes tagged presets/general root VM storage; stack 6784→3136→896 B. Retained (`R2-F1`, `R2-root`). |
| Immutable fragments / local admission | Shares unchanged phases; no population AST or crossover→mutation AST round trip. Changed phases still use local AST/membership/compile. Retained as restricted probe (`R2-fragments`, `R2-local`). |
| Owned bytecode certificates | Private owner/slot-bound immutable proofs; removes repeated whole-bytecode verification, not assembly/type flow. A few ms saved in repeats (`R2-owned-pack`, `R2-final/formal`). |
| Dense coordinate memo | Preserved lazy order/fuel/capacity; House kernel regressed ~32.3→34.4 ms. Rejected (`R2-F2`). |
| Cache top two VM stack values | Sum ~64→77, House ~31→37 ms kernel. Rejected (`R2-top`). |
| Actual one/two-state window executor | First one-state prototype did NOT cover frozen Sum/Median. Corrected carried-Int shape: Sum kernel ~66→58 ms. Retained (`R2-window-pair`). |
| Unboxed window frames | Preserve private list-view bit; frame 88→56 B for paired state, Sum kernel ~45 ms, increases concurrent workspace blocks. Retained (`R2-unboxed-window`). |
| Bound scalar Load/Load/Add | Supports implicit/explicit Return with exact instruction fuel/double conversion. Sum kernel ~41–43 ms. Retained (`R2-bound-add`). |
| Cohort eval/variation pipeline | Complete-case 256/512 cohorts; Sum ~97/84 vs76 ms, House ~81/70 vs65. Rejected; patch/binary archived (`R2-pipeline`). |
| Eager per-generation phase JIT | Optimistic NVRTC compile-only screen: 387 newly observed expressions take ~1.65 s, omitting real fuel/error code. No executor/parity claim; reject eager route (`R2-jit-screen`). |
| Fitness-tie parsimony | Changes search selection, reduces neutral growth in 128-gen screen. Validated 3 seeds ×256 generations; Sum/House growth reduced, Median still grows. Separate mode. |
| Payload registry overhaul | Measured transaction/read-set costs too small to justify broad rewrite before representation work (`R2-host`). |

**NCU launch interpretation, verified:** current NSYS traces show exactly one
fitness launch per generation (Sum/Median grid374, House416); `kernels.cuh` uses
`prog_idx += gridDim.x` inside the kernel. All1024 programs are covered by that
single launch. This also exists at checkpoint6f18efc. The earlier note claiming
that skip1 was another chunk was incorrect and is withdrawn: old `.warm` labels
DO select the next generation. Workspace changes concurrent blocks, not number
of eval launches. Final counters use the trace-confirmed skip and report whole
evaluation scope; clocks/caches remain uncontrolled. Local sectors are not DRAM
bytes or spilling proof; active and eligible warps are distinct.

## Validation / active work

Native 120 tests passed together with every native flag, local admission and
owned certificates enabled (`R2-release/ctest.log`);
89 tool tests (4 optional skips), 24 repository checks passed. ASan/UBSan fragment
ownership/lifetime check passed (`R2-validation/asan-test.log`). Snapshot equality
and targeted window/fuel/binding tests: `R2-bound-add/snapshots2`, `R2-final/focused3.log`.
Earlier F1/root candidate: 3 seeds ×128 generations native and fragments, full
export/admission and CPU top16, plus held-out 1024-case audits; recorded in
[quality summary](results/round2-quality.json). Native Median top16 including
best -28478 was importable; this is not support for every native skeleton.

Final 32-gen native /128-gen fragments and 256-gen parsimony completed under
`R2-final/quality` and `R2-final/parsimony256`. All cases scored, zero timeouts/
unscored/fallbacks, top16 CPU/GPU equal. These predate API hardening/cold grammar
copy: CUDA SASS remains identical; 72 four-generation searches on the hardened
binary preserve initial/final fitness/node rows and CPU audits. Cold copy costs measured.

Completed fresh 72-cell matrix (2413.32 s), NSYS, NCU, 27 held-out audits and
32-case behavioral-diversity probes. Newer native128 Median top16 programs all
imported, including seed-best fitness -10554/-16120/-14170. These show expression
coverage, not equivalent search behavior. Held-out draws use a simpler uniform
within-range distribution, not ASGP's full edge-case generator.

Formal results and original-file/frozen-input audit are now in
[round2-final.json](results/round2-final.json), with separate raw repeats, cold
costs, scaling, profile, counters, quality and memory CSV/JSON files alongside it.
Every full search generation scored 1,048,576 pairs. Zero timeouts, unscored and
fallback-token outputs; runtime errors are NOT zero (final pair error fraction
up to 5.69% in ordinary fragments, 5.00% in parsimony, 3.38% in native32).
Fallback-token counts do not measure profile demotion; see profile-coverage JSON.
No convergence-equivalence claim. RSS remains sensitive to program bloat despite
bounded live fragment counts. NSYS allocation peaks (~517–564 MiB) exclude CUDA
context/local backing and are short traces, not long-run total device RSS.

## Final optional checkpoint / resume

**d1faa9b** adds opt-in `GAGP_BUCKET_SMALL_PHASES` (driver `--buckets`) and owned
snapshot diagnostics. Common best settings above do NOT enable buckets.
Archived binary `logs/optimization/R2-buckets2/bench`, source patch and pilot
under `R2-buckets2/`. It partitions certified stack/local capacities, moves the
existing code/proof pairs, reuses the immediately preceding immutable view proof,
and scatters all results to original indices. No search or execution change.
Mixed-capacity/fuel, view-ineligible fallback, immutable lifetime and payload
regressions passed after re-linking five affected tests; repository checks 24/24.
No failed test removed. Formal buckets: three processes × one warmup + three
measurements, including all outliers, full cases and identical input population:

| Late frozen population | Off median ms | On median ms |
|---|---:|---:|
| Sum | 239.316 | 192.724 |
| House | 161.394 | 163.575 |
| Median | 81.463 | 79.716 |

Sum improves 19.5%; House/Median do not establish a robust benefit. Initial
population medians off/on: Sum71.147/71.021, House63.315/62.929,
Median33.812/33.307 ms. Do not substitute these later populations into headline.
[Bucket results](results/round2-buckets.json),
[nine raw repeats](results/round2-buckets-repeats.csv),
[scaling](results/round2-buckets-scaling.csv).
First prototype repeated copies/proofs and regressed House by ~22 ms;
archived `R2-buckets/`, superseded by the private consuming partition.

**All queues finished.** `R2-buckets-validation/`: nine initial/late A/B repeats,
3 seeds ×128 bucket generations, all per-generation semantic counters and final
exported ASTs exactly equal to the preceding path; 12 additional fragment scaling
cells; NSYS timeline and five re-linked tests. Scaling explicitly reuses unchanged
archived ASGP/native controls from the 72-cell matrix. Fresh bucket-off initial
controls check environment drift. `post-v2.py` excludes the two trailing raw
final/export audit launches from each one-generation trace; original failed
parser assertion is archived. Common-configuration NVTX attribution is separate.

Resume commands: the four commands above reproduce the common candidate. Add
`--buckets --binary logs/optimization/R2-buckets2/bench` for the optional path;
use fresh output directories. Late A/B and its frozen late-input hashes are in
`R2-buckets-validation/{run.py,manifest.json,summarize.py}`. Common reporting:
`R2-release/report.py`. Original worktree/41 modified files and all frozen inputs
rechecked unchanged. No prepare, push, system changes or background jobs left.

## Bottlenecks and limits

Profiled initial fragment generation: Sum79.54, House69.57, Median40.22 ms.
CUDA eval41.18/32.52/2.12; host reproduction21.56/24.79/22.86;
packing5.15/5.15/5.07 ms. Changed-phase compile/admission/verification unions are
nested inside host reproduction, not additive wall costs. See
[attribution](results/round2-final-profile.csv). No subtraction from unprofiled
headline. Late bucket Sum CUDA eval ~150→101 ms; House ~53→47, Median~7.8→7.4;
extra host/layout work explains why cheaper populations gain little.
NCU Sum: eligible warps0.178, issue15.4%, long-scoreboard59.9%; indexed local
loads and static spills both exist. No claim that every local load is a spill,
or that local sectors equal DRAM bytes. Final compiler entry report64 registers,
992 B stack,216/88 B spill stores/loads is static resource evidence.

Warm fresh-owner four-generation search totals (including setup):
321.241/258.820/152.322 ms; cold first process search434.505/381.775/277.459.
Full parse/diagnostic process costs and raw cold values are separately recorded.
Five-second quality thresholds: native House1/3, Median2/3; ordinary fragments
and parsimony both3/3. Three seeds, diagnostic timing and simplified held-out
sampling do not prove convergence equivalence. Parsimony remains a separate
search change, not required for performance headline; Median still grows.

Remaining architecture limits: fixed admitted root skeleton, independent phase
holes and closed scalar phases; no generic derivation-first backend or asynchronous
hot-phase JIT. Bytecode copies, type-flow packing, changed-phase AST admission and
dynamic interpreter work remain. Bucket launch/layout overhead matters on cheap
work; no supported adaptive policy yet. A next experiment must eliminate remaining
work, not merely add another cache around mutable ASTs.

Completed decision: retain the measured executor/ownership simplifications and
isolated capacity buckets, reject the measured regressions. Sum/House40× and
Median10–20 ms remain unmet. Further dynamic interpreter or subphase compiler
work needs a new bounded prototype; eager JIT and stack reshuffling did not
support pursuing a broad rewrite. Next entry is changed-phase direct derivation
compilation with explicit grammar/scope/resource proof, measured against local
admission+compile worker cost. It is not implemented or claimed as a gain here.
