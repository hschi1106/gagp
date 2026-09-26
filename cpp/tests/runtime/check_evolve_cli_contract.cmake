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

# Mixed roots use the same global evolution path through the production CLI.
set(mixed_grammar "${OUT_JSON}.mixed-grammar.json")
set(mixed_cases "${OUT_JSON}.mixed-cases.json")
file(WRITE "${mixed_grammar}" [=[{
  "format_version":"grammar-definition-v2",
  "entry":{"nonterminal":"Integer","type":"Int"},
  "search_limits":{"max_nodes":5,"max_depth":4},
  "execution_limits":{"fuel":100},
  "nonterminals":[
    {"id":"Integer","type":"Int","scope":[],"alternatives":[
      {"id":"zero","weight":1,"expression":{"constant":{"type":"Int","values":["0"]}}}]},
    {"id":"Real","type":"Float","scope":[],"alternatives":[
      {"id":"two","weight":1,"expression":{"constant":{"type":"Float","values":[2.0]}}}]}
  ]}]=])
file(WRITE "${mixed_cases}" [=[{"format_version":"fitness-cases","cases":[
  {"inputs":{},"expected":{"type":"int","value":0}},
  {"inputs":{},"expected":{"type":"float","value":2.0}}]}]=])
execute_process(COMMAND "${CLI}" --cases "${mixed_cases}"
  --grammar-definition "${mixed_grammar}" --population-roots Integer,Real
  --population-size 4 --generations 2 --selection-pressure 1 --penalty 9
  --timing none --out-json "${OUT_JSON}.mixed.json"
  RESULT_VARIABLE mixed_result OUTPUT_VARIABLE mixed_stdout ERROR_VARIABLE mixed_stderr)
if(NOT mixed_result EQUAL 0)
  message(FATAL_ERROR "mixed CLI evolution failed: ${mixed_stderr}")
endif()
file(READ "${OUT_JSON}.mixed.json" mixed_json)
string(JSON root0 GET "${mixed_json}" meta population_roots 0)
string(JSON root1 GET "${mixed_json}" meta population_roots 1)
if(NOT root0 STREQUAL "Integer" OR NOT root1 STREQUAL "Real")
  message(FATAL_ERROR "mixed CLI metadata lost ordered population roots")
endif()
string(FIND "${mixed_stdout}" "GEN 001 best=-2.000000 mean=-2.000000" mixed_score)
if(mixed_score EQUAL -1)
  message(FATAL_ERROR "mixed numeric fitness changed: ${mixed_stdout}")
endif()
foreach(roots IN ITEMS Integer,Missing Integer,Integer)
  execute_process(COMMAND "${CLI}" --cases "${mixed_cases}"
    --grammar-definition "${mixed_grammar}" --population-roots "${roots}"
    RESULT_VARIABLE invalid_result OUTPUT_VARIABLE invalid_stdout ERROR_VARIABLE invalid_stderr)
  if(NOT invalid_result EQUAL 2 OR NOT invalid_stdout STREQUAL "")
    message(FATAL_ERROR "invalid mixed roots accepted: ${roots}")
  endif()
endforeach()

execute_process(COMMAND "${GENERATOR}" --cases "${mixed_cases}"
  --grammar-definition "${mixed_grammar}" --population-roots Integer,Real
  --population-size 4 --out-json "${OUT_JSON}.mixed-population.json"
  RESULT_VARIABLE generated_result ERROR_VARIABLE generated_stderr)
if(NOT generated_result EQUAL 0)
  message(FATAL_ERROR "mixed population generation failed: ${generated_stderr}")
endif()
execute_process(COMMAND "${CLI}" --cases "${mixed_cases}"
  --grammar-definition "${mixed_grammar}" --population-roots Integer,Real
  --population-json "${OUT_JSON}.mixed-population.json"
  --generations 2 --selection-pressure 1 --penalty 9 --timing none
  --out-json "${OUT_JSON}.mixed-replayed.json"
  RESULT_VARIABLE replay_result OUTPUT_VARIABLE replay_stdout ERROR_VARIABLE replay_stderr)
if(NOT replay_result EQUAL 0 OR NOT replay_stdout STREQUAL mixed_stdout)
  message(FATAL_ERROR "mixed population evolution replay differed: ${replay_stderr}; ${replay_stdout}")
endif()
