# Bytecode Format

This document defines the release 1.0.0 JSON wire formats used by bytecode parity tooling,
runtime harnesses, and fixture-driven tests.

See also:

- [grammar.md](./grammar.md)
- [bytecode_isa.md](./bytecode_isa.md)

## Format Strings

- request payload: `bytecode-json`
- fixture payload: `bytecode-fixture`
- AST payload: `ast-prefix`

Old format strings are not valid release 1.0.0 payloads.

## Harness Request Schema

```jsonc
{
  "format_version": "bytecode-json",
  "engine": "cpu",
  "fuel": 20000,
  "programs": [
    {
      "n_locals": 3,
      "consts": [Value],
      "code": [Instr],
      "segments": {}
    }
  ],
  "shared_cases": [
    [{"idx": 0, "value": {"type": "int", "value": 1}}]
  ],
  "shared_answer": [{"type": "int", "value": 2}]
}
```

Fields:

- `format_version` must be `bytecode-json`.
- `engine` is an optional request hint.
- `fuel` is the required per-case execution budget.
- `programs` is the required list of bytecode programs.
- `shared_cases` is the required list of shared input cases.
- `shared_answer` is optional expected-output data for fitness paths.

## Optional lexical region metadata

The native AST forms append `LET_REGION`, `TRAVERSE`, `TRAVERSE_RANGE`, and
`REGION_VAR` numeric kinds after the existing kinds. Their prefix arities are
respectively 2, 4, 6, and 0. `REGION_VAR.i0` is a declaration ID; all other index
fields on these forms are zero. Optional `lexical_regions` rows contain
`node_index`, `body_argument`, and ordered `bindings` objects with `id` and `type`.
Types use the eight exact names such as `Int`, `Char`, and `StringList`.

Every LetRegion has one row with body argument 1 and one binding. Every Traverse
has one row with body argument 3 and three bindings; TraverseRange uses body
argument 5 and three bindings. Only those body arguments see the declarations.
Every traversal also has one `traversal_specs` row containing `node_index` and
`direction` (`"forward"` or `"reverse"`). Duplicate declarations or metadata,
missing rows, wrong owners/body slots/arity, invalid types or directions, and
out-of-scope references are rejected. Empty metadata arrays are omitted when
writing legacy ASTs so their serialized representation is preserved.

## Optional semantic fuel schedule

A bytecode program or nested phase may contain `"instruction_fuel": [3, 0]`
when its `code` array contains two instructions. Omission and `[]` select legacy
unit charging. A nonempty schedule must match the code length exactly and contain
only integers from zero through `INT_MAX`; `null` is invalid. Invalid zero-cost
control-flow cycles are rejected at the decode boundary. See the
[ISA fuel contract](./bytecode_isa.md#fuel-and-errors) for charge ordering.
CPU and GPU execution honor schedules in the root and supported nested phases.
GPU packing validates them before upload; private device instruction layout does
not change this wire format. Unsupported structured regions are rejected separately.

## Value Encoding

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

Invalid value tags:

- `none`
- `num_list`
- generic `list`
- `char_list`
- public `fallback_token`

Rules:

- `char.value` must contain exactly one supported Unicode scalar value.
- `string.value` is a sequence of `Char` values.
- list tags are direct and exact.
- empty lists are valid because the tag is explicit.

## Instruction Encoding

Instruction objects use opcode names from
[bytecode_isa.md](./bytecode_isa.md):

```jsonl
{"op": "PUSH_CONST", "a": 0, "b": 0}
{"op": "CALL_BUILTIN", "a": 7, "b": 2}
```

Fields:

- `op`: required opcode string.
- `a`: integer operand; required by opcodes that consume operand `a` and
  otherwise optional/ignored.
- `b`: integer operand; required by `CALL_BUILTIN` and otherwise
  optional/ignored.

For `CALL_BUILTIN`, `a` is the builtin id and `b` is the arity.
Missing required operands are malformed bytecode; they are not interpreted as
zero.

## Bytecode Program Encoding

```jsonc
{
  "n_locals": 4,
  "consts": [Value],
  "code": [Instr],
  "segments": {
    "dc_solve_0": Segment,
    "dc_divide_0": Segment,
    "dc_combine_0": Segment
  },
  "metadata": {}
}
```

Required fields:

- `n_locals`
- `consts`
- `code`

Optional fields:

- `segments`: ASGP or optimized structured-expression phase segments.
- `metadata`: non-semantic diagnostics.

Segment encoding:

```jsonc
{
  "n_locals": 3,
  "consts": [Value],
  "code": [Instr],
  "binder_locals": [
    {"name": 0, "local": 0}
  ],
  "metadata": {}
}
```

`binder_locals` maps source binder name ids to phase-local slots. It may be
omitted for a phase with no binders, but every required ASGP phase role must
have an in-range mapping. ASGP phase execution uses this map to bind solve,
divide, combine, dependency, or transition parameters before executing the
segment code.

The implementation-defined ASGP segment payloads are encoded under
`segments`:

```jsonc
{
  "segments": {
    "asgp_dc": [
      {
        "solve_xs_name": 0,
        "solve_n_name": 1,
        "solve_lo_name": 2,
        "divide_n_name": 3,
        "combine_left_name": 4,
        "combine_right_name": 5,
        "solve": Segment,
        "divide": Segment,
        "combine": Segment
      }
    ],
    "asgp_dp1d": [
      {
        "lo": 0,
        "hi": 10,
        "base_state": 0,
        "boundary_value": Value,
        "dep_kind": -1,
        "dep_offsets": [1],
        "solve_state_name": 0,
        "transition_state_name": 1,
        "transition_dep_names": [2],
        "solve": Segment,
        "transition": Segment
      }
    ],
    "asgp_dp2d": [
      {
        "i_lo": 0,
        "i_hi": 10,
        "j_lo": 0,
        "j_hi": 10,
        "base_i": 0,
        "base_j": 0,
        "boundary_value": Value,
        "dep_kind": 0,
        "solve_i_name": 0,
        "solve_j_name": 1,
        "transition_i_name": 2,
        "transition_j_name": 3,
        "transition_dep_names": [4, 5],
        "solve": Segment,
        "transition": Segment
      }
    ]
  }
}
```

Main bytecode refers to ASGP segment arrays by zero-based segment index in the
`a` operand of `ASGP_DC`, `ASGP_DP1D`, and `ASGP_DP2D`.

## Bounded Region Segment Encoding

The generic bounded-region representation is a private CPU/GPU execution format. A
program stores it in the `segments.bounded_region` array, and `BOUNDED_REGION.a`
is the zero-based index into that array. Region objects use a strict schema:
every field shown below is required, including empty arrays and `null`, and an
unknown field is an error.

```jsonc
{
  "plan": RegionPlan,
  "parameter_locals": [0],
  "boundary": RegionPhase,
  "base_predicate": RegionPhase,
  "base_body": RegionPhase,
  "preparations": [RegionPhase],
  "request_expressions": [RegionPhase],
  "combine": RegionPhase
}
```

`boundary` is a phase object for coordinate progress and is `null` for sequence
window progress. `parameter_locals` corresponds positionally to
`plan.parameter_types`. Preparation and request-expression phase counts must
equal their respective plan type-table counts. Parameter locals are distinct,
nonnegative caller-local indices. Standalone segment decoding validates the
segment using a synthetic caller extent; complete-program verification checks
each capture against the program's actual `n_locals`.

### GPU execution capacities

The current GPU transport supports at most 128 declared frames and 128 declared
memo cells per bounded region. It rejects larger declarations before upload,
including a larger cell declaration on a non-memoized plan; it does not clamp
serialized limits or execute the region on the CPU. A program can therefore be
valid for CPU execution while exceeding the GPU's supported resource profile.

The fixed device descriptor supports four state slots, eight ordered requests,
four preparations, 32 parameters, eight bound operands, and 32 request-expression
phases. The core plan verifier checks the corresponding structural bounds before
packing. CPU and GPU execution retain the same declared limits and semantic fuel
charges for programs within the supported GPU profile.

### Region plan

```jsonc
{
  "version": 1,
  "state_types": ["Int"],
  "result_type": "Int",
  "parameter_types": [],
  "preparations": [
    {"type": "Int", "kind": "identity"}
  ],
  "request_expression_types": [],
  "bound_operand_count": 0,
  "requests": [
    {"states": [
      {
        "kind": "coordinate_offset",
        "source_state": 0,
        "offset": "-1",
        "window": {
          "begin": {"kind": "begin", "cut": 0},
          "end": {"kind": "begin", "cut": 0}
        },
        "expression": 0
      }
    ]}
  ],
  "limits": {"frames": 64, "cells": 0, "entry_fuel": 1},
  "memoized": false,
  "duplicate_policy": "reject",
  "progress": "coordinates",
  "coordinate_slots": [0],
  "coordinate_rank": [{"coordinate": 0, "direction": 1}],
  "coordinate_domains": [
    {
      "lower": {"kind": "literal", "literal": "0", "operand": 0},
      "upper": {"kind": "literal", "literal": "10", "operand": 0}
    }
  ],
  "coordinate_endpoint": "exclusive",
  "sequence_state": 0
}
```

The exact public type strings are `Int`, `Float`, `Bool`, `Char`, `String`,
`IntList`, `FloatList`, and `StringList`. Other enum strings are:

- progress: `coordinates`, `sequence_windows`;
- duplicate policy: `reject`, `allow`;
- domain endpoint: `exclusive`, `inclusive`;
- bound kind: `literal`, `operand`;
- preparation kind: `identity`, `interior_cut`;
- transition kind: `copy_state`, `coordinate_offset`, `sequence_window`,
  `expression`;
- window endpoint kind: `begin`, `end`, `interior_cut`.

Every signed 64-bit bound literal and coordinate offset is a canonical base-10
string. Examples are `"0"`, `"-1"`, `"-9223372036854775808"`, and
`"9223372036854775807"`. Leading zeroes, a leading plus sign, numeric JSON
values, trailing characters, and out-of-range strings are invalid. Counts,
indices, directions, local numbers, and limits remain finite integral JSON
numbers in their declared `uint32` or `int` range. Rank direction is exactly
`1` or `-1`.

All structure fields are serialized even when inactive. Their canonical values
are:

- a literal bound has `operand: 0`; an operand bound has `literal: "0"`;
- `begin` and `end` endpoints have `cut: 0`;
- `copy_state` has `offset: "0"`, `expression: 0`, and the default
  begin-to-begin window;
- `coordinate_offset` has `expression: 0` and the default window;
- `sequence_window` has `offset: "0"` and `expression: 0`;
- `expression` has `source_state: 0`, `offset: "0"`, and the default window;
- coordinate plans have `sequence_state: 0`;
- sequence plans have empty coordinate arrays, an `exclusive` coordinate
  endpoint, and `bound_operand_count: 0`.

Version 1 permits one through four state slots, at most 32 parameters, four
preparations, eight additional bound operands, and one through eight ordered
requests. A request constructs every state slot. The request-expression type
table cannot exceed `state count * request count`. Coordinate rank/domain arrays
cannot exceed four entries. `frames` and `cells` are `uint32`; `entry_fuel` is
also `uint32` but cannot exceed `INT_MAX`. A nonmemoized plan has canonical
`cells: 0`. Memoization is limited to coordinate plans whose projection covers
every state slot. Static plan validation also checks exact transition types,
rank decrease, domain shape, duplicate policy, proper sequence windows, and
interior-cut preparation references.

The decoder applies these fixed capacities before reserving or decoding the
corresponding arrays.

### Region phases

```jsonc
{
  "program": {
    "n_locals": 1,
    "consts": [GrammarConstant],
    "code": [
      {"op": "LOAD", "a": 0, "b": null}
    ],
    "instruction_fuel": [1],
    "var2idx": [],
    "binder_locals": []
  },
  "bindings": [
    {"bank": "state", "slot": 0, "local": 0}
  ]
}
```

Region phase instructions use the existing opcode-name format, but both `a`
and `b` keys are present; an unused operand is `null`. `n_locals`, instruction
operands, and binding locals are bounded `int` values. `n_locals` and binding
locals are nonnegative, each binding local is below `n_locals`, and destination
locals cannot alias. A nonempty `instruction_fuel` array matches `code` exactly
and contains costs from zero through `INT_MAX`; `[]` selects legacy unit
charging.

A phase binding array is capped at 49 entries before allocation: four state,
32 parameter, four prepared, eight result, and one measure slot. The selected
plan and phase usually admit fewer; visibility, source uniqueness, and slot
bounds are checked after this aggregate wire cap.

The phase slot bank strings are `state`, `parameter`, `prepared`, `result`, and
`measure`. Each binding object has exactly `bank`, `slot`, and `local`.
Visibility and exact types follow the region phase contract: state and parameter
slots are available in every phase, prepared slots are available to later
preparations and to request/combine phases, results are combine-only, and
measure slot zero belongs only to sequence progress. Duplicate source bindings
and destination locals are invalid.

Generic phases do not use the legacy name maps. Both `var2idx` and
`binder_locals` are required canonical empty arrays. Phase constants use the
exact grammar artifact singleton-domain codec, rather than the legacy bytecode
`Value` codec. For example, an exact signed integer constant is
`{"type":"Int","values":["-9223372036854775808"]}`; list types retain
their explicit `IntList`, `FloatList`, or `StringList` name, including when the
single value is an empty array. Decoding reconstructs payload-backed strings and
typed lists and rejects opaque or invalid constants.

After structural decoding, the region plan and each phase program are verified.
This includes constant tags, instruction operands, fuel schedules, binding
visibility and types, required phase result types, structured-opcode exclusion
inside phases, and phase/control-flow validity. The containing program then runs
the complete bytecode verifier, including its real caller-local capture ranges.

## Decode-Time Verification

Native JSON decoders verify each complete program before returning it. The
verification contract covers:

- public constant tags and Unicode scalar `Char` constants;
- required operands and constant/local/builtin/segment index ranges;
- builtin bytecode arity and jump targets;
- control-flow stack depth and compatible stack types at joins;
- reachable stack underflow and main-program fallthrough;
- local, variable-map, binder-local, and phase-program ranges;
- ASGP bounds, dependency metadata, phase binders, and forbidden nested ASGP
  calls inside phase bytecode;
- optional instruction, constant, local, segment, and stack resource limits.

Verification rejects malformed representation and unsafe control-flow shape.
It does not replace runtime semantics: bytecode that is structurally valid but
deliberately evaluates to `TypeError`, `NameError`, `ZeroDiv`, or `Timeout`
remains decodable and produces that runtime result.

ASGP-DP segment arity constraints:

- `asgp_dp1d.dep_kind` is `-1` for backward dependencies and `1` for forward
  dependencies.
- `asgp_dp1d.dep_offsets` must be non-empty.
- `asgp_dp1d.dep_offsets` and `asgp_dp1d.transition_dep_names` must have the
  same length.
- `asgp_dp2d.dep_kind` encodes the selected 2D dependency pattern:
  `0 = cross_backward`, `1 = cross_forward`, `2 = diagonal_backward`,
  `3 = diagonal_forward`, `4 = neighborhood_backward3`,
  `5 = neighborhood_forward3`.
- `asgp_dp2d.transition_dep_names` length must match the selected pattern:
  cross patterns use arity 2, diagonal patterns use arity 1, and neighborhood
  patterns use arity 3.
- Decoders must reject malformed ASGP-DP segment metadata before execution.

## AST Encoding

Public AST encoding uses:

```jsonc
{
  "version": "ast-prefix",
  "nodes": [AstNode],
  "names": ["input1"],
  "consts": [Value],
  "linear_rec_binders": [],
  "asgp_dc_binders": [],
  "asgp_dp1d_specs": [],
  "asgp_dp2d_specs": []
}
```

Node encoding:

```json
{"kind": 0, "i0": 0, "i1": 0}
```

`kind` is the numeric `NodeKind` value written by the native AST
serializer. `i0` and `i1` are required integers. The native verifier rejects
unknown kinds and invalid uses of the index fields before compilation.

Current optional side-table arrays are:

- `linear_rec_binders`: `node_index`, `elem_name`, `accum_name`, and
  `index_name`;
- `asgp_dc_binders`: `node_index`, `solve_xs_name`, `solve_n_name`,
  `solve_lo_name`, `divide_n_name`, `combine_left_name`, and
  `combine_right_name`;
- `asgp_dp1d_specs`: `node_index`, `lo`, `hi`, `base_state`,
  `boundary_const`, numeric dependency-node `dep_kind`, positive
  `dep_offsets`, `solve_state_name`, `transition_state_name`, and
  `transition_dep_names`;
- `asgp_dp2d_specs`: `node_index`, both dimension bounds, the base cell,
  `boundary_const`, numeric dependency-node `dep_kind`, solve/transition state
  names, and `transition_dep_names`.

Native `BOUNDED_REGION` expressions use the optional `bounded_region_specs`
side table:

```jsonc
{
  "node_index": 0,
  "plan": RegionPlan,
  "parameters": [
    {"kind": "lexical", "index": 7},
    {"kind": "name", "index": 0}
  ],
  "phases": [
    {
      "argument": 3,
      "bindings": [
        {"bank": "state", "slot": 0, "binder_id": 8}
      ]
    }
  ]
}
```

Every row requires exactly the shown top-level fields. `plan` uses the complete
Region plan schema above, including exact decimal strings for all signed 64-bit
literals and offsets and canonical values in inactive fields. Capture `kind` is
`lexical` or `name`; `index` is respectively an immutable lexical binder ID or
an index in the AST name table. Both index forms and every `binder_id` are
nonnegative and strictly below `INT_MAX`.

`parameters` corresponds positionally and exactly to `plan.parameter_types`.
Phase rows are ordered as base predicate, base body, each preparation, each
request expression, combine, then coordinate boundary. Sequence-window plans
omit the final coordinate-boundary phase. `argument` is the zero-based child
argument owned by that phase. Binding bank strings are `state`, `parameter`,
`prepared`, `result`, and `measure`; `slot` selects the logical plan slot and
`binder_id` declares the immutable lexical ID visible in that phase expression.
Each phase binding array is capped at 49 entries before allocation, using the
same aggregate bank bound as bytecode region phases. Plan validation, exact
phase count, parameter count, owner uniqueness, child arguments, slot
visibility/types, and binder uniqueness/scope are checked before compilation.

The `BOUNDED_REGION` child order is initial states, additional bound operands,
base predicate, base body, preparations, request expressions, combine, and the
coordinate boundary. The final child is absent for sequence-window progress;
`i0` contains this dynamic arity and `i1` is canonical zero.

Omitted side-table arrays are treated as empty. Every entry is owned by exactly
one matching node index. Their verified semantics satisfy:

- capture-safe `BoundVar` references.
- exact result-type reconstruction.
- ASGP phase and visibility reconstruction.
- deterministic AST cache keys.

The `--eval-ast-json` boundary derives exact input name/type declarations from
the selected `fitness-cases` file and runs the full structural, scope, binder,
and type verifier before building genome metadata or compiling.

## DP Bounds Encoding

`DpBounds1D`:

```jsonc
{
  "kind": "dp_bounds_1d",
  "lo": 0,
  "hi": 10,
  "base": [{"state": 0}],
  "boundary_value": Value,
  "max_step": 3
}
```

`DpBounds2D`:

```jsonc
{
  "kind": "dp_bounds_2d",
  "i_lo": 0,
  "i_hi": 10,
  "j_lo": 0,
  "j_hi": 10,
  "base": [{"i": 0, "j": 0}],
  "boundary_value": Value
}
```

Bounds are inclusive at `lo` and exclusive at `hi`.

The `boundary_value` type must equal the ASGP-DP result type.

## Fixture Schema

```jsonc
{
  "format_version": "bytecode-fixture",
  "fuel": 20000,
  "scenarios": [
    {
      "intent": "integer addition preserves the Int tag",
      "program": BytecodeProgram,
      "fuel": 100,
      "cases": [
        {
          "inputs": [{"idx": 0, "value": Value}],
          "expected": Value
        },
        {
          "inputs": [],
          "expected_error": "TypeError"
        }
      ]
    }
  ]
}
```

`fuel` is required at the fixture root and may be overridden by a scenario.
`scenarios` must be non-empty. Every scenario has a non-empty semantic
`intent`, one verified `BytecodeProgram`, and a non-empty `cases` array. Each
case supplies input bindings and exactly one of:

- `expected`, compared by exact public value tag and payload content;
- `expected_error`, compared to the exact public error name (`NameError`,
  `TypeError`, `ZeroDivisionError`, `ValueError`, or `Timeout`).

The earlier single-program shape with top-level `program` and `cases` remains
accepted as a compatibility shorthand for one scenario. New corpus files use
the scenario form so every group carries its semantic intent.

Fixtures must not contain retired value tags. Program decoding runs the
bytecode verifier before any case is executed, so malformed representation is
covered by verifier/codec tests rather than encoded as a runtime-error case.


Source ASTs may carry optional `fuel_specs` rows of the form
`{"node_index":3,"charges":[{"event":"operation","cost":2}]}`. Owners must be
unique in-bounds node indexes; charges must be nonempty with unique event names
supported by the owner node kind. Numeric fields are finite integral values and
costs lie in [0, INT_MAX]. Rows and charges count toward metadata limits. The field
is omitted when empty. Event names and semantics are specified in grammar.md.
Lowering emits the existing parallel `instruction_fuel` schedule, with zero-cost
administrative instructions; no instruction-layout or opcode extension is needed.
