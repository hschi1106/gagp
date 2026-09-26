# GAGP Repository Guidelines

## Project Structure & Module Organization
- The implementation lives under `cpp/`.
  Current structure:
  - `include/gagp/`: public C++ headers
  - `src/runtime/`: CPU runtime, GPU runtime, payload support
  - `src/evolution/`: compiler, genome generation, operators, evolution loop
  - `src/cli/`: native CLIs such as `gagp_evolve_cli`
  - `tests/`: runtime, GPU smoke, parity, and evolution tests
- Native tests live in `cpp/tests/`.
- Operational tool tests live in `tools/tests/`; runtime-independent repository
  checks live in `tests/repository/`.
- Normative behavior is documented in `spec/`:
  - `grammar.md`
  - `bytecode_isa.md`
  - `bytecode_format.md`
  - `builtins_base.md`
  - `builtins_runtime.md`
  - `fitness.md`
  - `fitness_cases.md`
  - `grammar_config.md`
  - `grammar_definition.md`
  Treat these files as the release 2.0.0 behavioral source of truth. Historical spec
  files are not kept in-tree after the breaking refactor. Release details are
  recorded only in `VERSION.md`.
- Maintained documentation is indexed by `docs/README.md` and grouped by
  ownership:
  - `docs/design/`: architecture, dataflow, payload, and GPU reproduction
  - `docs/guides/`: development, benchmarking, PSB, grammar-config, and
    experiment workflows
  - `docs/reference/`: checked timing, tooling, and repository-layout records
  - `docs/refactor/`: historical refactor evidence

## Build, Test, and Development Commands
- Build native binaries:
  ```bash
  cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
  cmake --build cpp/build -j
  ```
- Run operational tool and repository checks:
  ```bash
  python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
  python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
  ```
- Run all native tests:
  ```bash
  ctest --test-dir cpp/build --output-on-failure
  ```
- Run the main native GPU/parity regression set:
  ```bash
  ctest --test-dir cpp/build -R 'gagp_test_vm_gpu_smoke|gagp_test_fitness_cpu_gpu_parity|gagp_test_evolution_cpu_gpu_parity' --output-on-failure
  ```
- Run commands from the repository root so relative fixtures and configs resolve.

## Coding Style & Naming Conventions
- Use Python with 4-space indentation and type hints where practical.
- Follow existing naming patterns: `snake_case` for functions/variables/modules, `PascalCase` for dataclasses/classes, `UPPER_CASE` for constants.
- Keep modules small and single-purpose; place reusable runtime logic in package modules, not tests.
- No formatter/linter config is committed yet; match the style already present in neighboring files.

## Testing Guidelines
- Python tool/repository-check framework: `unittest`.
- Test files use `test_*.py`; test classes use `Test*`; test methods use `test_*`.
- Native tests are built with CMake and run through `ctest`.
- Add or update tests with every behavior change, especially for:
  - error code behavior (`ErrCode` paths),
  - compiler/runtime contract parity,
  - CPU vs GPU fitness parity when touching runtime, payload, or GPU execution,
  - `IntList` / `FloatList` / `StringList` typed-list behavior when touching sequence values, fixture conversion, payloads, or generation,
  - compiled grammar-definition and generation-request behavior when touching generation, mutation, reproduction, or replay,
  - edge cases around fuel/timeouts and numeric/type operations.

## Profiling Guidelines
- GPU profiling must use `nsys` only.
- Do not use `ncu` in this project environment.
- GPU-capable C++ paths select the least-used visible CUDA device internally.
- To force a specific visible-device index for a run, use `GAGP_CUDA_DEVICE=0` or `GAGP_CUDA_DEVICE=1`.

## GPU Device Runbook
- Run GPU-capable binaries directly.
- The C++ GPU runtime selects the least-used visible CUDA device automatically.
- Recommended examples:
  ```bash
  ctest --test-dir cpp/build -R gagp_test_vm_gpu --output-on-failure -V
  cpp/build/gagp_evolve_cli --cases data/fixtures/simple_exp_1024.json --engine gpu --repro-backend gpu --repro-overlap on --blocksize 1024 --population-size 64 --generations 2 --out-json logs/simple_exp_1024.run.json
  ```

## Commit & Pull Request Guidelines
- Follow commit style: `<type>: <summary>`.
- Preferred types in this repo: `feat`, `fix`, `init`, `docs`, `test`, `refactor`, `chore`.
- PRs should include:
  - a clear behavior summary,
  - linked issue/task if available,
  - test evidence,
  - spec updates in `spec/` when semantics change.

## Agent workflow

- For feature location or cross-module impact, use
  `.agents/skills/gagp-navigation/SKILL.md` and follow only the matching route.
  Already-localized changes need not load the full map; confirm their callers,
  contract, and tests. Do not reread unchanged material still in context.
- Before finishing implementation, inspect the actual changed, added, and removed
  paths, including untracked files. In the same change, update only affected
  navigation or authority docs when module ownership, entry points, interfaces,
  dependencies, dataflow, settings, validation commands, or invariants changed.
  If navigation is unaffected, leave docs alone. Check new links and keep one
  authoritative source per contract.
