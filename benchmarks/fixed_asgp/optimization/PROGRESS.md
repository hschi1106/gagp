# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`
- Branch: `opt/gpu-generation-20261001`; original user worktree untouched.
- Starting checkpoint: first commit on this branch, snapshot of existing uncommitted benchmark/profiler artifacts.
- Baseline executable: `logs/optimization/baseline_bench` (hash in `logs/optimization/origin.json`). Frozen inputs symlink to original artifacts; never modify them.
- Reference ASGP 1T: Sum 1841.160175 ms, House 1282.011816 ms. Targets <46.029004 / <32.050295 ms per full generation (40×). These are unprofiled medians from archived profiler controls.
- `medium`: searched existing settings, results, docs and ASGP sources; no independent named workload found. Prior Median interpretation was an assumption. User clarification pending; Sum/House continue. Median remains an auxiliary overhead probe.
- Current best: E06 with `GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1`; Sum 379.1 / House 346.1 / Median 179.3 ms. Strict compilation remains inside every generation. Previous binaries retained in experiment directories.
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

## Next

1. Parallel bounded bytecode verification (same checks inside generation), pinned staging capacity reuse; validate malformed bytecode and parity.
2. Inspect eval phase interpreter and region workspace for high-impact compact execution prototype, guarded by structural applicability.
3. After each meaningful candidate, fixed programs + full cases and end-to-end generations; retain only measured gains. Collect errors/timeout/fallback and short search quality before final adoption.

Diagnostics: native `measure SOURCE PREPARED GRAMMAR 1024 snapshot OUT.json`; `GAGP_SNAPSHOT_CPU=1` adds full CPU reference. `GAGP_GPU_DIAGNOSTICS=1 GAGP_BM_SEED=0 ... measure ... 1024 search OUT.json` runs four generations plus separately timed final eval. Diagnostic kernel atomics are opt-in; never use those timings as headline speedups.

E05 is enabled with `GAGP_VIEW_PROFILE=1`. It removes bounded copy-pool losses for read-only lists; no change to case count, precision, fuel, limits or reproduction operators. Container equality/output and unsupported bytecode use generic backend, never skipped.

Resume point: E06 compiled and snapshot validated. Next high-value probes: length-aware case ordering/compact region frames (all cases preserved), then host/reproduction representation and construction proofs; do not keep tuning tiny transfer/sync phases. Need final broad tests, repeated controls, short multi-seed quality and 60-cell scaling before completion.
