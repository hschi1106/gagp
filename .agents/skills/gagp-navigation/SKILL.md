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
| Fitness result or payload/sequence ownership | `cpp/include/gagp/core/fitness_policy.hpp` for configurable metrics, then `cpp/src/runtime/cpu/fitness_cpu.cpp`, `cpp/src/runtime/gpu/fitness_gpu.cu` (`FitnessSessionGpu::eval_programs`), `cpp/src/runtime/view_profile.hpp` for shared opt-in capability proofs, `cpu/list_views.hpp` for invocation-owned CPU window views, `gpu/view_profile.hpp` for GPU root admission and `device/window_execution_device.cuh` for admitted window execution, `cpp/src/runtime/payload/payload.cpp`, `cpp/src/runtime/gpu/host_pack_gpu.cu`; inspect `PayloadLifetimeManager` in `cpp/src/evolution/lifecycle.cpp` if retention changes | `spec/fitness.md`, `docs/design/payload.md`; `gagp_test_payload_staging`, `gagp_test_compiled_payload_evolution_gpu`, and `gagp_test_fitness_cpu_gpu_parity` when shared semantics change. Preserve payload lifetime, typed tags, error/fuel results, and documented fallback behavior |
| Evolution, reproduction, or overlap scheduling | `evolve_population` in `cpp/src/evolution/evolve.cpp`, `cpp/src/evolution/lifecycle.cpp` (`gpu_reproduction_overlap_enabled`), `cpp/src/evolution/repro/prep.cpp`, `cpp/src/evolution/repro/pack.cpp`, `cpp/src/evolution/repro/gpu.cpp` (opt-in scalar selected-site preparation uses private `grammar/selected_variation.hpp` and execution-only canonical admission; `grammar/owned_population.hpp` owns optional immutable scalar admission continuations), then `cpp/src/evolution/repro/gpu/launch.cu` and only the affected device kernel; isolated lowering prototype `gpu/device/phase_compile.cuh` is tested by `gagp_test_gpu_phase_compile`; experimental GPU donor construction uses `gpu/phase_donors.cu`, `gpu/phase_grammar.hpp` and `gpu/device/phase_generate.cuh`, with `gagp_test_gpu_phase_generate` for independent grammar checks | `docs/design/dataflow.md`, `docs/design/gpu-reproduction.md`; `gagp_test_evolution_pipeline`, `gagp_test_grammar_reproduction`, and relevant GPU/evolution parity tests. Selection consumes completed fitness; overlap changes preparation scheduling and payload lifetime |
| Performance measurement or GPU optimization | `cpp/src/bench/final_candidate_bench.cpp`, `cpp/src/evolution/timing.cpp`; for migration comparisons, `tools/gagp_tools/experiments/grammar_migration.py` and its matching report module | `docs/guides/benchmarking.md`, `docs/guides/experiment-protocol.md`, `docs/reference/grammar-migration/README.md`; enable benchmark targets explicitly in CMake. Keep binary, inputs, and mode identity fixed; use `nsys` for GPU timelines; do not use `ncu` in this environment |
| Python operational command or experiment report | `tools/gagp_tools/cli.py` dispatch -> the matching module under `tools/gagp_tools/`; inspect its direct caller and paired `tools/tests/` test | `tools/README.md` for workflow; Python tools do not define product execution semantics |
| Fixed ASGP/GAGP comparison | `tools/gagp_tools/experiments/fixed_asgp/runner.py`, `grammar.py` (source-contract coverage uses `source_grammar.py` and `dp_grammar.py`; `low_budget.py` / `low_budget_report.py` own bounded30-task support, cheap quality and one-shot timing; full-case source/native output gates use native `sampled_audit.hpp`), then `cpp/src/bench/fixed_asgp/` for source translation and native timing; `benchmarks/fixed_asgp/optimization/native_candidate.py` serially reruns the checked native opt-in settings without preparing inputs; `multigen.py` runs five-mode changing populations, with `multigen_audit.py` / `multigen_native_audit.py` / `multigen_report.py` for full-case champion audits and plots; `long_search.py`, `long_search_audit.py`, and `long_search_report.py` extend unchanged archived runs under a time budget and render seed-resolved checkpoints; `short_search.py` serializes candidate comparisons on House32/Median64 (optional Sum32) with frozen inputs and explicit binary/settings; `results/expanded-20261014/run.py`, `summarize.py`, and `summarize_best.py` own the Count Odds/Last Index study, with native `expanded_audit.hpp` for independent quality; current Stock/list support is governed by source-contract gates | `docs/guides/fixed-asgp-benchmark.md`; `tools/tests/test_fixed_asgp.py` and optional `gagp_test_fixed_asgp_bench`. Optimization remains GPU evaluation plus GPU reproduction; the CPU benchmark modes are controls. Preserve frozen input hashes and complete generation timing |
| Build target, test registration, or preset failure | `cpp/CMakeLists.txt`, `cpp/CMakePresets.json`, then the target's source/test | `docs/guides/development.md`; verify the named CTest target exists before running it |

## Validation selection

Host parallelism defaults to one application CPU thread; owner is
`cpp/include/gagp/core/host_threads.hpp`. `GAGP_HOST_THREADS=2..20` opts into
workers. Single-thread GPU overlap uses the evaluator post-launch callback;
fair ASGP comparisons explicitly use `GAGP_HOST_THREADS=1`.

- Native focused checks: `ctest --test-dir cpp/build -R '<test-name-regex>' --output-on-failure`.
- Python tools and repository checks: use the two `unittest discover` commands
  in root `AGENTS.md`; narrow discovery to the affected test when practical.
- CUDA-enabled configuration fails if its compiler is unavailable; set
  `CMAKE_CUDA_COMPILER` explicitly or choose `GAGP_ENABLE_CUDA=OFF` intentionally.
- Run CPU/GPU parity checks only in a CUDA-enabled build with a usable device.
  GPU paths choose the least-used visible device; `GAGP_CUDA_DEVICE` can select
  one. GPU profiling uses `nsys` only; do not use `ncu` in this environment.
  Formal timings are unprofiled.
- Use the full suite when the change spans contracts or several modules, or when
  a focused result cannot cover the affected boundary. Do not infer that an
  unconfigured or skipped GPU test passed.

## Stop rule

Stop expanding the read set once the changed owner, affected callers, governing
contract, and matching tests are clear. For implementation-time documentation
maintenance, follow the rules in root `AGENTS.md`.

- Private evaluation identity/executable lookup: `cpp/src/evolution/grammar/evaluation_identity.hpp`, implemented beside certificates in `membership.cpp`; regression coverage in `test_grammar_membership.cpp`.
- Native immutable crossover continuation: `grammar/owned_population.hpp` (`from_gpu_pass`), `repro/compiled_decode.cpp`, `repro/gpu.cpp`; safety coverage in `cpp/tests/gpu/test_compiled_transport_gpu.cu`. Paths without `cpp/` are relative to `cpp/src/evolution/`.

- Isolated GPU subtree variation primitive: `cpp/src/evolution/repro/gpu/device/phase_variation.cuh`, host capability check `gpu/phase_variation.hpp`, test `gagp_test_gpu_phase_variation`. It underlies the opt-in native GPU genotype; grammar-site construction metadata is distinct from canonical admission. Unsupported scope/variation/resource policies decline the profile.
- Cold GPU genotype import/export adapter: `cpp/src/evolution/repro/gpu/phase_layout.hpp`, test `gagp_test_gpu_phase_layout`; `gpu/phase_population.cu` owns live GPU selection/variation/lowering, reached through `GAGP_GPU_PHASE_POPULATION` in `evolve.cpp`. `gagp_test_gpu_phase_population` covers full-case cross-generation CPU reevaluation, capacity rejection and explicit legacy fallback.
- GPU owner-only executable packing proof: `cpp/src/runtime/gpu/owned_programs.hpp`; only `NativePhasePopulation` constructs a borrowed view. `FitnessSessionGpu` and `host_pack_gpu.cu` consume it synchronously. External bytecode packing remains fully verified; `gagp_test_gpu_phase_population` compares owned, ordinary GPU and independent CPU evaluation every generation. `GAGP_OWNED_PHASE_PACKED` keeps private static packed tables plus current GPU-lowered phases; `programs()` materializes the reference on demand, and wrong-input-type sessions do so before fallback analysis. Specialization settings are owner-lifetime-fixed; direct metadata refresh lives in `refresh_owned_phase_patterns`.
- Opt-in single-coordinate executor: `gpu/device/coordinate_execution_device.cuh`, capability selection in `fitness_gpu.cu`, frame slicing in `device/kernels.cuh`; regression matrix and full-case multi-program isolation in `gagp_test_bounded_region_gpu`. Shape fallback and original fuel/memo capacity semantics remain required.
- `GAGP_GPU_DENSE_COORDINATE_MEMO`: `gpu/dense_coordinate_memo.hpp` proves complete literal Int coordinate domains; `gpu/device/region_execution_device.cuh` reuses invocation-owned key backing for dense lookup. Ownership/profile gate: `gpu/fitness_gpu.cu`. Contract: `docs/design/architecture.md`; tests: `gagp_test_bounded_region_gpu_results` (`dense_coordinate_memo_checks.cuh`). Paths relative to `cpp/src/runtime/` unless stated otherwise.
- Owned compact/generic capability buckets use index-mapped launches in `gpu/fitness_gpu.cu` and `device/kernels.cuh`, enabled by `GAGP_PHASE_CAP_BUCKETS`; native reproduction block scheduling uses `GAGP_PHASE_REPRO_THREADS` in `repro/gpu/phase_population.cu`. `gagp_test_gpu_phase_population` covers mixed stack bounds, original output ordering, partial offset-scan tiles, and independent CPU reevaluation. These settings do not alter the search rules.

- Optional native CPU deferred site analysis and reproduction-call-local selected
  parent ownership: `repro/backend.cpp`, `grammar/crossover.cpp`,
  `grammar/variation_internal.hpp`; flags `GAGP_CPU_LAZY_ANALYSIS` and
  `GAGP_CPU_SELECTED_PARENTS`, tests `gagp_test_grammar_reproduction`.
  Scalar-only ownership cannot cross reproduction calls; mutable/registry-backed
  inputs keep full admission. This is the native CPU control path, not GPU repro.

- `GAGP_CPU_SELECTED_MUTATION`: native `grammar/mutation.cpp` constructs just
  the exact selected logical subtree site via `selected_variation.hpp`; ordinary
  constant/projected-budget paths remain. Differential RNG/site coverage is in
  `gagp_test_grammar_reproduction`.

- Opt-in CPU compact phase execution: `runtime/cpu/typed_phase.hpp` and session-owned binding tables in `region_adapter.hpp`; `list_views.hpp` admits coordinate capture views, `recurrence_memo.hpp` owns bounded dense CPU memo slots. `coordinate_region.hpp` selects compact one-coordinate CPU frames after cached segment capability checks; other shapes keep the generic driver. These are CPU evaluator optimizations, not reproduction backends. Validate `gagp_test_cpu_execution_session`, `gagp_test_recurrence_memo`, `gagp_test_region_execution`, `gagp_test_bounded_region_sequence_compile` and CPU/GPU fitness parity. Paths are relative to `cpp/src/`.
- `GAGP_CPU_COUNTED_PAIRS`: native `grammar/crossover.cpp` groups exact
  compatibility keys and uniformly selects a rank over all feasible pairs.
  This changes RNG trajectories, not the feasible-pair distribution. Nonlocal
  projected-resource trial paths stay reservoir-based. Oracle/replay coverage
  is in `gagp_test_grammar_reproduction` and `gagp_test_derivation_resources`.

- `GAGP_GPU_INPLACE_MUTATION`: `repro/gpu/phase_population.cu` splices GPU mutation into a per-child temporary, then publishes only the changed phase to the crossed population. No input/output splice aliasing or RNG changes; `gagp_test_gpu_phase_population` compares both paths for 32 generations and full-case CPU audit.

- `GAGP_GPU_FUSED_DONOR_SITES`: private mutation generation emits exact construction sites in `gpu/device/phase_generate.cuh`; `gpu/phase_population.cu` chooses a dedicated kernel specialization. Import annotation remains independent. `gagp_test_gpu_phase_variation` compares sites to the annotation oracle and exercises scoped/unscoped generations.
- Opt-in fused single-root witness admission: `grammar/membership.cpp`, flag
  `GAGP_CPU_FUSED_ADMISSION`; exact node-local production proofs and repeated-hole
  checks replace the duplicated matching traversal, with ambiguous/alias fallback.
  It also affects public reconstruction and GPU imports; speculative work guard
  accounting differs. Contract: `spec/grammar_definition.md`; differential tests
  in `gagp_test_grammar_membership`, reproduction/resources tests above.

- Private scalar admission/compaction continuation: `grammar/membership.cpp`
  (`admit_and_compact_execution`), reached from `grammar/variation.cpp` through
  `GAGP_CPU_COMPACT_ADMISSION`. Original input is admitted before compaction;
  changed name tables rerun ordinary reconstruction. Fresh scalar single-root
  inputs only; proof is sealed against the final AST. Tests:
  `gagp_test_grammar_membership`, `gagp_test_grammar_reproduction`.

- `GAGP_GPU_CONSTRUCTIVE_EXPORT`: private `NativePhasePopulation::Impl::finish_export` in `gpu/phase_population.cu` omits optional canonical export caches using the live grammar-construction invariant. External import stays strict. `gagp_test_gpu_phase_population` clears supplied derivations for all-member grammar/CPU validation and checks owner lifetime, exact AST replay and modified-export rejection.

- `GAGP_COLD_PAYLOAD_PROOFS=1`: `evolution/grammar/cold_admission.hpp`
  scopes cold replay/import membership reads in existing payload snapshots;
  `evolution/population_init.cpp` and `evolution/repro/gpu/phase_layout.hpp`
  retain exact certificates only after read-only validation. Writes/conflicts
  discard speculative results and retry ordinary admission. This is not a hot
  generation transaction or registry-bytecode trust. Test:
  `gagp_test_grammar_membership`; integration: `gagp_test_gpu_phase_layout`.

- `GAGP_COLD_IMPORT_EXECUTABLES` additionally retains the already-lowered immutable artifact in cold read-snapshot certificates. Private lookup in `evolution/grammar/cold_admission.hpp`/`membership.cpp` checks decoded identity plus exact raw constant Values and publishes only after snapshot validation; public registry executable reuse stays declined. Contract/tests follow the cold-proof owners above.

- `GAGP_REUSE_IMPORT_CERTIFICATE`: `repro/gpu/phase_layout.hpp` retains only
  identity-checked private admission/executable caches on cold import. External
  mutations, grammar/request and evaluator input order still pass validation.
  Contract: `docs/design/gpu-reproduction.md`; test: `gagp_test_gpu_phase_layout`.

- `GAGP_PREPARED_CASE_VIEWS`: `runtime/gpu/fitness_gpu.cu` prepares per-evaluation
  private list views only for admitted view programs with list parameters.
  Raw session inputs remain available for generic fallback. Ownership/timing:
  `docs/design/payload.md`; test: `gagp_test_bounded_region_gpu_results`.

- Native owner reproduction work policies: `GAGP_GPU_DEFERRED_CROSSOVER_COPY`
  and `GAGP_GPU_FUSED_SPLICE_DEPTH` in `repro/gpu/phase_population.cu` and
  `gpu/device/phase_variation.cuh`; compact indexed lowering frames use
  `GAGP_GPU_COMPACT_COMPILE_FRAMES` and `gpu/device/phase_compile.cuh`.
  These retain logical sites, scopes, RNG and fuel; no persistent shared genotype
  or incremental executable cache is introduced. Tests:
  `gagp_test_gpu_phase_population`, `gagp_test_gpu_phase_compile`.
  Paths are relative to `cpp/src/evolution/` unless starting with `gpu/`, which
  is relative to `cpp/src/evolution/repro/`. Contract and owner lifetimes:
  `docs/design/gpu-reproduction.md`.

- Experimental `GAGP_GPU_PRIVATE_INTLIST_OFFSETS`: shared typeflow in
  `runtime/view_profile.hpp`, existing arena builtins and resident scoring under
  `runtime/gpu/device/`, private `OwnedGpuPrograms` capability composed with
  each fresh `phase_compile.cuh` result in `repro/gpu/phase_population.cu`.
  Contract: `docs/design/payload.md`; tests `gagp_test_extended_payload_gpu`,
  `gagp_test_gpu_phase_compile`, `gagp_test_gpu_phase_population`. Paths are
  relative to `cpp/src/` (the reproduction paths under `evolution/`).

- Resident payload scoring is shared by the VM and focused GPU probes through `cpp/src/runtime/gpu/device/resident_score_device.cuh`; keep full VM execution headers out of probe translation units that also link `gagp_gpu`.

- `GAGP_GPU_COMPACT_INTLIST_CELLS` adds typed int64 local storage only under
  the private IntList proof above. The existing arena VM owns typed span reads
  in `cpp/src/runtime/gpu/device/builtins_device.cuh` and resident scoring in
  `resident_score_device.cuh`; `fitness_gpu.cu` owns byte accounting/allocation.
  Contract and tests are the same private-list owners above.
  `GAGP_GPU_REUSE_PRIVATE_WORKSPACE` optionally retains one session-owned
  allocation for those cells/frames/memo, repartitioned after synchronization;
  `fitness_gpu.cu` accounts retained capacity plus current auxiliary scratch.
  Allocation/resident-byte diagnostics flow through evolution timing and fixed
  benchmark JSON. `test_reusable_private_workspace` covers headroom, dirty reuse,
  callbacks, moves, reset and fallback. The same 32-generation owner audit covers
  current pooled execution.

- `GAGP_GPU_PRIVATE_GLOBAL_OFFSETS`: `runtime/gpu/private_global_offsets.hpp` checks host descriptors; `runtime/gpu/fitness_gpu.cu` prepares current-table cases/constants and reuses the prepared-case upload for initial/replay execution. Contract: `docs/design/payload.md`; tests: `gagp_test_extended_payload_gpu`, `gagp_test_gpu_phase_population`.
- `GAGP_GPU_TRIVIAL_PHASES`: `runtime/gpu/host_pack_gpu.cu` refreshes constant/single-Load metadata (up to eight declared bindings); `runtime/gpu/device/region_execution_device.cuh` consumes it only in admitted private arenas. Contract: `docs/design/payload.md`; static counts: `docs/reference/trivial-private-phase-counts.json`; tests: `gagp_test_bounded_region_gpu_results`, `gagp_test_gpu_phase_population`. Paths relative to `cpp/src/` unless stated otherwise.
- `GAGP_GPU_PRIVATE_BOUND_PHASES`: `runtime/gpu/fitness_gpu.cu` gates existing compare/add metadata dispatch in `runtime/gpu/device/region_execution_device.cuh` on the current private-arena proof; matchers are unchanged. Contract: `docs/design/payload.md`; tests: `gagp_test_bounded_region_gpu_results`, `gagp_test_gpu_phase_population`.
- `GAGP_GPU_PRIVATE_TYPED_PHASES`: `runtime/gpu/fitness_gpu.cu` requires complete current global normalization plus private typeflow; `runtime/gpu/device/view_phase_device.cuh` reuses the typed loop with private builtins and `region_execution_device.cuh` binds captures lazily. Contract: `docs/design/payload.md`; tests: `gagp_test_bounded_region_gpu_results`, `gagp_test_extended_payload_gpu`, `gagp_test_gpu_phase_population`.
- `GAGP_GPU_PRIVATE_WARP_COPY`: `runtime/gpu/fitness_gpu.cu` requires private compact storage; `runtime/gpu/device/builtins_device.cuh` cooperates only among sampled active lanes on checked global-buffer copies, with initial/final memory ordering barriers. Contract: `docs/design/payload.md`; tests: `gagp_test_bounded_region_gpu_results`, `gagp_test_gpu_phase_population`.

- `GAGP_GPU_PRIVATE_MEASURE_CACHE` extends the existing Measure-only preparation
  cache to admitted private arenas; dependency admission/build lives in
  `runtime/gpu/device/kernels.cuh`, hits in `region_execution_device.cuh`, and
  actual-block shared-memory admission/launch sizing in `runtime/gpu/fitness_gpu.cu`.
  Contract: `docs/design/payload.md`; tests: `gagp_test_bounded_region_gpu`,
  `gagp_test_bounded_region_gpu_results`, `gagp_test_gpu_phase_population`.
  Paths relative to `cpp/src/`.

- `GAGP_GPU_PRIVATE_WINDOW_EXECUTOR` admits existing tagged compact arena frames
  to the two-window driver in `runtime/gpu/device/window_execution_device.cuh`;
  selection and replay propagation live in `runtime/gpu/fitness_gpu.cu`, with
  flavor dispatch in `device/region_execution_device.cuh`. Contract:
  `docs/design/payload.md`; tests: `gagp_test_bounded_region_gpu` and
  `gagp_test_gpu_phase_population`. Paths relative to `cpp/src/` (device dispatch
  under `runtime/gpu/`). Private results are IntList; unboxed/SoA stays view-only.
