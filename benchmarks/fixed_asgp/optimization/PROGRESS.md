# Autonomous optimization checkpoint

- Workspace: `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`
- Branch: `opt/gpu-generation-20261001`; original user worktree untouched.
- Starting checkpoint: first commit on this branch, snapshot of existing uncommitted benchmark/profiler artifacts.
- Baseline executable: `logs/optimization/baseline_bench` (hash in `logs/optimization/origin.json`). Frozen inputs symlink to original artifacts; never modify them.
- Reference ASGP 1T: Sum 1841.160175 ms, House 1282.011816 ms. Targets <46.029004 / <32.050295 ms per full generation (40×). These are unprofiled medians from archived profiler controls.
- `medium`: searched existing settings, results, docs and ASGP sources; no independent named workload found. Prior Median interpretation was an assumption. User clarification pending; Sum/House continue. Median remains an auxiliary overhead probe.
- Current best: E02; Sum 532.7 ms / House 554.9 ms / Median 210.2 ms. E01b retained as previous best; Median difference is not an established improvement.
- Build: `cmake --build cpp/build/release --target gagp_fixed_asgp_bench gagp_test_fitness_cpu_gpu_parity gagp_test_evolution_cpu_gpu_parity -j10`.
- Experiment results: `logs/optimization/`; compact summaries/checkpoints recorded here. GPUs run serially.

## Experiments

| ID | Hypothesis / change | Result | Decision |
| --- | --- | --- | --- |
| E00 | Preserved binary controls | Sum 805.0 / House 765.1 / Median 263.3 ms | reproduced |
| E01 | Parallel bounded verification + staging capacity reuse | First run exposed a second capacity guard in copyback; fixed consistently | superseded by E01b |
| E01b | Same checks in parallel, staging allocation dimensions only | 742.3 / 681.5 / 207.1 ms; all non-timing outputs identical; parity + bounded-region tests pass | retain, provisional |

| E02 | Verifier-proven phase stack <=16 and locals <=8 use smaller interpreter storage; generic fallback otherwise | 532.7 / 554.9 / 210.2 ms; kernel 357.6 / 354.1 / 9.3 ms; non-timing fields identical; bounded-region and parity pass | retain provisionally; need per-program diagnostic gate |

## Next

1. Parallel bounded bytecode verification (same checks inside generation), pinned staging capacity reuse; validate malformed bytecode and parity.
2. Inspect eval phase interpreter and region workspace for high-impact compact execution prototype, guarded by structural applicability.
3. After each meaningful candidate, fixed programs + full cases and end-to-end generations; retain only measured gains. Collect errors/timeout/fallback and short search quality before final adoption.
