# Grammar

This document defines the release 2.0.0 source grammar, type rules, structured
forms, and production generation/variation contract. All programs use the
prefix `AstProgram` representation. Production authoring, generation,
membership, crossover, and mutation use a compiled `grammar-definition-v2`.

Related specifications:

- [builtins_base.md](builtins_base.md)
- [builtins_runtime.md](builtins_runtime.md)
- [bytecode_isa.md](bytecode_isa.md)
- [bytecode_format.md](bytecode_format.md)
- [grammar_definition.md](grammar_definition.md)

## Value and type domain

```text
T ::= Int | Float | Bool | Char | String
    | IntList | FloatList | StringList

Seq ::= String | IntList | FloatList | StringList
```

`String` contains `Char`; each list has its named homogeneous element type.
Nested, heterogeneous, generic, and null list values do not exist. There is no
subtyping or implicit conversion. Explicit conversions and builtins are defined
by the builtin specifications.

## Environments

Expression typing is written `E[T; L; Q]`, where `L` maps ordinary local
names to types and `Q` maps immutable lexical declaration IDs to types.
`Var` reads `L`; `RegionVar` reads `Q`; `Assign` may update only
ordinary locals. Declaration IDs are capture-safe and immutable.

## Program grammar

```text
Program[R; L0] ::= Block[R; L0]

Block[R; L] ::= [Stmt[R; L], ...]

Stmt[R; L] ::= Assign(x:T, E[T; L; empty])
             | IfStmt(E[Bool; L; empty], Block[R; L], Block[R; L])
             | ForRange(i, E[Int; L; empty], Block[R; L + {i:Int}])
             | Return(E[R; L; empty])
```

`ForRange` evaluates its bound once, requires a non-negative `Int`, and
visits indices from zero while the index is less than the bound. Reading an
undefined local yields `NameError`; falling through without `Return` yields
`ValueError`.

## Expression grammar

```text
E[T; L; Q] ::= Const(v)                         if type(v) = T
             | Var(x)                           if L(x) = T
             | RegionVar(id)                    if Q(id) = T
             | Unary(op, E[A; L; Q])            if op: A -> T
             | Binary(op, E[A; L; Q], E[B; L; Q])
                                                   if op: A x B -> T
             | IfExpr(E[Bool; L; Q], E[T; L; Q], E[T; L; Q])
             | Call(name, args)                 if name(args) -> T
             | LetRegion(E[A; L; Q],
                         body(u:A): E[T; L; Q + {u:A}])
             | Traverse(E[S; L; Q], E[Int; L; Q], E[T; L; Q],
                        step(u:Elem(S), idx:Int, acc:T):
                          E[T; L; Q + {u,idx,acc}])
             | TraverseRange(E[S; L; Q], E[Int; L; Q],
                             E[Int; L; Q], E[Int; L; Q], E[T; L; Q],
                             step(u:Elem(S), idx:Int, acc:T):
                               E[T; L; Q + {u,idx,acc}])
             | CheckInt(E[Int; L; Q])
             | CheckList(E[A; L; Q])            if A is a list type
             | BoundedRegion(plan, phases)       if the general region
                                                   contract yields T
```

`S` is a sequence type. `Elem(String)=Char`, and list element types follow
their list tags. Operators require exact types: numeric arithmetic and ordering
accept `Int` or `Float`; boolean operators accept `Bool`; equality
requires the same runtime tag. `DIV` returns `Float`; the other arithmetic
operators preserve their exact numeric type.

## General lexical regions and traversal

`LetRegion(initializer, body)` evaluates its initializer once and evaluates
the body with one new immutable binding. The initializer cannot see that
binding.

`Traverse(sequence, start_index, seed, step)` visits the complete sequence in
its statically declared forward or reverse direction. `TraverseRange` visits
the half-open interval formed by independently clamping `begin` and `end`
to `[0, len(sequence)]`. An interval whose end is at most its beginning is
empty. The sequence and index operands are evaluated once in source order;
the seed is evaluated once even for an empty range. Each visited element binds
the element, physical index plus `start_index`, and current accumulator. The
step result becomes the next accumulator.

Bodies are static AST regions, not runtime closures. Lowering uses hidden locals
and ordinary bytecode control flow. CPU and GPU execute the same lowered program
and semantic fuel schedule.

`CheckInt` and `CheckList` evaluate their child once, perform the matching
runtime tag check, and return the unchanged value. A failed check yields
`TypeError`.

## General bounded regions

`BoundedRegion` is the only native recursive/memoized structured form. Its
`RegionPlan` declares one to four exact typed states, a result type, parameters,
optional preparations, up to eight ordered child requests, progress proof,
coordinate domains or sequence-window rules, duplicate policy, and explicit
frame/memo/entry-fuel limits. Phase ASTs bind declared plan-bank slots to
immutable lexical IDs.

Coordinate plans prove progress with a signed lexicographic permutation of
unique `Int` state slots. Every request must decrease rank. Bounds are checked
once and arithmetic overflow yields `ValueError`. Sequence plans prove proper
windows of one ranked source. Interior cuts clamp to `[1,n-1]`; full-source
windows and unproved transitions are rejected.

Execution uses an explicit bounded frame stack. Boundary and base selection
precede memo lookup. On a miss, preparations run once, requests complete in
source order, and combine runs after every child succeeds. Frame and memo
overflow yield `Timeout`. CPU storage may grow only to declared limits; the
GPU profile additionally rejects plans beyond its documented capacities before
upload. No production path invokes a target-specific interpreter.

## Removed release-1 forms

`MapList`, `FilterList`, `LinearRec`, `AsgpDC`, `AsgpDP1D`,
`AsgpDP2D`, and all DP dependency-pattern nodes are not release-2 AST forms.
Equivalent search intent is authored as compiled templates using the general
lexical, traversal, and bounded-region contracts. Package filenames may retain
historical names to describe the intent they reproduce; names do not create
native node kinds, runtime schemes, or compiler branches.

Numeric AST kinds 53 through 70 are permanently unassigned. Release-2 general
forms begin at 71. The normal AST decoder accepts only `ast-prefix-v2` and
rejects these holes.

## Generation, membership, and variation

The compiled definition supplies exact input/return types, constant domains,
productions, templates, visible lexical scope, search limits, and execution
limits. Generated materializations must pass native AST verification,
compiled-grammar membership, lowering, and bytecode verification.

Membership witnesses identify the grammar, production/nonterminal, physical AST
span, template instance, and logical repeated-hole identity. Reconstruction of
an imported AST is deterministic but records `seed_replayable=false`: it
certifies structure, not an original RNG history.

Replacement contracts contain the grammar/semantic identity, exact type and
category, visible lexical environment, native variable environment, template
slot, and destination budgets. Repeated hole occurrences vary atomically.
Typed-subtree crossover uses equal contracts; mutation either regenerates an
admitted subtree or resamples a mutable constant from its exact domain. Every
accepted complete child is verified and reconstructed under the original input
schema. CPU and GPU reproduction consume the same compiled contracts.

## Fuel and errors

Evaluation is deterministic. Each executed instruction or explicit semantic
event charges fuel before its operation. Insufficient fuel yields `Timeout`.
Optional node fuel profiles may assign non-negative event costs through
`INT_MAX`; omitted events cost one. A verifier rejects any possible zero-cost
control-flow cycle.

Runtime errors are `NameError`, `TypeError`, `ZeroDiv`, `ValueError`,
and `Timeout`. The first error terminates the case.

## Version and migration boundary

The public AST version is `ast-prefix-v2`. Release-1 materialized ASTs must be
converted by the offline migration command, which owns the only legacy decoder.
Normal evaluation, evolution, generation, verification, compiler, CPU runtime,
and GPU runtime do not decode release-1 node tables or side metadata.

Old bytecode cannot be converted into an AST because source type, scope, and
grammar provenance were erased; migrate the source AST and recompile it. A
seed-only release-1 artifact also cannot guarantee identical regeneration under
the v2 generator/RNG mapping. Materialize it with the frozen release-1 build,
then migrate the resulting AST.

## Unsupported constructs

The grammar has no user functions, first-class closures or phase bodies,
classes, attributes, exceptions, imports, I/O, `while`, `break`,
`continue`, generic lists, `CharList`, tuples, or product values.
