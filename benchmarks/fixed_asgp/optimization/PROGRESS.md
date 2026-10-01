# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`; branch `opt/gpu-generation-20261001`.
- Best runtime: `9d862ed43e9c626b22775713a50897c7f5439c91`; all experiments finished, no jobs pending. Runtime profiles remain opt-in. Original worktree and its 41 copied uncommitted files are unchanged; all 15 frozen artifact hashes rechecked.
- Baseline checkpoint: `edfcf0b6b6f2a8d868d920f45c4e750b05e5f1c9` contains the user's existing benchmark/profiler work. Baseline executable `logs/optimization/baseline_bench`; final executable `logs/optimization/final_bench`. Hashes, flags and evidence: [final.json](results/final.json). Historical `best_bench` means e26, not the final candidate; do not overwrite archived binaries or frozen inputs.
- Main targets **not met**. The user confirmed that `medium` was a typo for **Median**; it is the third optimization target and was included in every final measurement and quality screen. No case sampling, precision narrowing, fuel/depth/list/memo limit reduction or native operator change was adopted.

## Final measurements

Unprofiled full fixed-parent generation, P=1024, cases=1024, GPU evaluation + GPU reproduction + overlap. Each final median has nine measured samples from three isolated processes, one warmup per process. ASGP denominators stay fixed at the archived unprofiled baseline.

| Task | Original GAGP ms | Final median ms (range) | Improvement over GAGP | ASGP 1T speedup | 40× target ms |
| --- | ---: | ---: | ---: | ---: | ---: |
| Sum of Elements | 805.996 | 250.318 (242.954–254.208) | 3.22× | 7.36× | <46.029 |
| House Robber | 761.333 | 217.495 (215.009–221.310) | 3.50× | 5.89× | <32.050 |
| Median | 265.242 | 176.443 (173.863–178.349) | 1.50× | 0.35× | — |

Sum/House still require another 5.44×/6.79× reduction in complete generation time to reach 40×. These calculations use only unprofiled times. Exact raw repeats and alternating constant-shortcut controls: `logs/optimization/E23-repeats/`. E23 helps Sum modestly (254.412→250.318 ms); House/Median differences are within noise.

The [final scaling CSV](results/final-scaling.csv) covers all five modes and P=1024/2048/4096/8192. The complete e26 matrix ran 60 cells in 2435.98 s; the final candidate reran all 36 GPU cells in 291.68 s and explicitly reused the 24 unchanged ASGP/CPU controls. Source manifests, environment and hashes are in `logs/optimization/scaling-candidate/`; its ratios use that matrix's controls, not the fixed headline denominators. It is not a fresh 60-cell timing invocation.

| Overlap generation ms | P1024 | P2048 | P4096 | P8192 |
| --- | ---: | ---: | ---: | ---: |
| Sum | 249.173 | 473.493 | 966.235 | 1820.173 |
| House | 216.796 | 421.975 | 839.672 | 1593.075 |
| Median | 178.317 | 341.595 | 685.640 | 1312.121 |

Generation-owned analysis handoffs remove the >4096-entry cache cliff. Repeated P8192 controls showed 6228→2000 / 6613→1571 / 6814→1305 ms before the final Sum frame improvement. Peak host RSS increases by approximately 0.74/0.33/0.76 GiB, respectively; see `logs/optimization/E21-rss/`.

## Final attribution and adoption limits

The [profile CSV](results/final-profile.csv) uses one representative nsys generation per task/mode with additive wall attribution. Values below are **profiled diagnostic milliseconds**, not the formal benchmark. Overlapping work is attributed once; host work hidden under a kernel is not added again.

| Component, overlap | Sum | House | Median |
| --- | ---: | ---: | ---: |
| Compile | 26.34 | 32.49 | 27.59 |
| Packing | 13.56 | 12.88 | 13.27 |
| H2D | 3.94 | 3.73 | 3.69 |
| Eval kernel | 87.67 | 43.61 | 3.91 |
| Selection, host + GPU | 1.29 | 1.21 | 1.52 |
| Reproduction, host + GPU | 70.43 | 70.50 | 80.98 |
| D2H | 0.19 | 0.28 | 0.21 |
| Verification / analysis | 58.90 | 69.05 | 59.23 |
| Exposed sync | 0.09 | 0.09 | 0.08 |
| Allocation / other | 6.89 | 6.61 | 7.26 |
| Profiled generation total | 269.29 | 240.46 | 197.75 |

GPU reproduction itself is only about 1 ms. Raw CUDA wait overlaps evaluation; adding it to kernel time would double-count. Final actual CUDA eval medians are 85.56/43.15/3.91 ms. Six traces, the isolated NVTX source patch, source hashes, semantic-output checks and full analysis are in `logs/optimization/final-profiler/`. NCU 2024.3 counters were denied (`ERR_NVGPUCTRPERM`); no system setting changed. Do not derive unprofiled speedup ceilings from this table.

The retained native path removes repeated membership proof work, reuses pinned capacity, uses verified typed phase storage and read-only list views, sorts all eligible cases, and compacts structurally bounded frames. Strict compilation remains inside every generation. Unsupported bytecode uses the generic backend. List views remove bounded copy-pool losses: initial Sum error cases fall 453208→318332, with 192 fitness rows changed and closer CPU agreement. This is disclosed behavior improvement, not identical-workload evidence for that step; subsequent fixed snapshots match E09 exactly. No new numerical loss is retained.

The isolated fresh-phase bank tests a more radical representation: immutable compiled phases, chromosome handles and whole-phase variation. It preserves full cases/fuel/precision but fixes the region skeleton and restricts operators/search space. Cold unprofiled first generations including bank setup are 136.25/93.42/53.49 ms; subsequent medians 122.59/51.88/18.86 ms. Cold GPU initialization is separately 110.94/111.81/125.05 ms; per-run combined and four-generation amortized costs are in `final.json`. Initial analysis/compile/interning and fresh donor admission/compilation are measured at their actual frequency. Final export/admission, evaluation and CPU audits are also separately recorded. Later generations contain different programs, so these are not identical-snapshot speedups or main 40× results.

Fresh-phase mutation gives Sum best 0 for all three seeds and House -8479/-8479/-8443. Median stays at -31025 for all seeds while native reaches -28478 in two seeds. Therefore the bank remains a benchmark-only prototype, not a replacement for general native evolution. It rejects unsupported skeletons explicitly; fresh donors require registry-independent constants and this probe is capped at 16 generations because proof-source retention grows. See [the benchmark guide](../../../docs/guides/fixed-asgp-benchmark.md#optimization-and-diagnostic-probes) for its contract.

## Validation and continuation

- Full native suite: 119 passed. All final profile flags enabled: 20 GPU tests passed. Tool suite: 89 run, 4 optional skips, no failures. Repository checks: 24 passed. Logs are listed in `results/final.json`.
- Fixed snapshots: all 1024 per-program fitness/error/timeout/fallback/node rows equal E09 for every task. Native short search: three variation seeds × three tasks × four generations; final rows and CPU top-16 match previous optimized runs. Fresh-bank final chromosomes match E18; every final AST is exported, independently re-admitted and rescored with AST/handle equality.
- Every diagnostic generation scores 1,048,576 program/case pairs, with zero timeouts and unscored cases. Native runtime-error fractions are 11.35%/0%/9.89%; changed operator-output fractions are 88.51%/90.59%/90.92% (not final unique offspring). Final genome diversity is 887–1021 of 1024. This is a short screen with the same frozen initial parents, not proof of convergence equivalence. Per-task/seeds fitness, size, counters and limitations are in `results/final.json`; raw results in `logs/optimization/candidate-validation/`.
- Stop after testing distinct host-proof, allocation, VM, memory-layout, scheduling, case-layout, precision, dispatch and representation directions. Several remaining instruction/worker tweaks have plateaued or regressed. The radical phase-bank route reduces host work but has a demonstrated quality limitation; no measured candidate supports promoting it to the general backend. The Sum/House 40× goal remains unfinished. Median improves by 1.50×, but its 0.35× ASGP ratio still leaves substantial host overhead.
- If resumed, start from this runtime and frozen snapshots. The next unresolved architecture question is whether a general grammar-proof-carrying phase/IR representation can avoid AST round trips **without** the independent-hole restriction and quality loss. A small prototype must first establish admissible variation, effective fresh offspring and complete-generation cost, before GPU-resident evolution or a broad rewrite. Existing evidence does not establish its performance or justify treating it as an achieved optimization. For Sum, it must also address bounded-region evaluation; host removal alone has not demonstrated the target.

## Experiments

Historical single-batch numbers below locate decisions; use the repeated final measurements above for headline results.

| ID | Hypothesis / change | Result | Decision |
| --- | --- | --- | --- |
| E00 | Preserved binary controls | Sum 805.0 / House 765.1 / Median 263.3 ms | reproduced |
| E01 | Parallel bounded verification + staging capacity reuse | First run exposed a second capacity guard in copyback; fixed consistently | superseded by E01b |
| E01b | Same checks in parallel, staging allocation dimensions only | 742.3 / 681.5 / 207.1 ms; all non-timing outputs identical; parity + bounded-region tests pass | retain; final validation passed |
| E02 | Verifier-proven phase stack <=16 and locals <=8 use smaller interpreter storage; generic fallback otherwise | 532.7 / 554.9 / 210.2 ms; kernel 357.6 / 354.1 / 9.3 ms; non-timing fields identical; bounded-region and parity pass | retain; final per-program gate passed |
| E03 | Transpose generic region frame fields across lanes | Sum unchanged; House 655.6 ms (kernel 451.6 vs 354.1), clear regression; parity passed | REJECTED; patch saved in logs/optimization/E03/prototype.patch |
| Diagnostic | NCU 2022.3 minimal four metrics | Driver reports unsupported API on kernel launch; no metrics collected, no system changes | use nsys and targeted A/B |
| E04 | Opaque membership/verified-AST certificate plus reuse of admission bytecode | 511.4 / 515.4 / 173.6 ms; all output fields identical | superseded: primary BM keeps per-generation compilation explicitly |
| E04b | Keep membership proof only; exact AST/payload identity, grammar/request validation; full compile each generation | 518.8 / 531.4 / 177.3 ms; stale AST/constant/request and forged provenance tests + payload evolution pass | retain; final validation passed |
| D01 | Fixed-program snapshot evaluator with optional per-case counters; 4-generation search diagnostics | All 1024 per-program fitness/count rows equal between generic and small VM, all 1024 cases each. Sum errors 453208; House 0; Median 275609; all timeouts/fallback tokens 0 | diagnostic gate; no speedup claimed |
| E05 | Opt-in read-only IntList views, proven by forward type analysis, generic fallback otherwise | 472.5 / 481.6 / 180.4 ms; kernels 310.7 / 304.2 / 4.2. Sum errors decrease 453208→318332, 192 program fitness changes; House/Median exact per-program agreement. Full CPU comparison and known solutions recorded | retain opt-in; final quality screen passed |
| E06 | Type-proven phase VM with unboxed payloads; ordinary integer conversion semantics/fuel retained | 379.1 / 346.1 / 179.3 ms; kernels 213.4 / 168.7 / 3.8. All per-program fitness and diagnostic counts match E05; refreshed nsys Sum/House recorded | retain prototype; host now roughly half of Sum/House |
| E07 | Stable length ordering of all cases, only type-proven integer fitness with exact bounded sum; setup included in session initialization | 282.7 / 243.6 / 177.0 ms; kernels 118.2 / 70.7 / 4.0; all 1024 per-program rows exactly equal to E06 | retain opt-in; host now dominant |
| E08 | Carry complete variation analyses across decode/preparation, exact identity and budget checks | 284.8 / 249.0 / 181.9 ms; no end-to-end gain, extra copy/key retention cost | REJECTED; patch and binary saved |
| E09 | Structurally bounded 1-state/1-preparation/2-request frames (72 vs 264 bytes), same frame/memo limits | 286.3 / 223.7 / 176.3 ms; House kernel 49.3 vs 70.7; Sum unchanged; fixed rows equal | retain opt-in `GAGP_COMPACT_FRAMES=1` |
| E10 | Delay overlap preparation until compilation finishes | 280.6 / 219.9 / 184.7 ms; Sum/House changes small, Median regresses | REJECTED; preserve original overlap scheduling |
| E11 | Cap worker teams at 2/4/8/10 vs original 20 | Every cap slows all workloads; 2 workers about 458/430/366 ms | REJECTED; CPU computation, not oversubscription alone, dominates |
| E12c | Immutable whole-phase bank; grammar-compatible independent holes; tournament, phase crossover and 0.3 finite-bank mutation; all cases, full pack verification | Setup 31–36 ms; reproduction ~0.3 ms; initial fitness exactly matches E09. Four-generation outputs all re-admitted and AST/handle fitness equal. Effective changes ~957–1002/1023; no timeout/fallback. Timings diagnostic only | retain isolated prototype; final repeats/quality below, not a 40× claim |
| E13 | Force-inline typed phase interpreter | 285.6 / 223.9 / 177.6 ms; no gain | REJECTED |
| E14 | Fuel-preserving constant-only phase folding, bounded integer domain | 282.4 / 223.5 / 176.4 ms; compile overhead offsets tiny kernel gain | REJECTED |
| E15 | Strided traversal of sorted full cases; 512/256/128/64 thread sweep | 512 best Sum/House: 271.0/213.8 ms, kernels 109.1/42.1. Smaller blocks regress; Median difference negligible in full generation | retain strided traversal; remove thread override prototype |
| E16 | Binary exact phase identities and hash interning; phase-bank startup attribution | Startup 25.2/27.3/25.8 ms vs 31–36 ms. Analysis+compile 18–20 ms, skeleton ~2 ms, interning ~5 ms | retain in isolated phase-bank prototype; no primary benchmark speedup claim |
| E17 | NCU 2024.3 minimal metrics | Launcher works, but ERR_NVGPUCTRPERM blocks hardware counters | no system changes; nsys and targeted A/B continue |
| E21 | Generation-owned analysis handoff independent of the 4096-entry FIFO capacity | P8192 overlap 2012/1586/1319 ms versus 6307/6596/6852 ms; every non-timing field equal; capacity-3 ownership/eviction test passes | retain opt-in; repeated controls agree; peak RSS increases by ~0.74/0.33/0.76 GiB (Sum/House/Median) |
| E18 | Fresh grammar-generated whole-phase mutation | Reproduction ~5 ms/generation, 70–145 novel phases; all cases, final AST export/fitness audit passes. Three seeds: Sum best 0; House -8479/-8479/-8443; Median stays -31025. No timeout/unscored/failed mutation | retain isolated prototype; Median quality limitation persists |
| E19 | Verifier-proven smaller root VM, 16 stack / 8 locals | 276.9/219.4/176.1 ms; no meaningful end-to-end improvement; targeted GPU tests pass | REJECTED; patch/binary saved |
| E20 | Explicit Int32 phase arithmetic, same fuel and full cases | Kernel ~94/41/4 ms, no gain; 21 Sum and 36 House program fitness rows change; no timeout increase or false-perfect programs vs full CPU snapshot | REJECTED; overflow/division/list-cast/fuel tests passed; patch/binary preserved |
| E22c | Compact two-state frames (88 bytes), retaining one-state 72-byte layout | Sum ~261 ms / kernel 93 ms; House ~218 ms. All 1024 per-program fitness/counters identical to E09; 2D memo and GPU parity tests pass | retain; final repeats/scaling passed |
| E23b | Proven scalar constant phases bypass interpreter, preserving explicit/implicit return fuel | Sum kernel ~86 ms vs ~91 ms; full generation ~248 ms; House/Median near noise | retain opt-in; nine-sample median 250.318/217.495/176.443 ms; Sum improves 254.412→250.318 ms; others within noise |
| E24 | Fuse typed-VM operand load with arithmetic/comparison, preserving both fuel checks and jump targets | Sum kernel ~90 ms vs ~91 ms; combined result attributable to E23; no material generation gain | REJECTED; patch/binary saved |

## Reproduction

Run from the experimental worktree. Preserved original inputs are a read-only-by-convention symlink into the original worktree; never run `prepare` over them. Use a new output directory for each measurement. GPU work must run serially.

```sh
cmake -S cpp -B cpp/build/release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=86 -DGAGP_BUILD_BENCHMARKS=ON \
  -DGAGP_ASGP_SOURCE_DIR=/home/hschi1106/r13921069
cmake --build cpp/build/release -j10

export GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
export GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
export GAGP_CUDA_DEVICE=0
cpp/build/release/gagp_fixed_asgp_bench measure \
  logs/fixed-asgp/artifacts/sum_of_elements.source.json \
  logs/fixed-asgp/artifacts/sum_of_elements.p1024.prepared.json \
  logs/fixed-asgp/artifacts/sum_of_elements.grammar.json \
  1024 gpu_overlap /tmp/gagp-sum-new-run.jsonl

PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp run \
  --suite daily --out logs/fixed-asgp/new-daily
# Change daily to scaling for a fresh complete 60-cell matrix.
ctest --test-dir cpp/build/release --output-on-failure
```

Replace `gpu_overlap` with `snapshot` for full-case fixed-program diagnostics; `GAGP_SNAPSHOT_CPU=1` adds all-program CPU reference. Mode `search` runs four native generations plus final evaluation; add `GAGP_GPU_DIAGNOSTICS=1 GAGP_BM_SEED=0` (then 1/2). Mode `phase_bank` with `GAGP_BANK_FRESH_MUTATION=1` is the separately restricted prototype. Disable diagnostic counters for timing runs; never use their timings as headline results.

Control commands and exact artifact/binary identities are preserved in raw manifests. `logs/optimization/summarize_final.py` reconstructs final evidence and checks original/frozen files; `profile_final.py` and `instrument_final.py` preserve the profiler procedure. They are experiment-local scripts, not a new benchmark framework. Reproduce traces in a new directory. Failed prototypes and binaries remain under `logs/optimization/E03`, `E08`, `E19`, `E20`, `E24` and other experiment directories; none are in the retained runtime path.
