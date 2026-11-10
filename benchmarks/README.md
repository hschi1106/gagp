# Benchmarks

`asgp30/` is the benchmark of GAGP against the ASGP paper: the 30 program
synthesis tasks of *Grammar-Guided Program Synthesis for Algorithm-Structured
Programs* (ASGP), each as a ready-to-run `gagp run` task. `run.py` runs the
suite and writes a report with the paper's published success counts next to
GAGP's. Only GAGP runs here; ASGP itself is not needed.

## Run it

Build GAGP first ([README](../README.md#build)), then from the repository root:

```bash
python3 benchmarks/run.py run --quick                # smoke test: 6 tasks, 1 seed, 16 generations
python3 benchmarks/run.py run                        # the full suite: 30 tasks x seeds 1-5, P1024, G64
python3 benchmarks/run.py run --engine cpu --quick   # the same quick runs on the CPU reference engine
python3 benchmarks/run.py report                     # rewrite logs/bench/asgp30/report.md
```

On an RTX 3090 the quick run takes about 10 seconds and the full suite about
9 minutes. The CPU reference engine is 5 to 185 times slower on one thread
(its quick run takes about 14 minutes), so use it with `--quick` or a few
`--tasks`; `--jobs N` runs N CPU runs at once, which disturbs their timing.

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
| `--search FILE` | built-in default | Search configuration ([reference](../docs/reference/cli.md#search-configuration)) |
| `--jobs N` | 1 | Parallel runs, CPU engine only |
| `--out DIR` | `logs/bench/asgp30` | Output directory |
| `--gagp PATH` | `cpp/build/gagp` | The `gagp` executable |

## Reading the report

For each engine and configuration, `report.md` lists per task:

- **GAGP solved**: runs whose best program answers every held-out test case.
  This is the success criterion of the ASGP paper.
- **Train perfect**: runs whose best program makes no training error (it may
  still fail test cases).
- **Median time**: wall-clock seconds of a whole run, including initialization.
- **ASGP paper /100** and **ASGP thesis /25**: the published success counts.
  The short paper (Table III) reports 100 runs per task; the thesis
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
`test.cases.json`; `asgp_paper.json` holds the published success counts. 24
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
  Levenshtein distance for list outputs (with a 65,536-cell list limit), or
  tuple summary; the case-error cap is 1000.

These files were produced from the ASGP generator output by the GAGP 2.x
fixed-ASGP adapter (git tag `gagp-pre-3.0`), the same inputs as GAGP's final
scale study.

## Recorded results

Release 3.0 on an RTX 3090 (`python3 benchmarks/run.py run`, then the quick
runs on both engines):

- Full suite, GPU, P1024, G64, seeds 1-5: 56/150 runs solved, 524 s of runs in
  total. GAGP's final scale study (ASGP-initialized populations, otherwise the
  same search) solved 52/150.
- Quick runs, GPU vs CPU reference (P1024, G16, seed 1): the same result on all
  6 tasks; speedup from 5.2x (the median task) to 185x (double_letters), geometric mean
  30x, 129x in total time.
