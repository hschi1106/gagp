# Fixed ASGP/GAGP benchmark

Contract: `fixed-asgp-v1`; suite: `daily`.
Complete: True; process wall time: 180.36 s.

One warmup and three measured fixed-parent generations; 1024 cases per program.
Times include generation compilation, evaluation and reproduction. Setup is outside generation timing but inside process wall time.
ASGP/GAGP numerical and reproduction-policy differences are accepted. This is not time-to-solution.

| Task | P | Mode | Median ms | Range ms | vs ASGP 1T | vs GAGP CPU |
|---|---:|---|---:|---:|---:|---:|
| sum_of_elements | 1024 | asgp_1t | 1882.095 | 1881.720–1898.935 | 1.000× | 9.810× |
| sum_of_elements | 1024 | gagp_cpu | 18462.708 | 18275.469–19043.639 | 0.102× | 1.000× |
| sum_of_elements | 1024 | gpu_eval | 1382.352 | 1370.430–1387.137 | 1.362× | 13.356× |
| sum_of_elements | 1024 | gpu_repro | 837.195 | 835.195–840.070 | 2.248× | 22.053× |
| sum_of_elements | 1024 | gpu_overlap | 807.276 | 804.569–809.721 | 2.331× | 22.870× |
| median | 1024 | gagp_cpu | 1358.477 | 1351.796–1364.572 | 0.048× | 1.000× |
| median | 1024 | gpu_eval | 936.988 | 932.203–947.822 | 0.069× | 1.450× |
| median | 1024 | gpu_repro | 287.858 | 285.182–290.443 | 0.224× | 4.719× |
| median | 1024 | gpu_overlap | 262.598 | 262.479–268.493 | 0.246× | 5.173× |
| median | 1024 | asgp_1t | 64.535 | 64.466–64.625 | 1.000× | 21.050× |
| house_robber | 1024 | gpu_eval | 1480.927 | 1477.972–1481.818 | 0.883× | 5.808× |
| house_robber | 1024 | gpu_repro | 788.157 | 787.634–790.451 | 1.660× | 10.913× |
| house_robber | 1024 | gpu_overlap | 763.605 | 758.230–766.067 | 1.713× | 11.264× |
| house_robber | 1024 | asgp_1t | 1308.357 | 1304.576–1321.004 | 1.000× | 6.574× |
| house_robber | 1024 | gagp_cpu | 8601.055 | 8558.278–8613.232 | 0.152× | 1.000× |

Full commands, samples, status and artifact identities: [manifest.json](manifest.json).
Hardware/build: [environment.json](environment.json). Phase attribution: [summary.json](summary.json).

Variation counters classify operator outputs, not final offspring. Native fitness APIs do not expose per-case error/timeout counts; these are not inferred from fitness penalties.
