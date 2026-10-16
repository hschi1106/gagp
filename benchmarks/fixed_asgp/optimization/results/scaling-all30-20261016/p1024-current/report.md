# ASGP 1T / GPU-er-o: complete G4 scaling

Completed cells: 34/150; qualified ratios: 34.

| Task | P1024 | P2048 | P4096 | P8192 | P16384 |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 9.38x | 11.83x | pending | pending | pending |
| sum_of_elements | 21.44x | 22.83x | pending | pending | pending |
| is_sorted | 1.34x | 1.70x | pending | pending | pending |
| index_of_element | 11.63x | 12.97x | pending | pending | pending |
| all_equal | 1.31x | pending | pending | pending | pending |
| cumulative_sum | 13.18x | pending | pending | pending | pending |
| fuel_cost | 20.32x | pending | pending | pending | pending |
| luhn | 9.95x | pending | pending | pending | pending |
| min | 22.75x | pending | pending | pending | pending |
| mpi | 17.81x | pending | pending | pending | pending |
| count_odds | 18.50x | pending | pending | pending | pending |
| last_index_of_zero | 9.72x | pending | pending | pending | pending |
| small_or_large | 0.95x | pending | pending | pending | pending |
| for_loop_index | 8.06x | pending | pending | pending | pending |
| double_letters | 15.04x | pending | pending | pending | pending |
| replace_space_with_newline | 11.44x | pending | pending | pending | pending |
| even_squares | 11.54x | pending | pending | pending | pending |
| sum_of_squares | 13.44x | pending | pending | pending | pending |
| negative_to_zero | 12.49x | pending | pending | pending | pending |
| scrabble_score | 29.52x | pending | pending | pending | pending |
| grade | 1.21x | pending | pending | pending | pending |
| median | 0.89x | pending | pending | pending | pending |
| smallest | 0.81x | pending | pending | pending | pending |
| syllables | 31.52x | pending | pending | pending | pending |
| fibonacci | 6.37x | pending | pending | pending | pending |
| tribonacci | 4.94x | pending | pending | pending | pending |
| house_robber | 9.97x | pending | pending | pending | pending |
| subregion_sum | 2.97x | pending | pending | pending | pending |
| fibonacci_array | 5.39x | pending | pending | pending | pending |
| stock_with_cooldown | 5.68x | pending | pending | pending | pending |

Default:16GiB arena budget;8/4GiB allocation-only retries explicitly marked. Failed/pending cells have no speedup.
