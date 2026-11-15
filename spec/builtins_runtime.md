# Builtins Runtime

This document defines sequence, list-construction, and payload-backed
builtin behavior.

See also:

- [grammar.md](./grammar.md)
- [builtins_base.md](./builtins_base.md)
- [bytecode_isa.md](./bytecode_isa.md)
- [fitness.md](./fitness.md)

## Builtin Set

Sequence builtins:

- `len(x)`
- `concat(a, b)`
- `slice(x, lo, hi)`
- `index(x, i)`
- `index_nonnegative(x, i)`
- `index_clamped_or_zero(xs, i)`
- `append(xs, x)`
- `prepend(xs, x)`
- `reverse(x)`
- `find(s, sub)`
- `contains(s, sub)`
- `contains(xs, x)`
- `singleton(x)`
- `insert(xs, i, x)`
- `remove_at(xs, i)`
- `set_at(xs, i, x)`
- `index_mod(x, i)`
- `range(a, b)`

The string builtins `strip`, `lstrip`, `rstrip`, `startswith`, `endswith` and
`capitalize` are defined in [builtins_base.md](./builtins_base.md).

Container tags:

- `String`
- `IntList`
- `FloatList`
- `StringList`

`Char` is a scalar. There is no `CharList`.

## Shared Rules

- Builtins are pure.
- Builtins do not access external state.
- Invalid argument types yield `TypeError`.
- Invalid values for a valid type yield `ValueError`.
- Payload fallback tokens are internal runtime artifacts and are not public
  source-language values.
- CPU and GPU exact paths must produce identical public results.
- GPU fallback paths must be deterministic.

## Numeric List Strictness

Current does not perform implicit numeric-list promotion.

- `IntList` accepts only `Int` elements.
- `FloatList` accepts only `Float` elements.
- operations combining `IntList` and `FloatList` yield `TypeError`.
- operations that would insert an `Int` into `FloatList` or a `Float` into
  `IntList` yield `TypeError`.

## `len(x)`

- arity: `1`
- valid input: `String`, `IntList`, `FloatList`, or `StringList`
- result type: `Int`
- result: exact sequence length

`len(String)` counts `Char` values, not bytes.

## `concat(a, b)`

- arity: `2`
- valid inputs:
  - `String`, `String`
  - `IntList`, `IntList`
  - `FloatList`, `FloatList`
  - `StringList`, `StringList`
- result type: same tag as inputs
- mixed tags: `TypeError`

## `slice(x, lo, hi)`

- arity: `3`
- valid inputs:
  - `x`: `String`, `IntList`, `FloatList`, or `StringList`
  - `lo`: `Int`
  - `hi`: `Int`
- result type: same tag as `x`

Semantics:

- negative indices are normalized like Python slicing.
- bounds are clamped like Python slicing.
- source order is preserved.
- an empty slice returns an empty value with the same runtime tag as `x`.

## `index(x, i)`

- arity: `2`
- valid inputs:
  - `String`, `Int`
  - `IntList`, `Int`
  - `FloatList`, `Int`
  - `StringList`, `Int`
- out-of-range index: `ValueError`

Result types:

- `index(String, Int) -> Char`
- `index(IntList, Int) -> Int`
- `index(FloatList, Int) -> Float`
- `index(StringList, Int) -> String`

Negative indices are normalized like Python indexing.

## `index_nonnegative(x, i)`

Same arity, accepted types, result types, and payload access as `index`. Both
operands are evaluated once, left to right, with the ordinary binary builtin
operation fuel charge. An index below zero or at least the sequence length
raises `ValueError`; negative indices are never normalized. Invalid argument
types raise `TypeError` before index bounds are checked.

## `append(xs, x)`

- arity: `2`
- valid inputs:
  - `IntList`, `Int`
  - `FloatList`, `Float`
  - `StringList`, `String`
- result type: same list tag as `xs`
- result: new list with `x` after all original elements

`append(String, Char)` is not defined. Use `concat(s, char_to_string(c))`.

## `prepend(xs, x)`

- arity: `2`
- valid inputs:
  - `IntList`, `Int`
  - `FloatList`, `Float`
  - `StringList`, `String`
- result type: same list tag as `xs`
- result: new list with `x` before all original elements

The RSGP operation named `cons` maps to `prepend(xs, x)`.

## `reverse(x)`

- arity: `1`
- valid input: `String`, `IntList`, `FloatList`, or `StringList`
- result type: same tag as `x`
- result: reversed sequence

## `find(s, sub)`

- arity: `2`
- valid inputs: `String`, `String`
- result type: `Int`
- result: first substring index or `-1` when absent

## `contains(s, sub)`

- arity: `2`
- valid inputs: `String`, `String`
- result type: `Bool`
- result: true iff `sub` occurs in `s`

## `contains(xs, x)`

- arity: `2`
- valid inputs:
  - `IntList`, `Int`
  - `FloatList`, `Float`
  - `StringList`, `String`
- result type: `Bool`
- result: true iff some element equals `x` under the same equality as `eq`
  (integers by value, floats by IEEE comparison, strings by content)

An element of another type than the list's element type is a `TypeError`.
Like `find`, the result needs resident list contents: when the list's payload
is unavailable the call yields `ValueError`.

## `singleton(x)`

- arity: `1`
- valid inputs:
  - `Int`
  - `Float`
  - `String`
  - `Char`

Result types:

- `singleton(Int) -> IntList`
- `singleton(Float) -> FloatList`
- `singleton(String) -> StringList`
- `singleton(Char) -> String`

## Structured Payload Behavior

General traversal and bounded-region forms depend on exact sequence payload access.

Required behavior:

- source typed-list payload lookup occurs after the source expression is
  evaluated exactly once.
- missing exact payload in an exact path yields `ValueError`.
- fallback behavior must be deterministic.
- result builders must preserve exact result list tags.
- empty results must preserve statically selected list tags.

General region frame, memo, and payload materialization overflow is
deterministic. GPU implementations use explicit bounded storage and do not
depend on device recursion.

## `index_clamped_or_zero(xs, i)`

Binary `IntList, Int -> Int` operation. If `xs` is empty, return integer zero.
Otherwise clamp the signed index to `[0, len(xs)-1]` and return that exact
integer element. Clamping compares before subtracting or converting, including
`INT64_MIN` and `INT64_MAX`; it never normalizes a negative index from the end.
Both argument types are checked even for an empty list; other types produce
`ErrCode::Type`. Nonempty missing payload uses the same explicit fallback-token
behavior as ordinary `index`. This primitive does not change `index` or
`index_nonnegative`. It consumes one ordinary binary builtin operation's fuel,
not the multiple instructions charged by an authored conditional/clamp macro.

## Pure list edits and `range`

These builtins return a new value and never mutate their arguments. Each
charges one ordinary builtin operation of fuel. A resident result counts toward
the logical allocation limit by its length (`insert`, `remove_at`, `set_at`,
`range`), as listed in [fitness.md](./fitness.md). Argument types are checked
before any value: a non-list first argument, a non-`Int` index or bound, or an
element that is not the list's element type is a `TypeError`.

### `insert(xs, i, x)`

- arity: `3`; inputs: a list, `Int`, and an element of the list's type
- result: Python `list.insert(i, x)`. A negative `i` first gains `len(xs)`, then
  the position clamps to `[0, len(xs)]`, so `INT64_MIN` inserts at the front and
  `INT64_MAX` at the end.

### `remove_at(xs, i)`

- arity: `2`; inputs: a list and `Int`
- result: the list without the element at `i mod len(xs)` (mathematical modulo,
  never negative). An empty list is returned unchanged.

### `set_at(xs, i, x)`

- arity: `3`; inputs: a list, `Int`, and an element of the list's type
- result: the list with the element at `i mod len(xs)` replaced by `x`. An empty
  list is returned unchanged.

### `index_mod(x, i)`

- arity: `2`; inputs: a list or `String`, and `Int`
- result: the element at `i mod len(x)`, or the zero value of the element type
  when `x` is empty: `0` for `IntList`, `0.0` for `FloatList`, `""` for
  `StringList` and `String`.
- `index_mod(String, Int)` returns the one-character string at that position.
  There is no `Char` result.

### `range(a, b)`

- arity: `2`; inputs: `Int`, `Int`; result: `IntList`
- result: `[a, a+1, ..., b-1]`, empty when `b <= a`.
- The `b - a` cells count toward the logical allocation limit; an empty range
  allocates nothing. The count is computed in unsigned arithmetic, so no
  bound pair overflows, and the limit is checked before any cell is produced.
- A range of more than 65535 cells (`Value::k_container_len_max`) is a
  `ValueError`, since a longer list has no representable length. The check is
  independent of the allocation limit and identical on every engine.

### Unavailable payload

When the contents of a list or string argument are not resident, these builtins
publish deterministic fallback values instead of reading them: a list edit
returns a hash-only list token of the length the edit would produce (`insert`:
length + 1, saturating; `set_at`: same length; `remove_at`: length - 1; an
empty list is returned unchanged by `remove_at` and `set_at`), and `index_mod`
returns the same fallback token as `index` at the reduced position. The hash
mixes the builtin ID with the operands, so CPU and GPU agree. `range` has no
payload input.
