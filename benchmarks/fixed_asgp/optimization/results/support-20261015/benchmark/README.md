# Low-budget validation

Stage: benchmark; P=1024, cases=1024, generations=2.
Elapsed 69.1 s. CPU1T; GPU jobs serialized.
These are smoke/short-search samples, not precision speedups or convergence equivalence.

| Task | Status | Known solution | Source outputs | CPU/GPU snapshot | ASGP | CPU | GPU-e | GPU-er | GPU-er-o |
|---|---|---|---|---|---|---|---|---|---|
| sum_of_elements | passed | not_run | not_run | not_run | ok | ok | ok | ok | ok |
| house_robber | passed | not_run | not_run | not_run | ok | ok | ok | ok | ok |
| median | passed | not_run | not_run | not_run | ok | ok | ok | ok | ok |

Failure details: tasks.csv. Raw samples: searches.csv / generations.csv.
Search-rule differences are embedded in each native search JSON; aligned fitness does not mean identical search.
