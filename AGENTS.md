# GAGP Repository Guidelines

GAGP is grammar-guided genetic programming whose whole search loop runs on the
GPU, with a CPU reference engine that produces identical results. Start with
[README.md](README.md); the documentation index is [docs/README.md](docs/README.md).

## Layout

- `cpp/`: the C++/CUDA implementation (`include/gagp/`, `src/cli/`,
  `src/evolution/`, `src/runtime/`) and native tests (`tests/`).
- `spec/`: the normative formats and semantics. A behavior change updates its
  specification in the same change.
- `docs/`: guides, design notes and references, indexed by `docs/README.md`.
- `configs/`: grammars and the default search configuration.
- `examples/`: runnable tasks. `benchmarks/`: the `asgp30` suite and `run.py`.
- `tests/repository/`: documentation and repository checks.

The full map is [docs/reference/repository-layout.md](docs/reference/repository-layout.md).

## Build and test

Run commands from the repository root.

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build -j
ctest --test-dir cpp/build --output-on-failure
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

Focused sets: `ctest -L gpu`, `ctest -L cpu_gpu_parity`, or
`ctest -R gagp_test_phase_population_cpu_reference`. Use a Release tree for any
timing. Details are in [docs/guides/development.md](docs/guides/development.md).

## Rules

- **One program.** The only executable is `gagp` (`run`, `eval`, `grammar`).
  New options belong in `cpp/src/cli/run.cpp` and in the `gagp run` table of
  `docs/reference/cli.md`; the repository tests compare the two.
- **Search knobs live in the search configuration.** Anything that changes
  which programs are explored is a member of `gagp-search-config-v1`
  (`spec/search_config.md`), implemented identically by both engines. Do not
  add environment switches; the only environment variables are
  `GAGP_CUDA_DEVICE`, `GAGP_GPU_DIAGNOSTICS` and `GAGP_SEARCH_TRACE`.
- **Exactness.** The GPU engine and the CPU reference engine must publish
  identical populations, fitness values and champions. An engine change or
  optimization is accepted only if `gagp_test_phase_population_cpu_reference`
  and the CPU/GPU parity tests pass.
- **Optimize GPU evaluation and GPU reproduction.** Do not move reproduction
  to host worker threads and report it as a GPU speedup. Report speedups from
  unprofiled Release runs.
- **Retired designs stay retired** unless there is new evidence: islands,
  asynchronous evolution, small steady-state batches, population-wide phase
  sharing, native AST GPU reproduction and engine switches. See the table in
  [docs/design/research-mainline.md](docs/design/research-mainline.md).
- **Tests.** Add or update tests with every behavior change, especially error
  paths, fuel and resource limits, typed lists, grammar membership, and CPU/GPU
  parity for runtime, payload or GPU changes.

## GPU

- GPU paths pick the least used visible device; `GAGP_CUDA_DEVICE=N` forces one.
- Profile with `nsys` only; do not use `ncu` in this environment.
- A skipped or unconfigured GPU test is not a pass.

## Style and commits

- C++17 and CUDA, matching the style of neighboring files; Python with
  4-space indentation and type hints; `snake_case` functions, `PascalCase`
  types, `UPPER_CASE` constants. Python tests use `unittest`.
- Commits: `<type>: <summary>` with `feat`, `fix`, `refactor`, `docs`, `test`
  or `chore`. Do not commit builds, run outputs (`logs/`, `runs/`) or datasets.

## Agent workflow

- To locate a feature's code, contract and tests, use
  `.agents/skills/gagp-navigation/SKILL.md`. Already localized changes only
  need their callers, contract and tests confirmed.
- Before finishing, inspect every changed, added and removed path, including
  untracked files, and update only the documents whose facts changed
  (entry points, options, formats, layout, validation commands). Keep one
  authoritative source per fact and run the repository tests.
