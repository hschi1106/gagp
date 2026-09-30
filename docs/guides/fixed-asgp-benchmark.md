# Fixed ASGP/GAGP benchmark

This document owns `fixed-asgp-v1`, the fast fixed-parent system comparison.
It complements the broader [experiment protocol](experiment-protocol.md).
It measures scoring plus reproduction, not time-to-solution or equivalent
algorithm execution. Numerical differences and different reproduction outcomes
are accepted and disclosed. Runtime semantics remain owned by `spec/`.

## Workloads and matrix

| Task | Frozen input distribution | Execution path |
| --- | --- | --- |
| `sum_of_elements` | ASGP generator, list length 1–50, values 0–100 | DC, list slicing, phase execution |
| `median` | ASGP generator, exactly three integers in −100–100 | DC base size 3: no recursive subdivision; small evaluation and fixed overhead |
| `house_robber` | ASGP generator, list length 1–20, values 0–20 | 1D DP with n−1/n−2 dependencies and memoization |

Every task has 1024 actual evaluated training cases. The generator includes its
native edge cases; natural duplicates are retained and counted, not padded by
duplicating a small fixture. Data seed is 20260626. Test cases and simplification
do not participate in timing.

Each task freezes 8192 original ASGP parents, population seed 0, ramped phase
depths 2–7 with alternating grow/full construction. Populations are prefixes of
this artifact. The adapter maps source operators without screening or replacing
parents. GAGP imports the same source expressions into a task-specific grammar.
Translated ASTs are prepared once per required prefix size and reused.

| Mode | Evaluation | Reproduction | Overlap |
| --- | --- | --- | --- |
| `asgp_1t` | original ASGP Score, sequential | original ASGP operators | none |
| `gagp_cpu` | GAGP CPU | GAGP CPU | off |
| `gpu_eval` | GAGP GPU | GAGP CPU | off |
| `gpu_repro` | GAGP GPU | GAGP GPU | off |
| `gpu_overlap` | GAGP GPU | GAGP GPU | on |

The daily suite is three tasks × P=1024 × five modes: 15 cells. Each cell is an
isolated process with one warmup and three measured repetitions of the same
fixed-parent generation. Mode order rotates across tasks. Warmup and measured
samples are retained separately. This does not assert cross-process persistent
session reuse or a continuous multi-generation steady state.

The scaling suite uses all three tasks at P=1024/2048/4096/8192: 60 cells. It is
run at optimization milestones, especially after allocation, batching or cache
changes. It does not replace the daily suite.

Daily wall-time target is 180 seconds, budget 300 seconds, excluding build and
one-time artifact preparation. Setup, process startup, input reading and reporting
belong to daily wall time even when outside generation timing. The runner finishes
the requested matrix if the budget is exceeded and reports the overrun; it does
not silently reduce cases or repetitions. A per-cell process timeout is reported
as failure (default 300 seconds for daily, 1800 for scaling, adjustable via
`--cell-timeout`). Incomplete cells never contribute a speedup.

## Timing and configuration

The timed interval starts with in-memory, unscored parents and ends when the
next population has been produced. GAGP uses native `generation_total_ms`, with
compilation, evaluation, ranking, reproduction, transfers, decoding, validation
and in-generation payload retention included. ASGP uses a wall timer around
Score, tournament selection, elite copying and native crossover/mutation.
Offspring are not evaluated again. Overlapping phase times are not added together.

Offline source generation, adapter translation, grammar compilation, initial
parent admission/copy and GPU session initialization are excluded from generation
time. GAGP's wider evolve-call time and GPU initialization time are also retained.
These scopes follow the [native timing reference](../reference/timing.md).

- Release build; fixed visible CUDA device; record CPU/GPU, build settings,
  binary/source hashes, artifacts and exact commands. Run without competing work.
- No early stop, simplification, or fitness reuse across repetitions. Native
  within-call compilation/preparation behavior remains in effect.
- ASGP: tournament 2, crossover 0.7, mutation 0.3, one-phase variation, depth 7,
  elite 1, N−1 varied children. Score's fitness uses the task's native metric.
- GAGP: tournament 2, native crossover on each parent pair, mutation 0.3,
  subtree probability 1, N children, physical nodes 1024/depth 48. Literal ranges
  and phase visibility follow the task; mutation terminal/bound weights are 4,
  other production weights 1. Offspring distributions are not claimed equivalent.
- GAGP blocksize 512 in all GPU modes. DC frames 50; DP frames/cells 21. These
  cover the declared input ranges. ASGP recursion limit is 127 (root depth zero).
- Both execution budget numbers are 2,000,000, penalty/case-error cap 1000;
  budget units and fitness definitions are not equivalent. Integer remainder,
  large-integer numeric paths and bounded GPU payload behavior can differ.
- GAGP GPU preparation/compilation can use up to 20 CPU workers. Comparisons
  are CPU+GPU system speedups versus ASGP-1T, not isolated GPU hardware speedups.

Report per-task median/min/max generation milliseconds, ASGP-1T/GAGP-mode,
GAGP-CPU/GAGP-mode and synchronous/overlapped reproduction ratios. Do not compare
un-normalized native fitness values across systems. Variation counters classify
operator outputs, not final-population counts. Native fitness APIs do not expose
per-case error/timeout counts; the report explicitly leaves these unavailable.

Known hand-authored solutions are checked against every frozen case in ASGP and
GAGP CPU before timing. GPU aggregate fitness is recorded, including nonzero
error from its accepted payload approximation. No arbitrary evolved-program
parity gate is imposed. Crashes and missing results are failures, not losses.
Short evolution/held-out quality experiments are separate from this timing suite.

The 40× target applies to Sum of Elements at 1024×1024; Median intentionally
exposes fixed overhead and is not required to meet that same ratio.

## Build and run

The optional native adapter requires the external ASGP C++20 source tree. It
links original ASGP sources without editing them. Example on the RTX 3090 host:

```bash
cmake -S cpp -B cpp/build/release -DCMAKE_BUILD_TYPE=Release \
  -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc \
  -DCMAKE_CUDA_ARCHITECTURES=86 -DGAGP_BUILD_BENCHMARKS=ON \
  -DGAGP_ASGP_SOURCE_DIR="$HOME/r13921069"
cmake --build cpp/build/release --target gagp_fixed_asgp_bench -j10
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp prepare
GAGP_CUDA_DEVICE=0 PYTHONPATH=tools python3 -m gagp_tools benchmark fixed-asgp run \
  --out logs/fixed-asgp/daily-001
```

`--artifacts` selects the reusable frozen directory. Use a new directory if the
ASGP source, adapter or grammar changes; hash mismatches are rejected. A changed
GAGP implementation can reuse compatible artifacts. `run` requires a fresh output
directory and never overwrites previous results. For scaling, first prepare with
`--suite scaling`, then run with the same option. It prepares all 8192 translated
parents and evaluates prefixes; it never filters a parent that fails admission.

Output includes `manifest.json` with commands and all samples, `environment.json`,
per-cell logs/JSONL, `summary.json`, `summary.csv`, and `report.md`. Retain raw data
under ignored `logs/`; copy reviewed compact evidence to `benchmarks/fixed_asgp/`.

Ownership: `cpp/src/bench/fixed_asgp/` handles the native adapter and timing;
`tools/gagp_tools/experiments/fixed_asgp/` handles task grammar authoring, immutable
artifact preparation, matrix execution and reports. Neither defines product
runtime semantics. Validate runner aggregation/failures with
`python3 -m unittest discover -s tools/tests -p test_fixed_asgp.py -v`.
The optional `gagp_test_fixed_asgp_bench` CTest checks all three known solutions,
population translation and five modes using eight parents and all 1024 cases.
