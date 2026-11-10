# GAGP

**Grammar-guided genetic programming on the GPU.**
[繁體中文說明](README.zh-TW.md)

GAGP searches for programs that solve a task given by input/output examples.
You describe the space of candidate programs with a grammar (its inputs,
constants, operations, statements, loops and bounded recursion schemes), and
GAGP evolves a population of programs from that grammar with genetic
programming: tournament selection, typed subtree crossover and mutation.

The whole search loop runs on the GPU. Selection, crossover, mutation,
compilation of new programs to bytecode and their evaluation on every case all
happen on the device; the host only ranks the fitness vector. A plain C++
reference engine runs the same operators on the CPU, and for the same inputs
and seed both engines produce bit-identical results, so speed never changes
the search.

## Results

On one RTX 3090, with the default search (population 1024, 64 generations):

- **Speed.** The GPU engine runs the same searches 5x to 185x faster than the
  single-threaded CPU reference engine (geometric mean 30x), with identical
  results.
- **Benchmark.** On the 30 program-synthesis tasks of the ASGP paper
  ([`benchmarks/asgp30`](benchmarks/README.md)), GAGP solves 56 of 150 runs
  (5 seeds per task), judged on held-out test cases.

Details and the research context are in
[research-mainline.md](docs/design/research-mainline.md).

## Build

Requirements: Linux, CMake 3.16+, a C++17 compiler, and for the GPU engine an
NVIDIA GPU with the CUDA toolkit (verified: RTX 3090, CUDA 12.6, GCC 11.4).

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j --target gagp
```

Without a GPU, add `-DGAGP_ENABLE_CUDA=OFF` and run with `--engine cpu`.

## Quick start

```bash
cpp/build/gagp run examples/polynomial/task.json --out runs/polynomial
```

```text
gagp run polynomial: engine=gpu population=1024 generations=64 seed=0 cases=21
best fitness 0, wrong 0 of 21 train cases, 13 nodes; 0.4 s
program: (add 1 (mul x (sub (mul x x) 2)))
test: 40 of 40 correct (solved)
```

GAGP found a program for `x³ - 2x + 1` from 21 examples and checked it on 40
held-out ones. `runs/polynomial/result.json` holds the program, its scores,
the per-generation history and the exact search settings.

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

The [getting started guide](docs/guides/getting-started.md) walks through
writing your own task and grammar.

## Commands

| Command | Purpose |
| --- | --- |
| `gagp run TASK.json` | Search for a program (`--population`, `--generations`, `--seed`, `--engine gpu\|cpu`, `--search`, `--init`, `--out`, `--save-all`) |
| `gagp eval TASK.json PROGRAM.json` | Score a program on the test or training cases |
| `gagp grammar validate\|inspect\|resolve` | Check and explore a grammar |
| `gagp grammar generate` | Generate an initial population |
| `python3 benchmarks/run.py run --quick` | Run the benchmark suite (a 10-second smoke test) |

Search parameters live in one file, [`configs/search/default.json`](configs/search/default.json);
`--search` takes a file with just the members to change. Every option is in
the [command reference](docs/reference/cli.md).

## Documentation

| Document | Covers |
| --- | --- |
| [Getting started](docs/guides/getting-started.md) | Build, the example, your own task, troubleshooting |
| [Grammar authoring](docs/guides/grammar-authoring.md) | Writing grammars: constants, scopes, templates, recursion |
| [Command reference](docs/reference/cli.md) | Every command, option and setting |
| [Benchmarks](benchmarks/README.md) | The `asgp30` suite and recorded results |
| [Architecture](docs/design/architecture.md) | Components and how one run flows through them |
| [Specifications](spec/README.md) | Normative formats and semantics |
| [Documentation index](docs/README.md) | Every document |

## Repository

`cpp/` holds the C++/CUDA implementation and its tests, `spec/` the
specifications, `docs/` the guides and design notes, `configs/` grammars and
the default search, `examples/` runnable tasks and `benchmarks/` the benchmark
suite ([layout](docs/reference/repository-layout.md)).

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug && cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
```

See [development](docs/guides/development.md) for tests, presets and how to
make changes, and [VERSION.md](VERSION.md) for the release (3.0.1).
