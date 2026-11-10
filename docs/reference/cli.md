# Command Reference

Everything GAGP does from the command line goes through one program,
`cpp/build/gagp` (see the [README](../../README.md) for the build):

| Command | Purpose |
| --- | --- |
| `gagp run TASK.json [options]` | Search for a program that solves a task |
| `gagp eval TASK.json PROGRAM.json [--split train\|test]` | Score a program on a task's cases |
| `gagp grammar validate\|inspect\|resolve --grammar-definition FILE` | Check and explore a grammar |
| `gagp grammar generate ...` | Generate a random initial population from a grammar |

Every command prints its usage with `--help`. Errors exit with status 2 and a
one-line diagnostic on standard error.

## The task file

A task names a grammar, its training cases, optional held-out test cases, and
how answers are scored. Paths are relative to the task file.

```json
{
  "format": "gagp-task-v1",
  "name": "square_plus_one",
  "grammar": "grammar.json",
  "train": "train.cases.json",
  "test": "test.cases.json",
  "fitness": {"metric": "absolute", "case_cap": 1000}
}
```

| Member | Required | Meaning |
| --- | --- | --- |
| `format` | yes | `gagp-task-v1` |
| `name` | no | Label used in output (default: the task file's directory name) |
| `grammar` | yes | `grammar-definition-v2` file ([spec](../../spec/grammar_definition.md), [authoring guide](../guides/grammar-authoring.md)) |
| `train` | yes | Fitness cases used by the search ([format](../../spec/fitness_cases.md)) |
| `test` | no | Held-out cases; `gagp run` judges the best program on them |
| `fitness.metric` | yes | `absolute`, `exact`, `levenshtein` or `tuple_summary` ([definitions](../../spec/fitness.md)) |
| `fitness.case_cap` | no | Largest error one case can add (default 1000); also the error of a runtime failure |
| `fitness.proximity` | no | `capped` (default) or `discounted` (`error/(1+error)` instead of the cap) |
| `fitness.summaries` | `tuple_summary` only | `{"count": 1 or 2, "weight": 0.1, "cap": 100}` |
| `fitness.list_allocation_limit` | no | Per-case limit on list cells a program may allocate |

Unknown members are rejected.

## `gagp run`

```text
gagp run TASK.json [--search FILE] [--population N] [--generations N] [--seed N]
                   [--engine gpu|cpu] [--init FILE] [--out DIR] [--save-all] [--quiet]
```

| Option | Default | Meaning |
| --- | --- | --- |
| `--search FILE` | built-in, identical to [`configs/search/default.json`](../../configs/search/default.json) | Search configuration ([below](#search-configuration)) |
| `--population N` | 1024, or the search file's | Population size (at least 2) |
| `--generations N` | 64, or the search file's | Number of generations |
| `--seed N` | 0, or the search file's | Random seed; the same seed and inputs give the same result |
| `--engine gpu\|cpu` | `gpu` | `cpu` is the reference engine: the same results, much slower |
| `--init FILE` | generated | Start from a saved population (`--save-all` output or `gagp grammar generate`) instead of generating one |
| `--out DIR` | none | Write `DIR/result.json` |
| `--save-all` | off | Also write `DIR/population.json` (final population, usable with `--init`) and `DIR/history.json` (best program of each generation) |
| `--quiet` | off | Print nothing on standard error |

Options given on the command line override the search file.

Without `--quiet`, the run prints its settings, the best training fitness and,
when the task has test cases, how many test cases the best program solves.

### `result.json`

| Member | Meaning |
| --- | --- |
| `format` | `gagp-run-v1` |
| `task`, `engine`, `population`, `generations`, `seed`, `train_cases` | Run settings |
| `search` | The complete effective search configuration; passing it back as `--search` repeats the run |
| `best` | `fitness`, `wrong` (wrong training cases), `nodes` and `program` of the best final program |
| `test` | When the task has test cases: `cases`, `correct`, `wrong`, `runtime_errors`, `mean_loss`, `solved` |
| `history` | Per generation: `best_fitness`, `mean_fitness`, `best_wrong`, `best_nodes`, `elapsed_ms`, `eval_ms`, `repro_ms` |
| `time_ms` | `total` (wall clock), `search`, `initialization` |
| `resident_declined` | Present only when the resident engine could not hold the grammar ([below](#representations)) |

Fitness is the negative total error over the training cases: 0 is a perfect
program.

## Search configuration

A search configuration is a `gagp-search-config-v1` JSON file. The
[specification](../../spec/search_config.md) defines every member; the default
is [`configs/search/default.json`](../../configs/search/default.json):

| Member | Default | Meaning |
| --- | --- | --- |
| `representation` | `resident` | Genotype representation ([below](#representations)) |
| `population_size`, `generations`, `seed` | 1024, 64, 0 | As the options above |
| `selection` | tournament of 2 | Parent selection |
| `elitism` | 1 | Best members copied unchanged |
| `crossover` | probability 0.7, 8 attempts | Per-child crossover gate and site retries |
| `mutation` | probability 0.3 | Per-child subtree mutation after crossover |
| `sites` | one uniformly drawn phase, counted draw | Where crossover and mutation apply |
| `limits.site_depth` | 7 | Grammar-invocation depth cap of variation (0: no cap) |
| `ranking.tie_break` | `ast_nodes` | Equal fitness prefers fewer nodes |
| `schedule` | steady state, inverse tournament of 2 | Each generation's children replace losers of an inverse tournament, never the best |
| `fitness.penalty`, `execution.fuel` | the task's `case_cap`, the grammar's fuel | Fuel may only be lowered |

A search file only needs the members it changes. For example, a plain
generational search with a larger population:

```json
{"format": "gagp-search-config-v1", "population_size": 4096,
 "schedule": {"mode": "generational"}}
```

The search configuration decides results; the engine (`--engine`, the GPU
device) never does.

### Representations

- `resident` (default): the population lives on the GPU as fixed-shape phase
  genes, and selection, crossover, mutation, lowering to bytecode and
  evaluation all run there. This is the fast path. It accepts any grammar whose
  programs fit its capacity ([admission rules](../../spec/search_config.md#resident-admission)).
- `native_ast`: a typed AST population, evaluated on the selected engine and
  reproduced on the CPU host. When the resident engine declines a grammar,
  `gagp run` reruns the same search this way automatically, prints a notice,
  and records the reason in `result.json`.

## `gagp eval`

```text
gagp eval TASK.json PROGRAM.json [--split train|test]
```

Scores one program on the task's test cases (default) or training cases, and
prints `{"cases", "correct", "wrong", "runtime_errors", "mean_loss", "solved",
"task", "split"}` as JSON. `PROGRAM.json` may be a `result.json` (its best
program), a `population.json` (its first program) or a single program. A
program that does not belong to the task's grammar is rejected.

## `gagp grammar`

```text
gagp grammar {validate|inspect|resolve} --grammar-definition FILE [--out-json FILE]
gagp grammar generate --grammar-definition FILE --cases FILE --out-json FILE
                      [--population-size N] [--seed N]
gagp grammar generate --replay-json FILE --cases FILE --out-json FILE
```

- `validate` reports the grammar's identity, entry, limits and whether its
  entry can produce a program.
- `inspect` lists the compiled nonterminals, productions, scopes, templates and
  minimum costs.
- `resolve` writes the grammar with imports resolved.
- `generate` writes `--population-size` (default 1, at most 65536) random
  programs as a `grammar-population-v2` file, usable with `gagp run --init`;
  `--replay-json` re-derives a saved population and checks it.

Without `--out-json` the first three write to standard output. Failures print a
`grammar-diagnostic-v1` document on standard error. See the
[grammar authoring guide](../guides/grammar-authoring.md).

## Environment variables

| Variable | Meaning |
| --- | --- |
| `GAGP_CUDA_DEVICE=N` | Use visible GPU `N` (default: the least used one) |
| `GAGP_GPU_DIAGNOSTICS=1` | Collect GPU evaluation counters and diagnostics (developer use; slower) |
| `GAGP_SEARCH_TRACE=1` | Print each generation's best and mean fitness and timings on standard error |
