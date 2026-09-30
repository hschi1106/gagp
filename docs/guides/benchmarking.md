# Benchmarking

This guide owns supported fixed-population timing and run commands. Timing
fields are defined in [the timing reference](../reference/timing.md). The
maintained nonlinear benchmark definition is
`configs/grammar/benchmarks/simple_exp.json`; it exactly matches
`data/fixtures/simple_exp_1024.json`.

For the three-task ASGP/GAGP daily and scaling comparison, use the
[fixed ASGP benchmark contract and commands](fixed-asgp-benchmark.md).

## Fixed-population workflow

Create and same-version replay one population before comparing backends:

```bash
mkdir -p logs/benchmark
cpp/build/gagp_generate_cli \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --cases data/fixtures/simple_exp_1024.json \
  --population-size 1024 --seed 0 \
  --out-json logs/benchmark/simple-exp.population-v2.json
cpp/build/gagp_generate_cli \
  --replay-json logs/benchmark/simple-exp.population-v2.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --cases data/fixtures/simple_exp_1024.json \
  --out-json logs/benchmark/simple-exp.replayed.population-v2.json
cmp logs/benchmark/simple-exp.population-v2.json \
  logs/benchmark/simple-exp.replayed.population-v2.json
```

Run the same artifact in the sequential CPU baseline and all three GPU
combinations:

```bash
cpp/build/gagp_evolve_cli --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --population-json logs/benchmark/simple-exp.population-v2.json \
  --engine cpu --repro-backend cpu --repro-overlap off \
  --generations 1 --skip-final-eval on --timing all \
  --out-json logs/benchmark/simple-exp.cpu.run.json

cpp/build/gagp_evolve_cli --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --population-json logs/benchmark/simple-exp.population-v2.json \
  --engine gpu --repro-backend cpu --repro-overlap off --blocksize 1024 \
  --generations 1 --skip-final-eval on --timing all \
  --out-json logs/benchmark/simple-exp.gpu-eval.run.json

cpp/build/gagp_evolve_cli --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --population-json logs/benchmark/simple-exp.population-v2.json \
  --engine gpu --repro-backend gpu --repro-overlap off --blocksize 1024 \
  --generations 1 --skip-final-eval on --timing all \
  --out-json logs/benchmark/simple-exp.gpu-repro.run.json

cpp/build/gagp_evolve_cli --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --population-json logs/benchmark/simple-exp.population-v2.json \
  --engine gpu --repro-backend gpu --repro-overlap on --blocksize 1024 \
  --generations 1 --skip-final-eval on --timing all \
  --out-json logs/benchmark/simple-exp.gpu-repro-overlap.run.json
```

Reuse the same population, cases, resolved definition, limits, seed, and device
policy. Compare generation-0 timing first. `total_ms` is wall time; use
`generation_eval_ms`, `generation_repro_ms`, and the `gpu_eval_*`/`repro_*`
fields to explain it. GPU timing is relative to this repository's sequential
CPU backend, not to an optimized multicore CPU implementation.

## Evolution progress run

```bash
mkdir -p logs/benchmark
cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --blocksize 1024 --population-size 1024 --generations 20 \
  --out-json logs/benchmark/simple-exp.run.json
```

Use `/tmp` for disposable authoring and smoke artifacts. Keep retained raw
runs, profiler output, populations, and reports under ignored `logs/`; commit
only reviewed compact evidence under `benchmarks/`.

Release-1 seed-only populations are not v2 fixed-population inputs. Materialize
them with the frozen release-1 build and migrate each AST for a cross-version
oracle study. New v2 generation from the same numeric seed is not exact replay.
Release-2 speed or search-quality claims require measured artifacts; the format
cutover itself provides no result.
