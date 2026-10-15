# Low-budget validation

Stage: support; P=8, cases=1024, generations=2.
Elapsed 348.4 s. CPU1T; GPU jobs serialized.
These are smoke/short-search samples, not precision speedups or convergence equivalence.

| Task | Status | Known solution | Source outputs | CPU/GPU snapshot | ASGP | CPU | GPU-e | GPU-er | GPU-er-o |
|---|---|---|---|---|---|---|---|---|---|
| filter_odd | smoke_failed | ok | failed | ok | ok | ok | ok | ok | ok |
| sum_of_elements | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| is_sorted | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| index_of_element | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| all_equal | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| cumulative_sum | smoke_failed | failed | failed | ok | ok | ok | ok | ok | ok |
| fuel_cost | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| luhn | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| min | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| mpi | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| count_odds | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| last_index_of_zero | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| small_or_large | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| for_loop_index | smoke_failed | ok | failed | ok | ok | ok | ok | ok | ok |
| double_letters | smoke_failed | ok | failed | ok | ok | ok | ok | ok | ok |
| replace_space_with_newline | smoke_failed | ok | failed | ok | ok | ok | ok | ok | ok |
| even_squares | smoke_failed | ok | failed | ok | ok | ok | ok | ok | ok |
| sum_of_squares | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| negative_to_zero | smoke_failed | ok | ok | mismatch | ok | ok | ok | ok | ok |
| scrabble_score | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| grade | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| median | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| smallest | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| syllables | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| fibonacci | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| tribonacci | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| house_robber | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| subregion_sum | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| fibonacci_array | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |
| stock_with_cooldown | smoke_pass | ok | ok | ok | ok | ok | ok | ok | ok |

Failure details: tasks.csv. Raw samples: searches.csv / generations.csv.
Search-rule differences are embedded in each native search JSON; aligned fitness does not mean identical search.
