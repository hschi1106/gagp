# Architecture

GAGP—GPU-Accelerated Genetic Programming for Program Synthesis—is a native
C++/CUDA prefix-AST genetic programming system. This document owns component
boundaries and dependency direction. The end-to-end sequence is in
[dataflow.md](dataflow.md); language and wire behavior is owned by the
[specifications](../../spec/README.md).

## Dependency direction

```text
core value/bytecode contracts
      |             |
      v             v
CPU/GPU runtime   AST/verifier/compiler/operators
      |             |
      +------v------+
       evolution engine
              |
              v
      CLI support and commands
```

Operational Python tools consume files and invoke the native CLI. They do not
implement AST typing, bytecode execution, fitness, or reproduction semantics.

## Product boundaries

The grammar compiler uses a standalone `gagp_json` library for definition loading;
`gagp_cli_support` links the same parser. Strict grammar parsing is opt-in and rejects
duplicate object keys, invalid UTF-8, non-JSON whitespace and unrepresentable numbers.
The default CLI parser retains its existing behavior.

`grammar/definition` resolves local imports, detects conflicts/cycles, applies explicit
replace/extend operations and exports canonical resolved JSON. Its SHA-256 identity
includes schema/catalog/normalization versions and transitive source content, including
content replaced by overrides, without recording absolute paths. `grammar/constants`
owns decoded typed domains independently of payload registry tokens. `grammar/compiled`
checks scopes and exact signatures and builds numeric expression/production tables.
A depth-indexed fixed point computes minimum feasible node costs; this preserves the
joint depth/node constraint instead of combining unrelated minima. Templates retain
immutable fixed-body and typed-hole metadata, with explicit
scope mappings and capture checks. Numeric context indexes prepare compatible
nonterminals and productions by exact result type. Compilation is currently
internal; materialization and membership/replay integration follow in the next
migration stages before CLI cutover. The construction format is defined
in [grammar_definition.md](../../spec/grammar_definition.md).

The internal grammar-definition compiler lives under
`cpp/include/gagp/evolution/grammar/` and `cpp/src/evolution/grammar/`, owned by
`gagp_grammar`. Its primitive catalog resolves exact overloads into deterministic
numeric IDs once during grammar construction. Scalar and sequence signatures are
checked against the native AST verifier. Lexical `let` and ordered `traverse`
currently declare typed body slots and binders but explicitly reject execution
until their general lowering/runtime implementations are available. This catalog
does not expose ASGP or LinearRec aliases. The existing grammar-config path remains
the production path until grammar-driven materialization and all backends are integrated.

### Core and runtime

`cpp/include/gagp/core/` owns shared values, errors, opcodes, bytecode, and
bytecode-verification contracts. `cpp/src/runtime/cpu/` executes and scores
bytecode on the host. `cpp/src/runtime/gpu/` packs programs/cases and executes
fitness on CUDA. `cpp/src/runtime/payload/` owns host payload registration and
snapshot lookup for strings and typed lists.

Runtime semantics are defined in `spec/`; implementation details of container
transport are explained in [payload.md](payload.md).

The CMake targets mirror these boundaries: `gagp_core` owns bytecode
verification, `gagp_runtime_cpu` owns payload and host execution/fitness,
`gagp_evolution` owns AST/compiler/operator/engine code,
`gagp_cli_support` owns the command layer, and `gagp_gpu` owns CUDA runtime
and reproduction kernels. `gagp_cpu` remains an interface-only compatibility
aggregate for existing tests and embedders; new targets should link the
narrowest owner they use.

### AST, compiler, and verification

`cpp/include/gagp/evolution/` exposes prefix ASTs, node descriptors, verified
AST annotations, grammar search configuration, genome operations, evolution,
and timing. Its implementation is split by responsibility under
`cpp/src/evolution/`.

The host node descriptor is the common source for serialized names, categories,
arity, index fields, builtin mapping, grammar switches, typing rule IDs, and
side-table ownership. Structural/type verification produces subtree, type, and
scope annotations used by typed variation. External AST JSON is verified before
genome construction; external bytecode JSON is verified before execution;
compiler output is verified in debug/test builds.

Verification is a trust-boundary and test invariant. Release evolution does not
perform an additional heap-heavy full-AST verification pass for every individual
after every generation.

### Evolution engine

The engine composes focused owners:

| Owner | Responsibility |
| --- | --- |
| `CaseSet` | Canonical names, input types/bindings, expected values, and return-type inference |
| `PopulationInitialization` | Generated population versus fixed replay boundary |
| compiler/cache | Verified AST-to-bytecode lowering and reuse |
| evaluator adapters | CPU/GPU fitness vectors with one timing shape |
| selection | Fitness canonicalization, ranking, and scored-reference materialization |
| reproduction backends | Selection inputs, typed crossover, mutation, and decoded-child acceptance |
| `PayloadLifetimeManager` | Live payload roots and registry pruning |
| lifecycle overlap | GPU reproduction preparation scheduled around evaluation |
| timing model | Nested evaluation, reproduction, generation, and run aggregates |

CPU and GPU evaluation converge on one fitness-vector boundary before ranking.
CPU and GPU reproduction share the public operator contract but do not promise
child-for-child RNG identity. Detailed reproduction scheduling is in
[gpu-reproduction.md](gpu-reproduction.md); timing names are in
[`../reference/timing.md`](../reference/timing.md).

### CLI

`gagp_cli_support` owns the JSON parser, codecs, complete option parser, input
loading, command workflows, and output adaptation. `evolve_cli.cpp` is only the
process-level parse/dispatch/error boundary. No C++ implementation file is
included textually.

Parser flags/defaults are checked against
[`../reference/cli.md`](../reference/cli.md). CLI JSON retains its stable flat
keys even though timing storage inside `EvolutionResult` is nested.

### Operational tools

`tools/gagp_tools/` is an independently installable, standard-library Python
package organized into dataset, experiment, report, and shared-format modules.
Historical top-level scripts are compatibility wrappers. The command pipeline
and artifact policy are owned by [`../../tools/README.md`](../../tools/README.md),
and every auxiliary command/binary is classified in
[`../reference/tooling.md`](../reference/tooling.md).

## Stable performance invariants

- GPU fitness uses one production mixed kernel per accepted population.
- GPU reproduction overlap may hide host preprocessing behind evaluation, but
  selection still consumes the completed fitness vector.
- Evolution ranks with lightweight scored references and materializes owned
  scored genomes only for retained public results.
- Final-population retention is opt-in at the CLI boundary.
- Payload roots are retained across active cases, populations, history, best,
  and optional final results.

These invariants are locked by native contract/property/parity tests and the
fixed-population benchmark gate; they are not alternate semantic definitions.

## Test ownership

- `cpp/tests/runtime/`: runtime, codec, CLI, payload, and bytecode contracts
- `cpp/tests/evolution/`: descriptor, verifier, compiler, property, operator,
  and orchestration contracts
- `cpp/tests/fixtures/runtime/`: intent-labelled semantic corpus
- `cpp/tests/gpu/` and `cpp/tests/parity/`: GPU execution and CPU/GPU agreement
- `cpp/tests/fuzz/`: bounded malformed-input smoke and opt-in libFuzzer targets
- `tools/tests/`: operational tool contracts
- `tests/repository/`: docs, CLI-reference, spec-freeze, and layout contracts

Named build/test configurations and focused commands are in
[`../guides/development.md`](../guides/development.md).

## Change ownership

- AST, typing, control flow, or lowering: update the owning grammar/ISA spec and
  verifier/compiler tests.
- Builtin or payload behavior: update the owning builtin spec, payload design
  when transport changes, and CPU/GPU parity coverage.
- Fitness behavior: update `spec/fitness.md`, fitness contracts, and benchmark
  interpretation.
- CLI flags/defaults/output: update the parser, CLI reference, and command
  contract together.
- Repository moves: update `docs/README.md`, the checked repository layout, and
  the external repository skill references.

The grammar catalog declares general recursive and memoized static-region contracts
in `grammar/structured.hpp`. Parameterized state/result signatures become numeric
compiled contract entries, with phase-specific binder visibility. Execution remains
explicitly unavailable until rank/transition descriptors and generic runtime support
are implemented. Existing control shapes have a separate syntax-category catalog;
structural productions enforce category/type contracts and resolve declared mutable
locals to numeric IDs before materialization. Repeated template
holes retain one logical slot identity, and composed templates may forward holes
through explicitly declared scopes.

Goal 03 materialization is being integrated through `grammar/generate.hpp/.cpp`,
compiled into `gagp_evolution` and backed by immutable `gagp_grammar` tables. The
initial internal path handles native value/control nodes and owned constant domains,
then performs native AST verification before returning a genome. It reports logical
steps, derived nodes and node-aligned origins separately from total AST size.
Expression entries reserve four envelope nodes and three prefix levels from the
search budget; Program entries already own that structure. Execution fuel is copied
separately into derivation metadata. Shared/forwarded template holes now materialize with reserved copy budgets and
logical identity. `ProgramGenome` optionally retains immutable derivation metadata;
clones and table compaction preserve it, while newly changed legacy children have
no grammar provenance. The staged `grammar-generated-v1` artifact stores a resolved
grammar, versioned seed replay and lossless typed constants separately from AST
structure. Replay checks the complete canonical regenerated artifact; materialized
decoding independently checks native validity and returns the input/return contract
and recorded execution limits. The one-AST CLI accepts this artifact and enforces its
fuel and fixture schema. `grammar-population-v1` bundles initial members with exact
replay on encoding and decoding, rejecting stale attached provenance.
Compiled-grammar overloads in genome generation and population initialization keep
that provenance and validate exact case schemas. Every generated result passes the
independent materialized membership matcher before acceptance. Its per-invocation
memoization cannot leak matches between grammars or ASTs. Generation also lowers and
verifies bytecode, recording its instruction count separately from AST size. Generated
runtime cache identities include decoded constants, input ordering, fuel and the runtime
semantic version while excluding search weights and provenance. The staged
`gagp_generate_cli` exercises generation and exact population replay end to end.
The versioned grammar-config adapter emits ordinary typed productions with explicit
domains, initialized local scope and structural limits; it retains the legacy path
for the migration oracle. Finite constant-domain membership indexes are built once
with the compiled grammar rather than reconstructed for each candidate. The production CLI cutover
and grammar-aware reproduction remain later migration work.

`grammar/budget.hpp/.cpp` is shared by compilation and materialization. It constrains
all copies of a logical template hole to the tightest occurrence depth and computes
the corresponding joint minimum cost. Generation adds active enclosing reservations
without parsing source definitions. Template plans sample holes once and copy native
subtrees with remapped physical spans and parent-choice indexes. Execution preflight
visits only entry-reachable rules and instantiated bodies.


Contextual generation uses `grammar/request.hpp`: callers select a compiled nonterminal,
exact type, visible lexical bindings and remaining structural budget. Scope mappings
are validated and recorded in immutable provenance and artifacts. The entry APIs are
wrappers around this request path. Request-aware membership and population generation
share the same contract; executable preflight follows the requested nonterminal.
Actual bound-value lowering remains part of the subsequent general-runtime work.

Grammar membership now also offers deterministic witness reconstruction for imported
or varied native ASTs. The matcher records only successful production decisions; a
second traversal builds node/choice/template/hole metadata and preserves shared-hole
logical identity across physical copies. Reconstruction ignores supplied provenance
and verifies/lower-checks the materialized program. Its metadata is explicitly not
seed-replayable, so the original generation artifact encoder rejects it. Compiled reproduction uses these witnesses; the legacy grammar-config path remains
available until the public migration cutover.

`grammar/variation_contract` derives logical replacement sites and exact compatibility
keys from reconstructed witnesses and opt-in native scope annotations. It groups
physical hole copies, computes destination-relative node/depth/template allowances,
and separates contract equality from donor fit. `grammar/variation_cache` retains an
immutable compiled grammar, owns its compatibility registry and cached analyses, and
bounds retained entries with FIFO eviction. Its identities include materialized values
and contextual limits, so stale metadata cannot certify a cache hit. Compiled operators and shared host preparation consume these contracts. Legacy device
kernels cannot consume them and reject compiled mode.

`grammar/frame` validates isolated donor inputs: original grammar inputs followed by
explicitly available declared native locals. Expression generation recomputes minimum
costs for that availability frame, including alias and shared-hole costs. Framed
membership and reconstruction use the same schema and return provenance that cannot
claim original seed replay. The destination operator must still certify the complete
spliced child with the original inputs before acceptance.

`grammar/donor` packages isolated donor payloads and explicit input schemas, checking
node/depth/template allowances. `grammar/variation` owns run context, certification,
atomic repeated-hole insertion and outcome accounting. Compiled-grammar overloads of
`crossover` and `mutate` use these shared contracts while retaining the public operator
names. Constant mutation resamples the reconstructed production domain. CPU backend selection and mutation order now use these overloads in compiled mode.
Shared host packing transports the contracts, while legacy GPU dispatch rejects that mode.

`EvolutionConfig` retains the compiled definition and optional generation request by
ownership, including configuration copies. Compiled initialization certifies imports
before scoring; reproduction validates every parent and shares one variation context
within each generation. `repro/grammar_prep` samples logical sites and generates donors
for each site's frame and budgets. `repro/pack` verifies preparation identities, preserves
atomic occurrence groups, and prescans capacities to avoid truncating compiled payloads.
Compiled buffers have an explicit mode; legacy device entry points reject them before
allocation or execution. Variation counters remain integer counts through timing and CLI
serialization and classify operator outputs rather than final retained population members.
