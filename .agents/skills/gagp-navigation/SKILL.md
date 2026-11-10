---
name: gagp-navigation
description: Use when a GAGP coding task needs to locate the implementation, contract and tests of a feature, or to judge cross-module impact. Skip when the edit and its test are already localized.
---

# GAGP navigation

Flow of one run: task, grammar and cases -> initial population -> verified
AST -> bytecode -> CPU or CUDA fitness -> ranking -> reproduction (resident
GPU engine, its CPU reference, or host AST reproduction) -> next population.
The overview is `docs/design/architecture.md`; read the section you need, not
the whole file. Read the owner, its direct callers and one matching test
before editing; stop expanding once those and the governing spec are clear.

| Task | Start in | Contract | Tests |
| --- | --- | --- | --- |
| `gagp` command, option, task or result format | `cpp/src/cli/gagp_main.cpp`, `run.cpp`, `task.cpp`, `search_config.cpp`, `grammar_author.cpp`, `grammar_generate.cpp` | `spec/task.md`, `spec/search_config.md`, `docs/reference/cli.md` | `gagp_test_cli`, `gagp_test_search_config`, `gagp_test_repository_contract` |
| JSON codecs for programs, bytecode, cases | `cpp/src/cli/codec.cpp`, `program_json.cpp`, `region_codec.cpp`, `json.cpp` | `spec/grammar.md`, `spec/bytecode_format.md`, `spec/fitness_cases.md` | `gagp_test_cli_json`, `gagp_test_fixture_codec`, `gagp_test_region_codec` |
| Grammar loading, generation, membership, variation | `cpp/src/evolution/grammar/` (`definition.cpp`, `compiled.cpp`, `generate.cpp`, `membership.cpp`, `crossover.cpp`, `mutation.cpp`) | `spec/grammar_definition.md`, `docs/guides/grammar-authoring.md` | `gagp_test_grammar_*` |
| AST node, typing, lowering | `cpp/src/evolution/node_descriptor.cpp`, `ast_verify.cpp`, `ast_type_verify.cpp`, `compiler.cpp` | `spec/grammar.md`, `spec/bytecode_isa.md` | `gagp_test_compiler_lowering`, `gagp_test_ast_verify_*`, parity tests |
| Builtin or bytecode semantics | `cpp/src/runtime/cpu/builtins_cpu.cpp`, `execute_bytecode_cpu.cpp`; the CUDA mirror in `cpp/src/runtime/gpu/device/` | `spec/builtins_base.md`, `spec/builtins_runtime.md`, `spec/bytecode_isa.md` | `gagp_test_runtime_*`, `gagp_test_fitness_cpu_gpu_parity`, `gagp_test_vm_gpu_smoke` |
| Bounded regions (recursion, memo) | `cpp/src/runtime/region_plan.cpp`, `cpp/src/evolution/bounded_region.cpp`, `cpu/bounded_region.hpp`, `gpu/device/region_execution_device.cuh` | `spec/grammar_definition.md`, `spec/bytecode_isa.md` | `gagp_test_bounded_region*`, `gagp_test_region_*` |
| Fitness metrics and payloads | `cpp/include/gagp/core/fitness_policy.hpp`, `cpu/fitness_cpu.cpp`, `gpu/fitness_gpu.cu`, `gpu/host_pack_gpu.cu`, `cpp/src/runtime/payload/payload.cpp` | `spec/fitness.md`, `docs/design/payload.md` | `gagp_test_fitness_contract`, `gagp_test_extended_payload_gpu`, `gagp_test_payload_*` |
| Evolution loop, schedules, fallback | `evolve_population`, `run_phase_population`, `run_phase_population_steady` in `cpp/src/evolution/evolve.cpp` | `spec/search_config.md` | `gagp_test_evolution_pipeline`, `gagp_test_evolution_cpu_gpu_parity` |
| Resident GPU engine | `cpp/src/evolution/repro/gpu/`: `phase_operators.cuh` (shared per-child steps), `phase_population.cu` (GPU), `phase_population_cpu.cpp` (CPU reference), `phase_layout.hpp`, `phase_grammar.hpp`, `device/phase_compile.cuh` | `docs/design/gpu-reproduction.md`, `spec/search_config.md` | `gagp_test_phase_population_cpu_reference`, `gagp_test_gpu_phase_*`, `gagp_test_phase_whole_program` |
| Host AST reproduction | `cpp/src/evolution/repro/backend.cpp` | `spec/search_config.md` | `gagp_test_grammar_reproduction` |
| Benchmarks | `benchmarks/run.py`, `benchmarks/asgp30/` | `benchmarks/README.md` | `gagp_test_bench_runner` |
| Build or test registration | `cpp/CMakeLists.txt`, `cpp/CMakePresets.json` | `docs/guides/development.md` | check the test exists with `ctest -N` |

Rules that matter across modules:

- The GPU engine and the CPU reference must publish identical results; any
  engine change keeps `gagp_test_phase_population_cpu_reference` passing.
- Anything that changes which programs are explored belongs in the search
  configuration, implemented by both engines, never in an environment switch.
- GPU paths pick the least used device; `GAGP_CUDA_DEVICE` forces one. Profile
  with `nsys` only. A skipped GPU test is not a pass.
