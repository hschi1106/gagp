# Bytecode Format

This document defines the release 2.0.0 JSON wire formats used by runtime
harnesses, parity tooling, and fixtures.

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
  "engine": "cpu",
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

`op` must be a release-2 name from [bytecode_isa.md](bytecode_isa.md).
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
operands, and 32 request-expression phases. GPU upload additionally limits
frames and memo cells to 128 each; exceeding that profile is a capability error,
not a request to clamp or fall back to CPU.

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

There are no release-2 `linear_rec_binders`, ASGP binder/spec arrays, or DP
bounds arrays. Those keys are valid only to the offline release-1 migration
reader.

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

## Migration

Release-1 bytecode is deliberately not migratable. Recompile a migrated source
AST so type/scope and general-region metadata are regenerated and verified.
Normal codecs issue this instruction when they see a release-1 identifier.
