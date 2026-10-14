# Rejected static phase execution experiments

All four experiment families were rejected. No runtime, setting, or test change
is retained. [summary.json](summary.json) records exact samples, raw log paths,
binary and patch hashes, correctness checks, and test results. Complete rejected
patches, including their tests, are preserved in `rejected-patches/`.

GPU measurements use House32 seed0 and Median64 seed2, P1024, all 1024 cases,
CPU1T, and the authoritative best manifest. Inputs were reused unchanged.
Search milliseconds include cold evolution preparation and final evaluation;
generation kernel/decode sums exclude those boundary stages. Diagnostic output
runs are excluded. Builds, tests, and measurements used the shared exclusive lock.

| Experiment | House OFF → ON search ms | Median OFF → ON search ms | Decision |
| --- | ---: | ---: | --- |
| GPU scalar dispatch fusion | 1382.47 → 1364.68 | 1190.60 → 1226.44 | Reject: host annotation cost and Median regression |
| GPU constant power-of-two division | 1387.94 → 1387.17 | 1200.19 → 1203.75 | Reject: no useful kernel gain |
| GPU exact guarded 32-bit division | 1409.32 → 1409.32 | 1253.00 → 1233.65 | Reject: no House gain; Median kernel essentially unchanged |

These are medians of three clean ON runs and three OFF runs, except fusion has
four OFF runs. Archived production binary controls measured 1372.16ms House and
1192.74ms Median (three runs each). Same-binary OFF measurements alone cannot
establish improvement over production.

Fusion eliminated 34.63% of statically counted House dispatches and reduced its
generation kernel sum from 620.14 to 597.39ms, but increased decode/annotation from
25.36 to 36.10ms. Median decode increased from approximately 62 to 101ms. These
static coverage counts are not execution-weighted dynamic instruction counts.
Power-of-two eligibility covered 5.70% of House and 10.06% of Median builtin
division sites; its House kernel sum stayed approximately 620ms while decode
increased by approximately 9ms. Guarded 32-bit arithmetic avoided a host scan,
but House kernel sums stayed approximately 626ms. No profiling evidence supports
a more specific causal explanation for that lack of improvement.

The fourth family was CPU session-cached scalar fusion, tested on four-generation
changing-population runs and frozen-parent repetitions. A dynamic fusion check
appeared to improve House from 13775.09 to 12467.52ms (three-run medians), but the
archived production binary took 12443.08ms. The apparent gain was a regression in
the experimental OFF executable. Frozen-parent House evaluation likewise
improved against that regressed OFF executable, without establishing a gain
against production. Median was 2701.69 → 2682.80ms versus archived 2675.15ms.

A separate compile-time specialization removed the per-instruction check from
the OFF executor. Its OFF function size matched production exactly, but the
single paired run still gave House 13036.97 → 12672.30ms versus archived 12443.08ms,
and Median 2587.93 → 2689.30ms versus archived 2675.15ms. This was sufficient to
reject adoption, not to establish a precise speed difference. GPU-e controls
were noisy and showed no demonstrated gain; GPU runtime source was unchanged
for both CPU prototypes.

Focused tests passed, covering per-op fuel and error priority, branch entry into
group suffixes, missing or mistyped captures, numeric boundaries, session reuse,
and CPU-oracle parity. The division matrices included signed boundaries and
protected zero/-1 cases; power-of-two shifts covered 0 through 63. OP_MOD was not
specialized. Final exported populations were identical across paired variants;
GPU semantic generation traces also matched. Both metadata structs retained
their original sizes for the guarded 32-bit experiment. These correctness results
do not change the performance rejection.

Raw artifacts remain under the absolute paths recorded in the JSON. All runtime
changes were restored to base `ce0d8119bc0927f758dcdaac4904b1019dce35dd` before
this evidence-only commit. Module ownership and runtime contracts are unchanged,
so navigation and normative documents require no updates.
