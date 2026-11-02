# Benchmark Manifests

This directory stores compact, reviewable evidence for release and refactor
gates. Raw run logs, generated populations, datasets, and profiler captures are
artifacts and remain outside version control.

- [ASGP paper reproduction](asgp-paper-reproduction-20261025/README.md) freezes the author-provided archive, records native solver quality experiments and unresolved paper discrepancies, audits dataset overlap, and exports shared GAGP fixtures.
- [Final scale experiment](final-scale-20261103/README.md) compares GAGP GPU, the GAGP CPU reference and ASGP-1T at P = 1024/4096/16384 with 1024 cases on the 30 ASGP-paper tasks (speed, quality and bitwise engine parity).
- [Customizable grammar report measurements](customizable-grammar-20260929/manifest.json) record the 2026-09-29 four-population, four-backend matrix; [slides and reproduction commands](../docs/presentations/customizable-grammar/README.md).
- `spec_freeze.json` records hashes of normative specifications.
- `simple_exp_*.json` records fixed-population performance comparisons.
- `psb*.json` records PSB support, quality, and performance comparisons.
- `fixed_asgp/` records the 2026-10-01 daily and profiler results of the [historical fixed ASGP/GAGP contract](../docs/reference/fixed-asgp-history.md). Earlier quick/tuning (2026-09-29) and optimization-round (2026-10) evidence was removed from the tree on 2026-11-03 and remains in git history (commit `51aa1dc`).

Every committed manifest must identify its inputs, configuration, relevant
commit(s), raw timing or quality measurements, and gate result. The commands
that produce manifests are documented in [`../tools/README.md`](../tools/README.md);
the experiment constraints are defined in
[`../docs/guides/experiment-protocol.md`](../docs/guides/experiment-protocol.md).
Do not hand-edit a measured result to make a gate pass.
