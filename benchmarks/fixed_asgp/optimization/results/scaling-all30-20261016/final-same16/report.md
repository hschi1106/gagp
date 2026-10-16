# ASGP 1T / GPU-er-o: complete G4 scaling

Completed cells: 150/150; qualified ratios: 149.

| Task | P1024 | P2048 | P4096 | P8192 | P16384 |
| --- | ---: | ---: | ---: | ---: | ---: |
| filter_odd | 9.32x | 12.28x | 14.40x | 11.98x | 13.90x |
| sum_of_elements | 20.80x | 24.76x | 30.39x | 30.29x | 33.37x |
| is_sorted | 1.33x | 1.68x | 1.87x | 2.08x | 2.18x |
| index_of_element | 11.57x | 13.43x | 16.08x | 16.04x | 16.28x |
| all_equal | 1.28x | 1.60x | 1.89x | 2.00x | 2.16x |
| cumulative_sum | 13.04x | 15.90x | 19.24x | 15.29x | 17.07x |
| fuel_cost | 20.23x | 23.87x | 30.00x | 29.50x | 31.83x |
| luhn | 9.65x | 13.94x | 16.06x | 15.37x | 16.06x |
| min | 22.23x | 27.94x | 36.85x | 36.75x | 38.94x |
| mpi | 17.55x | 22.60x | 25.30x | 26.10x | 28.36x |
| count_odds | 18.69x | 24.21x | 27.15x | 28.14x | 29.06x |
| last_index_of_zero | 9.60x | 11.26x | 12.34x | 12.38x | 13.02x |
| small_or_large | 0.93x | 1.13x | 1.32x | 0.67x | 0.95x |
| for_loop_index | 7.84x | 11.17x | 12.82x | 9.62x | 11.49x |
| double_letters | 15.06x | 17.48x | 17.93x | 16.89x | 18.03x |
| replace_space_with_newline | 11.34x | 12.52x | 14.25x | 13.28x | 13.87x |
| even_squares | 11.53x | 15.00x | 15.38x | 13.18x | 14.60x |
| sum_of_squares | 13.81x | 17.74x | 21.74x | 22.23x | 23.69x |
| negative_to_zero | 12.39x | 16.78x | 18.32x | 15.60x | 18.17x |
| scrabble_score | 29.61x | 39.22x | 41.47x | 43.03x | 46.31x |
| grade | 1.21x | 1.08x | 1.25x | 0.66x | 0.80x |
| median | 0.89x | 1.14x | 1.58x | 1.27x | 1.45x |
| smallest | 0.80x | 1.14x | 1.30x | 1.26x | 1.43x |
| syllables | 31.86x | 42.55x | 46.61x | 43.55x | 47.60x |
| fibonacci | 6.36x | 7.37x | 8.01x | 8.81x | 8.71x |
| tribonacci | 4.92x | 4.88x | 5.24x | 5.23x | 5.30x |
| house_robber | 9.89x | 12.71x | 15.77x | 16.73x | initial_admission_limit |
| subregion_sum | 2.96x | 3.71x | 4.38x | 4.84x | 4.57x |
| fibonacci_array | 5.38x | 6.11x | 6.26x | 6.48x | 6.56x |
| stock_with_cooldown | 5.68x | 6.27x | 6.68x | 6.77x | 6.99x |

Fixed16GiB arena budget in every GPU cell; no smaller-capacity retries. Full final-population CPU fitness audit at P16384 is outside evolve wall time. Failed/pending cells have no speedup.
