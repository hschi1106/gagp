# Goal 11.5 Stage A: working implementation checkpoint

Date: 2026-09-26. Starting HEAD: `6fe14ca`. This checkpoint retains the
current grammar implementation and its Goal 11 evidence. It does not implement
Goal 11.5's three grammar restrictions or pass the original Goal 11 speedup gate.

## Source and repair

The starting worktree contained 141 modified tracked paths and 43 untracked
implementation, test, config, and documentation paths. The meeting report
`gagp_progress_report.html` was unrelated and excluded. The resulting staged
source tree has 565 files. Every one matched the source used for the remote
Release/CUDA build by SHA-256 before this note was added.

Two repairs were needed for a functioning snapshot:

- Removed the unused legacy `variation_kernels.cuh` include from GPU reproduction
  launch code. Its obsolete static shared-memory declaration prevented the
  earlier sm89 build, although no production path called that kernel. Both sm86
  full build and sm89 `gagp_gpu` compilation now succeed.
- Changed the Goal 11 performance note's links to ignored local `goals/` files
  into a tracked protocol link. Repository documentation checks now pass from
  the portable source snapshot.

The original Goal 01 source, binaries, and measurement artifacts were preserved.

## Build and integrated checks

The isolated source snapshot on Snoopy is at
`/home/hschi1106/gagp-stage-a-20260926-2y3tr2g0/source`. It was produced from
the reviewed source files, excluding the meeting HTML and local goal plans. The
build used GNU C++, CUDA 12.6.85, RTX 3090 (driver 560.35.05), Release,
`GAGP_ENABLE_CUDA=ON`, `GAGP_BUILD_BENCHMARKS=ON`,
`CMAKE_CUDA_ARCHITECTURES=86`, and `CMAKE_CUDA_FLAGS=--maxrregcount=64`.
The complete native targets, including the production CLIs and both benchmark
targets, built successfully. A second build compiled `gagp_gpu` for sm89.

| Check | Result |
| --- | --- |
| Native CTest, with CUDA device execution | 118/118 passed; 19 GPU labeled tests |
| Operational Python tools | 84/84 passed |
| Repository checks | 23/23 passed |
| Grammar `init`, `validate`, `inspect`, `resolve` | passed |
| Population generation and byte-identical replay | passed |
| Two-generation scalar evolution: CPU, GPU evaluation with CPU reproduction, GPU direct reproduction, GPU overlap | all four passed |

The remote build logs are `release-build.log`, `sm89-build.log`,
`native-tests.log`, `tool-tests.log`, and `repository-tests-fixed.log` beside
that source snapshot. `workflow-results.json` has commands and outputs. The
Release binary SHA-256 values are:

| Binary | SHA-256 |
| --- | --- |
| `gagp_evolve_cli` | `bd4e926c4d298b8bc007e6037ab70b02580847def10c4abdd318c3589f1f8614` |
| `gagp_generate_cli` | `4cf997a5c8ac3b79d4cfcbeb6c350a2015fc1151913ae0b1f866c15950d7ca0f` |
| `gagp_grammar_cli` | `8464e54417c19e72193c10df60507f237baf745e8e20bfee7119d317f80166a4` |
| `gagp_final_candidate_bench` | `59f17fcf98d2f44d75d771bafced6f0c8ec8ca34b4e8f97e5a7f0693d12a7089` |
| `gagp_final_candidate_stats` | `4fc7653a27f84c9249c5f95696334c89496743cbc836b676a6236e7126270785` |

## p1024 CPU/GPU fitness discrepancy

The fixed `simple_exp_1024-p1024` population and cases on Snoopy were evaluated
by the frozen reference adapter and this Stage A build. For each backend, all
1024 reference and Stage A fitness values match exactly. Mean fitness was
`-996.9936474650558` on CPU and `-997.6108500141229` on GPU for both versions.
Only members 202 and 727 differ across CPU and GPU by more than `1e-6`:

| Member | CPU fitness | GPU fitness |
| --- | ---: | ---: |
| 202 | -580.4711255867696 | -1024 |
| 727 | -835.5134641669138 | -1024 |

`p1024-per-member-audit.json`, the four `*-steady.json` files, and
`steady-commands.json` beside the remote source preserve this recheck. The
[Goal 11 investigation](goal-11-performance.md) also preserves direct evaluator
probes for every one of the 1024 cases of each member: all 2048 CPU/GPU
value, error, and remaining-fuel observations matched the reference within
each backend. The cross-backend difference comes from inherited bounded GPU
payload fallback behavior. The supported parity contract for this workload is
exact before/after agreement **within** each backend, with cross-backend
fallback differences disclosed; ordinary numeric CPU/GPU tolerance does not
apply to these fallback members.

## Remaining work

Stage A is a recovery checkpoint. The current Goal 11 p1024 candidate remains
well below the original speedup target, including GPU reproduction preparation
and decode costs documented in [the performance record](goal-11-performance.md).
Stage B must implement and measure the three proposed grammar restrictions.
