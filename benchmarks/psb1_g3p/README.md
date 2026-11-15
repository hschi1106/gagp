# The `psb1_g3p` suite

Six problems of the general program synthesis benchmark suite (PSB1), with the
grammars and settings of Forstenlechner's grammar-guided genetic programming
(G3P) ported to GAGP, so that GAGP's results can be set beside the published G3P
counts. The grammars are the ones PonyGE2 ships under `grammars/progsys/`
(listed in appendix B of Forstenlechner's PhD thesis) and the settings are those
of thesis Table 4.1. Only GAGP runs here. G3P and PonyGE2 are not needed except
for the data files.

| Problem | Inputs | Output | Fitness | Generations |
| --- | --- | --- | --- | ---: |
| `smallest` | 4 Int | Int | exact | 200 |
| `median` | 3 Int | Int | exact | 200 |
| `count_odds` | IntList | Int | absolute error | 300 |
| `small_or_large` | Int | String | Levenshtein | 300 |
| `replace_space_with_newline` | String | String | Levenshtein | 300 |
| `negative_to_zero` | IntList | IntList | Levenshtein | 300 |

The published G3P success counts for these problems are in
[`literature.json`](literature.json), each with its source.

## Get the data

The PSB1 case files are not part of the repository. They are the files that
PonyGE2 ships in `datasets/progsys/<Problem>/{Train,Test}.txt`. Clone PonyGE2 and
convert them (the converter reads Python literals with `ast.literal_eval`):

```bash
git clone https://github.com/PonyGE/PonyGE2 /path/to/PonyGE2
python3 benchmarks/psb1_g3p/build.py cases --data /path/to/PonyGE2
```

This writes `train.cases.json` and `test.cases.json` into each problem directory
(they are ignored by git). `--data` accepts the checkout or its `datasets/progsys`
directory and `--out DIR` chooses another suite directory. The files hold 100
training and 1000 test cases for `smallest`, `median`, `small_or_large` and
`replace_space_with_newline`, and 200 and 2000 for `count_odds` and
`negative_to_zero`.

The `grammar.json`, `task.json` and `search.json` of each problem are committed
so that the grammars can be read and reviewed. `build.py files` regenerates them
and `build.py files --check` fails when they are stale. A repository test checks
that they equal what `build.py` generates, so edit `build.py` and not the JSON.

## Run it

Build GAGP first ([README](../../README.md#build)), then from the repository root:

```bash
python3 benchmarks/run.py run --suite psb1_g3p --quick                 # 6 problems, seed 1, 10 generations
python3 benchmarks/run.py run --suite psb1_g3p                         # 6 problems x seeds 1-100, 300 or 200 generations
python3 benchmarks/run.py run --suite psb1_g3p --engine cpu --quick    # the same quick runs on the CPU reference engine
python3 benchmarks/run.py run --suite psb1_g3p --tasks smallest --seeds 1-10
python3 benchmarks/run.py report --suite psb1_g3p                      # rewrite logs/bench/psb1_g3p/report.md
```

The options are those of the [`asgp30` runner](../README.md#run-it) with these
differences: seeds default to 1-100, runs are ordered seed by seed, and
`--population` and `--generations` default to the values in each problem's
`search.json` (an explicit value overrides it and is part of the configuration
directory name). Runs land in
`logs/bench/psb1_g3p/<engine>/thesis/<problem>/s<seed>/result.json`. A run that
already has a `result.json` is skipped, so rerunning resumes. A full run is
long, because one run of `negative_to_zero` takes minutes. Run the GPU engine
and the CPU reference engine as two invocations, and the report then compares
them.

## Reading the report

Per engine and configuration, `report.md` lists per problem:

- **GAGP solved**: runs whose best program answers every held-out test case.
- **Train perfect**: runs whose best program has no wrong training case.
- **Median time**: wall-clock seconds of a whole run.
- **Published columns**: the number of runs out of 100 that solved the problem
  in the G3P work, one column per source in `literature.json` (EuroGP 2017
  and thesis Table 4.3, each with tournament and lexicase selection). GAGP's
  search is tournament selection, so the tournament columns are the matching
  ones.

When a GPU and a CPU run of the same seed exist, the report adds a table that
checks the two result files are identical except for the timing fields (members
ending in `_ms`, `time_ms`) and the engine name: champion, fitness, test verdict
and the whole per-generation history.

## Settings

| Setting | Here | Thesis Table 4.1 |
| --- | --- | --- |
| Population | 1000 | 1000 |
| Generations | 300 (200 for `median` and `smallest`) | 300 (200 for Median, Number IO and Smallest) |
| Selection | tournament, size 7 | tournament, size 7 |
| Crossover probability | 0.9 | 0.9 |
| Mutation probability | 0.05 | 0.05 |
| Elite size | 1 | 1 |
| Schedule | generational | generational |
| Node limit | 170 AST nodes of the body (see below) | 250 derivation-tree nodes |
| Variables per type | 3 temporaries per type | 3 |
| Execution limit | 20,000 fuel | 1 second of wall time |
| Runs | 100 | 100 |

The constants that are a choice of this port are named at the top of
[`build.py`](build.py): `FUEL`, `LOOP_BOUND`, `MAX_DEPTH`, `BODY_NODES`, the loop translation
(`LOOP_COUNTER`, `SLICE_END`) and `CHAR_MODULUS`. They may be retuned, and the
committed files then change with them.

## Differences from the original

The port keeps each nonterminal, its variables and its operators, and keeps the
probability with which each original production is chosen. G3P chooses a
production uniformly among the productions of a nonterminal. Unit-production
chains such as `<statement> ::= <simple_stmt> | <compound_stmt>` are inlined with
the probabilities multiplied. Everything below is a difference from the original
and not a claim about results.

### Constructs and how they are expressed

| Original | Here | Exact? |
| --- | --- | --- |
| `v = e`, `v op= e` (`+ - *`) | `assign`, with `op=` as `v = op(v, e)` | yes |
| `if` with and without `else` | `if_stmt` with an empty or a full else block | yes |
| `while <bool>` with the `loopBreak` guard | `while_stmt(LOOP_BOUND, <bool>, body)` | bounded, see below |
| `for forCounter in <list_int>` | `for_range(len(<list_int>), body)` | yes except the loop note below |
| `for <int_var> in <list_int>` | `for_range(len(L), [v = index_mod(L, k)] + body)` with the hidden counter `k`, for a list variable `L` only | restricted, see below |
| `<list>.append(e)` | `L = append(L, e)` | yes |
| `<list>.insert(i, e)` | `L = insert(L, i, e)` | yes, `insert` is Python's `list.insert` (a negative index gains the length, the position clamps) |
| `deleteListItem(L, i)` | `L = remove_at(L, i)` | yes, both take the index modulo the length and leave an empty list unchanged |
| `setListIndexTo(L, i, v)` | `L = set_at(L, i, v)` | yes, same modulo rule |
| `getIndexIntList(L, i)` | `index_mod(L, i)` | yes, both give 0 for an empty list |
| `x in L`, `x not in L` | `contains(L, x)`, `not(contains(L, x))` | yes |
| `list(saveRange(a, b))` | `range(a, b)` | no, see below |
| `L[a:b]`, `L[:b]`, `L[a:]` | `slice(L, a, b)`, `slice(L, 0, b)`, `slice(L, a, 1000000)` | yes, slices clamp like Python and no list is that long |
| `len(L)`, `len(s)` | `len` | yes |
| `getCharFromString(s, i)` | `index_mod(s, i)` | yes, both give `''` for an empty string |
| `saveChr(n)` | `char_to_string(chr(imod0(n, 128)))` | yes, `imod0` takes the sign of the divisor like Python's `%` |
| `saveOrd(s)` | `ord(string_to_char(index_mod(concat(s, " "), 0)))` | yes, 32 for the empty string, at the cost of 7 AST nodes where a plain `ord` costs 3 |
| `s.strip()`, `lstrip()`, `rstrip()` | `strip`, `lstrip`, `rstrip` | yes except the whitespace note below |
| `s.strip(c)`, `lstrip(c)`, `rstrip(c)` | `strip(s, c)`, `lstrip(s, c)`, `rstrip(s, c)` | yes, an empty `c` removes nothing |
| `s.capitalize()` | `capitalize(s)` | yes for ASCII, the only characters in these tasks |
| `s.startswith(t)`, `s.endswith(t)` | `startswith(s, t)`, `endswith(s, t)` | yes, an empty affix is true and a longer affix is false |
| `s in t`, `s not in t`, `==`, `!=` | `contains(t, s)`, `not(contains(t, s))`, `eq`, `ne` | yes |
| `(s + t)` | `concat(s, t)` | yes |
| `min`, `max`, `abs`, `-x`, `+ - *` | the same builtins | yes except 64-bit integers, see below |
| `int(<number>.0)` | an Int constant of 1 to 5 digits, with probabilities 1/2, 1/4, 1/8, 1/16, 1/16 | no, numbers have at most 5 digits (the original has no bound) |
| string literal constants | one character (space to `}`, tab, newline) or the empty string, plus `'small'` and `'large'` where the grammar has them | no, the original builds multi-character constants by recursion |

### Constructs that could not be ported or are changed

- **`while` loops** are ported with a bound. `while <bool>: <code>` is
  `while_stmt(LOOP_BOUND, <bool>, <code>)`: the loop leaves normally after
  `LOOP_BOUND` iterations or when the condition is false, and reaching the bound
  is not an error. It keeps its original probability, 1/6 of `<statement>` for
  `count_odds` and `negative_to_zero` (where `<compound_stmt>` has `for`, `if`,
  `while`) and 1/4 for `smallest`, `median`, `small_or_large` and
  `replace_space_with_newline` (`if`, `while`), so every original production has
  its original probability. `LOOP_BOUND` is 200. It is a choice of this port: the
  thesis sets `loopBreakConst` in a header but its text does not give the value,
  and the PonyGE2 grammars never define it. The longest input of these six
  problems is a list of 49 integers or a string of 20 characters (checked against
  the Train and Test files), so 200 covers a loop over any input, and it leaves
  about 100 instructions of fuel per iteration at fuel 20,000. A `for` loop has
  no such bound here beyond its list length and fuel.
- **`for <int_var> in <list_int>`** iterates a list variable only. Its source
  is one of the 4 or 5 list variables of the problem, with the production's
  probability unchanged. A slice or `range` source would need the list stored in
  a hidden variable, and nested loops would overwrite it.
- **`deleteListItem` and `setListIndexTo` on a temporary.** Their first argument is
  a `<list_int>`. When it is a list variable (1/3 of `<list_int>`) the statement
  edits it. When it is a slice or a range the original edits a temporary copy and
  nothing happens. That 2/3 of each production is kept as the statement
  `i0 = i0`, so the other statements keep their probabilities.
- **`+<int>`.** `<arith_prefix> ::= '+' | '-'` is dropped to `-` (`neg`). The
  identity half of that production is given to `neg`, so `neg` has twice its
  original probability among the `<int>` productions.
- **List sources of the loop and aliasing.** Python lists are shared objects:
  after `li1 = li0` an edit of one is visible through the other, and
  `for v in L` reads the live list. Here every list variable holds its own value,
  and the loop runs `len(L)` times, the length when the loop starts. Programs
  that edit the list they iterate over, or that edit an alias, behave
  differently. Each case also starts from fresh copies of its inputs.
- **`range` and `saveRange`.** `saveRange(a, b)` returns at most 10,000 numbers.
  `range(a, b)` returns up to 65,535 numbers and fails with a value error beyond
  that, and the list cells of one case are limited to 65,536 by the task's
  `list_allocation_limit`. Programs that build longer ranges differ.
- **Integers** are 64-bit signed values that wrap on `+ - *`. Python integers have
  no bound.
- **`divInt` and `mod`** become `idiv0` and `imod0`. The original returns the
  dividend when the divisor is at most 0.00001 and otherwise divides with `//`
  or `%`. `idiv0` truncates toward zero, so `divInt(-7, 2)` is -4 in the original
  and -3 here, and `idiv0` gives 0 for a divisor of 0 where the original returns
  the dividend. `imod0` agrees with `%` for a positive divisor and differs for a
  divisor at most 0 (0 for a zero divisor where the original returns the
  dividend).
- **Whitespace.** `strip` without an argument removes the six ASCII whitespace
  characters (space, tab, newline, carriage return, form feed, vertical tab).
  Python's `str.strip()` also removes the control characters 28 to 31. They can
  occur only through `saveChr`.
- **Loop counters.** Every original loop ends with
  `if loopBreak% > loopBreakConst or stop.value: break` followed by
  `loopBreak% += 1`, and starts with the line `loopBreak% = 0`. PonyGE2's
  `src/fitness/progsys.py` (`format_individual`) deletes every `loopBreak% = 0`
  line and replaces `loopBreak%` by `loopBreak`, so in the formatted program the
  counter is one name shared by all loops and nothing resets it at loop entry. The
  Embed files of the checkout do not initialize `loopBreak` or define
  `loopBreakConst` either. How the thesis' own implementation sets them is not
  visible in these files. Here each `while` has its own counter that starts at
  zero on entry, and `for` loops have none (the original's `for` carries the same
  guard). The wall-clock guard `stop.value` is replaced by fuel.
### Fitness

Thesis Table A.1 against `task.json`:

| Problem | Thesis Table A.1 | Here |
| --- | --- | --- |
| `smallest`, `median` | Boolean difference | `exact` |
| `count_odds` | absolute difference | `absolute` |
| `small_or_large` | Levenshtein distance | `levenshtein` |
| `replace_space_with_newline` | Levenshtein distance of the string and the absolute difference of the count of non-whitespace characters | `levenshtein` of the string only, the second output of the PonyGE2 data is not scored |
| `negative_to_zero` | sum of absolute differences with a length-difference penalty of 5000 | `levenshtein` of the two lists, which is a different function |

Errors of the cases add up over the training set in both. A case that fails with
a runtime error or runs out of fuel costs `case_cap`, 1,000,000 here, which is
large enough to rank it last. For `exact` a wrong answer costs 1 and an error costs
1,000,000. The thesis gives a timeout the worst possible fitness. A problem is
*solved* here when the best program answers every test case. The thesis counts a
run as successful when the program solves all training cases and generalizes to
the test cases, and the report shows *train perfect* separately.

### Initialization, variation and limits

- **Initialization.** The thesis creates the first generation with PTC2, which
  samples derivation trees uniformly over tree sizes up to the node limit. GAGP
  creates its initial population by sampling the grammar with the run's seed,
  within the grammar's node and depth limits. The two do not give the same
  distribution of programs.
- **Operators.** Crossover and mutation are GAGP's typed subtree operators on the
  grammar's derivations ([search configuration](../../spec/search_config.md)),
  not the PonyGE2 or thesis implementations.
- **Node limit.** The thesis limits a derivation tree to 250 nodes, counting every
  nonterminal. GAGP counts the AST nodes of the program. `max_nodes` is the
  fixed prologue (the zero-initialization of every variable, 3 nodes each) plus
  `BODY_NODES` = 170, which is 250 divided by 1.5, the assumed number of
  derivation nodes per AST node. The ratio is an estimate and was not measured.
  Unit productions and nonterminals that add no AST node count in the original and
  not here. GAGP also applies a depth limit, `MAX_DEPTH` = 40, which the original
  does not have.
- **Execution limit.** The original stops a program after 1 second of CPython wall
  time. GAGP counts fuel, 20,000 instructions, which is deterministic and
  identical on both engines. The two limits do not stop the same programs.
- **Program form.** The generated Python function becomes a GAGP program: every
  temporary and `res0` starts at its type's zero value, and the program returns
  `res0`.
- **Data.** The case files are the ones in the PonyGE2 checkout. This benchmark
  does not check that they equal the sets of the published experiments.

## Provenance

The published counts in `literature.json` were transcribed from EuroGP 2017 (Forstenlechner,
Fagan, Nicolau and O'Neill, *A Grammar Design Pattern for Arbitrary Program
Synthesis Problems in Genetic Programming*, Table 3) and the thesis (Table 4.3),
100 runs each. The EuroGP 2017 and thesis grammars are not identical, since the
thesis states that its grammars were combined automatically and tweaked after the
paper.
