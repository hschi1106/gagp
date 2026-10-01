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
set(GAGP_ASGP_SOURCE_DIR "" CACHE PATH "Optional ASGP checkout for the fixed comparison benchmark")
option(GAGP_ENABLE_OPENSSL_SHA256 "Use system OpenSSL SHA-256 when available" ON)

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
  if(NOT CMAKE_CUDA_COMPILER)
    message(FATAL_ERROR
      "GAGP_ENABLE_CUDA=ON requires a CUDA compiler. Set CUDACXX or "
      "CMAKE_CUDA_COMPILER to nvcc, or configure a CPU-only build with "
      "-DGAGP_ENABLE_CUDA=OFF.")
  endif()
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
  src/runtime/region_plan.cpp
  src/runtime/recurrence_rank.cpp
  src/runtime/sequence_rank.cpp
  src/runtime/semantic_fuel.cpp
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
  src/evolution/bounded_region.cpp
  src/evolution/case_set.cpp
  src/evolution/population_init.cpp
  src/evolution/timing.cpp
  src/evolution/lifecycle.cpp
  src/evolution/ast_verify.cpp
  src/evolution/ast_type_verify.cpp
  src/evolution/genome.cpp
  src/evolution/subtree_utils.cpp
  src/evolution/compiler.cpp
  src/evolution/genome_generation.cpp
  src/evolution/selection.cpp
  src/evolution/repro/prep.cpp
  src/evolution/repro/grammar_prep.cpp
  src/evolution/repro/pack.cpp
  src/evolution/repro/splice_metadata.cpp
  src/evolution/repro/constant_prep.cpp
  src/evolution/repro/compiled_decode.cpp
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

add_library(gagp_region_plan_json src/serialization/region_plan_json.cpp)
target_include_directories(gagp_region_plan_json PUBLIC include)
target_link_libraries(gagp_region_plan_json PUBLIC gagp_core gagp_json)

add_library(gagp_grammar
  src/evolution/fuel_events.cpp
  src/evolution/grammar/catalog.cpp
  src/evolution/grammar/definition.cpp
  src/evolution/grammar/identity.cpp
  src/evolution/grammar/constants.cpp
  src/evolution/grammar/compiled.cpp
  src/evolution/grammar/structured.cpp
  src/evolution/grammar/random.cpp
  src/evolution/grammar/request.cpp
  src/evolution/grammar/frame.cpp
  src/evolution/grammar/budget.cpp
  src/evolution/grammar/joint_budget.cpp
  src/evolution/grammar/resource_projection.cpp
)
target_include_directories(gagp_grammar PUBLIC include)
target_link_libraries(gagp_grammar PUBLIC gagp_core gagp_json gagp_region_plan_json)
if(GAGP_ENABLE_OPENSSL_SHA256)
  find_package(OpenSSL QUIET COMPONENTS Crypto)
  if(OpenSSL_FOUND)
    target_link_libraries(gagp_grammar PRIVATE OpenSSL::Crypto)
    target_compile_definitions(gagp_grammar PRIVATE GAGP_HAS_OPENSSL_SHA256=1)
  endif()
endif()
target_sources(gagp_evolution PRIVATE
  src/evolution/grammar/generate.cpp
  src/evolution/grammar/values.cpp
  src/evolution/grammar/cache.cpp
  src/evolution/grammar/membership.cpp
  src/evolution/grammar/derivation_resources.cpp
  src/evolution/grammar/resource_invariance.cpp
  src/evolution/grammar/donor.cpp
  src/evolution/grammar/variation.cpp
  src/evolution/grammar/crossover.cpp
  src/evolution/grammar/mutation.cpp
  src/evolution/grammar/variation_contract.cpp
  src/evolution/grammar/variation_cache.cpp
)
target_link_libraries(gagp_evolution PUBLIC gagp_grammar)


add_executable(gagp_test_grammar_cache tests/evolution/test_grammar_cache.cpp)
target_link_libraries(gagp_test_grammar_cache PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_cache COMMAND gagp_test_grammar_cache)
set_tests_properties(gagp_test_grammar_cache PROPERTIES LABELS "contract;unit")

add_executable(gagp_test_grammar_membership tests/evolution/test_grammar_membership.cpp)
target_link_libraries(gagp_test_grammar_membership PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_membership COMMAND gagp_test_grammar_membership)
set_tests_properties(gagp_test_grammar_membership PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_packages tests/evolution/test_grammar_packages.cpp)
target_link_libraries(gagp_test_grammar_packages PRIVATE gagp_evolution)
target_compile_definitions(gagp_test_grammar_packages PRIVATE
  GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_grammar_packages COMMAND gagp_test_grammar_packages)
set_tests_properties(gagp_test_grammar_packages PROPERTIES LABELS "contract;integration")

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
  src/cli/grammar_author.cpp
  src/cli/codec.cpp
  src/cli/region_codec.cpp
  src/cli/options.cpp
  src/cli/commands.cpp
)
target_include_directories(gagp_cli_support PUBLIC include)
target_link_libraries(gagp_cli_support PUBLIC gagp_cpu gagp_json gagp_region_plan_json)

add_executable(gagp_generate_cli src/cli/generate_cli.cpp)
target_link_libraries(gagp_generate_cli PRIVATE gagp_cli_support)

add_executable(gagp_grammar_cli src/cli/grammar_cli.cpp)
target_link_libraries(gagp_grammar_cli PRIVATE gagp_cli_support)

add_executable(gagp_test_ast_verify_exact_scope tests/evolution/test_ast_verify_exact_scope.cpp)
target_link_libraries(gagp_test_ast_verify_exact_scope PRIVATE gagp_evolution)
add_test(NAME gagp_test_ast_verify_exact_scope COMMAND gagp_test_ast_verify_exact_scope)
set_tests_properties(gagp_test_ast_verify_exact_scope PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_variation_contract tests/evolution/test_grammar_variation_contract.cpp)
target_link_libraries(gagp_test_grammar_variation_contract PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_variation_contract COMMAND gagp_test_grammar_variation_contract)
set_tests_properties(gagp_test_grammar_variation_contract PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_variation_cache tests/evolution/test_grammar_variation_cache.cpp)
target_link_libraries(gagp_test_grammar_variation_cache PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_variation_cache COMMAND gagp_test_grammar_variation_cache)
set_tests_properties(gagp_test_grammar_variation_cache PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_variation_fallback tests/evolution/test_grammar_variation_fallback.cpp)
target_link_libraries(gagp_test_grammar_variation_fallback PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_variation_fallback COMMAND gagp_test_grammar_variation_fallback)
set_tests_properties(gagp_test_grammar_variation_fallback PROPERTIES LABELS "contract;property")

add_executable(gagp_test_constant_prep tests/evolution/test_constant_prep.cpp)
target_include_directories(gagp_test_constant_prep PRIVATE src/evolution/repro)
target_link_libraries(gagp_test_constant_prep PRIVATE gagp_evolution)
add_test(NAME gagp_test_constant_prep COMMAND gagp_test_constant_prep)
set_tests_properties(gagp_test_constant_prep PROPERTIES LABELS "contract;property;integration")

add_executable(gagp_test_splice_metadata tests/evolution/test_splice_metadata.cpp)
target_include_directories(gagp_test_splice_metadata PRIVATE src/evolution/repro)
target_link_libraries(gagp_test_splice_metadata PRIVATE gagp_cpu)
add_test(NAME gagp_test_splice_metadata COMMAND gagp_test_splice_metadata)
set_tests_properties(gagp_test_splice_metadata PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_repro_prep tests/evolution/test_grammar_repro_prep.cpp)
target_link_libraries(gagp_test_grammar_repro_prep PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_repro_prep COMMAND gagp_test_grammar_repro_prep)
set_tests_properties(gagp_test_grammar_repro_prep PROPERTIES LABELS "contract;property;integration")

add_executable(gagp_test_grammar_reproduction tests/evolution/test_grammar_reproduction.cpp)
target_link_libraries(gagp_test_grammar_reproduction PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_reproduction COMMAND gagp_test_grammar_reproduction)
set_tests_properties(gagp_test_grammar_reproduction PROPERTIES LABELS "contract;property;integration")

add_executable(gagp_test_evolution_pipeline tests/evolution/test_evolution_pipeline.cpp)
target_link_libraries(gagp_test_evolution_pipeline PRIVATE gagp_cpu)
add_test(NAME gagp_test_evolution_pipeline COMMAND gagp_test_evolution_pipeline)
set_tests_properties(gagp_test_evolution_pipeline PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_mutation tests/evolution/test_grammar_mutation.cpp)
target_link_libraries(gagp_test_grammar_mutation PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_mutation COMMAND gagp_test_grammar_mutation)
set_tests_properties(gagp_test_grammar_mutation PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_generation_stages tests/evolution/test_grammar_generation_stages.cpp)
target_link_libraries(gagp_test_grammar_generation_stages PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_generation_stages COMMAND gagp_test_grammar_generation_stages)
set_tests_properties(gagp_test_grammar_generation_stages PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_sequence_constants tests/evolution/test_grammar_sequence_constants.cpp)
target_link_libraries(gagp_test_grammar_sequence_constants PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_sequence_constants COMMAND gagp_test_grammar_sequence_constants)
set_tests_properties(gagp_test_grammar_sequence_constants PROPERTIES LABELS "contract;property")
add_executable(gagp_test_grammar_numeric_constants tests/evolution/test_grammar_numeric_constants.cpp)
target_link_libraries(gagp_test_grammar_numeric_constants PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_numeric_constants COMMAND gagp_test_grammar_numeric_constants)
set_tests_properties(gagp_test_grammar_numeric_constants PROPERTIES LABELS "contract;property")

add_executable(gagp_test_resource_projection tests/evolution/test_resource_projection.cpp)
target_link_libraries(gagp_test_resource_projection PRIVATE gagp_grammar)
add_test(NAME gagp_test_resource_projection COMMAND gagp_test_resource_projection)
set_tests_properties(gagp_test_resource_projection PROPERTIES LABELS "contract;property")

add_executable(gagp_test_derivation_resources tests/evolution/test_derivation_resources.cpp)
target_link_libraries(gagp_test_derivation_resources PRIVATE gagp_evolution)
add_test(NAME gagp_test_derivation_resources COMMAND gagp_test_derivation_resources)
set_tests_properties(gagp_test_derivation_resources PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_crossover tests/evolution/test_grammar_crossover.cpp)
target_link_libraries(gagp_test_grammar_crossover PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_crossover COMMAND gagp_test_grammar_crossover)
set_tests_properties(gagp_test_grammar_crossover PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_donor tests/evolution/test_grammar_donor.cpp)
target_link_libraries(gagp_test_grammar_donor PRIVATE gagp_evolution)
add_test(NAME gagp_test_grammar_donor COMMAND gagp_test_grammar_donor)
set_tests_properties(gagp_test_grammar_donor PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_contextual_donor tests/evolution/test_grammar_contextual_donor.cpp)
target_link_libraries(gagp_test_grammar_contextual_donor PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_contextual_donor COMMAND gagp_test_grammar_contextual_donor)
set_tests_properties(gagp_test_grammar_contextual_donor PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_frame tests/evolution/test_grammar_frame.cpp)
target_link_libraries(gagp_test_grammar_frame PRIVATE gagp_grammar)
add_test(NAME gagp_test_grammar_frame COMMAND gagp_test_grammar_frame)
set_tests_properties(gagp_test_grammar_frame PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_witness tests/evolution/test_grammar_witness.cpp)
target_link_libraries(gagp_test_grammar_witness PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_witness COMMAND gagp_test_grammar_witness)
set_tests_properties(gagp_test_grammar_witness PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_request tests/evolution/test_grammar_request.cpp)
target_link_libraries(gagp_test_grammar_request PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_request COMMAND gagp_test_grammar_request)
set_tests_properties(gagp_test_grammar_request PROPERTIES LABELS "contract;property")

add_executable(gagp_test_grammar_generate_cli tests/evolution/test_grammar_generate_cli.cpp)
target_link_libraries(gagp_test_grammar_generate_cli PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_generate_cli COMMAND gagp_test_grammar_generate_cli)
set_tests_properties(gagp_test_grammar_generate_cli PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_author_cli tests/evolution/test_grammar_author_cli.cpp)
target_link_libraries(gagp_test_grammar_author_cli PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_author_cli COMMAND gagp_test_grammar_author_cli)
set_tests_properties(gagp_test_grammar_author_cli PROPERTIES LABELS "contract;integration")

add_test(NAME gagp_test_grammar_examples
  COMMAND ${CMAKE_COMMAND}
    -DGRAMMAR_CLI=$<TARGET_FILE:gagp_grammar_cli>
    -DGENERATE_CLI=$<TARGET_FILE:gagp_generate_cli>
    -DREPOSITORY_ROOT=${CMAKE_CURRENT_SOURCE_DIR}/..
    -DOUTPUT_DIR=${CMAKE_CURRENT_BINARY_DIR}/grammar-example-test
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/evolution/check_grammar_examples.cmake)
set_tests_properties(gagp_test_grammar_examples PROPERTIES LABELS "contract;integration")

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
  add_executable(gagp_runtime_multi_bench
    src/bench/runtime_multi_bench.cpp
  )
  target_link_libraries(gagp_runtime_multi_bench PRIVATE gagp_cpu)
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
    -DVALID_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_valid.json
    -DINVALID_AST=${CMAKE_CURRENT_SOURCE_DIR}/tests/fixtures/ast_eval_invalid.json
    -P ${CMAKE_CURRENT_SOURCE_DIR}/tests/runtime/check_ast_json_boundary.cmake
)
set_tests_properties(gagp_test_ast_json_boundary PROPERTIES LABELS "contract;integration")
add_test(
  NAME gagp_test_evolve_cli_contract
  COMMAND ${CMAKE_COMMAND}
    -DCLI=$<TARGET_FILE:gagp_evolve_cli>
    -DGENERATOR=$<TARGET_FILE:gagp_generate_cli>
    -DCASES=${CMAKE_CURRENT_SOURCE_DIR}/../configs/grammar/examples/restricted_combine.cases.json
    -DGRAMMAR=${CMAKE_CURRENT_SOURCE_DIR}/../configs/grammar/examples/restricted_combine.json
    -DLEGACY_GRAMMAR=${CMAKE_CURRENT_SOURCE_DIR}/../configs/grammar/migration/v1/all.json
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
  target_link_libraries(gagp_gpu PUBLIC gagp_core)
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

add_executable(gagp_test_payload_staging tests/runtime/test_payload_staging.cpp)
target_link_libraries(gagp_test_payload_staging PRIVATE gagp_cpu)
add_test(NAME gagp_test_payload_staging COMMAND gagp_test_payload_staging)
set_tests_properties(gagp_test_payload_staging PROPERTIES LABELS "unit;contract")

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

find_package(Python3 COMPONENTS Interpreter QUIET)
if(Python3_Interpreter_FOUND)
  add_test(
    NAME gagp_test_repository_contract
    COMMAND ${Python3_EXECUTABLE} -m unittest discover -s tests/repository -p test_*.py -v
  )
  set_tests_properties(gagp_test_repository_contract PROPERTIES
    WORKING_DIRECTORY ${CMAKE_CURRENT_SOURCE_DIR}/..
    LABELS "tooling;contract")
endif()

add_executable(gagp_test_region_plan tests/runtime/test_region_plan.cpp)
target_link_libraries(gagp_test_region_plan PRIVATE gagp_core)
add_test(NAME gagp_test_region_plan COMMAND gagp_test_region_plan)
set_tests_properties(gagp_test_region_plan PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_sequence_rank tests/runtime/test_sequence_rank.cpp)
target_link_libraries(gagp_test_sequence_rank PRIVATE gagp_core)
add_test(NAME gagp_test_sequence_rank COMMAND gagp_test_sequence_rank)
set_tests_properties(gagp_test_sequence_rank PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_bounded_sequence tests/runtime/test_bounded_sequence.cpp)
target_link_libraries(gagp_test_bounded_sequence PRIVATE gagp_runtime_cpu)
add_test(NAME gagp_test_bounded_sequence COMMAND gagp_test_bounded_sequence)
set_tests_properties(gagp_test_bounded_sequence PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_recurrence_memo tests/runtime/test_recurrence_memo.cpp)
target_link_libraries(gagp_test_recurrence_memo PRIVATE gagp_core)
add_test(NAME gagp_test_recurrence_memo COMMAND gagp_test_recurrence_memo)
set_tests_properties(gagp_test_recurrence_memo PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_bounded_region tests/runtime/test_bounded_region.cpp)
target_link_libraries(gagp_test_bounded_region PRIVATE gagp_core)
add_test(NAME gagp_test_bounded_region COMMAND gagp_test_bounded_region)
set_tests_properties(gagp_test_bounded_region PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_recurrence_rank tests/runtime/test_recurrence_rank.cpp)
target_link_libraries(gagp_test_recurrence_rank PRIVATE gagp_core)
add_test(NAME gagp_test_recurrence_rank COMMAND gagp_test_recurrence_rank)
set_tests_properties(gagp_test_recurrence_rank PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_recurrence_rank_properties tests/runtime/test_recurrence_rank_properties.cpp)
target_link_libraries(gagp_test_recurrence_rank_properties PRIVATE gagp_core)
add_test(NAME gagp_test_recurrence_rank_properties COMMAND gagp_test_recurrence_rank_properties)
set_tests_properties(gagp_test_recurrence_rank_properties PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_semantic_fuel tests/runtime/test_semantic_fuel.cpp)
target_link_libraries(gagp_test_semantic_fuel PRIVATE gagp_core)
add_test(NAME gagp_test_semantic_fuel COMMAND gagp_test_semantic_fuel)
set_tests_properties(gagp_test_semantic_fuel PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_semantic_fuel_execution tests/runtime/test_semantic_fuel_execution.cpp)
target_link_libraries(gagp_test_semantic_fuel_execution PRIVATE gagp_cpu)
add_test(NAME gagp_test_semantic_fuel_execution COMMAND gagp_test_semantic_fuel_execution)
set_tests_properties(gagp_test_semantic_fuel_execution PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_builtin_arguments tests/runtime/test_builtin_arguments.cpp)
target_link_libraries(gagp_test_builtin_arguments PRIVATE gagp_cpu)
add_test(NAME gagp_test_builtin_arguments COMMAND gagp_test_builtin_arguments)
set_tests_properties(gagp_test_builtin_arguments PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_value_stack tests/runtime/test_value_stack.cpp)
target_link_libraries(gagp_test_value_stack PRIVATE gagp_cpu)
add_test(NAME gagp_test_value_stack COMMAND gagp_test_value_stack)
set_tests_properties(gagp_test_value_stack PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_payload_index tests/runtime/test_payload_index.cpp)
target_link_libraries(gagp_test_payload_index PRIVATE gagp_cpu)
add_test(NAME gagp_test_payload_index COMMAND gagp_test_payload_index)
set_tests_properties(gagp_test_payload_index PROPERTIES LABELS "unit;contract")

if(GAGP_HAS_CUDA)
  add_executable(gagp_test_semantic_fuel_gpu_rejection tests/gpu/test_semantic_fuel_gpu_rejection.cpp)
  target_include_directories(gagp_test_semantic_fuel_gpu_rejection PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
  target_link_libraries(gagp_test_semantic_fuel_gpu_rejection PRIVATE gagp_gpu)
  add_test(NAME gagp_test_semantic_fuel_gpu_rejection COMMAND gagp_test_semantic_fuel_gpu_rejection)
  set_tests_properties(gagp_test_semantic_fuel_gpu_rejection PROPERTIES LABELS "unit;contract;gpu")

  add_executable(gagp_test_lexical_region_gpu tests/gpu/test_lexical_region_gpu.cpp)
  target_include_directories(gagp_test_lexical_region_gpu PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
  target_link_libraries(gagp_test_lexical_region_gpu PRIVATE gagp_cli_support gagp_gpu)
  add_test(NAME gagp_test_lexical_region_gpu COMMAND gagp_test_lexical_region_gpu)
  set_tests_properties(gagp_test_lexical_region_gpu PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_compiled_evolution_stress_gpu tests/gpu/test_compiled_evolution_stress_gpu.cpp)
  target_link_libraries(gagp_test_compiled_evolution_stress_gpu PRIVATE gagp_gpu gagp_cpu)
  target_compile_definitions(gagp_test_compiled_evolution_stress_gpu PRIVATE GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME gagp_test_compiled_evolution_stress_gpu COMMAND gagp_test_compiled_evolution_stress_gpu)
  set_tests_properties(gagp_test_compiled_evolution_stress_gpu PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_compiled_payload_evolution_gpu tests/gpu/test_compiled_payload_evolution_gpu.cpp)
  target_link_libraries(gagp_test_compiled_payload_evolution_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_payload_evolution_gpu COMMAND gagp_test_compiled_payload_evolution_gpu)
  set_tests_properties(gagp_test_compiled_payload_evolution_gpu PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_compiled_backend_gpu tests/gpu/test_compiled_backend_gpu.cpp)
  target_link_libraries(gagp_test_compiled_backend_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_backend_gpu COMMAND gagp_test_compiled_backend_gpu)
  set_tests_properties(gagp_test_compiled_backend_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_compiled_mutation_gpu tests/gpu/test_compiled_mutation_gpu.cu)
  target_link_libraries(gagp_test_compiled_mutation_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_mutation_gpu COMMAND gagp_test_compiled_mutation_gpu)
  set_tests_properties(gagp_test_compiled_mutation_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_constant_mutation_gpu tests/gpu/test_constant_mutation_gpu.cu)
  target_link_libraries(gagp_test_constant_mutation_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_constant_mutation_gpu COMMAND gagp_test_constant_mutation_gpu)
  set_tests_properties(gagp_test_constant_mutation_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_compiled_transport_gpu tests/gpu/test_compiled_transport_gpu.cu)
  target_link_libraries(gagp_test_compiled_transport_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_transport_gpu COMMAND gagp_test_compiled_transport_gpu)
  set_tests_properties(gagp_test_compiled_transport_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_compiled_variation_gpu tests/gpu/test_compiled_variation_gpu.cu)
  target_link_libraries(gagp_test_compiled_variation_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_variation_gpu COMMAND gagp_test_compiled_variation_gpu)
  set_tests_properties(gagp_test_compiled_variation_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_grammar_random_gpu tests/gpu/test_grammar_random_gpu.cu)
  target_link_libraries(gagp_test_grammar_random_gpu PRIVATE gagp_gpu gagp_evolution)
  add_test(NAME gagp_test_grammar_random_gpu COMMAND gagp_test_grammar_random_gpu)
  set_tests_properties(gagp_test_grammar_random_gpu PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_compiled_splice_gpu tests/gpu/test_compiled_splice_gpu.cu)
  target_link_libraries(gagp_test_compiled_splice_gpu PRIVATE gagp_gpu gagp_cpu)
  add_test(NAME gagp_test_compiled_splice_gpu COMMAND gagp_test_compiled_splice_gpu)
  set_tests_properties(gagp_test_compiled_splice_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_atomic_splice_gpu tests/gpu/test_atomic_splice_gpu.cu)
  target_link_libraries(gagp_test_atomic_splice_gpu PRIVATE gagp_gpu)
  add_test(NAME gagp_test_atomic_splice_gpu COMMAND gagp_test_atomic_splice_gpu)
  set_tests_properties(gagp_test_atomic_splice_gpu PROPERTIES LABELS "contract;integration;gpu")

  add_executable(gagp_test_bounded_region_gpu_results
    tests/gpu/test_bounded_region_gpu_results.cu)
  target_link_libraries(gagp_test_bounded_region_gpu_results PRIVATE gagp_cli_support)
  target_compile_definitions(gagp_test_bounded_region_gpu_results PRIVATE
    GAGP_GPU_FITNESS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/gpu/fitness_gpu.cu"
    GAGP_CAPTURE_HAS_GENERIC_REGIONS=1)
  add_test(NAME gagp_test_bounded_region_gpu_results COMMAND gagp_test_bounded_region_gpu_results)
  set_tests_properties(gagp_test_bounded_region_gpu_results PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_grammar_package_gpu_results
    tests/gpu/test_grammar_package_gpu_results.cu)
  target_link_libraries(gagp_test_grammar_package_gpu_results PRIVATE gagp_cli_support)
  target_compile_definitions(gagp_test_grammar_package_gpu_results PRIVATE
    GAGP_GPU_FITNESS_SOURCE="${CMAKE_CURRENT_SOURCE_DIR}/src/runtime/gpu/fitness_gpu.cu"
    GAGP_CAPTURE_HAS_GENERIC_REGIONS=1
    GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
  add_test(NAME gagp_test_grammar_package_gpu_results COMMAND gagp_test_grammar_package_gpu_results)
  set_tests_properties(gagp_test_grammar_package_gpu_results PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")

  add_executable(gagp_test_bounded_region_gpu tests/gpu/test_bounded_region_gpu.cpp)
  target_include_directories(gagp_test_bounded_region_gpu PRIVATE ${CMAKE_CUDA_TOOLKIT_INCLUDE_DIRECTORIES})
  target_link_libraries(gagp_test_bounded_region_gpu PRIVATE gagp_cli_support gagp_gpu)
  add_test(NAME gagp_test_bounded_region_gpu COMMAND gagp_test_bounded_region_gpu)
  set_tests_properties(gagp_test_bounded_region_gpu PROPERTIES LABELS "contract;integration;gpu;cpu_gpu_parity")
endif()

add_executable(gagp_test_lexical_regions tests/evolution/test_lexical_regions.cpp)
target_link_libraries(gagp_test_lexical_regions PRIVATE gagp_cli_support)
add_test(NAME gagp_test_lexical_regions COMMAND gagp_test_lexical_regions)
set_tests_properties(gagp_test_lexical_regions PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_region_codec tests/evolution/test_region_codec.cpp)
target_link_libraries(gagp_test_region_codec PRIVATE gagp_cli_support)
add_test(NAME gagp_test_region_codec COMMAND gagp_test_region_codec)
set_tests_properties(gagp_test_region_codec PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_region_splice tests/evolution/test_region_splice.cpp)
target_include_directories(gagp_test_region_splice PRIVATE src/evolution)
target_link_libraries(gagp_test_region_splice PRIVATE gagp_cpu)
add_test(NAME gagp_test_region_splice COMMAND gagp_test_region_splice)
set_tests_properties(gagp_test_region_splice PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_grammar_regions tests/evolution/test_grammar_regions.cpp)
target_link_libraries(gagp_test_grammar_regions PRIVATE gagp_cli_support)
target_compile_definitions(gagp_test_grammar_regions PRIVATE GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_grammar_regions COMMAND gagp_test_grammar_regions)
set_tests_properties(gagp_test_grammar_regions PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_region_artifacts tests/evolution/test_grammar_region_artifacts.cpp)
target_link_libraries(gagp_test_grammar_region_artifacts PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_region_artifacts COMMAND gagp_test_grammar_region_artifacts)
set_tests_properties(gagp_test_grammar_region_artifacts PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_grammar_region_variation tests/evolution/test_grammar_region_variation.cpp)
target_link_libraries(gagp_test_grammar_region_variation PRIVATE gagp_cli_support)
add_test(NAME gagp_test_grammar_region_variation COMMAND gagp_test_grammar_region_variation)
set_tests_properties(gagp_test_grammar_region_variation PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_checked_values tests/evolution/test_checked_values.cpp)
target_link_libraries(gagp_test_checked_values PRIVATE gagp_cpu)
add_test(NAME gagp_test_checked_values COMMAND gagp_test_checked_values)
set_tests_properties(gagp_test_checked_values PROPERTIES LABELS "contract;unit")

add_executable(gagp_test_ast_fuel_codec tests/evolution/test_ast_fuel_codec.cpp)
target_link_libraries(gagp_test_ast_fuel_codec PRIVATE gagp_cli_support)
add_test(NAME gagp_test_ast_fuel_codec COMMAND gagp_test_ast_fuel_codec)
set_tests_properties(gagp_test_ast_fuel_codec PROPERTIES LABELS "contract;unit")

add_executable(gagp_test_source_fuel tests/evolution/test_source_fuel.cpp)
target_link_libraries(gagp_test_source_fuel PRIVATE gagp_cpu)
add_test(NAME gagp_test_source_fuel COMMAND gagp_test_source_fuel)
set_tests_properties(gagp_test_source_fuel PROPERTIES LABELS "contract;unit")

add_executable(gagp_test_fuel_optimization tests/evolution/test_fuel_optimization.cpp)
target_link_libraries(gagp_test_fuel_optimization PRIVATE gagp_cpu)
add_test(NAME gagp_test_fuel_optimization COMMAND gagp_test_fuel_optimization)
set_tests_properties(gagp_test_fuel_optimization PROPERTIES LABELS "contract;unit")

# Transition-only oracle adapter; production executables do not link this target.
add_library(gagp_transition
  src/migration/typed_storage.cpp
  src/migration/legacy_budget.cpp
  src/migration/legacy_constants.cpp
  src/migration/artifact.cpp
  src/migration/legacy_grammar_config.cpp
  src/transition/legacy_ast_v1.cpp
  src/transition/linear_rec.cpp
  src/transition/bounded_regions.cpp)
target_include_directories(gagp_transition PUBLIC include)
target_link_libraries(gagp_transition PUBLIC gagp_cpu gagp_cli_support)

add_executable(gagp_test_typed_storage tests/migration/test_typed_storage.cpp)
add_executable(gagp_test_legacy_budget tests/migration/test_legacy_budget.cpp)
target_link_libraries(gagp_test_legacy_budget PRIVATE gagp_transition)
add_test(NAME gagp_test_legacy_budget COMMAND gagp_test_legacy_budget)
set_tests_properties(gagp_test_legacy_budget PROPERTIES LABELS "contract;migration;unit")
target_link_libraries(gagp_test_typed_storage PRIVATE gagp_transition)
add_test(NAME gagp_test_typed_storage COMMAND gagp_test_typed_storage)
set_tests_properties(gagp_test_typed_storage PROPERTIES LABELS "contract;migration;integration")

add_executable(gagp_migrate_artifact src/migration/migrate_artifact_cli.cpp)
target_link_libraries(gagp_migrate_artifact PRIVATE gagp_transition)

if(GAGP_BUILD_BENCHMARKS)
  # Goal 11 comparison adapter. It is deliberately transition-only and is not
  # linked into either production CLI.
  add_executable(gagp_final_candidate_bench
    src/bench/final_candidate_bench.cpp
    src/bench/final_candidate_snapshot.cpp)
  target_link_libraries(gagp_final_candidate_bench PRIVATE gagp_transition)
  add_executable(gagp_final_candidate_stats
    src/bench/final_candidate_stats.cpp
    src/bench/final_candidate_snapshot.cpp)
  target_link_libraries(gagp_final_candidate_stats PRIVATE gagp_transition)
  if(GAGP_HAS_CUDA)
    target_link_libraries(gagp_final_candidate_stats PRIVATE gagp_gpu)
    target_link_libraries(gagp_final_candidate_bench PRIVATE gagp_gpu)
  endif()
  if(GAGP_ASGP_SOURCE_DIR)
    if(NOT GAGP_HAS_CUDA)
      message(FATAL_ERROR "The fixed ASGP comparison requires CUDA")
    endif()
    add_subdirectory("${GAGP_ASGP_SOURCE_DIR}/src" "external-asgp" EXCLUDE_FROM_ALL)
    target_compile_features(asgp_core PUBLIC cxx_std_20)
    add_executable(gagp_fixed_asgp_bench
      src/bench/fixed_asgp/main.cpp
      src/bench/fixed_asgp/adapter.cpp)
    target_compile_features(gagp_fixed_asgp_bench PRIVATE cxx_std_20)
    target_link_libraries(gagp_fixed_asgp_bench PRIVATE gagp_cli_support gagp_gpu asgp_core)
  endif()
endif()

find_package(Python3 COMPONENTS Interpreter REQUIRED)
if(TARGET gagp_fixed_asgp_bench)
  add_test(NAME gagp_test_fixed_asgp_bench
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/evolution/test_fixed_asgp_bench.py
      $<TARGET_FILE:gagp_fixed_asgp_bench> ${CMAKE_CURRENT_SOURCE_DIR}/..)
  set_tests_properties(gagp_test_fixed_asgp_bench PROPERTIES
    LABELS "contract;integration;benchmark;gpu" TIMEOUT 180)
endif()
add_test(NAME gagp_test_migrate_artifact_cli
  COMMAND ${Python3_EXECUTABLE}
    ${CMAKE_CURRENT_SOURCE_DIR}/tests/migration/test_migrate_artifact_cli.py
    $<TARGET_FILE:gagp_migrate_artifact>
    ${CMAKE_CURRENT_SOURCE_DIR}/..
    $<TARGET_FILE:gagp_evolve_cli>)
set_tests_properties(gagp_test_migrate_artifact_cli PROPERTIES
  LABELS "contract;integration;migration")

if(GAGP_BUILD_BENCHMARKS)
  add_test(NAME gagp_test_final_candidate_bench
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/migration/test_final_candidate_bench.py
      $<TARGET_FILE:gagp_final_candidate_bench>
      $<TARGET_FILE:gagp_generate_cli>)
  set_tests_properties(gagp_test_final_candidate_bench PROPERTIES
    LABELS "contract;integration;migration;benchmark")
  add_test(NAME gagp_test_final_candidate_stats
    COMMAND ${Python3_EXECUTABLE}
      ${CMAKE_CURRENT_SOURCE_DIR}/tests/migration/test_final_candidate_stats.py
      $<TARGET_FILE:gagp_final_candidate_bench>
      $<TARGET_FILE:gagp_final_candidate_stats>
      $<TARGET_FILE:gagp_generate_cli>
      ${CMAKE_CURRENT_SOURCE_DIR}/..
      $<BOOL:${GAGP_HAS_CUDA}>)
  set_tests_properties(gagp_test_final_candidate_stats PROPERTIES
    LABELS "contract;integration;migration;benchmark")
  if(GAGP_HAS_CUDA)
    set_property(TEST gagp_test_final_candidate_stats APPEND PROPERTY LABELS gpu)
  endif()
endif()

add_executable(gagp_test_legacy_ast_migration_boundary
  tests/evolution/test_legacy_ast_migration_boundary.cpp)
target_link_libraries(gagp_test_legacy_ast_migration_boundary PRIVATE gagp_transition)
add_test(NAME gagp_test_legacy_ast_migration_boundary
  COMMAND gagp_test_legacy_ast_migration_boundary)
set_tests_properties(gagp_test_legacy_ast_migration_boundary PROPERTIES
  LABELS "contract;integration;migration")

add_executable(gagp_test_legacy_package_equivalence
  tests/migration/test_legacy_package_equivalence.cpp)
target_link_libraries(gagp_test_legacy_package_equivalence PRIVATE gagp_transition)
target_compile_definitions(gagp_test_legacy_package_equivalence PRIVATE
  GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_legacy_package_equivalence
  COMMAND gagp_test_legacy_package_equivalence)
set_tests_properties(gagp_test_legacy_package_equivalence PROPERTIES
  LABELS "contract;integration;migration")

add_executable(gagp_test_source_fuel_preservation tests/evolution/test_source_fuel_preservation.cpp)
target_link_libraries(gagp_test_source_fuel_preservation PRIVATE gagp_cli_support)
target_include_directories(gagp_test_source_fuel_preservation PRIVATE src/evolution)
add_test(NAME gagp_test_source_fuel_preservation COMMAND gagp_test_source_fuel_preservation)
set_tests_properties(gagp_test_source_fuel_preservation PROPERTIES LABELS "contract;integration")

add_executable(gagp_test_region_bytecode_verify tests/runtime/test_region_bytecode_verify.cpp)
target_link_libraries(gagp_test_region_bytecode_verify PRIVATE gagp_core)
add_test(NAME gagp_test_region_bytecode_verify COMMAND gagp_test_region_bytecode_verify)
set_tests_properties(gagp_test_region_bytecode_verify PROPERTIES LABELS "unit;contract")

add_executable(gagp_test_region_execution tests/runtime/test_region_execution.cpp)
target_link_libraries(gagp_test_region_execution PRIVATE gagp_runtime_cpu)
add_test(NAME gagp_test_region_execution COMMAND gagp_test_region_execution)
set_tests_properties(gagp_test_region_execution PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_cpu_execution_session tests/runtime/test_cpu_execution_session.cpp)
target_link_libraries(gagp_test_cpu_execution_session PRIVATE gagp_runtime_cpu)
add_test(NAME gagp_test_cpu_execution_session COMMAND gagp_test_cpu_execution_session)
set_tests_properties(gagp_test_cpu_execution_session PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_region_bytecode_codec tests/runtime/test_region_codec.cpp)
target_link_libraries(gagp_test_region_bytecode_codec PRIVATE gagp_cli_support)
add_test(NAME gagp_test_region_bytecode_codec COMMAND gagp_test_region_bytecode_codec)
set_tests_properties(gagp_test_region_bytecode_codec PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_bounded_region_structure tests/evolution/test_bounded_region_structure.cpp)
target_include_directories(gagp_test_bounded_region_structure PRIVATE src/evolution)
target_link_libraries(gagp_test_bounded_region_structure PRIVATE gagp_evolution)
add_test(NAME gagp_test_bounded_region_structure COMMAND gagp_test_bounded_region_structure)
set_tests_properties(gagp_test_bounded_region_structure PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_bounded_region_typing tests/evolution/test_bounded_region_typing.cpp)
target_include_directories(gagp_test_bounded_region_typing PRIVATE src/evolution)
target_link_libraries(gagp_test_bounded_region_typing PRIVATE gagp_evolution)
add_test(NAME gagp_test_bounded_region_typing COMMAND gagp_test_bounded_region_typing)
set_tests_properties(gagp_test_bounded_region_typing PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_bounded_region_compile tests/evolution/test_bounded_region_compile.cpp)
target_include_directories(gagp_test_bounded_region_compile PRIVATE src/evolution)
target_link_libraries(gagp_test_bounded_region_compile PRIVATE gagp_cpu)
add_test(NAME gagp_test_bounded_region_compile COMMAND gagp_test_bounded_region_compile)
set_tests_properties(gagp_test_bounded_region_compile PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_bounded_region_ast_codec tests/evolution/test_bounded_region_ast_codec.cpp)
target_include_directories(gagp_test_bounded_region_ast_codec PRIVATE src/evolution)
target_link_libraries(gagp_test_bounded_region_ast_codec PRIVATE gagp_cli_support)
add_test(NAME gagp_test_bounded_region_ast_codec COMMAND gagp_test_bounded_region_ast_codec)
set_tests_properties(gagp_test_bounded_region_ast_codec PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_bounded_region_sequence_compile tests/evolution/test_bounded_region_sequence_compile.cpp)
target_link_libraries(gagp_test_bounded_region_sequence_compile PRIVATE gagp_cpu)
add_test(NAME gagp_test_bounded_region_sequence_compile COMMAND gagp_test_bounded_region_sequence_compile)
set_tests_properties(gagp_test_bounded_region_sequence_compile PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_bounded_parse tests/evolution/test_grammar_bounded_parse.cpp)
target_link_libraries(gagp_test_grammar_bounded_parse PRIVATE gagp_cpu)
add_test(NAME gagp_test_grammar_bounded_parse COMMAND gagp_test_grammar_bounded_parse)
set_tests_properties(gagp_test_grammar_bounded_parse PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_bounded_generation tests/evolution/test_grammar_bounded_generation.cpp)
target_link_libraries(gagp_test_grammar_bounded_generation PRIVATE gagp_cpu)
target_compile_definitions(gagp_test_grammar_bounded_generation PRIVATE GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_grammar_bounded_generation COMMAND gagp_test_grammar_bounded_generation)
set_tests_properties(gagp_test_grammar_bounded_generation PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_bounded_membership tests/evolution/test_grammar_bounded_membership.cpp)
target_link_libraries(gagp_test_grammar_bounded_membership PRIVATE gagp_cpu)
add_test(NAME gagp_test_grammar_bounded_membership COMMAND gagp_test_grammar_bounded_membership)
set_tests_properties(gagp_test_grammar_bounded_membership PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_bounded_variation tests/evolution/test_grammar_bounded_variation.cpp)
target_link_libraries(gagp_test_grammar_bounded_variation PRIVATE gagp_cpu)
add_test(NAME gagp_test_grammar_bounded_variation COMMAND gagp_test_grammar_bounded_variation)
set_tests_properties(gagp_test_grammar_bounded_variation PROPERTIES LABELS "unit;contract;integration")

add_executable(gagp_test_grammar_bounded_artifact tests/evolution/test_grammar_bounded_artifact.cpp)
target_link_libraries(gagp_test_grammar_bounded_artifact PRIVATE gagp_cli_support)
target_compile_definitions(gagp_test_grammar_bounded_artifact PRIVATE GAGP_REPOSITORY_ROOT="${CMAKE_CURRENT_SOURCE_DIR}/..")
add_test(NAME gagp_test_grammar_bounded_artifact COMMAND gagp_test_grammar_bounded_artifact)
set_tests_properties(gagp_test_grammar_bounded_artifact PROPERTIES LABELS "contract;integration")
