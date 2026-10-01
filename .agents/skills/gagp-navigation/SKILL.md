---
name: gagp-navigation
description: Use when a GAGP coding task needs implementation or benchmark ownership, call paths, cross-module impact, or matching tests across native C++/CUDA, grammar evolution, payloads, or Python tooling. Skip when the edit and its contract/test are already localized.
---

# GAGP change navigation

All paths below are relative to the repository root. GAGP's native flow is
grammar definition and cases -> verified prefix-AST -> bytecode -> CPU/CUDA
fitness -> shared ranking -> CPU/CUDA reproduction -> verified next population.
Python tools orchestrate data and experiments; runtime semantics belong to C++.
Treat routes as starting points: inspect the implementation, direct callers, and
one matching test before editing. Expand only when the change crosses a boundary.
If ownership remains unclear, read only the `Dependency direction`,
`Component boundaries`, or `Evolution` section of
`docs/design/architecture.md`, not the entire design document.
The main orchestration symbol is `evolve_population`; CLI commands reach it
through `parse_cli_options` and `run_evolve_command`.
For target ownership, use `cpp/CMakeLists.txt`: `gagp_runtime_cpu` links
`gagp_core`; `gagp_evolution` adds `gagp_grammar` and CUDA `gagp_gpu` when
enabled; `gagp_cli_support` composes evolution with the JSON/CLI codecs.

| Task or symptom | Read first and follow | Contract and validation |
| --- | --- | --- |
| CLI option, command dispatch, or artifact replay | `cpp/src/cli/options.cpp`, `cpp/src/cli/commands.cpp`, then the relevant entry under `cpp/src/cli/` and target in `cpp/CMakeLists.txt` | `docs/reference/cli.md`; `cpp/tests/runtime/test_cli_options.cpp` (`gagp_test_cli_options`) or `gagp_test_evolve_cli_contract` (`ctest -R '<target>'`) |
| Grammar definition, generation, membership, or variation | `cpp/src/evolution/grammar/definition.cpp`, `cpp/src/evolution/grammar/compiled.cpp`, `cpp/src/evolution/grammar/generate.cpp`, `cpp/src/evolution/grammar/membership.cpp`, `cpp/src/evolution/grammar/variation.cpp`, `cpp/src/evolution/grammar/crossover.cpp`, and `cpp/src/evolution/grammar/mutation.cpp`; inspect `cpp/src/evolution/population_init.cpp` or `cpp/src/evolution/repro/grammar_prep.cpp` if its caller is affected | `spec/grammar_definition.md`, `docs/guides/grammar-authoring.md`; matching tests `gagp_test_grammar_definition`, `gagp_test_grammar_generation`, `gagp_test_grammar_membership`, `gagp_test_grammar_variation_contract`, `gagp_test_grammar_crossover`, `gagp_test_grammar_mutation`, or `gagp_test_grammar_reproduction`. Generated and accepted children must satisfy active grammar membership, not only AST type/scope checks |
| AST node, type/scope rule, lowering, builtin, or bytecode behavior | `cpp/src/evolution/node_descriptor.cpp`, `cpp/src/evolution/ast_verify.cpp`, `cpp/src/evolution/ast_type_verify.cpp`, `cpp/src/evolution/compiler.cpp`; then CPU `cpp/src/runtime/cpu/` and the corresponding CUDA `cpp/src/runtime/gpu/` implementation when behavior is shared | `spec/grammar.md`, `spec/bytecode_isa.md`, and the relevant builtin spec; `gagp_test_compiler_lowering`, `gagp_test_ast_verify_exact_scope`, `gagp_test_ast_verify_structure`, `gagp_test_ast_verify_types`, and CPU/GPU parity tests when backend-visible |
| Fitness result or payload/sequence ownership | `cpp/src/runtime/cpu/fitness_cpu.cpp`, `cpp/src/runtime/gpu/fitness_gpu.cu` (`FitnessSessionGpu::eval_programs`), `cpp/src/runtime/gpu/view_profile.hpp` for opt-in capability proofs, `cpp/src/runtime/payload/payload.cpp`, `cpp/src/runtime/gpu/host_pack_gpu.cu`; immutable bytecode composition is owned by `cpp/src/runtime/region_executable.cpp` (bytecode safety, not membership), tested in `gagp_test_bounded_region_gpu`; inspect `PayloadLifetimeManager` in `cpp/src/evolution/lifecycle.cpp` if retention changes | `spec/fitness.md`, `docs/design/payload.md`; `gagp_test_payload_staging`, `gagp_test_compiled_payload_evolution_gpu`, and `gagp_test_fitness_cpu_gpu_parity` when shared semantics change. Preserve payload lifetime, typed tags, error/fuel results, and documented fallback behavior |
| Evolution, reproduction, or overlap scheduling | `evolve_population` in `cpp/src/evolution/evolve.cpp`, `cpp/src/evolution/lifecycle.cpp` (`gpu_reproduction_overlap_enabled`), `cpp/src/evolution/repro/prep.cpp`, `cpp/src/evolution/repro/pack.cpp`, `cpp/src/evolution/repro/gpu.cpp`, then `cpp/src/evolution/repro/gpu/launch.cu` and only the affected device kernel | `docs/design/dataflow.md`, `docs/design/gpu-reproduction.md`; `gagp_test_evolution_pipeline`, `gagp_test_grammar_reproduction`, and relevant GPU/evolution parity tests. Selection consumes completed fitness; overlap changes preparation scheduling and payload lifetime |
| Performance measurement or GPU optimization | `cpp/src/bench/final_candidate_bench.cpp`, `cpp/src/evolution/timing.cpp`; for migration comparisons, `tools/gagp_tools/experiments/grammar_migration.py` and its matching report module | `docs/guides/benchmarking.md`, `docs/guides/experiment-protocol.md`, `docs/reference/grammar-migration/README.md`; enable benchmark targets explicitly in CMake. Keep binary, inputs, and mode identity fixed; use `nsys` for timelines and `ncu` for focused counters |
| Python operational command or experiment report | `tools/gagp_tools/cli.py` dispatch -> the matching module under `tools/gagp_tools/`; inspect its direct caller and paired `tools/tests/` test | `tools/README.md` for workflow; Python tools do not define product execution semantics |
| Fixed ASGP/GAGP comparison | `tools/gagp_tools/experiments/fixed_asgp/runner.py`, `grammar.py`, then `cpp/src/bench/fixed_asgp/` for source translation and native timing | `docs/guides/fixed-asgp-benchmark.md`; `tools/tests/test_fixed_asgp.py` and optional `gagp_test_fixed_asgp_bench`; `phase_bank.cpp` is the historical independent-phase probe; `fragment_probe.cpp` exercises `cpp/src/evolution/grammar/executable_fragments.*`, a restricted immutable phase owner with local-site variation and collection (`gagp_test_executable_fragments`). Neither is native CLI reproduction. Preserve frozen input hashes and complete generation timing |
| Build target, test registration, or preset failure | `cpp/CMakeLists.txt`, `cpp/CMakePresets.json`, then the target's source/test | `docs/guides/development.md`; verify the named CTest target exists before running it |

## Validation selection

- Native focused checks: `ctest --test-dir cpp/build -R '<test-name-regex>' --output-on-failure`.
- Python tools and repository checks: use the two `unittest discover` commands
  in root `AGENTS.md`; narrow discovery to the affected test when practical.
- CUDA-enabled configuration fails if its compiler is unavailable; set
  `CMAKE_CUDA_COMPILER` explicitly or choose `GAGP_ENABLE_CUDA=OFF` intentionally.
- Run CPU/GPU parity checks only in a CUDA-enabled build with a usable device.
  GPU paths choose the least-used visible device; `GAGP_CUDA_DEVICE` can select
  one. GPU profiling uses `nsys` or focused `ncu` counters; formal timings are unprofiled.
- Use the full suite when the change spans contracts or several modules, or when
  a focused result cannot cover the affected boundary. Do not infer that an
  unconfigured or skipped GPU test passed.

## Stop rule

Stop expanding the read set once the changed owner, affected callers, governing
contract, and matching tests are clear. For implementation-time documentation
maintenance, follow the rules in root `AGENTS.md`.
