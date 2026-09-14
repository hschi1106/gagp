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

`core/recurrence_rank.hpp` owns the shared coordinate progress proof and bounded
cardinality checks, implemented in `runtime/recurrence_rank.cpp` within `gagp_core`.
The proof uses signed lexicographic axes and ordered constant offset vectors, with
explicit endpoint and duplicate policies. It rejects unproved edges and possible
coordinate overflow without allocating an execution table. Grammar, bytecode, and
device packing can share this validation boundary; the utility alone does not
enable the staged recursive runtime.

The CPU staging substrate in `runtime/cpu/bounded_region.hpp` uses an explicit
frame vector with fixed arrays for state, prepared values and ordered child
results. Its phase adapter runs boundary/base checks before memo lookup, prepares
each missed frame once, constructs one request at a time, and combines after all
children succeed. Request construction precedes the frame-capacity check; successful
combine and result validation precede memo-capacity checks. It does not call the
legacy DC/DP evaluators. The adapter remains an internal boundary for subsequently
verified materialized regions, not a user-supplied executable extension.

`runtime/cpu/recurrence_memo.hpp` supplies bounded, reusable open-addressing storage
for fixed coordinate keys and exact `Value` results. It allocates lazily and checks
power-of-two growth before allocation. Frame and memo limits count live logical
entries; scratch may retain larger physical allocations from a previous invocation.
Both structures expose resident storage measurements for the migration benchmarks.
`node_prefix_arity(const AstNode&)` is the common host AST traversal entrypoint for
the upcoming statically declared structured argument layouts; existing node arities
remain unchanged.

`core/sequence_rank.hpp` owns proper-window construction proofs and safe interior
cut resolution. `core/region_plan.hpp`, validated by `runtime/region_plan.cpp`,
combines coordinate or sequence progress with exact state/result types, typed
preparations, request constructors, literal/operand domain bounds and phase slot
banks. These core types do not depend on evolution types or package names. Native
AST binders and bytecode local mappings remain separate representation concerns;
the shared plan is their static contract, not yet a wire format or executable gate.

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

### Semantic fuel lowering foundation

Bytecode blocks optionally carry a parallel `instruction_fuel` vector. The core
validator checks shape, bounded costs, and acyclicity of the zero-cost control-flow
subgraph using an iterative traversal. The CPU interpreter dispatches once per
block between unit-cost and scheduled execution, preserving the legacy instruction
layout and loop. This permits semantic charges to survive changes in compiler
bookkeeping. GPU fitness and direct host packing reject scheduled blocks until the
general GPU path implements their contract. Nested phases retain their existing
call-entry charges in addition to any instruction schedule.

### General static lexical bodies

The staged CPU AST represents lexical bodies through one `lexical_regions` table
with ordered typed declarations. Traversal direction is a general per-node
specification. Region reference IDs are disjoint from ordinary name indices;
typing and lowering resolve them to lexical environments and hidden local slots.
The current verifier internally reserves negative environment keys for these IDs,
while public AST IDs remain nonnegative. Let and traversal bodies may capture
outer lexical bindings. Whole-sequence and ranged traversal lower to ordinary
bytecode loops rather than per-element AST expansion. Table compaction preserves
region IDs and metadata. GPU packing rejects these staged forms until its general
region implementation is available.


Contextual grammar donors carry explicit native binder IDs alongside their ordered
formal scope. Membership reconstructs per-choice environments as an optional runtime
sidecar, independent of serialized derivation provenance. Variation stores these
physical mappings per occurrence and uses formal contracts for compatibility.
Copying freshens introduced IDs before remapping captures, then applies ordinary
subtree compaction and full membership certification. Isolated donor verification
uses a private input projection of captured REGION_VAR nodes; public materialized
AST execution continues to require closed lexical scopes.

Generic CHECK_INT and CHECK_LIST expressions lower directly to existing validation
opcodes, preserving explicit validation order without introducing runtime package
identity. Their compiled programs use the experimental semantic-fuel envelope and
are rejected by GPU execution/reproduction alongside general regions until Goal07.


Source fuel profiles attach supported semantic event costs to native expression
nodes. The compiler indexes profiles once, isolates child expression schedules and
emits one charge per event plus zero-cost administrative instructions. Unprofiled
nodes retain their previous instruction charges. Validation rejects malformed event
contracts and possible zero-cost cycles in both Debug and Release lowering. Profiles
are preserved in standalone AST codec/cache/splicing; current compiled grammars
reject unsolicited profiles because their schema does not declare this metadata.

Zero-cost traversal setup can reuse immutable lexical captures and a sequence length
computed by an enclosing let. These facts expire with their lexical scope. Redundant
Int checks are removed only when an earlier operation proves the runtime value is an
Int; an input's declared type alone is insufficient. Literal-zero endpoints need no
zero-cost clamp because sequence lengths are nonnegative. Charged events remain
observable even when their values are known. Compiler tests cover dynamic type errors,
exact fuel boundaries, sibling branches and changing state in nested traversals.
The compiler also tracks bounded offsets from an observed sequence length. A branch
that compares that exact length with zero can prove a nonempty range endpoint such
as `length - 1`. These source-specific facts permit zero-cost clamp removal and
immutable endpoint reuse; they do not suppress charged events or operand evaluation.

CPU builtin calls borrow their contiguous argument range from the VM stack for the
duration of the call. The stack consumes those operands after the builtin returns.
This avoids a temporary argument-vector allocation while preserving arity, underflow,
unknown-builtin and value-error ordering. The vector-based C++ entrypoint remains a
wrapper around the borrowed-range entrypoint.
The CPU operand stack keeps its first eight non-owning values inline and grows into
retained heap storage when necessary. This is an allocation optimization, not an
execution limit. Stack-growth and builtin tests cover preserved operands and fuel
boundaries after spilling.
Local storage similarly keeps up to sixteen slots inline and allocates larger
storage when required; initialization and bounds checks remain the same.
CPU `INDEX` copies only the selected element while holding the payload registry
lock, avoiding a full-container temporary. The builtin still validates the sequence,
index type and logical bounds before looking up payload data, and retains the existing
fallback-token behavior when no element is resident.


The transition-only `gagp_transition` library provides LinearRec-to-general-AST
lowering for differential validation. Production executables do not link it.
It checks start before sequence, retains lazy empty/last/step selection, binds the
last element once, and traverses the preceding range in reverse. Semantic profiles
encode the old charging order without a runtime package-name condition. Copying
respects legacy binder shadowing and isolated ASGP phase scopes; introduced native
IDs are globally fresh. The transform copies each source subtree once and remaps
retained metadata in a separate linear pass; it does not unroll sequence elements.


The CPU bounded-region bytecode adapter owns invocation-bound resolution, lazy
caller-local capture snapshots and explicit phase-bank bindings. It calls the
shared iterative frame engine and ordinary instruction evaluator with no root
program handle in phases, preventing hidden recursive dispatch. The bytecode
verifier independently proves phase success types and slot visibility before an
unchecked native descriptor can execute. Initial captured local tags are checked
lazily by LOAD; stores replace that initial constraint. This CPU integration is
staged before device execution.


Native bounded regions share the core RegionPlan and add only node ownership,
explicit capture references and phase lexical binder mappings. Dynamic arity is
cached in AstNode.i0 and validated against the plan before prefix traversal.
The compiler lowers isolated phase expressions into RegionPhase bindings, resolving
captured caller locals without emitting a read. Exact native typing and bytecode
phase verification independently check the boundary. AST identity and generated
runtime cache identity include all plan fields; compaction and subtree insertion
preserve metadata and rename named captures and introduced lexical IDs separately.

`CpuExecutionSession` owns a private immutable bytecode snapshot and thread-confined
execution state. It validates a bounded segment on first invocation, after the
containing instruction's fuel charge, and retains that result only for the owned
snapshot. Unreachable invalid segments remain unobserved. The session reuses frame,
memo, operand, parameter and phase-binding storage while resetting invocation values
and memo contents each time. CPU fitness evaluation creates one session per bounded
program and reuses it across cases; one-shot execution of mutable BytecodeProgram
values still validates each invocation. Sessions do not own payload-registry entries
and follow the same payload lifetime contract as ordinary bytecode execution.
The retained-region memory metric reports frame and memo storage separately from
the bytecode snapshot, validation results and binding buffers. Cold construction
and first execution are measured separately from warm execution.
Structural grammar productions carry the complete plan, positional captures and
explicit phase binding lists. Pure phase layout helpers live in core so grammar
compilation does not depend on native evolution. Phase scopes are closed; a graph
check follows concrete nonterminal and template expansions to reject implicit
inputs, locals and nested recursive regions. Abstract template holes are checked
when instantiated. Generation, membership witnesses and typed variation preserve
phase ownership and alpha-rename captures and declarations across repeated holes.
Instruction budgets include root code and every phase program. Standalone examples
in `configs/grammar_definitions/bounded_sequence.json` and `bounded_memo.json`
exercise three-way sequence decomposition and a custom two-coordinate dependency
pattern without runtime package dispatch.

The transition-only `lower_bounded_regions` adapter composes the LinearRec rewrite
and replaces legacy DC/DP AST nodes with these descriptors. DC uses an explicit
sequence/offset state, a clamped preparation and ordered proper windows; DP uses
inclusive coordinate domains and ordered signed offsets. Synthetic expressions
carry zero-cost fuel profiles while copied phase expressions retain their charges.
DP type checks carry the old opcode charge, and DP2 binds both initial expressions
before checking either type. The CPU oracle profile uses `INT_MAX` frames and memo
cells, with zero cells for nonmemoized DC; GPU capacity observations remain separate.
The adapter verifies its source and output. Its equivalence contract uses the
declared native input types: raw VM callers can violate those types, and legacy DC
accepted alternate sequence tags that an exact typed region rejects. Such calls
are tested and recorded separately from matching-schema differential comparisons.

The separate `lower_bounded_bytecode` oracle adapter accepts exact type hints for
legacy tables, retains phase bytecode and fuel schedules, and remaps root jumps
after inserting typed state operands and guards. It preserves out-of-domain base
predicates because boundary evaluation precedes base handling; a verification copy
normalizes only that legacy metadata defect before checking remaining structure.
The lowered program always passes complete descriptor verification. The frozen
checker records strict phase-output rejections separately from translated result
and fuel comparisons; rejection is not evidence of execution parity.


`gagp_region_plan_json` owns the exact RegionPlan wire codec and depends only on
core contracts and the JSON value library. Both grammar parsing and CLI codecs can
use it without an evolution-to-CLI dependency cycle. Bytecode phase constants and
segment serialization remain in CLI support, which already depends on evolution.
