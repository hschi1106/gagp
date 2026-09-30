# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`
- Branch: `opt/gpu-generation-20261001`; original user worktree untouched.
- Starting checkpoint: first commit on this branch, snapshot of existing uncommitted benchmark/profiler artifacts.
- Baseline executable: `logs/optimization/baseline_bench` (hash in `logs/optimization/origin.json`). Frozen inputs symlink to original artifacts; never modify them.
- Reference ASGP 1T: Sum 1841.160175 ms, House 1282.011816 ms. Targets <46.029004 / <32.050295 ms per full generation (40×). These are unprofiled medians from archived profiler controls.
- `medium`: searched existing settings, results, docs and ASGP sources; no independent named workload found. Prior Median interpretation was an assumption. User clarification pending; Sum/House continue. Median remains an auxiliary overhead probe.
- Current general path: E22 compact frames plus E21 large-population handoff, `GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1 GAGP_COMPACT_FRAMES=1`; prior e26 nine-sample medians 274.706 / 217.977 / 177.029 ms; E22 Sum ~255–261 ms, final repeats pending. Strict compilation remains inside every generation. Phase-bank E16 is a separate finite-search-space prototype, with initialization charged to first generation.
- Build: `cmake --build cpp/build/release --target gagp_fixed_asgp_bench gagp_test_fitness_cpu_gpu_parity gagp_test_evolution_cpu_gpu_parity -j10`.
- Experiment results: `logs/optimization/`; compact summaries/checkpoints recorded here. GPUs run serially.

## Experiments

| ID | Hypothesis / change | Result | Decision |
| --- | --- | --- | --- |
| E00 | Preserved binary controls | Sum 805.0 / House 765.1 / Median 263.3 ms | reproduced |
| E01 | Parallel bounded verification + staging capacity reuse | First run exposed a second capacity guard in copyback; fixed consistently | superseded by E01b |
| E01b | Same checks in parallel, staging allocation dimensions only | 742.3 / 681.5 / 207.1 ms; all non-timing outputs identical; parity + bounded-region tests pass | retain, provisional |

| E02 | Verifier-proven phase stack <=16 and locals <=8 use smaller interpreter storage; generic fallback otherwise | 532.7 / 554.9 / 210.2 ms; kernel 357.6 / 354.1 / 9.3 ms; non-timing fields identical; bounded-region and parity pass | retain provisionally; need per-program diagnostic gate |

| E03 | Transpose generic region frame fields across lanes | Sum unchanged; House 655.6 ms (kernel 451.6 vs 354.1), clear regression; parity passed | REJECTED; patch saved in logs/optimization/E03/prototype.patch |
| Diagnostic | NCU 2022.3 minimal four metrics | Driver reports unsupported API on kernel launch; no metrics collected, no system changes | use nsys and targeted A/B |

| E04 | Opaque membership/verified-AST certificate plus reuse of admission bytecode | 511.4 / 515.4 / 173.6 ms; all output fields identical | superseded: primary BM keeps per-generation compilation explicitly |
| E04b | Keep membership proof only; exact AST/payload identity, grammar/request validation; full compile each generation | 518.8 / 531.4 / 177.3 ms; stale AST/constant/request and forged provenance tests + payload evolution pass | retain provisionally |

| D01 | Fixed-program snapshot evaluator with optional per-case counters; 4-generation search diagnostics | All 1024 per-program fitness/count rows equal between generic and small VM, all 1024 cases each. Sum errors 453208; House 0; Median 275609; all timeouts/fallback tokens 0 | diagnostic gate; no speedup claimed |

| E05 | Opt-in read-only IntList views, proven by forward type analysis, generic fallback otherwise | 472.5 / 481.6 / 180.4 ms; kernels 310.7 / 304.2 / 4.2. Sum errors decrease 453208→318332, 192 program fitness changes; House/Median exact per-program agreement. Full CPU comparison and known solutions recorded | retain prototype, quality/search gate still pending |

| E06 | Type-proven phase VM with unboxed payloads; ordinary integer conversion semantics/fuel retained | 379.1 / 346.1 / 179.3 ms; kernels 213.4 / 168.7 / 3.8. All per-program fitness and diagnostic counts match E05; refreshed nsys Sum/House recorded | retain prototype; host now roughly half of Sum/House |

| E07 | Stable length ordering of all cases, only type-proven integer fitness with exact bounded sum; setup included in session initialization | 282.7 / 243.6 / 177.0 ms; kernels 118.2 / 70.7 / 4.0; all 1024 per-program rows exactly equal to E06 | retain opt-in; host now dominant |

| E08 | Carry complete variation analyses across decode/preparation, exact identity and budget checks | 284.8 / 249.0 / 181.9 ms; no end-to-end gain, extra copy/key retention cost | REJECTED; patch and binary saved |
| E09 | Structurally bounded 1-state/1-preparation/2-request frames (72 vs 264 bytes), same frame/memo limits | 286.3 / 223.7 / 176.3 ms; House kernel 49.3 vs 70.7; Sum unchanged; fixed rows equal | retain opt-in `GAGP_COMPACT_FRAMES=1` |

| E10 | Delay overlap preparation until compilation finishes | 280.6 / 219.9 / 184.7 ms; Sum/House changes small, Median regresses | REJECTED; preserve original overlap scheduling |
| E11 | Cap worker teams at 2/4/8/10 vs original 20 | Every cap slows all workloads; 2 workers about 458/430/366 ms | REJECTED; CPU computation, not oversubscription alone, dominates |

| E12c | Immutable whole-phase bank; grammar-compatible independent holes; tournament, phase crossover and 0.3 finite-bank mutation; all cases, full pack verification | Setup 31–36 ms; reproduction ~0.3 ms; initial fitness exactly matches E09. Four-generation outputs all re-admitted and AST/handle fitness equal. Effective changes ~957–1002/1023; no timeout/fallback. Timings diagnostic only | retain isolated prototype, not default or 40× claim; needs multiple seeds/unprofiled repetitions |

| E13 | Force-inline typed phase interpreter | 285.6 / 223.9 / 177.6 ms; no gain | REJECTED |
| E14 | Fuel-preserving constant-only phase folding, bounded integer domain | 282.4 / 223.5 / 176.4 ms; compile overhead offsets tiny kernel gain | REJECTED |
| E15 | Strided traversal of sorted full cases; 512/256/128/64 thread sweep | 512 best Sum/House: 271.0/213.8 ms, kernels 109.1/42.1. Smaller blocks regress; Median difference negligible in full generation | retain strided traversal; remove thread override prototype |
| E16 | Binary exact phase identities and hash interning; phase-bank startup attribution | Startup 25.2/27.3/25.8 ms vs 31–36 ms. Analysis+compile 18–20 ms, skeleton ~2 ms, interning ~5 ms | retain in isolated phase-bank prototype; no primary benchmark speedup claim |

| E17 | NCU 2024.3 minimal metrics | Launcher works, but ERR_NVGPUCTRPERM blocks hardware counters | no system changes; nsys and targeted A/B continue |
| E21 | Generation-owned analysis handoff independent of the 4096-entry FIFO capacity | P8192 overlap 2012/1586/1319 ms versus 6307/6596/6852 ms; every non-timing field equal; capacity-3 ownership/eviction test passes | retain opt-in; repeated controls agree; peak RSS increases by ~0.74/0.33/0.76 GiB (Sum/House/Median) |

| E18 | Fresh grammar-generated whole-phase mutation | Reproduction ~5 ms/generation, 70–145 novel phases; all cases, final AST export/fitness audit passes. Three seeds: Sum best 0; House -8479/-8479/-8443; Median stays -31025. No timeout/unscored/failed mutation | retain isolated prototype; Median quality limitation persists |
| E19 | Verifier-proven smaller root VM, 16 stack / 8 locals | 276.9/219.4/176.1 ms; no meaningful end-to-end improvement; targeted GPU tests pass | REJECTED; patch/binary saved |

| E20 | Explicit Int32 phase arithmetic, same fuel and full cases | Kernel ~94/41/4 ms, no gain; 21 Sum and 36 House program fitness rows change; no timeout increase or false-perfect programs vs full CPU snapshot | REJECTED; overflow/division/list-cast/fuel tests passed; patch/binary preserved |
| E22c | Compact two-state frames (88 bytes), retaining one-state 72-byte layout | Sum ~261 ms / kernel 93 ms; House ~218 ms. All 1024 per-program fitness/counters identical to E09; 2D memo and GPU parity tests pass | retain; final repeats/scaling still needed |

| E23b | Proven scalar constant phases bypass interpreter, preserving explicit/implicit return fuel | Sum kernel ~86 ms vs ~91 ms; full generation ~248 ms; House/Median near noise | retain opt-in; nine-sample median 250.318/217.495/176.443 ms; Sum improves 254.412→250.318 ms; others within noise |
| E24 | Fuse typed-VM operand load with arithmetic/comparison, preserving both fuel checks and jump targets | Sum kernel ~90 ms vs ~91 ms; combined result attributable to E23; no material generation gain | REJECTED; patch/binary saved |

## Current checkpoint and continuation

- Stable source: `6fd4f5c` (E22 two-state frames + E21 handoffs + isolated fresh
  phase-bank probe). Current uncommitted experiments: E23 constant scalar phase
  shortcut and E24 fused typed-VM dispatch; one serial build/test/A-B shell is
  running, logs `build-E24.log`, `test-E24.log`, result directories `E24*`.
- Preserved binaries: original `baseline_bench`; e26 `best_bench`; E21
  `E21/bench`; combined rejected Int32 experiment `E20/bench`; pre-E24
  `E23b/bench`, all under `logs/optimization/`. See individual manifests for
  hashes and exact flags. Never overwrite frozen artifacts or old binaries.
- Main flags: `GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
  GAGP_COMPACT_FRAMES=1`; add `GAGP_POPULATION_HANDOFF=1` for large populations.
  No numeric narrowing is retained. Root-storage reduction was rejected.
- Completed 60-cell scaling on e26: all cells succeeded, 2435.98 seconds,
  `logs/optimization/scaling-final/`. E21 repeated 8192 overlap controls:
  Sum 6228→2000 ms, House 6613→1571 ms, Median 6814→1305 ms; every non-timing
  field equal. Additional peak RSS ~0.74/0.33/0.76 GiB, respectively.
- e26 validation: 119 native, 20 optimized GPU, 24 repository checks pass;
  89 tooling tests ran (4 optional skips). New E21 eviction/ownership test, E18
  adapter integration, E22 GPU/2D-memo/fuel/parity tests pass. Final full build
  and affected regression/scaling checks still required after experiments settle.
- Formal e26 repeats/quality: `repeats-final/` and `quality-final/`; nine timing
  samples; 27 four-generation cells over three variation seeds with the SAME
  frozen initial population. Sum best CPU/GPU 0, House generic trajectory exact;
  no timeout or unscored cases. This is not a convergence-quality proof.
- E18 fresh phase mutation: `E18/`, nine cells, complete final AST admission and
  handle/AST fitness equality. Sum best 0; House -8479/-8479/-8443; Median
  remains -31025. Reproduction ~5 ms, 70–145 novel phases per generation. It
  remains an optional benchmark prototype with a fixed skeleton, not a native
  full-grammar replacement. Its startup and final audits are separately timed.
- NCU 2024.3 works but counters are denied (`ERR_NVGPUCTRPERM`); no system changes.
  Latest nsys trace `E21-nsys/`: actual CUDA eval 110.85/43.26/4.04 ms before
  two-state compaction. Sum grid=79 blocks exposed its frame-size/concurrency
  issue; E22 kernel falls to ~93 ms. Reprofile the final retained candidate.
- Next: assess E24 dispatch fusion with and without E23; compare all snapshot
  rows and exact fuel boundaries. Retain only improvements exceeding timing
  noise. Then repeat timing/short quality, update scaling GPU cells and clearly
  identify any reused unchanged controls. If these instruction-reduction
  experiments plateau, document remaining interpreter/host costs and quality
  restrictions rather than inventing a 40× claim.

## Reproduction

Run from this worktree; GPU tests serially. Native command pattern:

```sh
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1 GAGP_COMPACT_FRAMES=1 GAGP_CUDA_DEVICE=0 \
cpp/build/release/gagp_fixed_asgp_bench measure \
 logs/fixed-asgp/artifacts/sum_of_elements.source.json \
 logs/fixed-asgp/artifacts/sum_of_elements.p1024.prepared.json \
 logs/fixed-asgp/artifacts/sum_of_elements.grammar.json 1024 gpu_overlap /tmp/sum.run.jsonl
```

Replace `gpu_overlap` with `snapshot` for fixed-program full-case diagnostics;
`GAGP_SNAPSHOT_CPU=1` adds the full CPU reference. Mode `search` runs four native
full generations plus separately timed final evaluation; enable
`GAGP_GPU_DIAGNOSTICS=1 GAGP_BM_SEED=0` for quality screening. Mode `phase_bank`
is the explicitly restricted independent-phase prototype, not the primary fixed
benchmark. Diagnostic atomics and CPU audits never supply headline speedups.

View execution removes bounded copy-pool losses for read-only lists; case count,
precision, fuel, limits and native reproduction operators remain unchanged.
Unsupported programs use the generic backend, never silently skip evaluation.
`medium` remains unresolved; Median results do not resolve that ambiguity.
