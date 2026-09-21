if(NOT DEFINED GRAMMAR_CLI OR NOT DEFINED GENERATE_CLI OR
   NOT DEFINED REPOSITORY_ROOT OR NOT DEFINED OUTPUT_DIR)
  message(FATAL_ERROR
    "GRAMMAR_CLI, GENERATE_CLI, REPOSITORY_ROOT, and OUTPUT_DIR are required")
endif()

set(example_names
  authoring/scalar
  authoring/sequence
  authoring/template
  authoring/memo
  types/int
  types/float
  types/bool
  types/char
  types/string
  types/int_list
  types/float_list
  types/string_list)

file(REMOVE_RECURSE "${OUTPUT_DIR}")
file(MAKE_DIRECTORY "${OUTPUT_DIR}")

foreach(name IN LISTS example_names)
  set(grammar "${REPOSITORY_ROOT}/configs/grammar/examples/${name}.json")
  set(cases "${REPOSITORY_ROOT}/configs/grammar/examples/${name}.cases.json")
  string(REPLACE "/" "-" output_name "${name}")

  execute_process(
    COMMAND "${GRAMMAR_CLI}" validate --grammar-definition "${grammar}"
    RESULT_VARIABLE validate_result
    OUTPUT_QUIET
    ERROR_VARIABLE validate_error)
  if(NOT validate_result EQUAL 0)
    message(FATAL_ERROR
      "maintained grammar validation failed for ${name}: ${validate_error}")
  endif()

  execute_process(
    COMMAND "${GENERATE_CLI}"
      --grammar-definition "${grammar}"
      --cases "${cases}"
      --population-size 2
      --seed 7
      --out-json "${OUTPUT_DIR}/${output_name}.json"
    RESULT_VARIABLE generate_result
    OUTPUT_QUIET
    ERROR_VARIABLE generate_error)
  if(NOT generate_result EQUAL 0)
    message(FATAL_ERROR
      "maintained grammar generation failed for ${name}: ${generate_error}")
  endif()
endforeach()

set(benchmark "${REPOSITORY_ROOT}/configs/grammar/benchmarks/simple_exp.json")
execute_process(
  COMMAND "${GRAMMAR_CLI}" validate --grammar-definition "${benchmark}"
  RESULT_VARIABLE benchmark_validate_result
  OUTPUT_QUIET
  ERROR_VARIABLE benchmark_validate_error)
if(NOT benchmark_validate_result EQUAL 0)
  message(FATAL_ERROR
    "benchmark grammar validation failed: ${benchmark_validate_error}")
endif()

execute_process(
  COMMAND "${GENERATE_CLI}"
    --grammar-definition "${benchmark}"
    --cases "${REPOSITORY_ROOT}/data/fixtures/simple_exp_1024.json"
    --population-size 2
    --seed 7
    --out-json "${OUTPUT_DIR}/benchmark-simple-exp.json"
  RESULT_VARIABLE benchmark_generate_result
  OUTPUT_QUIET
  ERROR_VARIABLE benchmark_generate_error)
if(NOT benchmark_generate_result EQUAL 0)
  message(FATAL_ERROR
    "benchmark grammar generation failed: ${benchmark_generate_error}")
endif()

file(REMOVE_RECURSE "${OUTPUT_DIR}")
