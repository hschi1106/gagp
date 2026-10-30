# Benchmark Manifests

This directory stores compact, reviewable evidence for release and refactor
gates. Raw run logs, generated populations, datasets, and profiler captures are
artifacts and remain outside version control.

- [ASGP paper reproduction](asgp-paper-reproduction-20261025/README.md) freezes the author-provided archive, records native solver quality experiments and unresolved paper discrepancies, audits dataset overlap, and exports shared GAGP fixtures.
- [ASGP vs GAGP GPU quick experiment](asgp-quick-20260929/README.md) records shared-initial-population DC timings with overlap off/on and ASGP 1/10/20-thread baselines; numerical and reproduction-policy differences are retained explicitly.
- [ASGP 1T comparison at population=cases=1024](asgp-tuning-20260929/README.md) records 14 GPU frame/block settings and full-generation overlap off/on timings for the original and selected settings.
- [Customizable grammar report measurements](customizable-grammar-20260929/manifest.json) record the 2026-09-29 four-population, four-backend matrix; [slides and reproduction commands](../docs/presentations/customizable-grammar/README.md).
- `spec_freeze.json` records hashes of normative specifications.
- `simple_exp_*.json` records fixed-population performance comparisons.
- `psb*.json` records PSB support, quality, and performance comparisons.
- `fixed_asgp/` records reviewed results from the [fixed ASGP/GAGP contract](../docs/guides/fixed-asgp-benchmark.md).

Every committed manifest must identify its inputs, configuration, relevant
commit(s), raw timing or quality measurements, and gate result. The commands
that produce manifests are documented in [`../tools/README.md`](../tools/README.md);
the experiment constraints are defined in
[`../docs/guides/experiment-protocol.md`](../docs/guides/experiment-protocol.md).
Do not hand-edit a measured result to make a gate pass.
