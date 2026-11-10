# End-to-end contract of the `gagp` command (docs/usage/cli.md).
# Inputs: GAGP (binary), FIXTURES (tests/fixtures/cli), AST_FIXTURES (tests/fixtures), WORK (scratch dir).
foreach(required GAGP FIXTURES AST_FIXTURES WORK)
  if(NOT DEFINED ${required})
    message(FATAL_ERROR "${required} is required")
  endif()
endforeach()
file(REMOVE_RECURSE "${WORK}")
file(MAKE_DIRECTORY "${WORK}")
set(task "${FIXTURES}/task.json")
# The fixture grammar has no bounded region, so it runs on the native AST
# representation (the resident phase population needs one bounded region).
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
gagp_expect_exit(2 run)
require_text("${last_stderr}" "missing TASK" "missing task diagnostic")
gagp_expect_exit(2 run "${task}" --bogus)
require_text("${last_stderr}" "unknown option --bogus" "unknown option diagnostic")
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
foreach(saved population.json history.json)
  if(NOT EXISTS "${WORK}/a/${saved}")
    message(FATAL_ERROR "--save-all did not write ${saved}")
  endif()
endforeach()

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
