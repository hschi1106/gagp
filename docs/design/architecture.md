# Architecture

GAGP is a native C++/CUDA prefix-AST genetic programming system. This document
owns component boundaries and invariants. Normative behavior lives in the
[specifications](../../spec/README.md).

## Dependency direction

```text
compiled grammar definition
          |
          v
grammar compiler and immutable tables
          |
          v
materialized ast-prefix-v2 + membership witness
          |
          v
AST verifier/compiler -> bytecode-json-v2
          |                         |
          v                         v
 CPU/GPU fitness          CPU/GPU compiled reproduction
          +------------+------------+
                       v
                 evolution loop
```

The compiled grammar is the sole production source of initial programs,
membership contracts, donors, crossover sites, and mutation domains. There is
no parallel grammar-config generator or specialized scheme interpreter.

## Component boundaries

`gagp_grammar` owns strict `grammar-definition-v2` loading, local imports,
canonical resolution and SHA-256 identity, exact primitive lookup, templates,
scope checking, productivity/budget analysis, and immutable compiled tables.
The catalog exposes scalar/sequence operations and general lexical, traversal,
and bounded-region constructs. It has no MapList, FilterList, LinearRec, ASGP,
or target-specific DP primitive.

`gagp_evolution` owns materialization, membership reconstruction, verified
ASTs, lowering, selection, compiled variation, and lifecycle orchestration.
Every generated or accepted child is a release-2 native AST and is certified
against the active compiled definition. Reconstructed witnesses certify
membership but set `seed_replayable=false`.

`gagp_core` owns values, opcodes, bytecode verification, general region plans,
and progress proofs. Opcode values 25–27 remain holes; only
`BOUNDED_REGION` at 28 executes a recursive/memoized plan. AST kinds 53–70
remain holes; general structured forms begin at 71.

`gagp_runtime_cpu` and `gagp_gpu` execute the same verified bytecode and
bounded-region descriptors. They do not receive authoring packages and have no
target-specific dispatch. CUDA uses fixed-capacity descriptors and rejects a
plan outside its documented capability before upload.

`gagp_cli_support` owns v2 codecs, option parsing, compiled-definition loading,
artifact replay, and output. `gagp_evolve_cli` requires
`--grammar-definition` for evolution. `gagp_generate_cli` generates or
same-version replays `grammar-population-v2`.

## Public representations

- source/materialized program: prefix `AstProgram`, `ast-prefix-v2`;
- search space: `grammar-definition-v2`;
- generated member: `grammar-generated-v2`;
- generated population: `grammar-population-v2`;
- migrated materialization: `grammar-materialized-v2`;
- runtime request/fixture: `bytecode-json-v2` and
  `bytecode-fixture-v2`;
- cases: unchanged `fitness-cases`.

Package definitions may have compatibility-oriented filenames. Their paths do
not participate in semantics, and a materialized release-2 program executes
without those authoring files installed.

## AST, compiler, and runtime

The node descriptor is the common owner of serialized name, numeric kind,
prefix arity, index fields, typing, and metadata ownership. Structural/type
verification records subtree, exact type, lexical scope, and bounded-region
annotations. External AST and bytecode inputs are verified at their trust
boundaries.

Lexical bodies use declaration IDs and explicit metadata. Traversal lowers to
ordinary bytecode control flow. Recursive/memoized templates materialize a
general `RegionPlan` and phase bindings, then lower to opcode 28. CPU and GPU
share plan verification, event/fuel ordering, request order, result-tag checks,
and error behavior.

The value domain is `Int`, `Float`, `Bool`, `Char`, `String`,
`IntList`, `FloatList`, and `StringList`. Payload transport and fallback
details are owned by [payload.md](payload.md).

## Evolution

Initialization compiles the definition, derives the exact case request, and
generates verified members. Imported v2 populations are same-version replayed
or independently reconstructed against the active grammar before fitness.

CPU and GPU evaluation converge on one fitness vector before ranking. CPU and
GPU reproduction consume the same witness-derived compatibility contracts:
typed-subtree crossover, constant/subtree mutation, atomic repeated holes,
destination budgets, and final membership certification. Backend choice does
not change language or operator semantics.

Stable performance invariants are:

- one mixed GPU fitness kernel per accepted population;
- optional reproduction preparation/evaluation overlap;
- lightweight scored references inside generations;
- opt-in final-population retention;
- payload roots retained across active cases, populations, history, and result.

No release-2 performance result is claimed by the documentation cutover alone.
Benchmark evidence belongs in versioned manifests after measured runs.

## Migration isolation

Release-1 `grammar-config` and materialized AST decoding live only in migration
targets. The `constrained-intent-v1` config conversion emits a compiled definition
but is explicitly lossy for the release-1 search space because v1 omitted exact
domains, fuel, schema, and a fixed typed assignment environment. Exact program
migration emits `grammar-materialized-v2` with target semantic version
`gagp-native-2.0.0`, exact schemas and limits, an empty AST constant table, and one
detached lossless constant/payload pool. A complete `grammar-generated-v1` member
supplies that contract from its embedded fields; a plain `ast-prefix` input requires
cases and explicit limits. Normal codecs reject old AST/bytecode/seed formats with
migration instructions.

Old bytecode is not migrated because it cannot recover the source contract.
Seed-only populations must first be materialized with the frozen release-1
build. `grammar-population-v1` must be split and each complete member migrated
independently. A changed generator/RNG mapping is never reported as exact replay.
`cpp/src/transition/` and `cpp/src/migration/` are oracle/conversion support,
not production execution paths.

## Ownership and validation

- `cpp/tests/runtime/`: codecs, runtime, CLI, migration boundary;
- `cpp/tests/evolution/`: grammar compiler, membership, variation, engine;
- `cpp/tests/gpu/` and `cpp/tests/parity/`: device behavior and parity;
- `tools/tests/`: operational tools;
- `tests/repository/`: docs, layouts, production-core audit, spec freeze.

AST/typing/lowering changes update grammar/ISA specs. Wire changes update the
format spec. CLI changes update [the CLI reference](../reference/cli.md).
Repository moves update [the layout reference](../reference/repository-layout.md).
