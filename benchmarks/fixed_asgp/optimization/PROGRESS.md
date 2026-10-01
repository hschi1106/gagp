# Native 1T optimization checkpoint — 2026-10-02

Active objective: preserve CPU / GPU-e / GPU-er / GPU-er-o architecture, use
CPU 1T + GPU, target fastest native GPU >30× frozen ASGP-1T on Sum/House;
Median remains fixed-overhead workload. Do not resume retired CPU fragment route.

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
A strace clone/backtrace smoke records only CUDA driver thread creation, no GAGP
host workers. Logs are alongside the baseline.

Next experiment hypothesis: membership already lowers admitted ASTs; retaining
that existing bytecode in the opaque native derivation certificate can eliminate
recompilation without changing AST/genotype or GPU reproduction. Start scalar
constants only; mutable payload tokens must not be accidentally retained/reused.
Measure evolve-call/cold cost as well as generation, and actual changing search.
CPU perf sampling currently blocked by perf_event_paranoid=4; optional user
request pending. Continue using existing stage timers; no system settings changed.

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
