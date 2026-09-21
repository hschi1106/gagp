# Grammar

This document defines the source grammar, type rules, structured expression
forms, and ASGP scheme forms used by GAGP.

All programs use prefix `AstProgram` representation. The grammar is statically
typed for generation, crossover, mutation, compilation, and runtime
validation.

Related specifications:

- [builtins_base.md](./builtins_base.md)
- [builtins_runtime.md](./builtins_runtime.md)
- [bytecode_format.md](./bytecode_format.md)
- [bytecode_isa.md](./bytecode_isa.md)
- [fitness_cases.md](./fitness_cases.md)
- [fitness.md](./fitness.md)
- [grammar_config.md](./grammar_config.md)

## Value Domain

Runtime values are:

```text
Value ::= Int
        | Float
        | Bool
        | Char
        | String
        | IntList
        | FloatList
        | StringList
```

Value rules:

- `Char` is a scalar character value.
- `String` is a sequence of `Char`.
- `IntList` contains only `Int` elements.
- `FloatList` contains only `Float` elements.
- `StringList` contains only `String` elements.
- Heterogeneous lists, nested lists, generic lists, char lists, product values,
  and null values are not part of the value domain.

## Type Notation

Expression types:

```text
T ::= Int | Float | Bool | Char | String
    | IntList | FloatList | StringList
```

List element types:

```text
ListElem ::= Int | Float | String
```

List type constructor:

```text
ListOf(Int)    = IntList
ListOf(Float)  = FloatList
ListOf(String) = StringList
```

Sequence types:

```text
Seq ::= String | IntList | FloatList | StringList
```

ASGP source element types:

```text
AsgpElem ::= Int | Float | String | Char
```

ASGP source sequence constructor:

```text
SeqOf(Int)    = IntList
SeqOf(Float)  = FloatList
SeqOf(String) = StringList
SeqOf(Char)   = String
```

`SeqOf(Char)` denotes string traversal by character. It does not introduce a
`CharList` runtime value.

## Conversion Policy

No subtype relation or implicit conversion is defined.

Explicit conversions:

```text
char_to_string: Char -> String
string_to_char: String -> Char
ord:            Char -> Int
chr:            Int -> Char
to_string:      Int -> String
to_string:      Float -> String
```

Conversion rules:

- `string_to_char(s)` succeeds only when `len(s) == 1`; otherwise it yields
  `ValueError`.
- `chr(i)` succeeds only for supported character code points; otherwise it
  yields `ValueError`.
- `Char` is not accepted where `String` is required.
- `String` is not accepted where `Char` is required.
- `IntList` and `FloatList` are not interchangeable.

## Lexical Environments

Expression typing is written:

```text
E[T; L; Q]
```

where:

- `T` is the expression result type.
- `L` maps ordinary local variable names to grammar types.
- `Q` maps immutable structured or scheme binder names to grammar types.

Ordinary locals and binders are distinct:

- `Var(x)` reads an ordinary local from `L`.
- `BoundVar(b)` reads an immutable binder from `Q`.
- `Assign` may update ordinary locals.
- Binders cannot be assigned.

Implementations must represent binder references in a capture-safe way.

## Program Grammar

```text
Program[R; L0] ::= Block[R; L0]

Block[R; L] ::= [Stmt[R; L], ...]

Stmt[R; L] ::= Assign(x:T, E[T; L; empty])
             | IfStmt(E[Bool; L; empty],
                      Block[R; L],
                      Block[R; L])
             | ForRange(i, E[Int; L; empty],
                        Block[R; L + {i:Int}])
             | Return(E[R; L; empty])
```

Statement rules:

- `Assign` evaluates its expression and stores the result in the ordinary local
  environment.
- `IfStmt` requires a `Bool` condition and executes only the selected block.
- `ForRange` evaluates its bound exactly once, requires a non-negative `Int`,
  and iterates with indices `0, 1, ...` while the index is less than the bound.
- `Return` evaluates its expression and terminates execution.
- Reading an undefined ordinary local yields `NameError`.
- Reaching the end of a program without `Return` yields `ValueError`.

## Expression Grammar

```text
E[T; L; Q] ::= Const(v)                         if type(v) = T
             | Var(x)                           if L(x) = T
             | BoundVar(b)                      if Q(b) = T
             | Unary(op, E[T1; L; Q])           if op: T1 -> T
             | Binary(op, E[T1; L; Q],
                           E[T2; L; Q])          if op: T1 x T2 -> T
             | IfExpr(E[Bool; L; Q],
                      E[T; L; Q],
                      E[T; L; Q])
             | Call(name, args)                 if name(args) -> T

             | MapList[A, C](
                   E[ListOf(A); L; Q],
                   body(u:A): E[C; L; Q + {u:A}]
               )

             | FilterList[A](
                   E[ListOf(A); L; Q],
                   pred(u:A): E[Bool; L; Q + {u:A}]
               )

             | LinearRec[A, R](
                   E[ListOf(A); L; Q],
                   E[Int; L; Q],
                   E[R; L; Q],
                   step(u:A, v:R, idx:Int):
                       E[R; L; Q + {u:A, v:R, idx:Int}],
                   last(u:A, idx:Int):
                       E[R; L; Q + {u:A, idx:Int}]
               )

             | AsgpDC[A, T](
                   source: E[SeqOf(A); L; Q],
                   solve(xs:SeqOf(A), n:Int, lo:Int):
                       E[T; empty; Qdc_solve(A)],
                   ordivide(n:Int):
                       E[Int; empty; Qdc_divide],
                   andcombine(r1:T, r2:T):
                       E[T; empty; Qdc_combine(T)]
               )

             | AsgpDP1D[T](
                   state: E[Int; L; Q],
                   bounds: DpBounds1D[T],
                   solve(s:Int):
                       E[T; empty; Qdp1_solve],
                   depselect(s:Int):
                       DpDeps1D[K],
                   transition(s:Int, d1:T, ..., dK:T):
                       E[T; empty; Qdp1_transition(T, K)]
               )

             | AsgpDP2D[T](
                   state_i: E[Int; L; Q],
                   state_j: E[Int; L; Q],
                   bounds: DpBounds2D[T],
                   solve(i:Int, j:Int):
                       E[T; empty; Qdp2_solve],
                   depselect(i:Int, j:Int):
                       DpDeps2D[K],
                   transition(i:Int, j:Int, d1:T, ..., dK:T):
                       E[T; empty; Qdp2_transition(T, K)]
               )
```

`A` and `C` range over `ListElem`. `R` and `T` range over expression types.

## Operators

Exact numeric operands:

```text
SameNumeric(A, B)       is valid only when A and B are the same runtime type
SameNumeric(A, B, C)    is valid only when A, B, and C are the same runtime type
```

Unary operators:

```text
NEG: A -> A       where A is Int or Float
NOT: Bool -> Bool
```

Arithmetic operators:

```text
ADD, SUB, MUL, MOD:
  A x A -> A              where A is Int or Float

DIV:
  A x A -> Float          where A is Int or Float
```

Comparison operators:

```text
LT, LE, GT, GE: A x A -> Bool
                  where A is Int or Float

EQ, NE:         T x T -> Bool
                  for exact same runtime type T

AND, OR:        Bool x Bool -> Bool
```

Ordering comparisons are valid only for numeric scalar types. Equality and
inequality require exact same runtime type.

## Builtins

Scalar builtins:

```text
abs:      A -> A
            where A is Int or Float

min:      A x A -> A
max:      A x A -> A
            where A is Int or Float

clip:     A x A x A -> A
            where A is Int or Float

idiv0:    Int x Int -> Int
imod0:    Int x Int -> Int
```

Sequence builtins:

```text
len:      Seq -> Int
concat:   Seq x Seq -> Seq                         for matching sequence tags
slice:    Seq x Int x Int -> Seq
index:    String x Int -> Char
index:    ListOf(A) x Int -> A
append:   ListOf(A) x A -> ListOf(A)
prepend:  ListOf(A) x A -> ListOf(A)
reverse:  Seq -> Seq
find:     String x String -> Int
contains: String x String -> Bool
```

Character and string builtins:

```text
char_to_string: Char -> String
string_to_char: String -> Char
ord:            Char -> Int
chr:            Int -> Char
is_letter:      Char -> Bool
is_digit:       Char -> Bool
is_space:       Char -> Bool
is_vowel:       Char -> Bool
to_lower:       Char -> Char
to_upper:       Char -> Char
to_string:      Int -> String
to_string:      Float -> String
```

Construction helpers:

```text
singleton: A -> ListOf(A)
             where A is Int, Float, or String

singleton: Char -> String
```

Builtin rules:

- Builtins are pure.
- Builtins do not access external state.
- Invalid argument types yield `TypeError`.
- Invalid values for a valid type yield `ValueError`.
- Protected integer operations return `0` when the divisor is `0`.

## Structured List Expressions

Structured list expressions are syntax forms with lexical bodies. They are not
first-class functions and cannot be returned, stored, or passed to ordinary
builtins.

### `MapList`

```text
MapList[A, C](
  xs: E[ListOf(A); L; Q],
  body(u:A): E[C; L; Q + {u:A}]
) -> ListOf(C)
```

Rules:

- Evaluate `xs` exactly once.
- `xs` must evaluate to `ListOf(A)`.
- Visit source elements left to right.
- Evaluate `body` once per element with `u` bound to the current element.
- Every body result must have type `C`.
- Return `ListOf(C)`.
- Empty input returns an empty list with type `ListOf(C)`.

### `FilterList`

```text
FilterList[A](
  xs: E[ListOf(A); L; Q],
  pred(u:A): E[Bool; L; Q + {u:A}]
) -> ListOf(A)
```

Rules:

- Evaluate `xs` exactly once.
- `xs` must evaluate to `ListOf(A)`.
- Visit source elements left to right.
- Evaluate `pred` once per element with `u` bound to the current element.
- Preserve elements whose predicate result is `True`.
- Preserve source order.
- Return `ListOf(A)`.

### `LinearRec`

```text
LinearRec[A, R](
  xs: E[ListOf(A); L; Q],
  start_idx: E[Int; L; Q],
  empty_case: E[R; L; Q],
  step(u:A, v:R, idx:Int): E[R; L; Q + {u:A, v:R, idx:Int}],
  last(u:A, idx:Int): E[R; L; Q + {u:A, idx:Int}]
) -> R
```

Rules:

- Evaluate `xs` exactly once.
- Evaluate `start_idx` exactly once.
- `xs` must evaluate to `ListOf(A)`.
- `start_idx` must evaluate to `Int`.
- Empty input evaluates only `empty_case`.
- Singleton input evaluates only `last`.
- Longer input evaluates `last` for the final element, then evaluates `step`
  from right to left.
- `step`, `last`, and `empty_case` must all produce `R`.

Mathematical definition:

```text
LinearRec([], idx, empty, step, last) =
  empty

LinearRec([u], idx, empty, step, last) =
  last(u, idx)

LinearRec(u :: rest, idx, empty, step, last) =
  step(u,
       LinearRec(rest, ADD(idx, 1), empty, step, last),
       idx)
```

## ASGP-DC

ASGP-DC is a fixed divide-and-conquer scheme. The evolvable phase grammar is:

```text
AsgpDC[A, T] ::= source
               + solve(xs:SeqOf(A), n:Int, lo:Int): T
               + ordivide(n:Int): Int
               + andcombine(r1:T, r2:T): T
```

The fixed template semantics are:

```text
base = SizeAtMost(1)
split = clamp(ordivide(n), 1, n - 1)
left/right recursive evaluation
fuel accounting
phase visibility
```

### DC Phase Grammar

Solve phase:

```text
Qdc_solve(A) = {
  xs: SeqOf(A),
  n: Int,
  lo: Int
}

solve(xs:SeqOf(A), n:Int, lo:Int): E[T; empty; Qdc_solve(A)]
```

Divide phase:

```text
Qdc_divide = {
  n: Int
}

ordivide(n:Int): E[Int; empty; Qdc_divide]
```

Combine phase:

```text
Qdc_combine(T) = {
  r1: T,
  r2: T
}

andcombine(r1:T, r2:T): E[T; empty; Qdc_combine(T)]
```

Visibility rules:

- `solve` may read only `xs`, `n`, `lo`, and constants.
- `ordivide` may read only `n` and constants.
- `andcombine` may read only `r1`, `r2`, and constants.
- `ordivide` must not inspect source elements.
- `andcombine` must not inspect the original source or subproblem sequence.
- ASGP phase bodies cannot assign locals.
- ASGP phase bodies cannot recursively call ASGP schemes.

### DC Evaluation

```text
eval_dc(xs, lo):
  n = len(xs)

  if n <= 1:
    return solve(xs, n, lo)

  raw = ordivide(n)
  k = clamp(raw, 1, n - 1)

  r1 = eval_dc(slice(xs, 0, k), lo)
  r2 = eval_dc(slice(xs, k, n), lo + k)

  return andcombine(r1, r2)

AsgpDC(source, solve, ordivide, andcombine):
  return eval_dc(source, 0)
```

`lo` is the source index offset of the current subproblem.

## ASGP-DP

ASGP-DP is a fixed memoized dynamic-programming scheme. The evolvable phase
grammar is:

```text
solve
depselect
transition
```

`depselect` produces a scheme-level dependency pattern. It is not an ordinary
runtime expression value.

### ASGP-DP 1D

```text
AsgpDP1D[T] ::= state
              + bounds
              + solve(s:Int): T
              + depselect(s:Int): DpDeps1D[K]
              + transition(s:Int, d1:T, ..., dK:T): T
```

Node form:

```text
E[T; L; Q] ::= AsgpDP1D[T](
  state: E[Int; L; Q],
  bounds: DpBounds1D[T],

  solve(s:Int):
    E[T; empty; Qdp1_solve],

  depselect(s:Int):
    DpDeps1D[K],

  transition(s:Int, d1:T, ..., dK:T):
    E[T; empty; Qdp1_transition(T, K)]
)
```

Dependency grammar:

```text
DpDeps1D[K] ::= Backward1(c1)
              | Backward2(c1, c2)
              | Backward3(c1, c2, c3)
              | Forward1(c1)
              | Forward2(c1, c2)
              | Forward3(c1, c2, c3)
```

Dependency constraints:

- `c_i` is a positive `Int` constant.
- `c_i <= max_step`.
- All offsets in one dependency pattern use the same direction.
- `K` is the number of dependencies selected by the pattern.

Dependency expansion:

```text
BackwardK(c1, ..., cK) => [s - c1, ..., s - cK]
ForwardK(c1, ..., cK)  => [s + c1, ..., s + cK]
```

Phase grammar:

```text
Qdp1_solve = {
  s: Int
}

solve(s:Int): E[T; empty; Qdp1_solve]

Qdp1_transition(T, K) = {
  s: Int,
  d1: T,
  ...,
  dK: T
}

transition(s:Int, d1:T, ..., dK:T):
  E[T; empty; Qdp1_transition(T, K)]
```

Evaluation:

```text
eval_dp1d(s):
  if out_of_bounds(s):
    return boundary_value

  if is_base(s):
    return solve(s)

  if memo[s] is defined:
    return memo[s]

  deps = depselect(s)
  values = [eval_dp1d(s2) for s2 in deps]
  out = transition(s, values...)
  memo[s] = out
  return out
```

`bounds`, `is_base`, and `boundary_value` are fixed scheme parameters.

### ASGP-DP 2D

```text
AsgpDP2D[T] ::= state_i
              + state_j
              + bounds
              + solve(i:Int, j:Int): T
              + depselect(i:Int, j:Int): DpDeps2D[K]
              + transition(i:Int, j:Int, d1:T, ..., dK:T): T
```

Node form:

```text
E[T; L; Q] ::= AsgpDP2D[T](
  state_i: E[Int; L; Q],
  state_j: E[Int; L; Q],
  bounds: DpBounds2D[T],

  solve(i:Int, j:Int):
    E[T; empty; Qdp2_solve],

  depselect(i:Int, j:Int):
    DpDeps2D[K],

  transition(i:Int, j:Int, d1:T, ..., dK:T):
    E[T; empty; Qdp2_transition(T, K)]
)
```

Dependency grammar:

```text
DpDeps2D[K] ::= CrossBackward
              | CrossForward
              | DiagonalBackward
              | DiagonalForward
              | NeighborhoodBackward3
              | NeighborhoodForward3
```

Dependency expansion:

```text
CrossBackward => [(i - 1, j), (i, j - 1)]

CrossForward => [(i + 1, j), (i, j + 1)]

DiagonalBackward => [(i - 1, j - 1)]

DiagonalForward => [(i + 1, j + 1)]

NeighborhoodBackward3 => [(i - 1, j), (i, j - 1), (i - 1, j - 1)]

NeighborhoodForward3 => [(i + 1, j), (i, j + 1), (i + 1, j + 1)]
```

Dependency constraints:

- All dependencies must be monotone in the configured direction.
- Dependencies outside bounds use `boundary_value`.
- Dependency grammar must guarantee an acyclic memo graph.
- `K` is fixed by the selected dependency pattern.

Phase grammar:

```text
Qdp2_solve = {
  i: Int,
  j: Int
}

solve(i:Int, j:Int): E[T; empty; Qdp2_solve]

Qdp2_transition(T, K) = {
  i: Int,
  j: Int,
  d1: T,
  ...,
  dK: T
}

transition(i:Int, j:Int, d1:T, ..., dK:T):
  E[T; empty; Qdp2_transition(T, K)]
```

Evaluation:

```text
eval_dp2d(i, j):
  if out_of_bounds(i, j):
    return boundary_value

  if is_base(i, j):
    return solve(i, j)

  if memo[i, j] is defined:
    return memo[i, j]

  deps = depselect(i, j)
  values = [eval_dp2d(i2, j2) for (i2, j2) in deps]
  out = transition(i, j, values...)
  memo[i, j] = out
  return out
```

`bounds`, `is_base`, and `boundary_value` are fixed scheme parameters.

## Generation And Variation Contract

Generation, crossover, and mutation must preserve:

- exact result type
- ordinary local scope
- binder scope
- ASGP scheme kind
- ASGP phase name
- ASGP visible environment
- dependency pattern arity for DP transition phases

General expression bucket key:

```text
bucket_key =
  result_type
  + scope_signature
```

ASGP phase bucket key:

```text
asgp_bucket_key =
  scheme_kind
  + phase_name
  + result_type
  + visible_env_signature
```

ASGP crossover is valid only when both donor and destination have the same
scheme kind, phase name, result type, and visible environment signature.

`depselect` mutation regenerates a dependency pattern from the dependency
grammar, not from ordinary runtime expressions.

## Runtime Requirements

- Evaluation is deterministic.
- Builtins and structured expressions are pure.
- Each executed instruction or equivalent lowered operation consumes fuel.
- Fuel exhaustion yields `Timeout`.
- The first runtime error terminates execution.
- ASGP runtime recursion must be bounded by fuel and implementation-defined
  depth or memo-table limits.
- GPU implementations must not depend on device recursion.
- Overflow of ASGP frame stacks, memo tables, or payload materialization must
  produce a deterministic runtime result.

## Unsupported Constructs

The grammar does not include:

- user-defined functions
- first-class closures
- first-class ASGP phase bodies
- classes or attributes
- exceptions
- imports
- I/O
- `while`
- `break`
- `continue`
- generic list values
- `CharList`
- tuple or product values


## Compiled-grammar membership witnesses

Materialized native ASTs may be executed without grammar provenance. The
`reconstruct_derivation` API certifies membership against a supplied compiled grammar
and contextual generation request, independently of any metadata attached to the
genome. It performs native verification, exact grammar/domain/template matching and
native lowering before returning a witness. An incompatible AST fails with a grammar
membership diagnostic. This API does not change the legacy production variation path.

Reconstruction is deterministic for a given grammar, AST and request. Membership
matching considers production order and excludes active zero-node alias cycles; it
records successful production decisions, then constructs provenance in a separate
traversal. Failed alternatives do not contribute witness rows. Positive decisions
may be memoized; their provenance is rebuilt with the correct parent and physical
span when reached from the selected derivation.

Witness rows use the same nonterminal, production, expression, template and slot IDs
as the compiled grammar. Repeated or forwarded template holes retain common logical
node and template-instance identities while recording each physical occurrence.
Fixed template skeletons remain fixed. An expression root's four structural envelope
nodes are fixed sentinel origins with no production/nonterminal identity. Table
compaction may renumber names and constants without changing this provenance.

A reconstructed witness has `seed_replayable=false`: it certifies membership, not
an original sequence of RNG decisions. Original generation retains
`seed_replayable=true`. The `grammar-generated-v1` encoder rejects reconstructed
witnesses rather than presenting them as seed replay artifacts. Cloning and table
compaction preserve the distinction. Materialized execution remains independent of it.

Reconstruction enforces the request's complete AST node/depth budgets and the same
1,048,576-instruction lowered-code limit as generation. Its witness traversal is
limited to 1,048,576 logical steps, 4,096 grammar frames and 256 nested template
instances; exhaustion is an explicit error. General lexical and structured forms
execute through their compiled CPU/GPU runtime contracts.

### Replacement-site contracts

`analyze_variation` reconstructs a witness and collects admitted Expression and
complete Program nonterminal boundaries. Fixed template interiors cannot invent new
replacement sites; an enclosing nonterminal may replace the entire template it
admits. Block/Stmt fragment generation is not enabled by this analysis API.
Each logical choice groups every physical occurrence, including forwarded/repeated
holes. Its incoming template definition and declared slot identify the compatibility
contract; a per-genome template instance identifies the atomic group only.

Compatibility compares an exact length-framed key containing grammar/semantic identity,
nonterminal, category, exact type, compiled context, declared template/slot, ordered
visible lexical environment and exact available native variable environment. Native
name-table indices are normalized to names for this comparison. Current native locals
are uniquely named declarations within a grammar; lexical binder instances use their
explicit declaration IDs. All free local references of an
expression site are recorded and must be available at every grouped occurrence.
Conservative full-environment equality may reject otherwise legal substitutions.

The verifier's opt-in `capture_exact_scopes` records interned sorted local and binder
environments separately, with one numeric ID per expression and the UINT32_MAX
sentinel on structural nodes. Interning compares complete environments. The default
verification path leaves these annotations empty. Repeated sites use the intersection
of available native environments, so a donor must be valid at all physical copies.

Budgets are separate from compatibility equality. For an atomic group with C copies,
N total program nodes and R removed nodes, the per-copy node allowance is
`floor((max_nodes - N + R) / C)`. Depth uses the tightest physical occurrence allowance.
Template nesting also uses physical occurrence depth, not logical hole-owner ancestry;
forwarding can make those differ. Witness copying adjusts recorded physical depths
while preserving shared logical IDs, and rejects physical nesting above 256. Donor
size, prefix depth and template height must fit independently. `donor_request` adds
the four-node/three-level envelope for standalone expression generation.

A compatibility registry assigns dense IDs to exact keys, up to 65,536 contracts.
IDs are comparable only within the same registry; equal IDs from separate registries
do not establish compatibility. The analysis cache owns its immutable grammar
and registry, stores owned immutable analyses, and uses bounded FIFO eviction
(default 128 entries). Its key covers grammar identity, complete materialized runtime
identity (including decoded constants, inputs, fuel and semantic version), requested
nonterminal/type, ordered lexical environment and both structural limits. Invalid
requests cannot hit the cache; expired payload tokens must be resolved before lookup.
Hits, misses and evictions are counted. The mutable cache is owned by one preparation
worker; concurrent workers must not share it without synchronization.

### Isolated donor frames

`GenerationFrame` supplies available declared native locals as additional inputs only
for isolated donor verification and lowering. It does not change grammar inputs or
interpret the request's lexical environment as native variables. Bindings must have
unique declared local names and exact types; unknown names, input collisions and type
mismatches are rejected. Complete Program generation requires an empty local frame.

For Expression donors, generation computes depth-indexed minimum costs with unavailable
local leaves excluded. The same costs govern alternative eligibility, alias feasibility,
child reservations and repeated template-hole multiplicity. If no derivation fits the
frame and budget, generation fails before sampling. Program generation retains its
normal assignment/dataflow verification because locals can become available internally.

Framed membership and witness reconstruction verify against the explicit original-input
plus local schema. The resulting metadata is not original seed replay provenance and
cannot be encoded as an original generated artifact. A framed expression may refer to
locals absent from the original input schema; it must be spliced into a destination and
the complete child verified and reconstructed under the original grammar inputs before
population acceptance. Compiled operators use these APIs solely for isolated donors.

`generate_donor` validates the site's nonterminal/type/category/context, derives its
native local frame, and returns both the isolated input schema and a payload span.
Expression payloads exclude their fixed standalone envelope; Program payloads include
the complete native program. Measured payload nodes, prefix depth and physical template
height must fit the destination. Invalid availability and infeasible generation produce
an explicit donor-generation error.

### Compiled variation operators

The existing `crossover` and `mutate` APIs have compiled-grammar overloads using a
worker-owned `VariationContext`. This context owns an immutable grammar/request, one
analysis cache/compatibility registry and cumulative counters. CPU reproduction uses
these overloads when `EvolutionConfig::compiled_grammar` is set. GPU reproduction
packs the same contracts and domains, performs selection, crossover and mutation on
the device, and reconstructs and certifies accepted children on the host.

Crossover samples uniformly among pairs of admitted sites whose exact contracts match
and whose payloads fit both destinations. Each selected logical group's occurrences
are replaced together, in descending physical order, using the same donor payload.
The complete children are compacted and independently certified under the original
input schema. Invalid imported parents raise errors before they can serve as fallbacks;
all returned fallback parents also carry freshly validated, non-seed-replayable witness
metadata. Internal logic/resource failures are not treated as invalid child fallbacks.

Mutation selects subtree regeneration according to its configured probability.
Otherwise it samples a logical mutable constant group and resamples from the exact
constant domain in its reconstructed production. All physical copies receive the same
value. Fixed template constants are excluded. If no mutable constant group exists,
mutation regenerates an admitted nonterminal instead. Generation or acceptance failure
returns the certified parent and increments the corresponding rejection counter.

Counters distinguish operator attempts, rejected candidate contracts/budgets, donor
generation failures, child acceptance failures, fallback children, unchanged children
and changed children. Candidate rejection counts describe pair enumeration, not operator
attempts. Fallback children are a subset of unchanged children. Actual-change comparison
resolves referenced names and constant values per node, ignoring unused tables and
constant-pool sharing, so table remapping cannot count as evolutionary progress.

### Compiled reproduction integration

`EvolutionConfig` owns an immutable compiled grammar and an optional generation request;
without an explicit request it uses the grammar entry. A request without a grammar is
invalid. Compiled execution fuel must equal the grammar's declared fuel. Legacy limits
and grammar-config restrictions do not override compiled search limits or input types.
Population initialization uses compiled generation; imported populations require witness
reconstruction before their first fitness evaluation. Fitness cases must match the exact
input schema and supply every input in every case.

The CPU backend creates one worker-owned variation context per generation and validates
all parents, including unselected parents. It preserves tournament selection, shuffling,
seed draws, crossover before mutation, and per-child mutation decisions. Legacy CPU
ablation paths are incompatible with compiled reproduction. Operator counters propagate
through generation and aggregate timing and CLI output. Counts describe operator outputs:
a crossover produces two classified children even when an odd population size discards
the second; mutation classifies its output again. They are not final-population counts.

Shared host preparation carries numeric compatibility IDs, flattened atomic occurrence
spans, independent destination budgets, materialized donor measures and contiguous
per-site donor ranges. IDs use one retained registry across the population. Logical sites
are sampled without replacement; physical copies do not become independent candidates.
The packed program metadata records the actual candidate count, with invalid padding
rather than repeated candidates. Compiled donor pools are per site because equal contracts
may have different budgets. Generation failure leaves the site eligible for crossover,
possibly with zero mutation donors, and increments generation rejection accounting.

Preparation owns the immutable grammar, registry keys and materialized population/donor
identities. Packing rejects stale identities, changed prepared search limits and invalid spans/contracts. It extracts
standalone donor payloads without expression envelopes and preserves referenced names and
constant values. Packing prescans donor tables and grows required capacities; it never
truncates a compiled payload. GPU transport limits are 512 nodes per program/payload,
128 names, 128 constants and 256 MiB of padded buffers; preparation additionally bounds
item counts. These transport limits do not restrict CPU grammar evolution.

Compiled-mode buffers enter the compiled CUDA selection and two-pass variation path;
legacy-only entry points reject them. Guards cover top-level evolution, backend
dispatch, direct GPU preparation and execution, overlap start/finish, decoding, and
low-level allocation/upload/launch/copyback. They run before CUDA operations or pointer
access, including empty populations. Compiled kernels enforce numeric contract IDs,
destination budgets, atomic occurrence groups, and explicit copyback provenance.

## General lexical regions and traversal

`LetRegion(initializer, body)` evaluates its initializer once, stores the value in
an immutable lexical binding, and evaluates the body with that binding visible.
The initializer cannot see its own binding. A nested body may capture enclosing
lexical bindings. Ordinary variables and transitional named `BoundVar` bindings
are separate namespaces. `RegionVar(id)` refers to a globally unique declaration
ID in the current AST, independent of the ordinary name table. Declaration IDs
are integers in `[0, INT_MAX)` and are immutable during execution; copying a region
must explicitly rename introduced IDs to avoid capture.

`Traverse(sequence, start_index, seed, step)` visits the whole sequence in its
statically declared forward or reverse direction. `TraverseRange(sequence,
start_index, begin, end, seed, step)` visits a half-open physical offset interval.
Each endpoint is clamped independently to `[0, len(sequence)]`; an interval whose
end is at most its beginning is empty. Forward order is beginning through end
minus one; reverse order is end minus one through beginning.

Traversal evaluates the sequence and index operands once in source order, then
checks the index operands as exact Ints in source order and obtains the sequence
length. It then evaluates the seed once, including for empty intervals. The step
is evaluated only for visited elements. Its three immutable bindings, in order,
are the current element, the Int index, and the current accumulator. The index is
`ADD(start_index, physical_offset)` using the existing CPU ADD behavior;
it is not an array-bound constraint. The initial accumulator is the seed. Each
step returns the next accumulator, and traversal returns the final accumulator.

Sequences are String, IntList, FloatList, or StringList. String elements are Char;
there is no CharList. The element binding has exactly the sequence's element type,
the index binding is exactly Int, and the accumulator binding, seed, step result,
and traversal result share one of the eight exact public value types. All body
branches are statically checked even when a branch is not executed. Runtime
conditionals remain lazy. Traversal uses the existing len/index semantics and
payload lifetime contract; it does not eagerly materialize the sequence or a slice.

Bodies are static prefix-AST regions, not runtime closures. The
lowering uses hidden locals and a bounded-size loop, with no per-element grammar
lookup. Compiled blocks containing these forms carry an explicit unit-cost fuel
schedule unless explicit source semantic event profiles override its charging as
described below.
GPU evaluation executes the lowered lexical and traversal bytecode with the same
semantic fuel schedule. Compiled-grammar GPU reproduction preserves the native
body metadata and capture mappings through device variation and verified copyback.

Migration compatibility note: the frozen CPU ADD implementation converts Int
operands through double before its wrapping helper. Large Ints can therefore lose
precision: on the frozen reference, `9007199254740993 + 2` produces
`9007199254740994`, and `INT64_MAX + 2` produces `INT64_MIN + 2`. Traversal index
calculation preserves that measured behavior; it does not substitute idealized
64-bit addition. Range endpoints are clamped by comparisons and selection, avoiding
this numeric conversion. The boundary reference probe is recorded in the Goal05
execution evidence; the existing scalar runtime is unchanged by this compiled path.


### Checked values

`CHECK_INT(value)` evaluates its child once, applies the existing CHECK_INT bytecode
validation and returns the unchanged value. Its static argument and result type are
Int. `CHECK_LIST(value)` similarly applies CHECK_LIST and preserves its child's exact
static IntList, FloatList or StringList type. At runtime CHECK_LIST accepts any of
these three list tags; it does not resolve payload storage or convert elements.
Both validations run after child evaluation and raise Type on an inadmissible tag.
They remain lazy when placed in an unselected conditional branch. These general
expressions allow validation order to be represented explicitly in a composition.
CPU and GPU execution apply the same validation bytecode, and compiled GPU
reproduction preserves the corresponding native nodes.


### Source semantic fuel events

An AST may attach a `fuel_specs` row to a supported expression node. A row maps
named semantic events to nonnegative costs bounded by INT_MAX; unspecified events
of that profiled node cost one. Nodes without a profile retain unit instruction
charging. Profiles never implicitly apply to child expressions. Costs are paid
before the associated event; insufficient fuel produces Timeout before its effects
or validation. Zero-cost events may execute with zero remaining fuel. The compiler
rejects possible zero-cost control-flow cycles, including in Release builds.
GPU bytecode packing carries the same validated schedule on every instruction.

For a simple constant, variable, unary operation, arithmetic/comparison or builtin,
`operation` applies after argument evaluation, at the operation itself. Boolean
AND/OR and legacy ASGP structured expressions do not accept profiles. A bounded
region accepts the `operation` event independently from its plan's frame-entry fuel.
Let supports
`bind`, after the initializer. IfExpr supports `branch_test` after the condition and
`branch_merge` only when the then branch jumps over the else branch.

Traversal supports the following ordered events; argument and body evaluations
occur between these events and keep their own schedules:

- `store_sequence`, `store_start`, and (for ranged traversal) `store_begin`,
  `store_end`, each after evaluation of the corresponding argument.
- `check_start` and, when ranged, `check_begin`, `check_end`, validating index values.
- `observe_sequence`, obtaining and retaining the sequence length.
- `clamp_begin`, `clamp_end` for ranged traversal; otherwise `set_begin`, `set_end`.
- `initialize_state` after seed evaluation, then `initialize_cursor`.
- `test_cursor` at every loop guard, including the final failed guard.
- `read_element`, `bind_element`, `compute_index`, then step-body evaluation.
- `update_state`, `advance_cursor`, `repeat` for each completed step.
- `result` when loading the final state.

Each event is charged once when entered, independent of the number of administrative
instructions used to implement it. Grouped loads, stores and arithmetic carry zero
additional cost within that event. Events and profiles have no package-name branch.
Unselected branches and unentered loop bodies incur no charges. Profiles are
materialized execution metadata; the current compiled grammar schema does not
declare them, so grammar membership rejects unsolicited source profiles.


## Native bounded recursive regions

The general native `BOUNDED_REGION` expression is represented by one node and a
node-indexed `BoundedRegionSpec`. Its shared `RegionPlan` specifies typed states,
result, captures, preparations, ordered recursive state constructors, progress
proof, memo policy and execution bounds as defined in the private bounded-region
ISA. This is an internal materialized expression; recursion is not a dynamic
function call and no tuple value is introduced.

The flat prefix arguments have canonical order: initial states, additional Int
bound operands, base predicate, base body, preparations, request expressions,
combine, and coordinate boundary when applicable. The node's `i0` caches this
arity (at most 52); verification requires exact equality with the plan. `i1` is
zero. Every phase has one metadata row in the same canonical order, identifying
its argument and binding visible state/parameter/prepared/result/measure slots to
fresh native lexical binder IDs. All binder IDs are globally unique across these
phase declarations and ordinary lexical regions. The shared plan determines exact
phase output types and slot visibility; the metadata cannot broaden either.

Initial state and bound expressions use the surrounding scope and evaluate in
source order. Parameter metadata explicitly captures either a visible lexical
binder ID or a name-table index. A lexical capture must resolve to a visible binder
of the declared exact type. A named capture declares its exact type through the
plan: if the name has a known surrounding type, that type must agree; otherwise
its physical caller local may remain unset. Capturing does not evaluate a LOAD.
This does not relax the undefined-local rule for ordinary `VAR` expressions.
The runtime checks a named capture only when a phase reads it, preserving Name
for unset locals and Type for actual values with a different tag.

Phase expressions use only their declared lexical binders and bindings introduced
within the phase. They cannot implicitly read surrounding ordinary names or
lexical binders. An outer value enters through an explicit Parameter-bank binding.
Each phase has its exact declared nominal result type; the base predicate returns
Bool. At runtime, boundary, base and combine results may carry the internal payload
fallback representation under the sibling/combine tag rules in `bytecode_isa.md`.
State, predicate, preparation and request-expression values retain exact runtime tags.
Nested bounded regions are permitted in initial argument expressions, but all
structured recursive source forms are rejected inside isolated phases. Ordinary
local lexical regions and traversals remain valid within phase expressions.

The source `operation` fuel event controls the containing BOUNDED_REGION opcode;
phase expressions carry their own ordinary source fuel events. Frame entry uses
the plan's positive serialized charge. Prefix traversal, hashing, table compaction
and subtree replacement preserve every plan field and capture/binding mapping.
Subtree insertion freshens introduced phase binder IDs before remapping captures.
Compiled-grammar GPU reproduction preserves bounded-region plans and phase bindings
through device variation and verified copyback. GPU execution uses bounded explicit
frame and memo storage; declarations exceeding supported device capacities reject
explicitly rather than selecting a CPU execution path. See the device transport
limits in `bytecode_format.md`.
