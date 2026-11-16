# Benchmarks

Two suites, both run by `run.py` (`--suite asgp30`, the default, or
`--suite psb1_g3p`). They are the evidence for three of GAGP's claims: that
user-defined grammars of two very different families run, that the GPU engine is
fast, and that its search quality does not get worse. Each suite runs on either
engine (`--engine gpu|cpu`), and the report compares the two.

| Suite | What it is |
| --- | --- |
| `asgp30/` | The 30 tasks of the ASGP paper, shown beside its published success counts (below) |
| [`psb1_g3p/`](psb1_g3p/README.md) | Six PSB1 problems with the G3P grammars and settings of Forstenlechner, shown beside the published G3P counts |

`asgp30/` holds the 30 program synthesis tasks of *Grammar-Guided Program
Synthesis for Algorithm-Structured Programs* (ASGP), each as a ready-to-run
`gagp run` task. `run.py` runs the suite and writes a report with the paper's
published success counts next to GAGP's. Only GAGP runs here, and ASGP itself
is not needed. The published counts are context. They are not a matched
baseline, and GAGP does not claim that its search beats ASGP's, matches it, or
reaches the state of the art on PSB1.

## Run it

Build GAGP first ([README](../README.md#build)), then from the repository root:

```bash
python3 benchmarks/run.py run --quick                # smoke test: 6 tasks, 1 seed, 16 generations
python3 benchmarks/run.py run                        # the full suite: 30 tasks x seeds 1-5, P1024, G64
python3 benchmarks/run.py run --engine cpu --quick   # the same quick runs on the CPU reference engine
python3 benchmarks/run.py report                     # rewrite logs/bench/asgp30/report.md
```

On an RTX 3090 the quick run takes about 10 seconds and the full suite about
9 minutes. The CPU reference engine is one to two orders of magnitude slower on
one thread (its quick run takes about 14 minutes), so use it with `--quick` or a few
`--tasks`. `--jobs N` runs N CPU runs at once, which disturbs their timing.

Runs land in `logs/bench/asgp30/<engine>/p<population>-g<generations>/<task>/s<seed>/`
(`result.json`, the [`gagp run` result](../spec/task.md#run-result)).
A run that already has a `result.json` is skipped, so rerunning the same
command resumes an interrupted benchmark. Every `run` rewrites `report.md`.

| Option | Default | Meaning |
| --- | --- | --- |
| `--quick` | off | `count_odds, median, smallest, double_letters, fibonacci, house_robber`, seed 1, 16 generations (explicit options still win) |
| `--engine gpu\|cpu` | `gpu` | `cpu` is GAGP's CPU reference engine: the same search, for speedup and exactness |
| `--tasks A,B` | all 30 | Task subset |
| `--seeds 1-5` | `1-5` | Seeds, e.g. `1-10` or `1,3` |
| `--population N` | 1024 | Population size |
| `--generations N` | 64 | Generations |
| `--suite NAME\|DIR` | `asgp30` | `asgp30`, `psb1_g3p` or a suite directory. A directory with a `literature.json` is run like `psb1_g3p`: seeds 1-100, settings from each task's `search.json`, [its own report](psb1_g3p/README.md#reading-the-report) |
| `--search FILE` | built-in default | Search configuration ([reference](../docs/reference/cli.md#search-configuration)) |
| `--jobs N` | 1 | Parallel runs, CPU engine only |
| `--out DIR` | `logs/bench/<suite>` | Output directory |
| `--gagp PATH` | `cpp/build/gagp` | The `gagp` executable |

## Reading the report

For each engine and configuration, `report.md` lists per task:

- **GAGP solved**: runs whose best program answers every held-out test case.
  This is the success criterion of the ASGP paper.
- **Train perfect**: runs whose best program makes no training error (it may
  still fail test cases).
- **Median time**: wall-clock seconds of a whole run, including initialization.
- **ASGP paper /100** and **ASGP thesis /25**: the published success counts.
  The short paper (Table III) reports 100 runs per task. The thesis
  (Table 5.4) reports the six dynamic-programming tasks with 25 runs each and
  the same counts. Keep the two denominators apart.

When both a GPU and a CPU run of the same configuration exist, the report adds
the GPU speedup per task and checks that both engines found the same best
program with the same fitness history.

The paper columns are a reference, not a matched baseline. The paper used
population 1000 and 500,000 evaluations (about 488 generations of 1024), and
did not publish its data files, data seeds or training-set sizes. This suite
uses 1024 training cases, so a task can be easier or harder here than in the
paper. Compare GAGP configurations with each other on this suite, and quote
the paper only as context.

## Search settings

The default search ([`configs/search/default.json`](../configs/search/default.json))
aligns the parameters that ASGP also has: tournament of 2, crossover 0.7,
mutation 0.3, one phase drawn per operator, phase depth 7, elitism 1,
case-error cap 1000 and fuel 2,000,000. Two GAGP choices differ from ASGP:
the population schedule (synchronous steady state with an inverse tournament
of 2) and the initial population, which GAGP generates itself from the
grammar and the seed.

## The `asgp30` suite

Each task directory holds `task.json`, `grammar.json`, `train.cases.json` and
`test.cases.json`. `asgp_paper.json` holds the published success counts. 24
tasks use ASGP's divide-and-conquer scheme and 6 its dynamic-programming
scheme.

Provenance:

- **Cases** come from ASGP's own task generator (`bench dataset`, data seed
  20261103). Training cases are the first 1024 rows of the generated training
  pool, duplicates kept. The test split holds up to 1000 unique inputs from the
  generated test pool that do not occur in training. Five tasks have finite
  input domains (fibonacci, tribonacci, even_squares, sum_of_squares,
  fibonacci_array): there, 20% of all unique inputs are reserved for test and
  removed from training, which leaves 6 to 34 test cases.
- **Inputs**: the sequence is `source` (strings as character codes), task
  scalars are `scalar0`, `scalar1`, ..., and the DP tasks other than
  house_robber also receive `start_a`, `start_b`, `extent_a`, `extent_b`.
  Tasks with a predicate answer (all_equal, is_sorted) expect
  `[answer, summary1, summary2]` and score it with `tuple_summary`.
- **Grammars** are ASGP's task grammars (scheme template plus the
  `list-combine-v1` operator set) written as GAGP `grammar-definition-v2`
  files: at most 2048 nodes, depth 24, fuel 2,000,000.
- **Fitness** follows ASGP's per-task metric: absolute error, exact match,
  Levenshtein distance for list outputs (with a limit of 65,536 list cells per
  case, input lists included), or tuple summary. The case-error cap is 1000.

These files were produced from the ASGP generator output by the fixed-ASGP
adapter of an earlier release (git tag `gagp-pre-3.0`).

## Recorded results

The numbers below are for the commit that the report names. The release history
of earlier measurements is in [VERSION.md](../VERSION.md).

<!-- FREEZE-RESULTS -->

One RTX 3090 and an Intel i9-10900K, commit `89b515db`, Release build without
a profiler.

**Default search.** `python3 benchmarks/run.py run` (30 tasks, seeds 1 to 5,
population 1024, 64 generations) solves 56 of 150 runs in about 9 minutes
(506 s of run time).

**Against ASGP on one CPU thread.** The searches start from frozen
ASGP-initialized populations and run 64 generations. ASGP and these
populations are not part of the repository, so this table is recorded and not
reproducible here. Each cell is the geometric mean over the 30 tasks of the
ASGP time divided by the GAGP time.

| Population | Seeds | Mean over seeds | Lowest seed | Highest seed |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 5 | 46.5 | 43.6 | 49.2 |
| 4096 | 5 | 58.7 | 55.9 | 62.4 |
| 16384 | 1 | 73.0 | | |

The ASGP times of seed 1 were measured alone where available. The others are
times from 8 concurrent ASGP runs divided by the measured contention factor
1.260. Every GAGP run has the same champion and the same best program in every
generation as the engine before release 3.0 on the same population.

**Against the CPU reference engine.** On the same populations at 1024 members
and seed 1, the GPU is 14 to 298 times faster than the CPU reference engine on
one thread (geometric mean 108, CPU 16,594 s and GPU 102 s over the 30 tasks),
and the 30 pairs of results are identical. Nine CPU runs shared the machine,
one pinned core each.

**Where the time goes.** Shares of the search time summed over the 30 tasks
(seed 1). Initialization is the time before the first evaluation: admission,
the GPU session and the initial population.

| Population | Evaluation | Reproduction | Initialization | Other |
| ---: | ---: | ---: | ---: | ---: |
| 1024 | 91.1% | 2.4% | 4.7% | 1.8% |
| 4096 | 91.0% | 1.9% | 5.0% | 2.1% |
| 16384 | 90.9% | 1.9% | 5.2% | 2.0% |
