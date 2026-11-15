# Bytecode Format

This document defines the JSON wire formats used by runtime
harnesses, parity tests, and fixtures.

## Format identifiers

- request payload: `bytecode-json-v2`
- fixture payload: `bytecode-fixture-v2`
- AST payload: `ast-prefix-v2`

The unversioned release-1 identifiers are rejected. There is no normal decoder
compatibility mode and numeric opcode values are never reinterpreted.

## Request schema

```jsonc
{
  "format_version": "bytecode-json-v2",
  "fuel": 20000,
  "programs": [{
    "n_locals": 3,
    "consts": [],
    "code": [],
    "instruction_fuel": [],
    "segments": {"bounded_region": []}
  }],
  "shared_cases": [[{"idx": 0, "value": {"type": "int", "value": 1}}]],
  "shared_answer": [{"type": "int", "value": 2}]
}
```

`engine` is an optional hint. `fuel`, `programs`, and `shared_cases`
are required. `shared_answer` is optional expected-output data for fitness
paths.

## Values

Values use an exact tagged object:

```jsonc
{"type": "int", "value": 1}
{"type": "float", "value": 1.5}
{"type": "bool", "value": true}
{"type": "char", "value": "x"}
{"type": "string", "value": "text"}
{"type": "int_list", "value": [1, 2]}
{"type": "float_list", "value": [1.0, 2.0]}
{"type": "string_list", "value": ["a", "b"]}
```

Integers are signed 64-bit values. Floats must be finite. A character contains
exactly one supported character. Lists are homogeneous and cannot nest.

## Instructions and programs

```jsonc
{"op": "PUSH_CONST", "a": 0, "b": 0}
```

`op` must be a name from [bytecode_isa.md](bytecode_isa.md).
`a` and `b` are signed integer operands whose allowed domains depend on the
opcode. The codec rejects unknown names and the reserved numeric opcode holes
25–27.

A program contains `n_locals`, `consts`, root `code`, optional
`instruction_fuel`, and `segments`. An absent or empty fuel array means
unit cost; otherwise its length equals the code length and every cost is in
`[0, INT_MAX]`.

`segments` permits only the `bounded_region` array. Release-1 keys for DC,
DP1D, DP2D, LinearRec, map, filter, binder sidecars, bounds tables, or
dependency patterns are invalid.

## Bounded-region segment

```jsonc
{
  "plan": {
    "version": 1,
    "state_types": ["Int"],
    "result_type": "Int",
    "parameter_types": [],
    "preparations": [],
    "request_expression_types": [],
    "bound_operand_count": 0,
    "requests": [],
    "limits": {"frames": 64, "cells": 0, "entry_fuel": 1},
    "memoized": false,
    "duplicate_policy": "reject",
    "progress": "coordinates",
    "coordinate_slots": [0],
    "coordinate_rank": [{"coordinate": 0, "direction": 1}],
    "coordinate_domains": [{
      "lower": {"kind": "literal", "literal": "0", "operand": 0},
      "upper": {"kind": "literal", "literal": "10", "operand": 0}
    }],
    "coordinate_endpoint": "exclusive",
    "sequence_state": 0
  },
  "captures": [],
  "phases": []
}
```

Exact type strings are `Int`, `Float`, `Bool`, `Char`, `String`,
`IntList`, `FloatList`, and `StringList`. Plan enum strings and canonical
inactive fields follow the native `RegionPlan` version-1 codec. Signed
64-bit literals and offsets are canonical base-10 strings with no plus sign or
leading zeroes.

Plan limits are finite integral JSON numbers. A plan declares one to four
states, at most eight requests, four preparations, 32 parameters, eight bound
operands, 32 request-expression phases, 1,024 frames and 4,096 memo cells.
Verification rejects larger declarations on both engines; no engine clamps a
limit or falls back to the other engine.

Each phase contains ordinary program fields plus explicit bindings from a plan
bank/slot to a local. Banks are validated by phase role. Every success path has
the exact nominal output type. Phases cannot contain structured-region calls.
Captures identify an ordinary name or enclosing lexical declaration; set/unset
state is preserved and unused unset captures are harmless.

## Verification

Decode-time verification checks:

- format identifier and complete known-field shape;
- value tags and payload ranges;
- opcode names, operands, indices, targets, stack joins, and returns;
- local initialization behavior and constant bounds;
- fuel array shape/ranges and positive-cost control-flow cycles;
- general region plan structure, rank/progress proof, capacities, captures,
  phase bindings, control flow, and exact output types.

Malformed input is rejected before CPU execution or GPU packing.

## AST encoding

Native AST JSON uses:

```jsonc
{
  "version": "ast-prefix-v2",
  "nodes": [{"kind": "PROGRAM", "i0": 0, "i1": 0}],
  "names": [],
  "consts": [],
  "lexical_regions": [],
  "traversal_specs": [],
  "fuel_specs": [],
  "bounded_region_specs": []
}
```

Only fields needed by the materialization may be omitted according to the
codec's canonical rules. General node kinds through 52 retain their stable
numbers; 53–70 are unassigned; `LET_REGION`, `TRAVERSE`,
`TRAVERSE_RANGE`, `REGION_VAR`, `CHECK_INT`, `CHECK_LIST`, and
`BOUNDED_REGION` begin at 71. Unknown names/numbers, reserved holes, wrong
prefix arity, duplicate/missing metadata, invalid lexical scope, or metadata
owned by the wrong node are rejected.

There are no `linear_rec_binders`, ASGP binder/spec arrays, or DP bounds
arrays; release-1 documents that carry them are rejected.

## Fixture schema

```jsonc
{
  "format_version": "bytecode-fixture-v2",
  "cases": [{
    "name": "example",
    "fuel": 20000,
    "program": {},
    "inputs": [],
    "expected": {"ok": {"type": "int", "value": 1}}
  }]
}
```

`expected` contains exactly one of `ok` or `error`. Error names use the
runtime error set from the ISA. Fixtures are verified before execution.

## Release-1 bytecode

Release-1 bytecode is rejected and cannot be converted: it lacks the source
type, scope and general-region metadata. Recompile the program from its
`ast-prefix-v2` source.

The `ast-prefix-v2` node kinds 78 and 79 are `CALL_TRUNC_DIV0` and
`CALL_TRUNC_MOD0`, respectively. They are binary Int expressions lowering to
`CALL_BUILTIN` IDs 28 and 29. Kinds 80 and 81 are `CALL_INDEX_NONNEGATIVE` and
`CALL_INDEX_CLAMPED_OR_ZERO`. Kinds 82 through 95 are the G3P builtins
`CALL_INSERT`, `CALL_REMOVE_AT`, `CALL_SET_AT`, `CALL_INDEX_MOD`, `CALL_RANGE`,
`CALL_STRIP`, `CALL_LSTRIP`, `CALL_RSTRIP`, `CALL_STRIP_CHARS`,
`CALL_LSTRIP_CHARS`, `CALL_RSTRIP_CHARS`, `CALL_STARTSWITH`, `CALL_ENDSWITH` and
`CALL_CAPITALIZE`; each lowers to the `CALL_BUILTIN` ID of the same name (32
through 45) with the node's arity. The program JSON spells them `call_insert`,
`call_remove_at`, and so on. List `contains` reuses `CALL_CONTAINS`.
Kind 96 is the statement `WHILE_STMT` (`while_stmt`), which lowers to existing
load, store, comparison and jump instructions. Existing numeric node kinds remain unchanged,
including reserved legacy kinds 53 through 70. Their contract is in
[builtins_base.md](builtins_base.md).
