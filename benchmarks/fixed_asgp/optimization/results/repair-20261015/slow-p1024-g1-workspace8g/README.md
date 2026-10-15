# Low-budget validation

Stage: bounded-slow-screen; P=1024, cases=1024, generations=1.
Elapsed 132.5 s. CPU1T; GPU jobs serialized.
These are smoke/short-search samples, not precision speedups or convergence equivalence.

| Task | Status | Known solution | Source outputs | CPU/GPU snapshot | ASGP | CPU | GPU-e | GPU-er | GPU-er-o |
|---|---|---|---|---|---|---|---|---|---|
| filter_odd | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| negative_to_zero | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| cumulative_sum | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| for_loop_index | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| double_letters | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| replace_space_with_newline | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |
| even_squares | passed | not_run | not_run | not_run | not_run | not_run | not_run | not_run | ok |

Failure details: tasks.csv. Raw samples: searches.csv / generations.csv.
Search-rule differences are embedded in each native search JSON; aligned fitness does not mean identical search.
