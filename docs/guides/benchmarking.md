# Benchmarking

This guide owns supported fixed-population timing and run commands. Timing
fields are defined in [the timing reference](../reference/timing.md).

## Fixed-population workflow

Create one release-2 materialized population:

```bash
cpp/build/gagp_generate_cli \
  --grammar-definition configs/grammar/basic/int.json \
  --cases data/fixtures/simple_exp_1024.json \
  --population-size 1024 --seed 0 \
  --out-json logs/fixed.population-v2.json
```

Reuse it for each mode:

```bash
cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/basic/int.json \
  --population-json logs/fixed.population-v2.json \
  --engine gpu --repro-backend gpu --repro-overlap off \
  --blocksize 1024 --generations 1 --skip-final-eval on \
  --timing all --out-json logs/fixed.gpu.run.json
```

For fair comparisons, reuse the same `grammar-population-v2`, cases,
definition content, limits, and device policy. Compare generation-0 timing
first. Treat `total_ms` as wall clock and inspect evaluation/reproduction
subphases to explain it.

Seed-only release-1 populations are not a v2 fixed-population input. Materialize
them with the frozen release-1 build and migrate every AST before using them in
a cross-version oracle study. Do not describe regenerated v2 seeds as exact
replay.

## Evolution progress run

```bash
cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/basic/int.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --blocksize 1024 --population-size 1024 --generations 20 \
  --out-json logs/simple-exp.run.json
```

`generation_eval_ms` includes compile, scoring, canonicalization, and scored
population rebuild. GPU evaluation detail uses the `gpu_eval_*` family.
Reproduction detail uses `repro_*`; with overlap, preparation/preprocess/pack
may be partially hidden behind evaluation.

Release-2 speed or transfer-cost claims require new measured artifacts. The
format cutover and documentation alone provide no performance result.
