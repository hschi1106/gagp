# GAGP

**Grammar-guided genetic programming on the GPU.**
[繁體中文說明](README.zh-TW.md)

GAGP searches for programs that solve a task given by input/output examples.
You describe the space of candidate programs with a grammar, and GAGP evolves a
population of programs from that grammar with tournament selection and typed
subtree crossover and mutation. The whole search loop runs on the GPU. A plain
C++ reference engine runs the same operators on the CPU and publishes identical
populations, fitness values and champions, so every speedup is measured on the
same search.

## Quick start

You need Linux, CMake 3.16+, a C++17 compiler and, for the GPU engine, an
NVIDIA GPU with the CUDA toolkit (verified: RTX 3090, CUDA 12.6, GCC 11.4).

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j --target gagp
cpp/build/gagp run examples/polynomial/task.json --out runs/polynomial
```

```text
gagp run polynomial: engine=gpu population=1024 generations=64 seed=0 cases=21
best fitness 0, wrong 0 of 21 train cases, 13 nodes; 0.4 s
program: (add 1 (mul x (sub (mul x x) 2)))
test: 40 of 40 correct (solved)
```

GAGP found a program for `x³ - 2x + 1` from 21 examples and checked it on 40
held-out ones. `runs/polynomial/result.json` holds the program, its scores, the
per-generation history and the exact search settings.

If CMake cannot find `nvcc`, set `CUDACXX=/usr/local/cuda/bin/nvcc`, and add
`-DCMAKE_CUDA_ARCHITECTURES=86` (your GPU's compute capability) to build for one
GPU. Without a GPU, add `-DGAGP_ENABLE_CUDA=OFF` and run with `--engine cpu`.

A task is a small JSON file naming a grammar, training cases, optional test
cases and an error measure:

```json
{
  "format": "gagp-task-v1",
  "grammar": "grammar.json",
  "train": "train.cases.json",
  "test": "test.cases.json",
  "fitness": {"metric": "absolute"}
}
```

The [getting started guide](docs/guides/getting-started.md) shows how to write
your own task and grammar.

## What GAGP shows

Each claim below lists what it means, the evidence in this repository and the
commands that reproduce it. Measurements are from one RTX 3090 with the default
search, and the linked records give the population size, generations and seeds.
GAGP does not
claim that its search is better than ASGP's, that it reaches the state of the
art on PSB1, or that it reproduces the numbers of the ASGP paper. The published
counts that the benchmark reports print are context, not a matched baseline.

### 1. The whole loop runs on the GPU

Selection, crossover, mutation, compilation of new programs to bytecode and
evaluation on every case all run on the device, including for programs that
work on strings and lists. The host only ranks the fitness vector and records
history. The design is in [gpu-reproduction.md](docs/design/gpu-reproduction.md)
and the string and list storage in [payload.md](docs/design/payload.md).

Evidence: the resident engine and its tests (`ctest -L gpu`), and the
`eval_ms` and `repro_ms` of every generation in `result.json`. The `asgp30`
tasks work on lists and the `psb1_g3p` tasks on strings and lists. The string
task needs the case files of claim 2:

```bash
cpp/build/gagp run benchmarks/asgp30/filter_odd/task.json --generations 16
cpp/build/gagp run benchmarks/psb1_g3p/small_or_large/task.json --search benchmarks/psb1_g3p/small_or_large/search.json --generations 16
```

### 2. User-defined grammars run

The search space is a grammar file, not a fixed instruction set. Two very
different families run on the same engine:

- **Algorithm-structured templates.** The 30 tasks of ASGP
  ([`benchmarks/asgp30`](benchmarks/README.md)): divide-and-conquer and
  dynamic-programming schemes with bounded recursion, filled by operator sets.
- **Statement grammars in the style of published G3P work.** Six PSB1 problems
  ([`benchmarks/psb1_g3p`](benchmarks/psb1_g3p/README.md)): assignments, `if`,
  `for` and `while` loops and list and string builtins, ported from the
  grammars of Forstenlechner's G3P.

The language has what these grammars need:

- **Bounded `while`.** `while_stmt(Int, Bool, Block)` runs its body until the
  condition is false or the bound is reached, then leaves normally
  ([program grammar](spec/grammar.md#program-grammar)).
- **List and string builtins.** `insert`, `remove_at`, `set_at`, `index_mod`,
  list `contains`, `range`, `strip`, `lstrip`, `rstrip`, `startswith`,
  `endswith` and `capitalize` run on both engines
  ([list builtins](spec/builtins_runtime.md), [string builtins](spec/builtins_base.md)).
- **An allocation rule.** When a task sets `fitness.list_allocation_limit`,
  every list and string a program creates and every input counts toward it, and
  exceeding it is a scored case error. Physical memory exhaustion is never a
  score and stops the run ([task file](spec/task.md#task-file),
  [allocation boundary](spec/fitness.md#cpu-payload-allocation-boundary)).

Both suites are run by `benchmarks/run.py`. The PSB1 case files come from a
PonyGE2 checkout, which the second command converts:

```bash
python3 benchmarks/run.py run --quick
git clone https://github.com/PonyGE/PonyGE2 /path/to/PonyGE2
python3 benchmarks/psb1_g3p/build.py cases --data /path/to/PonyGE2
python3 benchmarks/run.py run --suite psb1_g3p --quick
```

The grammar format is explained in the
[grammar authoring guide](docs/guides/grammar-authoring.md).

### 3. It is fast

- **Against GAGP's CPU reference engine.** On the 30 ASGP tasks at a
  population of 1024 the GPU runs the same searches 14x to 298x faster than
  one CPU thread (geometric mean 108x), and on the six `psb1_g3p` problems
  2.1x to 8.7x faster, with identical results.
- **Against ASGP on one CPU thread.** On frozen ASGP-initialized populations
  (30 tasks, 64 generations), single-threaded ASGP took 46x longer than GAGP
  at a population of 1024 and 59x at 4096 (geometric mean over the tasks,
  mean of 5 seeds), and 73x at 16384 (one seed). ASGP times are corrected for
  machine contention. ASGP and these populations are not part of this
  repository, so this comparison is recorded but not reproducible here
  ([details](docs/design/research-mainline.md#results)).

Run both engines on the same tasks and compare them. The report lists the
speedup per task and checks that both engines found the same best program with
the same fitness history. The CPU engine is slow, so select a few tasks:

```bash
python3 benchmarks/run.py run --quick
python3 benchmarks/run.py run --quick --engine cpu --tasks median,smallest
python3 benchmarks/run.py report
```

The native test that compares the two engines on complete populations:

```bash
ctest --test-dir cpp/build -R gagp_test_phase_population_cpu_reference --output-on-failure
```

Report speedups from a Release build, without a profiler attached.

### 4. Quality does not get worse

The speed is not bought with search quality. The search configuration decides
which programs are explored, and the engine never does, so an optimization is
accepted only when exported champions and histories stay identical to the
previous build. On the 30 ASGP tasks (population 1024, 64 generations) the GPU
engine solves 56 of 150 runs (5 seeds per task, judged on held-out test cases),
and `report.md` prints the published ASGP success counts next to GAGP's
([recorded results](benchmarks/README.md#recorded-results)).

```bash
python3 benchmarks/run.py run
python3 benchmarks/run.py report
```

The full run takes about 9 minutes on the GPU. `--engine cpu` runs the same
searches and reaches the same counts, only slowly.

On the six `psb1_g3p` problems with the published G3P settings (100 runs each),
GAGP is level with the published counts on three problems and below them on
three. The counts and the cause found for `median` are in the
[`psb1_g3p` results](benchmarks/psb1_g3p/README.md#recorded-results).

## Limits

- **No unbounded loops.** The language has `for_range`, bounded `while_stmt`,
  traversals of a list or string and bounded recursion regions with frame, memo
  and fuel limits. Fuel bounds every program, and a program that runs out of
  fuel fails that case.
- **Admission capacities.** The resident engine holds the population in
  fixed-size genes, so a grammar must fit its limits: at most 2048 nodes and
  depth 64 per program, 1024 nodes per varied phase, a population of at most
  16384, and more
  ([resident capacities](spec/search_config.md#resident-capacities)).
- **Language restrictions.** The resident engine rejects two constructs of the
  grammar language: `sequence` constant domains and a nonterminal reached under
  two different lexical binder environments. They are listed once in
  [language restrictions](spec/search_config.md#language-restrictions).
- **Rejection is an error.** A grammar that cannot be admitted stops `gagp run`
  before the first generation with exit status 2 and a message that names the
  construct or capacity and the offending value. There is no fallback engine.
- **Smaller limits of the language.** Selection is tournament only, lists are
  never nested and there are eight value types
  ([types](docs/guides/grammar-authoring.md#anatomy)).

## Commands

| Command | Purpose |
| --- | --- |
| `gagp run TASK.json` | Search for a program (`--population`, `--generations`, `--seed`, `--engine gpu\|cpu`, `--search`, `--init`, `--out`, `--save-all`) |
| `gagp eval TASK.json PROGRAM.json` | Score a program on the test or training cases |
| `gagp grammar validate\|inspect\|resolve` | Check and explore a grammar |
| `gagp grammar generate` | Generate an initial population |
| `python3 benchmarks/run.py run [--suite asgp30\|psb1_g3p]` | Run a benchmark suite |

Search parameters live in one file, [`configs/search/default.json`](configs/search/default.json),
and `--search` takes a file with just the members to change. Every option is in
the [command reference](docs/reference/cli.md).

## Documentation

| Document | Covers |
| --- | --- |
| [Getting started](docs/guides/getting-started.md) | Build, the example, your own task, troubleshooting |
| [Grammar authoring](docs/guides/grammar-authoring.md) | Writing grammars: constants, scopes, templates, recursion |
| [Command reference](docs/reference/cli.md) | Every command, option and setting |
| [Benchmarks](benchmarks/README.md) | The `asgp30` and `psb1_g3p` suites and recorded results |
| [Research mainline](docs/design/research-mainline.md) | The claims, what exact means, measurements, retired designs |
| [Architecture](docs/design/architecture.md) | Components and how one run flows through them |
| [Specifications](spec/README.md) | Normative formats and semantics |
| [Documentation index](docs/README.md) | Every document |

## Repository

`cpp/` holds the C++/CUDA implementation and its tests, `spec/` the
specifications, `docs/` the guides and design notes, `configs/` grammars and
the default search, `examples/` runnable tasks and `benchmarks/` the benchmark
suites ([layout](docs/reference/repository-layout.md)).

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug && cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

See [development](docs/guides/development.md) for tests, presets and how to
make changes, and [VERSION.md](VERSION.md) for the release (3.0.1).
