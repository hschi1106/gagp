# Fitness Cases

This document defines the `fitness-cases` schema of the training and test
cases a task names ([task.md](task.md)), also used by parity tests and
benchmarks.

See also:

- [grammar.md](./grammar.md)
- [fitness.md](./fitness.md)
- [task.md](./task.md)

## Top-Level Schema

```json
{
  "format_version": "fitness-cases",
  "meta": {
    "suite": "asgp30",
    "problem": "count_odds",
    "case_count": 2
  },
  "schema": {
    "inputs": {
      "xs": "int_list"
    },
    "expected": "int"
  },
  "cases": [
    {
      "inputs": {
        "xs": {"type": "int_list", "value": [1, 2, 3]}
      },
      "expected": {"type": "int", "value": 2}
    },
    {
      "inputs": {
        "xs": {"type": "int_list", "value": []}
      },
      "expected": {"type": "int", "value": 0}
    }
  ]
}
```

Required fields:

- `format_version` must be `fitness-cases`.
- `cases` must be a non-empty array.
- each case must contain an `inputs` object and one `expected` value.

Optional fields:

- `meta`: descriptive metadata that does not affect execution.
- `schema`: explicit type declarations used by conversion tools and
  compatibility-profile generation.

When `schema` is present, every case must conform exactly to it.

## Typed Value Encoding

Supported values:

```jsonl
{"type": "bool", "value": true}
{"type": "int", "value": 123}
{"type": "float", "value": 1.5}
{"type": "char", "value": "a"}
{"type": "string", "value": "abc"}
{"type": "int_list", "value": [1, 2, 3]}
{"type": "float_list", "value": [1, 2.5, 3]}
{"type": "string_list", "value": ["a", "bc"]}
```

Rules:

- `bool.value` must be a JSON boolean.
- `int.value` must be an integer JSON number; booleans are rejected.
- `float.value` must be a JSON number and decodes as runtime `Float`.
- `char.value` must be a JSON string containing exactly one supported Unicode
  scalar value.
- `string.value` must be a JSON string.
- `int_list.value` must be an array of integer JSON numbers; booleans are
  rejected.
- `float_list.value` must be an array of JSON numbers; every element decodes
  as runtime `Float`, including integer-looking JSON numbers.
- `string_list.value` must be an array of JSON strings.
- empty lists are valid because the direct list tag is explicit.

Invalid value tags:

- `none`
- `num_list`
- generic `list`
- `char_list`

## Input Type Consistency

Input names define the ordinary local environment used by typed generation and
variation.

Required behavior:

- every case must provide the same set of input names.
- one input name must have the same runtime type in every case.
- `IntList` and `FloatList` are different input types.
- `Char` and `String` are different input types.
- a grammar must not reinterpret an input value as another runtime type.

## Expected Output Types

Expected output values are scored according to
[fitness.md](./fitness.md).

When all cases have the same expected runtime type, evolution may use that
exact type for expected-output-aware initial generation if the grammar enables
it.

## Removed forms

Public `None` has no representation, and the release-1 `num_list` tag is
rejected: numeric lists are `int_list` or `float_list`.
