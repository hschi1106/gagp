# Tasks and Run Results

This document defines the `gagp-task-v1` task file that `gagp run` and
`gagp eval` read, and the `gagp-run-v1` result that `gagp run --out` writes.
Usage of the commands is in the [command reference](../docs/reference/cli.md).

## Task file

A task is a strict JSON object. Unknown members, at the top level or inside
`fitness`, are errors.

```json
{
  "format": "gagp-task-v1",
  "name": "polynomial",
  "grammar": "grammar.json",
  "train": "train.cases.json",
  "test": "test.cases.json",
  "fitness": {"metric": "absolute", "case_cap": 1000}
}
```

| Member | Required | Meaning |
| --- | --- | --- |
| `format` | yes | `gagp-task-v1` |
| `name` | no | Nonempty label used in output; default: the name of the directory holding the task file |
| `grammar` | yes | A [`grammar-definition-v2`](grammar_definition.md) file |
| `train` | yes | The [`fitness-cases`](fitness_cases.md) the search scores programs on |
| `test` | no | Held-out `fitness-cases` that judge the best program |
| `fitness` | yes | Scoring policy, below |

Paths are nonempty strings relative to the directory of the task file.

| `fitness` member | Required | Meaning |
| --- | --- | --- |
| `metric` | yes | `absolute`, `exact`, `levenshtein` or `tuple_summary` ([fitness.md](fitness.md)) |
| `case_cap` | no | Positive; the largest error one case can add and the error of a failed case (default 1000) |
| `proximity` | no | `capped` (default) or `discounted` |
| `summaries` | `tuple_summary` only | `{"count": 1 or 2, "weight": w, "cap": c}`; `weight` (default 0.1) and `cap` (default 100) are positive |
| `list_allocation_limit` | no | Positive integer at most 2^30: list cells one case may allocate beyond its inputs (default: no limit) |

`summaries` is required with `tuple_summary` and rejected with any other
metric.

## Judging a program on test cases

`gagp run` judges its best program on the task's `test` cases, and `gagp eval`
judges a given program on either split. A case is *correct* when:

- for an `Int` expected value, the output is the same `Int`;
- for an `IntList` expected value, the output is an `IntList` of the same
  length and elements; under `tuple_summary` only the first element (the answer)
  must match;
- for any other type, the `exact` metric scores the case 0.

A runtime error, a type mismatch or exceeding `list_allocation_limit` is a
wrong case and counts as a runtime error. The *loss* of a correct case is 0;
of a wrong `absolute` case, the error capped at `case_cap` (`case_cap` for a
runtime error); of any other wrong case, 1.

The verdict is `{"cases", "correct", "wrong", "runtime_errors", "mean_loss",
"solved"}`, where `solved` means every case is correct. `gagp eval` adds
`task` and `split`.

## Run result

`gagp run --out DIR` writes `DIR/result.json`:

| Member | Meaning |
| --- | --- |
| `format` | `gagp-run-v1` |
| `task`, `engine`, `population`, `generations`, `train_cases` | Run settings; `engine` is `gpu` or `cpu` |
| `seed` | The seed as a decimal string (it is an unsigned 64-bit integer) |
| `search` | The complete effective [search configuration](search_config.md); as `--search` it repeats the run |
| `best` | The best final program: `fitness`, `wrong` (wrong training cases), `nodes`, `text` and `program` |
| `test` | Present when the task has test cases: the verdict of `best` on them |
| `history` | One entry per generation: `generation`, `best_fitness`, `mean_fitness`, `best_wrong`, `best_nodes`, `elapsed_ms`, `eval_ms`, `repro_ms` |
| `time_ms` | `total` (wall clock of the run), `search`, `initialization` |
| `resident_declined` | Present only when the resident representation declined the grammar; the reason |

`best.program` is the program as [`ast-prefix-v2`](grammar.md) JSON.
`best.text` is a readable s-expression of it (`(sub 1 (mul x x))`) for people;
it is not parsed back. Variables bound inside the program print as `v<id>`, and
a binding argument lists its variables first
(`(traverse xs 0 0 [v6 v7 v8] (add v8 v6))`). Fitness is the negated total training error, so 0 is a
perfect program.

With `--save-all`, the run also writes:

- `DIR/population.json`: `{"format": "gagp-population-v1", "grammar_hash",
  "programs": [...]}`, the final population in rank order, best first;
- `DIR/history.json`: `{"history": [{"generation", "fitness", "program"}]}`,
  the best program of each generation.

`gagp run --init FILE` starts from the first `--population` programs of a
`gagp-population-v1` document or of a `grammar-population-v2` file written by
`gagp grammar generate`; it needs at least that many programs.

The `search`, `best`, `test` and `history` members depend only on the task,
the search configuration and the initial population; `engine` and the GPU
device never change them. Time members vary between runs.
