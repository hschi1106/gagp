# Optimization checkpoint — round two

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. Runtime checkpoint **b4ca289**; test contract fix
**1b4f4bb**. Archived final candidate `logs/optimization/R2-final/bench`;
its source, binary hash, flags and frozen input hashes are recorded in
`R2-final/formal/manifest.json`. Do not replace this binary or prepare frozen inputs.

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
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary logs/optimization/R2-final/bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary logs/optimization/R2-final/bench --native-generations 32 --generations 128
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary logs/optimization/R2-final/bench --only fragments --parsimony --generations 256
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary logs/optimization/R2-final/bench
```
Run serially; every output directory must be new. No performance runs alongside
builds or other GPU tests. Build: Release CUDA arch 86, `-lineinfo --ptxas-options=-v`.

## Formal result so far

P=1024, all 1024 cases; three processes, one warmup and three samples each.
Unprofiled complete-generation medians, ms:

| Path | Sum | House | Median |
|---|---:|---:|---:|
| Same-round previous-best control | 249.304 | 216.949 | 174.955 |
| New native fixed-parent | 204.420 | 207.321 | 178.958 |
| Owned fragments, first generation | 70.453 | 62.947 | 32.794 |

Native and fragment operator contracts differ. Later fragment generations contain
different programs and are never identical-workload speedups. Full raw nine
samples, cold owner/GPU setup and search totals: `R2-final/formal-summary.json`,
`R2-final/formal-repeats.csv`, `R2-final/formal/`. Targets Sum <46.029 and House
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
| Unboxed window frames | Preserve private list-view bit; frame 88→56 B for paired state, Sum kernel ~45 ms, includes changed workspace batching. Retained (`R2-unboxed-window`). |
| Bound scalar Load/Load/Add | Supports implicit/explicit Return with exact instruction fuel/double conversion. Sum kernel ~41–43 ms. Retained (`R2-bound-add`). |
| Cohort eval/variation pipeline | Complete-case 256/512 cohorts; Sum ~97/84 vs76 ms, House ~81/70 vs65. Rejected; patch/binary archived (`R2-pipeline`). |
| Eager per-generation phase JIT | Optimistic NVRTC compile-only screen: 387 newly observed expressions take ~1.65 s, omitting real fuel/error code. No executor/parity claim; reject eager route (`R2-jit-screen`). |
| Fitness-tie parsimony | Changes search selection, reduces neutral growth in 128-gen screen. Separate 256-gen validation pending. |
| Payload registry overhaul | Measured transaction/read-set costs too small to justify broad rewrite before representation work (`R2-host`). |

**NCU correction:** earlier `R2-A` and `R2-validation/ncu` files named `.warm`
used `--launch-skip 1`. Evaluation is workspace-batched, so this is a second chunk
of the same generation, NOT a warmed repeat of the first chunk. A/B comparisons
at the same skip remain meaningful; cold/warm interpretations are withdrawn.
Final profiling will use NSYS-counted launches per generation to choose the same
first chunk in the next generation. Local sectors are not DRAM bytes or evidence
of spilling by themselves; active and eligible warps are distinct.

## Validation / active work

Native 120 tests passed in aggregate (119 full-suite + corrected handoff test);
89 tool tests (4 optional skips), 24 repository checks passed. ASan/UBSan fragment
ownership/lifetime check passed (`R2-validation/asan-test.log`). Snapshot equality
and targeted window/fuel/binding tests: `R2-bound-add/snapshots2`, `R2-final/focused3.log`.
Earlier F1/root candidate: 3 seeds ×128 generations native and fragments, full
export/admission and CPU top16, plus held-out 1024-case audits; recorded in
[quality summary](results/round2-quality.json). Native Median top16 including
best -28478 was importable; this is not support for every native skeleton.

Currently running final 32-gen native /128-gen fragments and 256-gen parsimony
under `R2-final/quality` and `R2-final/parsimony256`. Next: final NSYS/NCU, held-out
and behavioral diversity audit, complete P=1024/2048/4096/8192 scaling (rerun shared
CPU controls), final manifest and original/frozen hash audit. Resume from manifests;
never rerun into existing output directories. No push authorized this round.

Remaining architecture limits: fixed admitted root skeleton, independent phase
holes and closed scalar phases; no generic derivation-first backend, mixed
capability buckets or asynchronous hot-phase JIT. Bytecode copies, type-flow
packing, changed-phase AST admission and dynamic interpreter work remain. Live
fragment counts are bounded, but program bloat can grow RSS/runtime; report both.
