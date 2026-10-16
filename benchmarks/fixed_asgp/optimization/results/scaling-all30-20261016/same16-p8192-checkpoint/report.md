# ASGP 1T / GPU-er-o: complete G4 scaling

Completed cells: 120/150; qualified ratios: 120.

| Task | P1024 | P2048 | P4096 | P8192 | P16384 |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 9.32x | 12.28x | 14.40x | 11.98x | pending |
| sum_of_elements | 20.80x | 24.76x | 30.39x | 30.29x | pending |
| is_sorted | 1.33x | 1.68x | 1.87x | 2.08x | pending |
| index_of_element | 11.57x | 13.43x | 16.08x | 16.04x | pending |
| all_equal | 1.28x | 1.60x | 1.89x | 2.00x | pending |
| cumulative_sum | 13.04x | 15.90x | 19.24x | 15.29x | pending |
| fuel_cost | 20.23x | 23.87x | 30.00x | 29.50x | pending |
| luhn | 9.65x | 13.94x | 16.06x | 15.37x | pending |
| min | 22.23x | 27.94x | 36.85x | 36.75x | pending |
| mpi | 17.55x | 22.60x | 25.30x | 26.10x | pending |
| count_odds | 18.69x | 24.21x | 27.15x | 28.14x | pending |
| last_index_of_zero | 9.60x | 11.26x | 12.34x | 12.38x | pending |
| small_or_large | 0.93x | 1.13x | 1.32x | 0.67x | pending |
| for_loop_index | 7.84x | 11.17x | 12.82x | 9.62x | pending |
| double_letters | 15.06x | 17.48x | 17.93x | 16.89x | pending |
| replace_space_with_newline | 11.34x | 12.52x | 14.25x | 13.28x | pending |
| even_squares | 11.53x | 15.00x | 15.38x | 13.18x | pending |
| sum_of_squares | 13.81x | 17.74x | 21.74x | 22.23x | pending |
| negative_to_zero | 12.39x | 16.78x | 18.32x | 15.60x | pending |
| scrabble_score | 29.61x | 39.22x | 41.47x | 43.03x | pending |
| grade | 1.21x | 1.08x | 1.25x | 0.66x | pending |
| median | 0.89x | 1.14x | 1.58x | 1.27x | pending |
| smallest | 0.80x | 1.14x | 1.30x | 1.26x | pending |
| syllables | 31.86x | 42.55x | 46.61x | 43.55x | pending |
| fibonacci | 6.36x | 7.37x | 8.01x | 8.81x | pending |
| tribonacci | 4.92x | 4.88x | 5.24x | 5.23x | pending |
| house_robber | 9.89x | 12.71x | 15.77x | 16.73x | pending |
| subregion_sum | 2.96x | 3.71x | 4.38x | 4.84x | pending |
| fibonacci_array | 5.38x | 6.11x | 6.26x | 6.48x | pending |
| stock_with_cooldown | 5.68x | 6.27x | 6.68x | 6.77x | pending |

Fixed16GiB arena budget in every GPU cell; no smaller-capacity retries. Full final-population CPU fitness audit at P16384 is outside evolve wall time. Failed/pending cells have no speedup.
