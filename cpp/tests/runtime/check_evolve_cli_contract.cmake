if(NOT DEFINED CLI OR NOT DEFINED CASES OR NOT DEFINED GRAMMAR OR
   NOT DEFINED LEGACY_GRAMMAR OR NOT DEFINED OUT_JSON)
  message(FATAL_ERROR "CLI, CASES, GRAMMAR, LEGACY_GRAMMAR, and OUT_JSON are required")
endif()

execute_process(
  COMMAND "${CLI}"
  RESULT_VARIABLE no_args_result
  OUTPUT_VARIABLE no_args_stdout
  ERROR_VARIABLE no_args_stderr
)
if(NOT no_args_result EQUAL 2 OR NOT no_args_stdout STREQUAL "" OR
   NOT no_args_stderr STREQUAL "gagp_evolve_cli error: --cases is required\n")
  message(FATAL_ERROR "no-argument CLI contract changed: ${no_args_result}; ${no_args_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --help
  RESULT_VARIABLE help_result
  OUTPUT_VARIABLE help_stdout
  ERROR_VARIABLE help_stderr
)
if(NOT help_result EQUAL 2 OR NOT help_stdout STREQUAL "" OR
   NOT help_stderr STREQUAL "gagp_evolve_cli error: unknown argument: --help\n")
  message(FATAL_ERROR "--help CLI contract changed: ${help_result}; ${help_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --cases "${CASES}"
  RESULT_VARIABLE missing_grammar_result
  OUTPUT_VARIABLE missing_grammar_stdout
  ERROR_VARIABLE missing_grammar_stderr
)
if(NOT missing_grammar_result EQUAL 2 OR NOT missing_grammar_stdout STREQUAL "" OR
   NOT missing_grammar_stderr STREQUAL
       "gagp_evolve_cli error: --grammar-definition is required for evolution\n")
  message(FATAL_ERROR "required grammar contract changed: ${missing_grammar_result}; ${missing_grammar_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --cases "${CASES}" --grammar-definition "${LEGACY_GRAMMAR}"
  RESULT_VARIABLE legacy_grammar_result
  OUTPUT_VARIABLE legacy_grammar_stdout
  ERROR_VARIABLE legacy_grammar_stderr
)
if(NOT legacy_grammar_result EQUAL 2 OR NOT legacy_grammar_stdout STREQUAL "")
  message(FATAL_ERROR "legacy grammar config was not rejected: ${legacy_grammar_result}; ${legacy_grammar_stderr}")
endif()
string(FIND "${legacy_grammar_stderr}"
  "migrate the config offline to grammar-definition-v2 before evolution"
  legacy_diagnostic_position)
if(legacy_diagnostic_position EQUAL -1)
  message(FATAL_ERROR "legacy grammar diagnostic lost migration guidance: ${legacy_grammar_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --cases "${CASES}" --grammar-definition "${GRAMMAR}" --fuel 1
  RESULT_VARIABLE fuel_conflict_result
  OUTPUT_VARIABLE fuel_conflict_stdout
  ERROR_VARIABLE fuel_conflict_stderr
)
if(NOT fuel_conflict_result EQUAL 2 OR NOT fuel_conflict_stdout STREQUAL "" OR
   NOT fuel_conflict_stderr STREQUAL
       "gagp_evolve_cli error: --fuel conflicts with grammar definition execution_limits.fuel=20000\n")
  message(FATAL_ERROR "definition fuel conflict contract changed: ${fuel_conflict_result}; ${fuel_conflict_stderr}")
endif()

execute_process(
  COMMAND "${CLI}" --cases "${CASES}" --grammar-definition "${GRAMMAR}"
          --population-size 4 --generations 1
          --seed 7 --timing none --show-program none --out-json "${OUT_JSON}"
  RESULT_VARIABLE evolve_result
  OUTPUT_VARIABLE evolve_stdout
  ERROR_VARIABLE evolve_stderr
)
if(NOT evolve_result EQUAL 0 OR NOT evolve_stderr STREQUAL "")
  message(FATAL_ERROR "representative evolution failed: ${evolve_result}; ${evolve_stderr}")
endif()

foreach(required IN ITEMS
    "GEN 000 best="
    "FINAL best="
    "repro_backend=cpu"
    "selection=round_based_tournament"
    "crossover=typed_subtree")
  string(FIND "${evolve_stdout}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "representative stdout lost '${required}': ${evolve_stdout}")
  endif()
endforeach()

file(READ "${OUT_JSON}" evolve_json)
foreach(required IN ITEMS
    "\"meta\""
    "\"history\""
    "\"timing\""
    "\"final\""
    "\"population_source\": \"generated\""
    "\"grammar_definition\""
    "\"content_sha256\""
    "\"format_version\": \"grammar-definition-v2\""
    "\"catalog_version\": \"gagp-primitives-v3\""
    "\"normalization_version\": \"1\""
    "\"semantic_version\": \"gagp-native-2.0.0\""
    "\"generator_version\": \"typed-derivation-v2\""
    "\"rng_version\": \"splitmix64-rejection-v1\""
    "\"eval_engine\": \"cpu\""
    "\"reproduction_backend\": \"cpu\""
    "\"skipped\": false"
    "\"init_population_ms\""
    "\"cpu_compile_ms_total\""
    "\"gpu_eval_pack_upload_ms_total\""
    "\"generations_repro_prepare_inputs_ms_total\""
    "\"generations_repro_crossover_attempts_total\""
    "\"generation_eval_ms\""
    "\"generation_gpu_eval_kernel_ms\""
    "\"generation_repro_decode_ms\""
    "\"generation_repro_changed_children\""
    "\"generation_total_ms\"")
  string(FIND "${evolve_json}" "${required}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "representative JSON lost '${required}'")
  endif()
endforeach()

file(REMOVE "${OUT_JSON}")
