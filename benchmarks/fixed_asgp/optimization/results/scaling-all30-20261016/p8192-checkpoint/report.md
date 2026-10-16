# ASGP 1T / GPU-er-o: complete G4 scaling

Completed cells: 120/150; qualified ratios: 120.

| Task | P1024 | P2048 | P4096 | P8192 | P16384 |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 9.38x | 11.83x | 14.33x | 14.45x (8GiB) | pending |
| sum_of_elements | 21.44x | 22.83x | 29.73x | 31.75x | pending |
| is_sorted | 1.34x | 1.70x | 1.88x | 2.20x (8GiB) | pending |
| index_of_element | 11.63x | 12.97x | 15.62x | 16.44x | pending |
| all_equal | 1.31x | 1.61x | 1.88x | 2.10x (8GiB) | pending |
| cumulative_sum | 13.18x | 15.28x | 19.17x | 18.67x (8GiB) | pending |
| fuel_cost | 20.32x | 23.41x | 29.25x | 31.01x | pending |
| luhn | 9.95x | 12.00x | 15.18x | 16.44x | pending |
| min | 22.75x | 26.20x | 35.84x | 38.68x | pending |
| mpi | 17.81x | 20.95x | 24.69x | 27.37x | pending |
| count_odds | 18.50x | 22.08x | 26.40x | 29.43x | pending |
| last_index_of_zero | 9.72x | 10.70x | 12.12x | 12.59x | pending |
| small_or_large | 0.95x | 1.12x | 1.22x | 1.44x (8GiB) | pending |
| for_loop_index | 8.06x | 10.69x | 12.64x | 12.35x (8GiB) | pending |
| double_letters | 15.04x | 17.24x | 17.86x | 14.70x (8GiB) | pending |
| replace_space_with_newline | 11.44x | 12.32x | 14.21x | 11.47x (8GiB) | pending |
| even_squares | 11.54x | 14.58x | 15.39x | 15.03x (8GiB) | pending |
| sum_of_squares | 13.44x | 16.10x | 20.94x | 23.64x | pending |
| negative_to_zero | 12.49x | 16.15x | 18.13x | 18.10x (8GiB) | pending |
| scrabble_score | 29.52x | 35.15x | 39.92x | 45.98x | pending |
| grade | 1.21x | 1.04x | 1.23x | 1.24x (8GiB) | pending |
| median | 0.89x | 1.08x | 1.56x | 1.48x | pending |
| smallest | 0.81x | 1.08x | 1.29x | 1.50x | pending |
| syllables | 31.52x | 39.13x | 44.83x | 46.34x | pending |
| fibonacci | 6.37x | 6.88x | 7.84x | 8.68x | pending |
| tribonacci | 4.94x | 4.69x | 5.17x | 5.22x | pending |
| house_robber | 9.97x | 11.25x | 15.05x | 17.25x | pending |
| subregion_sum | 2.97x | 3.58x | 4.18x | 4.62x | pending |
| fibonacci_array | 5.39x | 5.95x | 6.23x | 6.46x | pending |
| stock_with_cooldown | 5.68x | 6.09x | 6.62x | 6.82x | pending |

Default:16GiB arena budget;8/4GiB allocation-only retries explicitly marked. Failed/pending cells have no speedup.
