# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`
- Branch: `opt/gpu-generation-20261001`; original user worktree untouched.
- Starting checkpoint: first commit on this branch, snapshot of existing uncommitted benchmark/profiler artifacts.
- Baseline executable: `logs/optimization/baseline_bench` (hash in `logs/optimization/origin.json`). Frozen inputs symlink to original artifacts; never modify them.
- Reference ASGP 1T: Sum 1841.160175 ms, House 1282.011816 ms. Targets <46.029004 / <32.050295 ms per full generation (40×). These are unprofiled medians from archived profiler controls.
- `medium`: searched existing settings, results, docs and ASGP sources; no independent named workload found. Prior Median interpretation was an assumption. User clarification pending; Sum/House continue. Median remains an auxiliary overhead probe.
- Current best general path: E15 strided sorted cases, `GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1 GAGP_COMPACT_FRAMES=1`; nine-sample full-generation medians 274.706 / 217.977 / 177.029 ms. Strict compilation remains inside every generation. Phase-bank E16 is a separate finite-search-space prototype, with initialization charged to first generation.
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

## Current checkpoint and continuation

- Stable code: `e26dba3`; preserved executable `logs/optimization/best_bench`.
  Final scaling runs that unchanged binary; no concurrent GPU or build jobs.
- Validation: all 119 native tests, 20 optimized GPU tests, 24 repository tests
  passed; 89 tooling tests ran (4 optional skips). Payload proof reuse requires
  a coherent read snapshot for registry-backed constants; scalar genomes unchanged.
- `logs/optimization/repeats-final/`: nine alternated samples versus E09;
  Sum 274.706 ms, House 217.977 ms, Median 177.029 ms. Three independent cold
  finite-bank runs charge bank setup to generation one; GPU context initialization
  is reported separately under the existing timing contract.
- `logs/optimization/quality-final/`: 27 four-generation cells (three profiles,
  three tasks, seeds 0/1/2), all full cases. Optimized Sum best CPU/GPU fitness is
  0 in all seeds; House matches generic trajectories. No timeout or unscored cases.
  Finite phase-bank Median stays at -31025 versus standard variation reaching
  -28478 in 2/3 seeds. Finite mutation is not an equivalent search replacement.
- Completed: original 60-cell scaling, all cells succeeded in 2435.98 seconds.
  Results/environment: `logs/optimization/scaling-final/`. It exposed a large
  P8192 host-analysis cliff in every task; E21 is now under A/B and RSS validation
  (`logs/optimization/E21-rss/`). Original scaling binary/results remain intact.
- Next evidence-driven experiments, staged but not installed:
  1. NCU 2024.3 at `/usr/local/cuda-12.6/bin/ncu` with one evaluator launch and
     only SM/DRAM throughput, active warps, local loads. Earlier failure used
     NCU 2022.3. User explicitly permits NCU, overriding old repository guidance.
     No system/driver changes if counter access fails.
  2. Fresh grammar-generated phase mutation (`logs/optimization/phase_bank_fresh.cpp`)
     to address observed finite-bank stagnation. Copy to the isolated phase-bank
     benchmark source, build, run diagnostics; quality gate before adoption.
  3. Scaling discovered P8192 Sum GPU reproduction 7195 ms and overlap 6307 ms,
     versus P4096 1142/1059 ms. The 4096-entry cache disables population
     handoffs above capacity, triggering repeated serial analysis. Test an opt-in
     generation-owned handoff independent of LRU capacity, measure peak RSS and
     retain bounded persistent cache (`logs/optimization/apply_handoff_prototype.py`).
  4. Verified small root VM (`logs/optimization/apply_small_root_prototype.py`),
     opt-in `GAGP_SMALL_ROOT_VM=1`; phase VM was reduced earlier, root remains 64/64.
  5. Numerical-lossy Int32 phase arithmetic draft
     `logs/optimization/int32_view_phase.cuh`, not wired or tested. Preserve 64-bit
     list descriptors and fuel; report narrowing/overflow and quality differences.
     Only proceed if profiling or minimal A/B supports value.
- After retained changes: reprofile, repeat unprofiled timing, check snapshots and
  several seeds. Reuse explicitly identified unchanged CPU controls if updating
  scaling GPU cells. Do not mix profiler times with speedup denominators.

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
