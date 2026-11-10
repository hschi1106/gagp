# Examples

Complete tasks to run with `gagp run`. Each directory holds `task.json`,
`grammar.json`, `train.cases.json` and `test.cases.json`.

| Task | Finds | Shows |
| --- | --- | --- |
| [`polynomial`](polynomial) | `x³ - 2x + 1` from 21 values of `x` | An arithmetic expression grammar; see [getting started](../docs/guides/getting-started.md) |
| [`list-sum`](list-sum) | The sum of an integer list | A traversal with bound variables; see the [list-sum walkthrough](../docs/guides/list-sum-grammar.md) |

```bash
cpp/build/gagp run examples/polynomial/task.json
cpp/build/gagp run examples/list-sum/task.json
```

Larger tasks are in [`benchmarks/asgp30`](../benchmarks/README.md).
