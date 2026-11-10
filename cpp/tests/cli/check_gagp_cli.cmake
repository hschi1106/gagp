# End-to-end contract of the `gagp` command (docs/reference/cli.md).
# Inputs: GAGP (binary), FIXTURES (tests/fixtures/cli), AST_FIXTURES (tests/fixtures),
# CONFIGS (repository configs/), WORK (scratch dir).
foreach(required GAGP FIXTURES AST_FIXTURES CONFIGS WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(task "${FIXTURES}/task.json")
# Most checks pin the native AST representation; the default (resident) and its
# decline fallback are checked separately below.
set(native --search "${FIXTURES}/search_native.json")

function(gagp_expect_exit code)
  execute_process(COMMAND "${GAGP}" ${ARGN} RESULT_VARIABLE result OUTPUT_VARIABLE out ERROR_VARIABLE err)
  if(NOT result EQUAL ${code})
    message(FATAL_ERROR "gagp ${ARGN}: exit ${result}, expected ${code}\nstdout: ${out}\nstderr: ${err}")
  endif()
  set(last_stdout "${out}" PARENT_SCOPE)
  set(last_stderr "${err}" PARENT_SCOPE)
endfunction()

function(require_text haystack needle what)
  string(FIND "${haystack}" "${needle}" position)
  if(position EQUAL -1)
    message(FATAL_ERROR "${what} lacks '${needle}':\n${haystack}")
  endif()
endfunction()

# Usage and argument errors exit 2 with a diagnostic.
gagp_expect_exit(2)
require_text("${last_stdout}" "gagp run TASK.json" "no-argument usage")
gagp_expect_exit(0 --help)
gagp_expect_exit(0 run --help)
require_text("${last_stdout}" "--population" "run usage")
gagp_expect_exit(0 eval --help)
require_text("${last_stdout}" "--split" "eval usage")
gagp_expect_exit(2 run)
require_text("${last_stderr}" "missing TASK" "missing task diagnostic")
gagp_expect_exit(2 run "${task}" --bogus)
require_text("${last_stderr}" "unknown option --bogus (see `gagp run --help`)" "unknown option diagnostic")
gagp_expect_exit(2 run "${task}" --engine tpu)
gagp_expect_exit(2 run "${task}" --population 1)
gagp_expect_exit(2 run "${task}" --threads 4)
gagp_expect_exit(2 frobnicate)

# A short CPU run writes a result with the best program and its test verdict.
foreach(name a b)
  gagp_expect_exit(0 run "${task}" --engine cpu ${native} --seed 7 --population 32 --generations 3
                   --out "${WORK}/${name}" --save-all --quiet)
  if(NOT last_stderr STREQUAL "")
    message(FATAL_ERROR "--quiet run wrote to stderr: ${last_stderr}")
  endif()
endforeach()
file(READ "${WORK}/a/result.json" result_a)
file(READ "${WORK}/b/result.json" result_b)
string(JSON format GET "${result_a}" format)
string(JSON generations LENGTH "${result_a}" history)
string(JSON test_cases GET "${result_a}" test cases)
string(JSON program_a GET "${result_a}" best program)
string(JSON program_b GET "${result_b}" best program)
if(NOT format STREQUAL "gagp-run-v1" OR NOT generations EQUAL 3 OR NOT test_cases EQUAL 10)
  message(FATAL_ERROR "unexpected result document: ${result_a}")
endif()
if(NOT program_a STREQUAL program_b)
  message(FATAL_ERROR "the same seed found different programs")
endif()
# The readable form uses the grammar's names: input1, add, mul.
string(JSON text_a GET "${result_a}" best text)
if(NOT text_a MATCHES "^(input1|1|\\((add|mul) .*\\))$")
  message(FATAL_ERROR "unexpected readable program: ${text_a}")
endif()
foreach(saved population.json history.json)
  if(NOT EXISTS "${WORK}/a/${saved}")
    message(FATAL_ERROR "--save-all did not write ${saved}")
  endif()
endforeach()

# The default search runs the resident representation. A grammar the resident
# engine cannot hold reruns on the native representation, says so, and records
# the reason with the effective (native) search configuration.
gagp_expect_exit(0 run "${task}" --engine cpu --seed 7 --population 32 --generations 2 --out "${WORK}/resident" --quiet)
file(READ "${WORK}/resident/result.json" resident_result)
string(JSON representation GET "${resident_result}" search representation)
string(JSON declined ERROR_VARIABLE no_decline GET "${resident_result}" resident_declined)
if(NOT representation STREQUAL "resident" OR NOT declined MATCHES "NOTFOUND$")
  message(FATAL_ERROR "default run did not stay resident: ${resident_result}")
endif()
# configs/search/default.json is the built-in default search, member for member.
gagp_expect_exit(0 run "${task}" --engine cpu --seed 7 --population 32 --generations 2
                 --search "${CONFIGS}/search/default.json" --out "${WORK}/default_file" --quiet)
file(READ "${WORK}/default_file/result.json" file_result)
string(JSON built_in_search GET "${resident_result}" search)
string(JSON file_search GET "${file_result}" search)
string(JSON built_in_best GET "${resident_result}" best program)
string(JSON file_best GET "${file_result}" best program)
if(NOT built_in_search STREQUAL file_search OR NOT built_in_best STREQUAL file_best)
  message(FATAL_ERROR "configs/search/default.json differs from the built-in search:\n${built_in_search}\n${file_search}")
endif()
gagp_expect_exit(0 run "${FIXTURES}/declined_task.json" --engine cpu --seed 7 --population 32 --generations 2
                 --out "${WORK}/declined")
require_text("${last_stderr}" "resident engine declined this grammar" "decline notice")
file(READ "${WORK}/declined/result.json" declined_result)
string(JSON representation GET "${declined_result}" search representation)
string(JSON declined GET "${declined_result}" resident_declined)
if(NOT representation STREQUAL "native_ast")
  message(FATAL_ERROR "declined run recorded ${representation}")
endif()
require_text("${declined}" "lowering capacity" "decline reason")

# A run that starts from a saved population reproduces it.
gagp_expect_exit(0 run "${task}" --engine cpu ${native} --seed 7 --generations 1 --population 32
                 --init "${WORK}/a/population.json" --out "${WORK}/init" --quiet)

# eval scores a result, a population or a bare program; invalid programs are rejected.
gagp_expect_exit(0 eval "${task}" "${WORK}/a/result.json")
string(JSON eval_cases GET "${last_stdout}" cases)
string(JSON eval_split GET "${last_stdout}" split)
if(NOT eval_cases EQUAL 10 OR NOT eval_split STREQUAL "test")
  message(FATAL_ERROR "unexpected eval output: ${last_stdout}")
endif()
gagp_expect_exit(0 eval "${task}" "${WORK}/a/population.json" --split train)
gagp_expect_exit(0 eval "${task}" "${AST_FIXTURES}/ast_eval_valid.json" --split train)
string(JSON valid_correct GET "${last_stdout}" correct)
if(NOT valid_correct EQUAL 0)
  message(FATAL_ERROR "x is not x*x+1, yet eval reported ${valid_correct} correct cases")
endif()
gagp_expect_exit(2 eval "${task}" "${AST_FIXTURES}/ast_eval_invalid.json")
require_text("${last_stderr}" "invalid AST (name_index_out_of_range)" "malformed AST diagnostic")

# grammar generate writes a population that run accepts as --init.
gagp_expect_exit(0 grammar generate --grammar-definition "${FIXTURES}/grammar.json"
                 --cases "${FIXTURES}/train.cases.json" --population-size 16 --seed 3
                 --out-json "${WORK}/generated.json")
gagp_expect_exit(0 run "${task}" --engine cpu ${native} --population 16 --generations 1
                 --init "${WORK}/generated.json" --quiet)
gagp_expect_exit(0 grammar validate --grammar-definition "${FIXTURES}/grammar.json")

# The documented examples run and solve their tasks on the CPU reference engine.
foreach(example polynomial list-sum)
  gagp_expect_exit(0 run "${CONFIGS}/../examples/${example}/task.json" --engine cpu --quiet
                   --out "${WORK}/example-${example}")
  file(READ "${WORK}/example-${example}/result.json" example_result)
  string(JSON example_solved GET "${example_result}" test solved)
  if(NOT example_solved)
    message(FATAL_ERROR "examples/${example} was not solved:\n${example_result}")
  endif()
endforeach()
