cmake_minimum_required(VERSION 3.16)
project(gagp LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
enable_testing()

option(GAGP_ENABLE_CUDA "Build CUDA runtime and parity targets" ON)
option(GAGP_ENABLE_SANITIZERS "Enable CPU AddressSanitizer and UndefinedBehaviorSanitizer" OFF)
option(GAGP_BUILD_FUZZERS "Build Clang libFuzzer targets" OFF)
option(GAGP_BUILD_BENCHMARKS "Build auxiliary benchmark executables" OFF)
option(GAGP_BUILD_EXPERIMENTS "Build auxiliary experiment probes" OFF)

if((GAGP_ENABLE_SANITIZERS OR GAGP_BUILD_FUZZERS) AND GAGP_ENABLE_CUDA)
  message(FATAL_ERROR "sanitizer/fuzzer builds require -DGAGP_ENABLE_CUDA=OFF")
endif()
if(GAGP_ENABLE_SANITIZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang|GNU")
    message(FATAL_ERROR "GAGP_ENABLE_SANITIZERS requires Clang or GCC")
  endif()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=address,undefined>
                      $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
  add_link_options(-fsanitize=address,undefined)
endif()
if(GAGP_BUILD_FUZZERS)
  if(NOT CMAKE_CXX_COMPILER_ID MATCHES "Clang")
    message(FATAL_ERROR "GAGP_BUILD_FUZZERS requires Clang")
  endif()
  add_compile_options($<$<COMPILE_LANGUAGE:CXX>:-fsanitize=fuzzer-no-link,address,undefined>
                      $<$<COMPILE_LANGUAGE:CXX>:-fno-omit-frame-pointer>)
endif()

include(CheckLanguage)
if(GAGP_ENABLE_CUDA)
  check_language(CUDA)
endif()
if(GAGP_ENABLE_CUDA AND CMAKE_CUDA_COMPILER)
  enable_language(CUDA)
  set(GAGP_HAS_CUDA ON)
  set(CMAKE_CUDA_STANDARD 17)
  set(CMAKE_CUDA_STANDARD_REQUIRED ON)
  if(NOT DEFINED CMAKE_CUDA_ARCHITECTURES)
    set(CMAKE_CUDA_ARCHITECTURES 89)
  endif()
else()
  set(GAGP_HAS_CUDA OFF)
endif()

add_library(gagp_core
  src/runtime/bytecode_verify.cpp
)
target_include_directories(gagp_core PUBLIC include)

add_library(gagp_runtime_cpu
  src/runtime/payload/payload.cpp
  src/runtime/cpu/builtins_cpu.cpp
  src/runtime/cpu/execute_bytecode_cpu.cpp
  src/runtime/cpu/fitness_cpu.cpp
)
target_include_directories(gagp_runtime_cpu PUBLIC include)
target_link_libraries(gagp_runtime_cpu PUBLIC gagp_core)

add_library(gagp_evolution
  src/evolution/ast_program.cpp
  src/evolution/node_descriptor.cpp
  src/evolution/case_set.cpp
  src/evolution/population_init.cpp
  src/evolution/timing.cpp
  src/evolution/lifecycle.cpp
  src/evolution/ast_verify.cpp
  src/evolution/ast_type_verify.cpp
  src/evolution/genome.cpp
  src/evolution/grammar_config.cpp
  src/evolution/subtree_utils.cpp
  src/evolution/typed_expr_analysis.cpp
  src/evolution/compiler.cpp
  src/evolution/genome_generation.cpp
  src/evolution/mutation.cpp
  src/evolution/crossover.cpp
  src/evolution/selection.cpp
  src/evolution/repro/prep.cpp
  src/evolution/repro/pack.cpp
  src/evolution/repro/backend.cpp
  src/evolution/repro/gpu.cpp
  src/evolution/evolve.cpp
)
target_include_directories(gagp_evolution PUBLIC include)
target_link_libraries(gagp_evolution PUBLIC gagp_runtime_cpu)

# Compatibility aggregate for existing embedders and tests. New owners should
# link the narrowest target that provides their API surface.
add_library(gagp_cpu INTERFACE)
target_link_libraries(gagp_cpu INTERFACE gagp_evolution)

add_library(gagp_json src/cli/json.cpp)
target_include_directories(gagp_json PUBLIC include)

add_library(gagp_grammar
  src/evolution/grammar/catalog.cpp
  src/evolution/grammar/definition.cpp
  src/evolution/grammar/identity.cpp
  src/evolution/grammar/constants.cpp
  src/evolution/grammar/compiled.cpp
  src/evolution/grammar/structured.cpp
  src/evolution/grammar/random.cpp
  src/evolution/grammar/request.cpp
  src/evolution/grammar/budget.cpp
)
target_include_directories(gagp_grammar PUBLIC include)
target_link_libraries(gagp_grammar PUBLIC gagp_core gagp_json)
target_sources(gagp_evolution PRIVATE
  src/evolution/grammar/generate.cpp
  src/evolution/grammar/values.cpp
  src/evolution/grammar/cache.cpp
  src/evolution/grammar/config_adapter.cpp
  src/evolution/grammar/membership.cpp
)
target_link_libraries(gagp_evolution PUBLIC gagp_grammar)

add_executable(gagp_test_grammar_config_adapter tests/evolution/test_grammar_config_adapter.cpp)
target_link_libraries(gagp_test_grammar_config_adapter PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_config_adapter COMMAND gagp_test_grammar_config_adapter)
set_tests_properties(gagp_test_grammar_config_adapter PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_cache tests/evolution/test_grammar_cache.cpp)
target_link_libraries(gagp_test_grammar_cache PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_cache COMMAND gagp_test_grammar_cache)
set_tests_properties(gagp_test_grammar_cache PROPERTIES LABELS "contract;unit")

add_executable(gagp_test_grammar_membership tests/evolution/test_grammar_membership.cpp)
target_link_libraries(gagp_test_grammar_membership PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_membership COMMAND gagp_test_grammar_membership)
set_tests_properties(gagp_test_grammar_membership PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_population tests/evolution/test_grammar_population.cpp)
target_link_libraries(gagp_test_grammar_population PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_population COMMAND gagp_test_grammar_population)
set_tests_properties(gagp_test_grammar_population PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_generation tests/evolution/test_grammar_generation.cpp)
target_link_libraries(gagp_test_grammar_generation PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_generation COMMAND gagp_test_grammar_generation)
set_tests_properties(gagp_test_grammar_generation PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_catalog tests/evolution/test_grammar_catalog.cpp)
target_link_libraries(gagp_test_grammar_catalog PRIVATE gagp_grammar gagp_evolution)
add_test(NAME gagp_test_grammar_catalog COMMAND gagp_test_grammar_catalog)
set_tests_properties(gagp_test_grammar_catalog PROPERTIES LABELS "contract;unit")
add_executable(gagp_test_grammar_definition tests/evolution/test_grammar_definition.cpp)
target_link_libraries(gagp_test_grammar_definition PRIVATE gagp_grammar)
add_executable(gagp_test_grammar_compiled tests/evolution/test_grammar_compiled.cpp)
target_link_libraries(gagp_test_grammar_compiled PRIVATE gagp_grammar)
target_compile_definitions(gagp_test_grammar_compiled PRIVATE GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_grammar_compiled COMMAND gagp_test_grammar_compiled)
set_tests_properties(gagp_test_grammar_compiled PROPERTIES LABELS "contract;integration")
add_test(NAME gagp_test_grammar_definition COMMAND gagp_test_grammar_definition)
set_tests_properties(gagp_test_grammar_definition PROPERTIES LABELS "contract;integration")

add_library(gagp_cli_support
  src/cli/grammar_artifact.cpp
  src/cli/grammar_population_artifact.cpp
  src/cli/grammar_generate.cpp
  src/cli/codec.cpp
  src/cli/options.cpp
  src/cli/commands.cpp
)
target_include_directories(gagp_cli_support PUBLIC include)
target_link_libraries(gagp_cli_support PUBLIC gagp_cpu gagp_json)

add_executable(gagp_generate_cli src/cli/generate_cli.cpp)
target_link_libraries(gagp_generate_cli PRIVATE gagp_cli_support)

add_executable(gagp_test_grammar_request tests/evolution/test_grammar_request.cpp)
target_link_libraries(gagp_test_grammar_request PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_request COMMAND gagp_test_grammar_request)
set_tests_properties(gagp_test_grammar_request PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_generate_cli tests/evolution/test_grammar_generate_cli.cpp)
target_link_libraries(gagp_test_grammar_generate_cli PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_generate_cli COMMAND gagp_test_grammar_generate_cli)
set_tests_properties(gagp_test_grammar_generate_cli PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_population_artifact tests/evolution/test_grammar_population_artifact.cpp)
target_link_libraries(gagp_test_grammar_population_artifact PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_population_artifact COMMAND gagp_test_grammar_population_artifact)
set_tests_properties(gagp_test_grammar_population_artifact PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_artifact_cli tests/evolution/test_grammar_artifact_cli.cpp)
target_link_libraries(gagp_test_grammar_artifact_cli PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_artifact_cli COMMAND gagp_test_grammar_artifact_cli)
set_tests_properties(gagp_test_grammar_artifact_cli PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_artifact tests/evolution/test_grammar_artifact.cpp)
target_link_libraries(gagp_test_grammar_artifact PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_artifact COMMAND gagp_test_grammar_artifact)
set_tests_properties(gagp_test_grammar_artifact PROPERTIES LABELS "contract;integration")

if(GAGP_BUILD_BENCHMARKS)
  add_executable(gagp_grammar_initialization_bench src/bench/grammar_initialization_bench.cpp)
  target_link_libraries(gagp_grammar_initialization_bench PRIVATE gagp_cli_support)
  add_executable(gagp_runtime_multi_bench
    src/bench/runtime_multi_bench.cpp
  )
  target_link_libraries(gagp_runtime_multi_bench PRIVATE gagp_cpu)
  add_executable(gagp_grammar_migration_bench
    src/bench/grammar_migration_bench.cpp
    src/bench/migration_snapshot.cpp
  )
  target_link_libraries(gagp_grammar_migration_bench PRIVATE gagp_cli_support)
  if(GAGP_HAS_CUDA)
    add_executable(gagp_migration_gpu_result_capture
      src/bench/capture_gpu_results.cu src/bench/migration_snapshot.cpp)
    target_link_libraries(gagp_migration_gpu_result_capture PRIVATE gagp_cli_support)
    target_compile_definitions(gagp_migration_gpu_result_capture PRIVATE
      GAGP_GPU_FITNESS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/gpu/fitness_gpu.cu")
  endif()
  foreach(mode IN ITEMS plain capture)
    add_executable(gagp_migration_evolution_stats_${mode}
      src/bench/capture_evolution_stats.cpp src/bench/migration_snapshot.cpp)
    target_link_libraries(gagp_migration_evolution_stats_${mode} PRIVATE gagp_cli_support)
    add_executable(gagp_migration_cpu_reproduction_${mode}
      src/bench/capture_cpu_donors.cpp src/bench/migration_snapshot.cpp)
    target_link_libraries(gagp_migration_cpu_reproduction_${mode} PRIVATE gagp_cli_support)
    target_include_directories(gagp_migration_cpu_reproduction_${mode} PRIVATE src/evolution)
  endforeach()
  target_compile_definitions(gagp_migration_evolution_stats_capture PRIVATE
    GAGP_EVOLVE_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/evolution/evolve.cpp")
  target_compile_definitions(gagp_migration_cpu_reproduction_capture PRIVATE
    GAGP_MUTATION_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/evolution/mutation.cpp")
  add_executable(gagp_migration_oracle_replay
    src/bench/replay_migration_oracle.cpp
    src/bench/migration_snapshot.cpp
  )
  target_link_libraries(gagp_migration_oracle_replay PRIVATE gagp_cli_support)
  add_executable(gagp_test_migration_snapshot
    tests/runtime/test_migration_snapshot.cpp
    src/bench/migration_snapshot.cpp
  )
  target_include_directories(gagp_test_migration_snapshot PRIVATE src/bench)
  target_link_libraries(gagp_test_migration_snapshot PRIVATE gagp_cli_support)
  add_test(NAME gagp_test_migration_snapshot COMMAND gagp_test_migration_snapshot)
  set_tests_properties(gagp_test_migration_snapshot PROPERTIES LABELS "contract")
endif()

add_executable(gagp_evolve_cli
  src/cli/evolve_cli.cpp
)
target_link_libraries(gagp_evolve_cli PRIVATE gagp_cli_support)
add_test(
  NAME gagp_test_ast_json_boundary
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:gagp_evolve_cli>
    -DCASES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_cases.json
    -DASGP_CASES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_asgp_cases.json
    -DASGP_SCALAR_CASES=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_asgp_scalar_cases.json
    -DVALID_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_valid.json
    -DVALID_ASGP_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_asgp_dc.json
    -DVALID_DP1_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_asgp_dp1d.json
    -DVALID_DP2_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_asgp_dp2d.json
    -DINVALID_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_invalid.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_ast_json_boundary.cmake
)
set_tests_properties(gagp_test_ast_json_boundary PROPERTIES LABELS "contract;integration")
add_test(
  NAME gagp_test_evolve_cli_contract
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:gagp_evolve_cli>
    -DCASES=${CMAKE_CURRENT_SOURCE_DIR}/../data/fixtures/simple_exp_1024.json
    -DOUT_JSON=${CMAKE_CURRENT_BINARY_DIR}/evolve_cli_contract.run.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_evolve_cli_contract.cmake
)
set_tests_properties(gagp_test_evolve_cli_contract PROPERTIES LABELS "contract;integration")
if(GAGP_HAS_CUDA)
  add_library(gagp_gpu
    src/runtime/gpu/fitness_gpu.cu
    src/runtime/gpu/opcode_map_gpu.cpp
    src/runtime/gpu/host_pack_gpu.cu
    src/runtime/payload/payload.cpp
    src/evolution/repro/gpu/arena.cu
    src/evolution/repro/gpu/launch.cu
    src/evolution/repro/gpu/copyback.cu
  )
  target_include_directories(gagp_gpu PUBLIC include)
  target_compile_definitions(gagp_evolution PUBLIC GAGP_HAS_CUDA=1)
  target_link_libraries(gagp_evolution PUBLIC gagp_gpu)

  add_executable(gagp_test_vm_gpu_smoke
    tests/gpu/test_vm_gpu_smoke.cpp
  )
  target_link_libraries(gagp_test_vm_gpu_smoke PRIVATE gagp_gpu)
  add_test(NAME gagp_test_vm_gpu_smoke COMMAND gagp_test_vm_gpu_smoke)
  set_tests_properties(gagp_test_vm_gpu_smoke PROPERTIES LABELS "gpu")

  add_executable(gagp_test_fitness_cpu_gpu_parity
    tests/parity/test_fitness_cpu_gpu_parity.cpp
  )
  target_link_libraries(gagp_test_fitness_cpu_gpu_parity PRIVATE gagp_cpu gagp_gpu)
  add_test(NAME gagp_test_fitness_cpu_gpu_parity COMMAND gagp_test_fitness_cpu_gpu_parity)
  set_tests_properties(gagp_test_fitness_cpu_gpu_parity
    PROPERTIES LABELS "gpu;cpu_gpu_parity")

  add_executable(gagp_test_evolution_cpu_gpu_parity
    tests/parity/test_evolution_cpu_gpu_parity.cpp
  )
  target_link_libraries(gagp_test_evolution_cpu_gpu_parity PRIVATE gagp_cpu gagp_gpu)
  add_test(NAME gagp_test_evolution_cpu_gpu_parity COMMAND gagp_test_evolution_cpu_gpu_parity)
  set_tests_properties(gagp_test_evolution_cpu_gpu_parity
    PROPERTIES LABELS "gpu;cpu_gpu_parity")

  if(GAGP_BUILD_EXPERIMENTS)
    add_executable(gagp_simple_exp_population_probe
      src/experiments/simple_exp_population_probe.cpp
    )
    target_link_libraries(gagp_simple_exp_population_probe PRIVATE gagp_cli_support gagp_gpu)
  endif()

endif()

add_executable(gagp_test_vm_smoke
  tests/runtime/test_vm_smoke.cpp
)
target_link_libraries(gagp_test_vm_smoke PRIVATE gagp_cpu)
add_test(NAME gagp_test_vm_smoke COMMAND gagp_test_vm_smoke)

add_executable(gagp_test_bytecode_verify
  tests/runtime/test_bytecode_verify.cpp
)
target_link_libraries(gagp_test_bytecode_verify PRIVATE gagp_cpu)
add_test(NAME gagp_test_bytecode_verify COMMAND gagp_test_bytecode_verify)
set_tests_properties(gagp_test_bytecode_verify PROPERTIES LABELS "unit;contract;property")

add_executable(gagp_test_cli_json
  tests/runtime/test_cli_json.cpp
)
target_link_libraries(gagp_test_cli_json PRIVATE gagp_cli_support)
add_test(NAME gagp_test_cli_json COMMAND gagp_test_cli_json)
set_tests_properties(gagp_test_cli_json PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_cli_options
  tests/runtime/test_cli_options.cpp
)
target_link_libraries(gagp_test_cli_options PRIVATE gagp_cli_support)
add_test(NAME gagp_test_cli_options COMMAND gagp_test_cli_options)
set_tests_properties(gagp_test_cli_options PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_vm_cli_harness
  tests/runtime/test_vm_cli_harness.cpp
)
target_link_libraries(gagp_test_vm_cli_harness PRIVATE gagp_cli_support)

foreach(contract IN ITEMS scalar control_flow builtins typed_values)
  add_test(
    NAME gagp_test_runtime_${contract}
    COMMAND gagp_test_vm_cli_harness
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/${contract}.json
  )
  set_tests_properties(gagp_test_runtime_${contract} PROPERTIES LABELS "contract")
endforeach()

add_test(
  NAME gagp_test_fixture_codec
  COMMAND ${CMAKE_COMMAND}
    -DHARNESS=$<TARGET_FILE:gagp_test_vm_cli_harness>
    -DVALID_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/scalar.json
    -DMISMATCH_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/fixture_mismatch.json
    -DOLD_FORMAT_FIXTURE=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/runtime/fixture_old_format.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_fixture_harness.cmake
)
set_tests_properties(gagp_test_fixture_codec PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_payload_registry
  tests/runtime/test_payload_registry.cpp
)
target_link_libraries(gagp_test_payload_registry PRIVATE gagp_cpu)
add_test(NAME gagp_test_payload_registry COMMAND gagp_test_payload_registry)
set_tests_properties(gagp_test_payload_registry PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_compiler_lowering
  tests/evolution/test_compiler_lowering.cpp
)
target_link_libraries(gagp_test_compiler_lowering PRIVATE gagp_cpu)
add_test(NAME gagp_test_compiler_lowering COMMAND gagp_test_compiler_lowering)
set_tests_properties(gagp_test_compiler_lowering PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_grammar_config
  tests/evolution/test_grammar_config.cpp
)
target_link_libraries(gagp_test_grammar_config PRIVATE gagp_cli_support)
add_test(
  NAME gagp_test_grammar_config
  COMMAND gagp_test_grammar_config ${CMAKE_CURRENT_SOURCE_DIR}/../configs/grammar/all.json
)
set_tests_properties(gagp_test_grammar_config PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_ast_codec_compaction
  tests/evolution/test_ast_codec_compaction.cpp
)
target_link_libraries(gagp_test_ast_codec_compaction PRIVATE gagp_cli_support)
add_test(NAME gagp_test_ast_codec_compaction COMMAND gagp_test_ast_codec_compaction)
set_tests_properties(gagp_test_ast_codec_compaction PROPERTIES LABELS "contract;property")

add_executable(gagp_test_fitness_contract
  tests/runtime/test_fitness_contract.cpp
)
target_link_libraries(gagp_test_fitness_contract PRIVATE gagp_cpu)
add_test(NAME gagp_test_fitness_contract COMMAND gagp_test_fitness_contract)
set_tests_properties(gagp_test_fitness_contract PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_structured_semantics
  tests/evolution/test_structured_semantics.cpp
)
target_link_libraries(gagp_test_structured_semantics PRIVATE gagp_cpu)
add_test(NAME gagp_test_structured_semantics COMMAND gagp_test_structured_semantics)
set_tests_properties(gagp_test_structured_semantics PROPERTIES LABELS "contract")

add_executable(gagp_test_asgp_semantics
  tests/runtime/test_asgp_semantics.cpp
)
target_link_libraries(gagp_test_asgp_semantics PRIVATE gagp_cpu)
add_test(NAME gagp_test_asgp_semantics COMMAND gagp_test_asgp_semantics)
set_tests_properties(gagp_test_asgp_semantics PROPERTIES LABELS "contract")

add_executable(gagp_test_genome_properties
  tests/evolution/test_genome_properties.cpp
)
target_link_libraries(gagp_test_genome_properties PRIVATE gagp_cpu)
add_test(NAME gagp_test_genome_properties COMMAND gagp_test_genome_properties)
set_tests_properties(gagp_test_genome_properties PROPERTIES LABELS "property")

add_library(gagp_fuzz_support STATIC
  tests/fuzz/fuzz_entrypoints.cpp
)
target_include_directories(gagp_fuzz_support PUBLIC tests/fuzz)
target_link_libraries(gagp_fuzz_support PUBLIC gagp_cli_support)

add_executable(gagp_test_decode_fuzz_smoke
  tests/fuzz/fuzz_smoke.cpp
)
target_link_libraries(gagp_test_decode_fuzz_smoke PRIVATE gagp_fuzz_support)
add_test(
  NAME gagp_test_decode_fuzz_smoke
  COMMAND gagp_test_decode_fuzz_smoke ${CMAKE_CURRENT_SOURCE_DIR}/tests/fuzz/corpus/json
)
set_tests_properties(gagp_test_decode_fuzz_smoke PROPERTIES LABELS "property")

if(GAGP_BUILD_FUZZERS)
  add_executable(gagp_fuzz_bytecode_json tests/fuzz/fuzz_json_target.cpp)
  target_link_libraries(gagp_fuzz_bytecode_json PRIVATE gagp_fuzz_support)
  target_link_options(gagp_fuzz_bytecode_json PRIVATE -fsanitize=fuzzer,address,undefined)

  add_executable(gagp_fuzz_ast_verify tests/fuzz/fuzz_ast_target.cpp)
  target_link_libraries(gagp_fuzz_ast_verify PRIVATE gagp_fuzz_support)
  target_link_options(gagp_fuzz_ast_verify PRIVATE -fsanitize=fuzzer,address,undefined)
endif()

add_executable(gagp_test_genome
  tests/evolution/test_genome.cpp
)
target_link_libraries(gagp_test_genome PRIVATE gagp_cpu)
add_test(NAME gagp_test_genome COMMAND gagp_test_genome)
set_tests_properties(gagp_test_genome PROPERTIES LABELS "property")

add_executable(gagp_test_node_descriptor
  tests/evolution/test_node_descriptor.cpp
)
target_link_libraries(gagp_test_node_descriptor PRIVATE gagp_cpu)
add_test(NAME gagp_test_node_descriptor COMMAND gagp_test_node_descriptor)
set_tests_properties(gagp_test_node_descriptor PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_ast_verify_structure
  tests/evolution/test_ast_verify_structure.cpp
)
target_link_libraries(gagp_test_ast_verify_structure PRIVATE gagp_cpu)
add_test(NAME gagp_test_ast_verify_structure COMMAND gagp_test_ast_verify_structure)
set_tests_properties(gagp_test_ast_verify_structure PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_ast_verify_types
  tests/evolution/test_ast_verify_types.cpp
)
target_include_directories(gagp_test_ast_verify_types PRIVATE src/evolution)
target_link_libraries(gagp_test_ast_verify_types PRIVATE gagp_cpu)
add_test(NAME gagp_test_ast_verify_types COMMAND gagp_test_ast_verify_types)
set_tests_properties(gagp_test_ast_verify_types PROPERTIES LABELS "unit;contract;property")

add_executable(gagp_test_ast_verify_binders
  tests/evolution/test_ast_verify_binders.cpp
)
target_link_libraries(gagp_test_ast_verify_binders PRIVATE gagp_cpu)
add_test(NAME gagp_test_ast_verify_binders COMMAND gagp_test_ast_verify_binders)
set_tests_properties(gagp_test_ast_verify_binders PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_ast_verify_asgp
  tests/evolution/test_ast_verify_asgp.cpp
)
target_link_libraries(gagp_test_ast_verify_asgp PRIVATE gagp_cpu)
add_test(NAME gagp_test_ast_verify_asgp COMMAND gagp_test_ast_verify_asgp)
set_tests_properties(gagp_test_ast_verify_asgp PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_evolve
  tests/evolution/test_evolve.cpp
)
target_link_libraries(gagp_test_evolve PRIVATE gagp_cpu)
add_test(NAME gagp_test_evolve COMMAND gagp_test_evolve)
set_tests_properties(gagp_test_evolve PROPERTIES LABELS "unit")

add_executable(gagp_test_evolution_pipeline
  tests/evolution/test_evolution_pipeline.cpp
)
target_link_libraries(gagp_test_evolution_pipeline PRIVATE gagp_cpu)
add_test(NAME gagp_test_evolution_pipeline COMMAND gagp_test_evolution_pipeline)
set_tests_properties(gagp_test_evolution_pipeline PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_repro_prep
  tests/evolution/test_repro_prep.cpp
)
target_link_libraries(gagp_test_repro_prep PRIVATE gagp_cpu)
add_test(NAME gagp_test_repro_prep COMMAND gagp_test_repro_prep)
set_tests_properties(gagp_test_repro_prep PROPERTIES LABELS "property")

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  if(GAGP_BUILD_BENCHMARKS)
    add_test(NAME gagp_test_evolution_statistics
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_evolution_statistics.py
        --adapter $<TARGET_FILE:gagp_grammar_migration_bench>
        --plain $<TARGET_FILE:gagp_migration_evolution_stats_plain>
        --capture $<TARGET_FILE:gagp_migration_evolution_stats_capture>)
    set_tests_properties(gagp_test_evolution_statistics PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_cpu_donor_tape
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_cpu_donor_tape.py
        --adapter $<TARGET_FILE:gagp_grammar_migration_bench>
        --plain $<TARGET_FILE:gagp_migration_cpu_reproduction_plain>
        --capture $<TARGET_FILE:gagp_migration_cpu_reproduction_capture>)
    set_tests_properties(gagp_test_cpu_donor_tape PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_migration_oracle
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_oracle.py
        --replayer $<TARGET_FILE:gagp_migration_oracle_replay>
        --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration)
    set_tests_properties(gagp_test_migration_oracle PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_migration_ast_rejections
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_oracle.py
        --replayer $<TARGET_FILE:gagp_migration_oracle_replay>
        --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration --verifiers)
    set_tests_properties(gagp_test_migration_ast_rejections PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_migration_bytecode_rejections
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_oracle.py
        --replayer $<TARGET_FILE:gagp_migration_oracle_replay>
        --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration --bytecode-verifiers)
    set_tests_properties(gagp_test_migration_bytecode_rejections PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_migration_boundary_oracle
      COMMAND gagp_migration_oracle_replay
        ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration/reference-boundary-oracle.jsonl
        ${CMAKE_CURRENT_BINARY_DIR}/migration-boundary-replay.json)
    set_tests_properties(gagp_test_migration_boundary_oracle PROPERTIES LABELS "contract;integration")
    add_test(NAME gagp_test_migration_adapter
      COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_adapter.py
        --adapter $<TARGET_FILE:gagp_grammar_migration_bench>)
    set_tests_properties(gagp_test_migration_adapter PROPERTIES LABELS "contract;integration")
    if(GAGP_HAS_CUDA)
      add_test(NAME gagp_test_evolution_statistics_gpu
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_evolution_statistics.py
          --adapter $<TARGET_FILE:gagp_grammar_migration_bench>
          --plain $<TARGET_FILE:gagp_migration_evolution_stats_plain>
          --capture $<TARGET_FILE:gagp_migration_evolution_stats_capture> --gpu)
      set_tests_properties(gagp_test_evolution_statistics_gpu PROPERTIES LABELS "gpu;contract;integration")
      add_test(NAME gagp_test_gpu_result_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_gpu_result_oracle.py
          --capture $<TARGET_FILE:gagp_migration_gpu_result_capture>
          --oracle ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration/reference-oracle.jsonl)
      set_tests_properties(gagp_test_gpu_result_oracle PROPERTIES LABELS "gpu;cpu_gpu_parity;integration")
      add_test(NAME gagp_test_gpu_known_differences
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_gpu_result_oracle.py
          --capture $<TARGET_FILE:gagp_migration_gpu_result_capture>
          --oracle ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration/reference-gpu-differences.jsonl
          --expected-gpu)
      set_tests_properties(gagp_test_gpu_known_differences PROPERTIES LABELS "gpu;contract;integration")
      add_test(NAME gagp_test_gpu_boundary_oracle
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_gpu_result_oracle.py
          --capture $<TARGET_FILE:gagp_migration_gpu_result_capture>
          --oracle ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration/reference-boundary-oracle.jsonl
          --expected-gpu)
      set_tests_properties(gagp_test_gpu_boundary_oracle PROPERTIES LABELS "gpu;contract;integration")
      add_test(NAME gagp_test_migration_oracle_gpu
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_oracle.py
          --replayer $<TARGET_FILE:gagp_migration_oracle_replay>
          --fixtures ${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/migration --gpu)
      set_tests_properties(gagp_test_migration_oracle_gpu PROPERTIES LABELS "gpu;cpu_gpu_parity;integration")
      add_test(NAME gagp_test_migration_adapter_gpu
        COMMAND ${Python3_EXECUTABLE} ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_migration_adapter.py
          --adapter $<TARGET_FILE:gagp_grammar_migration_bench> --gpu)
      set_tests_properties(gagp_test_migration_adapter_gpu PROPERTIES LABELS "gpu;cpu_gpu_parity;integration")
    endif()
  endif()
  add_test(
    NAME gagp_test_repository_contract
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/repository -p test_*.py -v
  )
  set_tests_properties(gagp_test_repository_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    LABELS "tooling;contract")
endif()
