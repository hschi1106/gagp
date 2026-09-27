# Final representative measurements

Code `c574b7b`; all versions use the same tcmalloc library. Three measured observations per row.

296 records: 276 new processes and 20 reused, verified receipts. All 36 same-backend
reference/candidate steady fitness vector comparisons are exactly equal. The six
workloads were selected before final optimization, not selected by these results.

Modes: `cpu` is CPU evaluation/reproduction; `gpu_eval` uses GPU evaluation and
CPU reproduction; `gpu_repro` uses GPU evaluation/reproduction;
`gpu_repro_overlap` additionally overlaps preparation. S is CPU median divided by
the mode median; Q is candidate S divided by reference S **for the same mode**.
A_mode is reference time divided by candidate time, so <1 means regression.
CPU rows have S=Q=1 by definition, not a performance pass.

Evolution is one complete generation. Steady is fixed-population evaluation only,
one in-process warmup and one measured call in each of three fresh processes.
Five is the average per-generation wall time of five evolving generations, three
measured runs after one warmup run. Sampling/crossover compatibility differences
prevent interpreting multi-generation runs as identical offspring trajectories.
No confidence intervals or universal performance claims follow.

Raw samples, cold CLI times, fitness progress, best-program node counts, versions,
and receipt identities are in [the machine-readable summary](goal-11.5-final-measurements.json).
Full receipts/logs/hashes remain in the external archive described by the
[implementation report](../custom-grammar-implementation-report.md).

Historical p1024 CPU/direct reference measurements were reused, so those rows
are not a fully interleaved final campaign. Their direct S=37.8023 differs from
the newly measured overlap reference S=45.959; neither replaces the historical
default-allocator reference S=41.9585.

## evolution

| Workload | Mode | Reference ms | Candidate ms | Reference S | Candidate S | Q | A_mode |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dp2d-p64 | cpu | 17.713 | 47.256 | 1.000 | 1.000 | 1.0000 | 0.3748 |
| dp2d-p64 | gpu_eval | 4.510 | 27.069 | 3.928 | 1.746 | 0.4445 | 0.1666 |
| dp2d-p64 | gpu_repro | 7.632 | 21.180 | 2.321 | 2.231 | 0.9614 | 0.3604 |
| dp2d-p64 | gpu_repro_overlap | 6.512 | 20.148 | 2.720 | 2.345 | 0.8623 | 0.3232 |
| metadata_stress-p64 | cpu | 54.621 | 74.700 | 1.000 | 1.000 | 1.0000 | 0.7312 |
| metadata_stress-p64 | gpu_eval | 4.834 | 25.139 | 11.300 | 2.972 | 0.2630 | 0.1923 |
| metadata_stress-p64 | gpu_repro | 8.001 | 21.029 | 6.827 | 3.552 | 0.5203 | 0.3805 |
| metadata_stress-p64 | gpu_repro_overlap | 6.864 | 18.353 | 7.957 | 4.070 | 0.5115 | 0.3740 |
| mixed_exact_payloads-p64 | cpu | 50.261 | 58.183 | 1.000 | 1.000 | 1.0000 | 0.8638 |
| mixed_exact_payloads-p64 | gpu_eval | 3.782 | 16.211 | 13.291 | 3.589 | 0.2700 | 0.2333 |
| mixed_exact_payloads-p64 | gpu_repro | 6.832 | 18.312 | 7.357 | 3.177 | 0.4319 | 0.3731 |
| mixed_exact_payloads-p64 | gpu_repro_overlap | 6.269 | 17.417 | 8.018 | 3.341 | 0.4166 | 0.3599 |
| nested_binders-p64 | cpu | 87.084 | 111.828 | 1.000 | 1.000 | 1.0000 | 0.7787 |
| nested_binders-p64 | gpu_eval | 4.214 | 31.560 | 20.665 | 3.543 | 0.1715 | 0.1335 |
| nested_binders-p64 | gpu_repro | 7.031 | 19.600 | 12.385 | 5.706 | 0.4607 | 0.3587 |
| nested_binders-p64 | gpu_repro_overlap | 6.326 | 18.031 | 13.765 | 6.202 | 0.4506 | 0.3509 |
| simple_exp_1024-p1024 | cpu | 2660.746 | 5245.560 | 1.000 | 1.000 | 1.0000 | 0.5072 |
| simple_exp_1024-p1024 | gpu_eval | 50.759 | 2557.767 | 52.419 | 2.051 | 0.0391 | 0.0198 |
| simple_exp_1024-p1024 | gpu_repro | 70.386 | 205.081 | 37.802 | 25.578 | 0.6766 | 0.3432 |
| simple_exp_1024-p1024 | gpu_repro_overlap | 57.893 | 173.250 | 45.959 | 30.277 | 0.6588 | 0.3342 |
| simple_exp_1024-p64 | cpu | 84.663 | 345.220 | 1.000 | 1.000 | 1.0000 | 0.2452 |
| simple_exp_1024-p64 | gpu_eval | 5.852 | 269.116 | 14.468 | 1.283 | 0.0887 | 0.0217 |
| simple_exp_1024-p64 | gpu_repro | 10.082 | 43.783 | 8.397 | 7.885 | 0.9389 | 0.2303 |
| simple_exp_1024-p64 | gpu_repro_overlap | 8.318 | 38.983 | 10.178 | 8.856 | 0.8701 | 0.2134 |

## steady

| Workload | Mode | Reference ms | Candidate ms | Reference S | Candidate S | Q | A_mode |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| dp2d-p64 | cpu | 16.914 | 26.995 | 1.000 | 1.000 | 1.0000 | 0.6266 |
| dp2d-p64 | gpu_eval | 1.242 | 2.071 | 13.617 | 13.037 | 0.9574 | 0.5999 |
| metadata_stress-p64 | cpu | 53.552 | 56.728 | 1.000 | 1.000 | 1.0000 | 0.9440 |
| metadata_stress-p64 | gpu_eval | 1.409 | 2.109 | 38.003 | 26.897 | 0.7078 | 0.6681 |
| mixed_exact_payloads-p64 | cpu | 50.238 | 50.391 | 1.000 | 1.000 | 1.0000 | 0.9970 |
| mixed_exact_payloads-p64 | gpu_eval | 0.809 | 0.841 | 62.095 | 59.917 | 0.9649 | 0.9620 |
| nested_binders-p64 | cpu | 90.384 | 88.029 | 1.000 | 1.000 | 1.0000 | 1.0268 |
| nested_binders-p64 | gpu_eval | 0.923 | 1.065 | 97.890 | 82.694 | 0.8448 | 0.8674 |
| simple_exp_1024-p1024 | cpu | 2686.882 | 2705.115 | 1.000 | 1.000 | 1.0000 | 0.9933 |
| simple_exp_1024-p1024 | gpu_eval | 26.896 | 26.314 | 99.900 | 102.803 | 1.0291 | 1.0221 |
| simple_exp_1024-p64 | cpu | 82.722 | 82.916 | 1.000 | 1.000 | 1.0000 | 0.9977 |
| simple_exp_1024-p64 | gpu_eval | 2.116 | 2.121 | 39.095 | 39.094 | 1.0000 | 0.9976 |

## five

| Workload | Mode | Reference ms | Candidate ms | Reference S | Candidate S | Q | A_mode |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: |
| simple_exp_1024-p64 | cpu | 95.519 | 387.580 | 1.000 | 1.000 | 1.0000 | 0.2465 |
| simple_exp_1024-p64 | gpu_eval | 4.779 | 308.181 | 19.986 | 1.258 | 0.0629 | 0.0155 |
| simple_exp_1024-p64 | gpu_repro | 9.399 | 38.301 | 10.163 | 10.119 | 0.9957 | 0.2454 |
| simple_exp_1024-p64 | gpu_repro_overlap | 8.151 | 36.272 | 11.719 | 10.685 | 0.9118 | 0.2247 |

## Cold CLI scope

These are whole-process medians for the one-generation runs, including load,
initialization and teardown. They are not the generation values above.

| Workload | Mode | Reference cold ms | Candidate cold ms |
| --- | --- | ---: | ---: |
| dp2d-p64 | cpu | 22.468 | 72.891 |
| dp2d-p64 | gpu_eval | 169.532 | 217.672 |
| dp2d-p64 | gpu_repro | 177.601 | 217.131 |
| dp2d-p64 | gpu_repro_overlap | 174.375 | 212.040 |
| metadata_stress-p64 | cpu | 60.042 | 135.258 |
| metadata_stress-p64 | gpu_eval | 177.275 | 252.595 |
| metadata_stress-p64 | gpu_repro | 178.570 | 264.226 |
| metadata_stress-p64 | gpu_repro_overlap | 177.911 | 261.704 |
| mixed_exact_payloads-p64 | cpu | 55.672 | 109.767 |
| mixed_exact_payloads-p64 | gpu_eval | 171.971 | 228.048 |
| mixed_exact_payloads-p64 | gpu_repro | 174.856 | 254.411 |
| mixed_exact_payloads-p64 | gpu_repro_overlap | 175.126 | 250.141 |
| nested_binders-p64 | cpu | 92.206 | 159.383 |
| nested_binders-p64 | gpu_eval | 175.236 | 237.951 |
| nested_binders-p64 | gpu_repro | 172.833 | 239.477 |
| nested_binders-p64 | gpu_repro_overlap | 176.219 | 234.227 |
| simple_exp_1024-p1024 | cpu | 2728.797 | 5933.274 |
| simple_exp_1024-p1024 | gpu_eval | 291.290 | 3447.730 |
| simple_exp_1024-p1024 | gpu_repro | 389.254 | 1213.152 |
| simple_exp_1024-p1024 | gpu_repro_overlap | 299.999 | 1114.071 |
| simple_exp_1024-p64 | cpu | 93.637 | 695.279 |
| simple_exp_1024-p64 | gpu_eval | 194.167 | 791.408 |
| simple_exp_1024-p64 | gpu_repro | 195.315 | 606.019 |
| simple_exp_1024-p64 | gpu_repro_overlap | 190.871 | 579.988 |

NFE requested: one-generation p64/p1024 runs evaluate 64/1024 members; five-generation
p64 runs evaluate 320 members. Final evaluation is disabled for these timings.
The separate retained-population verification evaluates and checks every final
member and is excluded from the timing rows. Best/mean fitness and best-program
nodes for each measured generation are preserved in the JSON; unchanged fitness
or an unchanged child does not reduce the number of requested evaluations.
