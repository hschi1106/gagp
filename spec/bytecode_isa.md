# Bytecode ISA

This document defines the release 2.0.0 bytecode execution contract shared by
the compiler, CPU runtime, and GPU runtime. JSON encoding is defined in
[bytecode_format.md](bytecode_format.md).

## Version boundary

Release-2 bytecode uses `bytecode-json-v2` and `bytecode-fixture-v2`.
Release-1 `bytecode-json`, `bytecode-fixture`, and migration bytecode
snapshots are rejected by production decoders. Bytecode migration is
unsupported because bytecode lacks source AST, type/scope, and derivation
provenance. Migrate the source `ast-prefix` artifact offline and compile the
resulting `ast-prefix-v2`.

Numeric opcode values are fixed:

| Value | Opcode |
| ---: | --- |
| 0–20 | `PUSH_CONST`, `LOAD`, `STORE`, `NEG`, `NOT`, `ADD`, `SUB`, `MUL`, `DIV`, `MOD`, `LT`, `LE`, `GT`, `GE`, `EQ`, `NE`, `JMP`, `JMP_IF_FALSE`, `JMP_IF_TRUE`, `CALL_BUILTIN`, `RETURN` |
| 21 | `CHECK_LIST` |
| 22 | `CHECK_INT` |
| 23 | `EMPTY_LIST` |
| 24 | `EMPTY_LIST_LIKE` |
| 25–27 | reserved holes for removed specialized region opcodes |
| 28 | `BOUNDED_REGION` |

Values 25–27 are never aliases and must be rejected. The release-1 specialized
DC/DP opcodes, segment arrays, and dispatch paths do not exist in production.

## Execution state and fuel

A VM owns an instruction pointer, operand stack, local array with separate
initialization state, constant pool, remaining fuel, optional hidden locals,
and general bounded-region frames. Public `None` does not exist.

Without an explicit schedule, each instruction costs one. A nonempty
`instruction_fuel` array has one integer in `[0, INT_MAX]` per instruction.
The interpreter checks and subtracts the charge before validation or side
effects. A zero-cost instruction may execute at zero remaining fuel, but every
control-flow cycle must contain a positive cost. Verification rejects even
unreachable zero-cost cycles. CPU and GPU packing execute the same schedule.

The first runtime error terminates the current case:
`NameError`, `TypeError`, `ZeroDiv`, `ValueError`, or `Timeout`.

## Verification boundary

External bytecode is verified before execution. Verification checks opcode and
operand domains, constant/local/segment indices, control-flow targets, stack
shape and joins, fuel schedules, exact phase output types, and bounded-region
plans/bindings. Compiler output is verified in debug and test builds. Malformed
programs are decode errors; valid operations that fail on case values produce a
runtime error.

## Operators and builtins

Arithmetic accepts exact `Int` or `Float` operands; ordering accepts numeric
scalars; equality requires identical runtime tags. `Bool` and `Char` are
not numeric.

Builtin IDs are:

```text
0 abs        1 min             2 max          3 clip
4 len        5 concat          6 slice        7 index
8 append     9 reverse        10 find        11 contains
12 is_int   13 idiv0           14 imod0       15 prepend
16 char_to_string             17 string_to_char
18 ord      19 chr            20 is_letter   21 is_digit
22 is_space 23 is_vowel       24 to_lower    25 to_upper
26 to_string                  27 singleton
```

`is_int` is compiler-internal and is not source syntax.

## General helper opcodes

`CHECK_LIST` and `CHECK_INT` preserve the checked value after exact tag
validation. `EMPTY_LIST` constructs the statically selected list tag;
`EMPTY_LIST_LIKE` preserves the list family of its operand.

`BOUNDED_REGION` consumes the plan's initial state values followed by its
additional bound operands and produces one exact typed result. Operand `a`
indexes `bounded_region_segments`. Each segment contains a versioned
`RegionPlan` and ordinary bytecode phases with explicit plan-bank-to-local
bindings. Structured calls, including nested `BOUNDED_REGION`, are forbidden
inside phases.

Plans admit one to four state slots, up to eight ordered requests, four
preparations, 32 parameters, and eight bound operands. Coordinate progress and
sequence-window progress are validated structurally before execution. Bounds
resolve once; checked arithmetic overflow yields `ValueError`.

Every frame pays its positive entry charge before state tag checks. Boundary
and base selection precede memo lookup. Preparations run once per miss, child
requests run serially, and combine runs after all children succeed. Frame or
memo capacity exhaustion yields `Timeout`. Memo keys cover all state slots;
cached values retain exact runtime tags.

GPU transport supports at most 128 declared frames and 128 declared memo cells
per region and rejects larger declarations before upload. It does not clamp
limits or execute phases on the host. CPU and GPU preserve request order,
error order, fuel charges, result tags, and memo behavior within that profile.

## Compiler lowering

Lexical declarations map to hidden immutable locals. Ordinary `Var` and
`Assign` cannot access or write them. Traversal lowers to hidden locals and
ordinary branches with the source evaluation and event order defined in
[grammar.md](grammar.md). Bounded regions lower only to opcode 28 and the
general segment representation.

No compiler path emits `MapList`, `FilterList`, `LinearRec`, ASGP
opcodes, dependency-pattern instructions, or phase-specific legacy metadata.
Equivalent package templates materialize only general AST and bytecode forms.
