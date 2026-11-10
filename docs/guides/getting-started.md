# Getting Started

This guide builds GAGP, runs the included example, explains the output, and
shows how to set up a problem of your own. Run every command from the
repository root.

## 1. Build

You need Linux, CMake 3.16 or newer and a C++17 compiler. For the GPU engine
you also need an NVIDIA GPU with a driver and the CUDA toolkit (verified with
an RTX 3090, CUDA 12.6 and GCC 11.4).

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release
cmake --build cpp/build -j --target gagp
```

This produces `cpp/build/gagp`. CMake builds for the GPUs that `nvidia-smi`
reports; add `-DCMAKE_CUDA_ARCHITECTURES=86` (or your GPU's compute capability)
to choose explicitly, and `-DCMAKE_CUDA_COMPILER=/usr/local/cuda/bin/nvcc` if
`nvcc` is not on `PATH`.

Without a GPU, build with `-DGAGP_ENABLE_CUDA=OFF` and add `--engine cpu` to
every `gagp run` below. The CPU engine gives the same results, only slower.

## 2. Run the example

[`examples/polynomial`](../../examples/polynomial) asks for a program computing
`f(x) = x³ - 2x + 1` from 21 training cases (`x` from -10 to 10), using `x`,
the constants 1, 2 and 3, and `add`, `sub` and `mul`:

```bash
cpp/build/gagp run examples/polynomial/task.json --out runs/polynomial
```

```text
gagp run polynomial: engine=gpu population=1024 generations=64 seed=0 cases=21
best fitness 0, wrong 0 of 21 train cases, 13 nodes; 0.4 s
program: (add 1 (mul x (sub (mul x x) 2)))
test: 40 of 40 correct (solved)
```

The search evolved a population of 1024 programs for 64 generations. Fitness is
the negative total error on the training cases, so 0 means every training case
is right. The best program is then judged on 40 held-out test cases. Running
the same command again, or with `--engine cpu`, prints the same program.

`runs/polynomial/result.json` records the run: the settings, the complete
search configuration, the best program (`best.text` as above and
`best.program` as JSON), the test verdict and a per-generation history
([format](../../spec/task.md#run-result)).

## 3. Set up your own problem

A problem is three small JSON files next to a task file.

**Cases** (`train.cases.json`, and optionally `test.cases.json`) give inputs
and expected outputs ([format](../../spec/fitness_cases.md)):

```json
{"format_version": "fitness-cases", "cases": [
  {"inputs": {"x": {"type": "int", "value": 2}}, "expected": {"type": "int", "value": 5}},
  {"inputs": {"x": {"type": "int", "value": 3}}, "expected": {"type": "int", "value": 15}}
]}
```

Value types are `int`, `float`, `bool`, `char`, `string`, `int_list`,
`float_list` and `string_list`.

**The grammar** (`grammar.json`) defines which programs the search may build:
its inputs, its building blocks, and size limits. Start by copying
[`examples/polynomial/grammar.json`](../../examples/polynomial/grammar.json):
each alternative of a nonterminal is a choice with a sampling weight, such as an
input, a constant domain or an operation like `"add(Int,Int)->Int"` whose
arguments refer to nonterminals. The
[grammar authoring guide](grammar-authoring.md) explains the format and its
larger features (lists, statements, loops, recursion templates).

**The task** (`task.json`) ties them together and picks the error measure
([format](../../spec/task.md)):

```json
{
  "format": "gagp-task-v1",
  "grammar": "grammar.json",
  "train": "train.cases.json",
  "test": "test.cases.json",
  "fitness": {"metric": "absolute"}
}
```

Use `absolute` for numbers, `exact` for right-or-wrong answers, and
`levenshtein` for strings and lists. Check the grammar, then run:

```bash
cpp/build/gagp grammar validate --grammar-definition my-task/grammar.json
cpp/build/gagp run my-task/task.json --out runs/my-task
```

## 4. Common options

```bash
# A larger, longer search with another seed
cpp/build/gagp run my-task/task.json --population 4096 --generations 200 --seed 7

# Score a program from a result on the test (default) or training cases
cpp/build/gagp eval my-task/task.json runs/my-task/result.json --split train

# Save the final population and each generation's best, then continue from it
cpp/build/gagp run my-task/task.json --out runs/a --save-all
cpp/build/gagp run my-task/task.json --init runs/a/population.json --out runs/b

# Change search settings with a search configuration file
cpp/build/gagp run my-task/task.json --search my-search.json
```

A search configuration only lists what it changes; for example
`{"format": "gagp-search-config-v1", "mutation": {"probability": 0.5}}`. All
options and settings are in the [command reference](../reference/cli.md).

## Troubleshooting

| Symptom | What to do |
| --- | --- |
| CMake cannot find the CUDA compiler | Pass `-DCMAKE_CUDA_COMPILER=/path/to/nvcc`, or build without CUDA (`-DGAGP_ENABLE_CUDA=OFF`) |
| `gpu evaluation requested but CUDA is unavailable in this build` | The build has no CUDA; use `--engine cpu` |
| `no kernel image` or `invalid device function` | Rebuild with `-DCMAKE_CUDA_ARCHITECTURES=` set to your GPU (86 for RTX 3090, 89 for RTX 4090) |
| The build is killed | Use fewer build jobs, for example `-j 2` |
| A grammar is rejected | Run `gagp grammar validate` and follow the reported field; compare with the examples in `configs/grammar/examples/` |
| Case inputs do not match the grammar | Input names and types in the cases must equal the grammar's `inputs`, and the expected type its entry type |
| The run prints `resident engine declined this grammar` | The grammar exceeds the GPU engine's capacity; the run continues correctly but more slowly ([details](../reference/cli.md#representations)) |
| A specific GPU should be used | Set `GAGP_CUDA_DEVICE=N`; by default the least used GPU is chosen |

A short search that does not solve a problem is not an error: try a larger
population, more generations, other seeds, or a grammar with better building
blocks.
