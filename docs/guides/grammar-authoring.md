# Grammar Authoring

The grammar is GAGP's search space: every program in the initial population,
every crossover site and every mutation is drawn from it. This guide shows how
to write one, check it and run it. The normative format is
[grammar_definition.md](../../spec/grammar_definition.md); a complete worked
example of a grammar with a traversal is the
[list-sum walkthrough](list-sum-grammar.md).

## Anatomy

```json
{
  "format_version": "grammar-definition-v2",
  "entry": {"nonterminal": "Expr", "type": "Int"},
  "inputs": [{"name": "x", "type": "Int"}],
  "search_limits": {"max_nodes": 31, "max_depth": 8},
  "execution_limits": {"fuel": 1000},
  "nonterminals": [
    {"id": "Expr", "type": "Int", "scope": [], "alternatives": [
      {"id": "x", "weight": 3, "expression": {"input": "x"}},
      {"id": "small", "weight": 2, "expression": {"constant": {"type": "Int", "values": ["1", "2", "3"]}}},
      {"id": "add", "weight": 1, "expression": {"signature": "add(Int,Int)->Int", "args": [{"ref": "Expr"}, {"ref": "Expr"}]}}
    ]}
  ]
}
```

- **`inputs`** are the program's arguments. Their names and types must equal
  the inputs of the cases, and `entry.type` the type of their expected values.
- **`nonterminals`** each have a type and a list of **alternatives**. Generation
  picks an alternative with probability proportional to its `weight`.
- An **expression** is one of: `{"input": name}`, `{"constant": domain}`,
  `{"ref": nonterminal}`, `{"bound": name}` (a variable in scope), an operation
  `{"signature": "op(T,...)->R", "args": [...]}`, or a template use.
  The available operations and their semantics are in
  [builtins_base.md](../../spec/builtins_base.md) and
  [builtins_runtime.md](../../spec/builtins_runtime.md).
- **`search_limits`** bound program size (`max_nodes`) and depth (`max_depth`)
  during generation, crossover and mutation. **`execution_limits.fuel`** bounds
  the instructions one program may execute per case; a program out of fuel
  fails that case.

Types are `Int`, `Float`, `Bool`, `Char`, `String`, `IntList`, `FloatList` and
`StringList`; in case files they are written `int`, `float`, `bool`, `char`,
`string`, `int_list`, `float_list` and `string_list`. Lists are homogeneous and
never nested, and there are no implicit conversions. One example grammar per
type, with cases, is in `configs/grammar/examples/types/`.

## Check, inspect, run

```bash
cpp/build/gagp grammar validate --grammar-definition my-task/grammar.json
cpp/build/gagp grammar inspect  --grammar-definition my-task/grammar.json
cpp/build/gagp run my-task/task.json
```

- `validate` checks imports, types, scopes and limits, and that the entry can
  produce a program within the limits. Errors name the offending field.
- `inspect` shows the compiled grammar: nonterminals, productions, scopes,
  templates and the minimum size of each choice.
- `resolve --out-json FILE` writes the grammar with all imports inlined.

A grammar is identified by `grammar_hash`, a hash of its resolved content.
Weights, domains and limits are content; file names and import paths are not.
Results and saved populations record the hash, and `gagp run --init` and
`gagp eval` reject programs that do not belong to the task's grammar.

Edits take effect on the next run; nothing needs rebuilding.

## Constants

A constant domain has exactly one of `values`, an Int or Float `range`, or a
`sequence`:

```text
{"constant": {"type": "Int", "values": ["0", "1"]}}
{"constant": {"type": "Int", "range": ["-8", "8"]}}
{"constant": {"type": "Float", "range": [-5, 5], "quantization_scale": 1000}}
{"constant": {"type": "IntList", "sequence": {"length": [0, 5],
              "element": {"type": "Int", "range": ["-8", "8"]}}}}
```

- Int bounds are decimal strings; ranges include both ends.
- `quantization_scale: 1000` rounds sampled Floats to three decimals.
- A `sequence` draws a length uniformly, then each element from its domain.
  `String` elements are `Char`; list elements are the list's element type.
- `sample_from` draws new constants from a narrower domain while membership
  accepts the wider one, so evolved values may drift outside it:
  `{"type": "Float", "range": [-100, 100], "sample_from": {"type": "Float", "range": [-8, 8]}}`.
- `mutation` chooses how constant mutation changes a value: `"resample"`
  (default), `"keep"`, `"flip"` (Bool), or
  `{"kind": "add", "range": ["-2", "2"]}` to perturb a number.

The package `configs/grammar/packages/source_literals.json` provides ready-made
literal nonterminals for every type (`Source.Literal.Int`, ...).

## Scopes, statements and loops

Every nonterminal declares the variables visible in it (`scope`), and a
`{"bound": name}` expression may only read a variable in scope. Operations
that introduce variables, such as `let` and the traversals, bind them for their
body; statements (`assign`, `if_stmt`, `for_range`, `return`) build whole
programs with locals. The [list-sum walkthrough](list-sum-grammar.md) explains
binding and scopes step by step with a traversal. To let a program overwrite an input, use
`input_name` on its assignment instead of `name`.

Set `"variation": false` on a nonterminal to keep its occurrences out of
crossover and mutation sites, for example to freeze a program skeleton while
its expressions keep evolving.

## Templates, imports and recursion

A **template** is a fixed body with typed **holes**; a use fills each hole
with an expression:

```json
{"templates": [{"id": "Twice", "type": "Int", "scope": [],
  "holes": [{"id": "value", "type": "Int", "scope": []}],
  "body": {"signature": "add(Int,Int)->Int", "args": [{"hole": "value"}, {"hole": "value"}]}}]}
```

Grammars can `import` other grammar files and use their templates and
nonterminals. The packages in `configs/grammar/packages/` provide bounded
recursion schemes as templates: divide and conquer (`dc_intlist_int.json`),
linear recursion over a list (`linear_rec_intlist_int.json`), and one- and
two-dimensional dynamic programming (`dp1d_int.json`, `dp2d_int.json`). Each
scheme's holes (`solve`, `divide`, `combine`, ...) are filled by your own
nonterminals with the scopes the scheme requires; see
`configs/grammar/examples/restricted_combine.json`, and the 30 grammars of
[`benchmarks/asgp30`](../../benchmarks/asgp30) for complete uses. Recursion
always runs inside a bounded region with frame, memo and fuel limits, and every
recursive request must make progress, so programs terminate
([templates and regions](../../spec/grammar_definition.md#templates-and-fixed-regions)).

## Making the GPU engine take the grammar

The resident GPU engine holds the population on the device and admits a
grammar when every program it can derive fits its capacity: at most 2048
nodes and depth 64 over the whole program, 1024 nodes per varied phase, 16
locals in statement programs, and up to 8 varied phases around a fixed
template ([full rules](../design/gpu-reproduction.md#genome-layouts)). A
grammar outside these limits still runs: `gagp run` prints
`resident engine declined this grammar (...)` and uses the slower host
reproduction with the same evaluation engine. Tighter `search_limits` are the
usual fix.

## Initial populations

`gagp run` generates its initial population from the grammar and the seed. To
generate one separately, for inspection or to share between runs:

```bash
cpp/build/gagp grammar generate --grammar-definition my-task/grammar.json \
  --cases my-task/train.cases.json --population-size 1024 --seed 7 --out-json pop.json
cpp/build/gagp grammar generate --replay-json pop.json \
  --cases my-task/train.cases.json --out-json replayed.json   # identical to pop.json
cpp/build/gagp run my-task/task.json --init pop.json
```

The population file records the grammar's identity and the generator version;
replay re-derives every member and fails if anything differs.

## Starting points

| File | Shows |
| --- | --- |
| [`examples/polynomial/grammar.json`](../../examples/polynomial/grammar.json) | Arithmetic expressions |
| [`examples/list-sum/grammar.json`](../../examples/list-sum/grammar.json) | A list traversal with bound variables |
| `configs/grammar/examples/authoring/scalar.json` | The smallest grammar |
| `configs/grammar/examples/authoring/sequence.json` | `IntList` operations |
| `configs/grammar/examples/authoring/template.json` | A template with a shared hole |
| `configs/grammar/examples/authoring/memo.json` | A memoized two-coordinate recurrence |
| `configs/grammar/examples/types/` | One grammar per value type |
| `configs/grammar/examples/restricted_combine.json` | Filling a divide-and-conquer package |
| [`benchmarks/asgp30/`](../../benchmarks/asgp30) | 30 complete program-synthesis tasks |

Each `configs/grammar/examples` grammar comes with a `.cases.json` file; to run
one, put both next to a task file as in the
[getting started guide](getting-started.md#3-set-up-your-own-problem).
