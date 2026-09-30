# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`
- Branch: `opt/gpu-generation-20261001`; original user worktree untouched.
- Starting checkpoint: first commit on this branch, snapshot of existing uncommitted benchmark/profiler artifacts.
- Baseline executable: `logs/optimization/baseline_bench` (hash in `logs/optimization/origin.json`). Frozen inputs symlink to original artifacts; never modify them.
- Reference ASGP 1T: Sum 1841.160175 ms, House 1282.011816 ms. Targets <46.029004 / <32.050295 ms per full generation (40×). These are unprofiled medians from archived profiler controls.
- `medium`: searched existing settings, results, docs and ASGP sources; no independent named workload found. Prior Median interpretation was an assumption. User clarification pending; Sum/House continue. Median remains an auxiliary overhead probe.
- Current best general path: E15 strided sorted cases, `GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1 GAGP_COMPACT_FRAMES=1`; provisional full generations 271.0 / 213.8 / 177.8 ms. Strict compilation remains inside every generation. Phase-bank E16 is a separate finite-search-space prototype, with initialization charged to first generation.
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

## Next

1. Parallel bounded bytecode verification (same checks inside generation), pinned staging capacity reuse; validate malformed bytecode and parity.
2. Inspect eval phase interpreter and region workspace for high-impact compact execution prototype, guarded by structural applicability.
3. After each meaningful candidate, fixed programs + full cases and end-to-end generations; retain only measured gains. Collect errors/timeout/fallback and short search quality before final adoption.

Diagnostics: native `measure SOURCE PREPARED GRAMMAR 1024 snapshot OUT.json`; `GAGP_SNAPSHOT_CPU=1` adds full CPU reference. `GAGP_GPU_DIAGNOSTICS=1 GAGP_BM_SEED=0 ... measure ... 1024 search OUT.json` runs four generations plus separately timed final eval. Diagnostic kernel atomics are opt-in; never use those timings as headline speedups.

E05 is enabled with `GAGP_VIEW_PROFILE=1`. It removes bounded copy-pool losses for read-only lists; no change to case count, precision, fuel, limits or reproduction operators. Container equality/output and unsupported bytecode use generic backend, never skipped.

Resume point: E15 is best general-grammar path; E16 finite independent phase-bank prototype is being quality gated. Its initial fixed evaluation uses exactly the original fitness rows, but phase-bank mutation restricts the search space. Next: evaluate typed interpreter inlining, then repeat/quality/final scaling gates. Raw E12 initial counts used distinct source indices and overcounted duplicate phases; E12c canonicalizes executable phases before reporting effective variation/diversity. Need final broad tests, repeated controls, multi-seed quality and 60-cell scaling.

Final validation checkpoint: all 119 native tests pass; 24 repository checks pass;
89 operational tests run (4 pre-existing optional skips). Full-build testing caught
a positional `DRegionWorkspace` initializer in result probes; the new flag was
moved to the end to preserve that interface. Next queued commands run optimized
GPU regressions, `logs/optimization/quality_run.py`,
`logs/optimization/repeat_run.py`, then the existing scaling matrix.

Payload-proof audit: cache reuse/creation for registry-backed constants now requires
an enclosing read snapshot. Scalar fixed-benchmark genomes are unaffected. The
27-cell quality screen completed on ca436c0; repeats/scaling had not started and
were held for this correctness gate. Rebuild/tests, then run repeats/scaling on the
final binary; keep original quality evidence with its recorded hash.
