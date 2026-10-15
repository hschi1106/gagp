# Low-budget validation

Stage: benchmark; P=1024, cases=1024, generations=4.
Elapsed 966.4 s. CPU1T; GPU jobs serialized.
These are smoke/short-search samples, not precision speedups or convergence equivalence.

| Task | Status | Known solution | Source outputs | CPU/GPU snapshot | ASGP | CPU | GPU-e | GPU-er | GPU-er-o |
|---|---|---|---|---|---|---|---|---|---|
| filter_odd | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| sum_of_elements | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| is_sorted | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| index_of_element | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| all_equal | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| cumulative_sum | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| fuel_cost | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| luhn | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| min | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| mpi | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| count_odds | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| last_index_of_zero | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| small_or_large | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| for_loop_index | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| double_letters | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| replace_space_with_newline | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| even_squares | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| sum_of_squares | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| negative_to_zero | incomplete_or_failed | not_run | not_run | not_run | ok | not_run | not_run | not_run | timeout |
| scrabble_score | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| grade | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| median | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| smallest | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| syllables | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| fibonacci | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| tribonacci | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| house_robber | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| subregion_sum | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| fibonacci_array | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |
| stock_with_cooldown | passed | not_run | not_run | not_run | ok | not_run | not_run | not_run | ok |

Failure details: tasks.csv. Raw samples: searches.csv / generations.csv.
Search-rule differences are embedded in each native search JSON; aligned fitness does not mean identical search.
