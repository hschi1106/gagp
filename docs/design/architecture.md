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
The legacy native backend materializes each generated or accepted child as a
release-2 AST and certifies it against the active compiled definition.
Experimental immutable fragment profiles may retain internal executables and
materialize ASTs only at export/admission boundaries; their support and operator
contracts are explicit. Reconstructed witnesses certify membership but set
`seed_replayable=false`.

`gagp_core` owns values, opcodes, bytecode verification, general region plans,
and progress proofs. Opcode values 25–27 remain holes; only
`BOUNDED_REGION` at 28 executes a recursive/memoized plan. AST kinds 53–70
remain holes; general structured forms begin at 71.

`gagp_runtime_cpu` and `gagp_gpu` execute the same verified bytecode and
bounded-region descriptors. They do not receive authoring packages and have no
target-specific dispatch. CUDA uses fixed-capacity descriptors and rejects a
plan outside its documented capability before upload.

GPU fitness sessions retain bounded-region frame and memo workspace between
evaluations with the same allocation layout. Changing the layout releases the
previous allocation before reserving the replacement, preserving the 512 MiB
workspace budget. Each invocation resets its logical frame/memo state; retained
memory never carries memoized answers between programs or evaluations.
Within each block, frame depths and memo slots are interleaved across threads;
each thread retains exclusive ownership of its strided slice. The single-thread
result probe uses the same interpreter with stride one. The fitness kernel
declares a 1024-thread launch bound so its supported maximum block size does not
depend on an external compiler register cap.
Packing reserves the known root instruction and constant table capacities before
appending entries. Capacity checks remain in place. External bytecode is verified on every import;
controlled immutable executables may reuse their compositional safety certificate
as defined in `spec/bytecode_format.md`.
The reference phase executor uses a smaller preset array for at most four
bindings; larger phases retain the full descriptor capacity. Both paths use the same binding,
lazy capture checking, bytecode execution, result checking, and fuel rules.
Each phase invocation owns its operand stack and locals, with fresh stack depth
and local validity/type masks. The phase interpreter cannot reenter bounded
execution. Exact GPU error and remaining-fuel tests cover phase isolation.
An experimental shared operand stack was rejected after broad performance
measurements confirmed regressions; its evidence remains in the migration report.

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

The compiler reuses hidden local slots after their lexical expression or loop
finishes. Enclosing captures and loop state remain live through nested bodies;
ordinary named locals keep distinct slots for the entire program. This bounds
storage by simultaneous temporary lifetimes rather than all declarations in the
AST, without changing instructions, evaluation order, or semantic fuel charges.


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

Isolated donor validation projects external lexical references onto synthetic
inputs, including captures stored in bounded-region parameter metadata. Internal
region and phase bindings retain their lexical identity and cannot collide with
the external frame IDs. The projected copy is used only for verification and
lowering; the donor keeps its contextual captures for the eventual splice.

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

Constant domains include finite values, inclusive integer ranges, finite Float
intervals, and bounded
typed sequences. Sequence domains describe complete alphabets/element domains
and length intervals; they are not inferred from observed population constants.
CPU generation and variation share the domain sampler, and membership checks
each scalar and length. GPU preparation resamples sequence proposals for each
population preparation; GPU mutation selects and installs those values. Live
proposal snapshots participate in payload root retention across overlap.
Float interval membership includes both endpoints; its deterministic sampler
uses a half-open interval (or the exact endpoint for a singleton). CPU and GPU
round the interpolation operations separately, including full-width intervals
whose subtraction overflows. Scalar GPU interval mutation uses the device RNG
directly rather than a host-generated finite value table.
Numeric range domains can separate membership from construction through a checked
`sample_from` subset. Generation and resampling use that subset, while witness
reconstruction uses the outer domain. The grammar compiler checks subset bounds
without depending on evolution or payload materialization; GPU preparation caches
the scalar sampler descriptor under the owning grammar identity.
Constant domains separately select resampling (default), retaining the selected
value (`keep`), or complementing a Bool (`flip`). Retained constants remain
eligible mutation candidates and yield an unchanged accepted child; no subtree
fallback or unused sequence proposals are introduced. Both backends preserve
logical group identity and exclude fixed template constants.
Numeric range domains also support additive deltas independently of construction
support. Both backends check overflow and membership before publishing a sum;
inadmissible sums retain the current value without fallback. Optional Float GPU
grid sampling keeps its declared closed discrete law separate from continuous
CPU sampling. Delta descriptors and policies belong to the immutable grammar.

Authored per-expression `resource_charge` descriptors can
be projected from a reconstructed canonical membership witness through
`project_derivation_resources`. This separates construction accounting from fuel
without trusting attached provenance. Charge projection does not yet replace physical
generation feasibility or variation enforcement.
Generation now retains the resource index for its selected derivation; membership
rebuilds an index for its canonical witness, including contextual donor frames.
Variation analysis reuses that certified index, and generated donors expose their
payload cost without the standalone expression envelope. Ambiguous grammars can
give different charges to different derivations of the same AST; selected-generation
costs are therefore not interchangeable with canonical membership costs. Attached
resource indexes are not trusted when certifying a parent or accepting a child.
An optional native `EvolutionConfig::offspring_resource_budget` now applies node
and depth limits to the reconstructed candidate in the common CPU/GPU child
acceptance path. A rejected candidate returns its certified parent and increments
budget/fallback counters. Parent membership is separate so a deep initial tree
can be repaired; the option does not grant initial-population budget admission.
GPU prepared calls and run resources retain this policy and reject reuse under a
changed budget. CPU mutation and GPU donor preparation now use bounded rejection
sampling against the complete spliced candidate's canonical witness. They do not
infer feasibility from separate physical/projected minima or assume charges are
stable under ambiguous ancestor matching. All original production alternatives
remain available; exhaustion after 64 attempts reports an unresolved generation
failure rather than declaring the grammar infeasible. This changes the sampling
distribution through conditioning and does not claim original RNG trajectories.
`initial_resource_budget` separately controls native population construction and
replay admission; it can retain a source profile's different initial depth rule.
Budgeted generation records the accepted attempt seed for ordinary artifact replay.
Certified source profiles remain further integration work; these generic budget
controls alone are not source-search equivalence.
When charges cannot be certified local, CPU crossover checks both complete canonical children against the optional budget
before reservoir selection. Trial-child checks reconstruct the canonical derivation,
including native verification and bytecode lowering, without enumerating the child's
future variation sites or registering their compatibility keys. The selected children
still pass full variation acceptance. Invalid membership and resource failures retain separate
rejection counters. This exact path handles ambiguous ancestor charges but can cost
more than local subtree arithmetic; its overhead remains to be measured on the
final representative candidate.
For grammars whose materialized native-kind/fuel-profile pairs each have a unique
resource charge, analysis certifies that splicing preserves node costs. Cached
candidate descriptors then carry exact projected donor costs and replacement
allowances, including separate carried/reset depth limits. Both CPU and GPU
crossover selection apply these numeric bounds before selecting a pair. Implicit
expression envelopes stay outside expression payloads; complete Program sites
retain their explicit grammar charges. A grammar with conflicting charges is not
rejected: it receives no local pruning certificate, and full canonical child
admission remains required. CPU selection uses its exact whole-child check there;
GPU selection defers projected admission to copyback. No recursive grammar matcher
or package-name rule is introduced into a kernel.
The separate `joint_resource_frontier` query retains physical/projected cost
tradeoffs from actual construction derivations, with stage filtering and linked
choices for repeated template holes. It bounds computation and reports capacity
exhaustion explicitly. Native/contextual admission and projected-budget-aware
membership/generation/variation enforcement remain separate integration work.

The physical variation path uses the shared resource-allowance formula with a
specialized unit-charge scan, preserving physical search limits without allocating a weighted
index. The general prefix-tree projection uses the same formula and supports
explicit zero-cost administrative nodes and depth resets
for migration proofs; those charges do not establish grammar membership or
authorize larger production limits. Source-cost profiles still require certified
grammar derivations and generation/variation integration before activation.

Stable performance invariants are:

- complete evaluation of every accepted program/case pair; capability-proven
  execution buckets may use multiple kernels with results restored to population order;
- optional reproduction preparation/evaluation overlap;
- lightweight scored references inside generations;
- opt-in final-population retention;
- payload roots retained across active cases, populations, history, and result.

The variation layer also accepts a finite set of closed, exact root requests from
one compiled grammar. Roots have distinct result types and common node/depth
budgets; no generic union type is introduced. Membership reconstructs the root
contract from verified AST types, never attached provenance. Shared nonterminal
sites remain exchangeable across differently typed roots, while child acceptance
preserves its original parent's root contract. The bounded analysis cache includes
the entire request set. Shared GPU candidate/donor preparation uses this same
analysis. Native evolution accepts additional exact root requests, initializes them
in round-robin order, and preserves one global tournament population through CPU,
GPU, and overlapped reproduction. Imported members are certified against the
complete request set. Heterogeneous expected values must each match an admitted
root type; scoring remains unchanged. GPU prepared state and run resources include
the full ordered request set in their compatibility checks. The production CLI and
private migration benchmark expose this path through
`--population-roots RootA,RootB`. Existing population artifacts retain each member's
exact request, preserving mixed roots without a new artifact schema. Replay against
a selected root set still requires full evolutionary membership validation.

No release-2 performance result is claimed by the documentation cutover alone.
Benchmark evidence belongs in versioned manifests after measured runs.

The CPU `gpu_candidates` experiment uses compiled GPU preparation's bounded site
sets and hash-ranked compatible pairs. CPU tournament selection and post-crossover
mutation retain their usual RNG schedule. Splicing uses the generic variation
implementation, including repeated-hole atomicity and capture remapping; every
child passes compiled grammar membership before acceptance. The ablation does not
launch GPU reproduction and is rejected with the GPU reproduction backend.
The `gpu_coupled_donor` experiment retains ordinary CPU crossover and records its
selected original-parent sites. It prepares contextual donors for every admitted
site, including sites beyond the GPU candidate cap. Subtree mutation replaces the
recorded site in the original parent; the constant branch mutates the crossed
child. Both ablations prepare certified, compacted parents so unused name-table
entries cannot create scope contracts different from crossover. Donor pools carry
exact capture mappings and replacement budgets; prepared work remains charged to
reproduction preprocessing.

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
`migration/legacy_constants` lowers a source member with checked constant origins.
Its selection list follows source prefix order, keeps distinct occurrences even
when they share a value-table entry, and excludes compiler-added constants.
This supports correspondence checks for mutation without treating provenance as
a compiled grammar witness or changing production resource limits.

## Ownership and validation

- `cpp/tests/runtime/`: codecs, runtime, CLI, migration boundary;
- `cpp/tests/evolution/`: grammar compiler, membership, variation, engine;
- `cpp/tests/gpu/` and `cpp/tests/parity/`: device behavior and parity;
- `tools/tests/`: operational tools;
- `tests/repository/`: docs, layouts, production-core audit, spec freeze.

AST/typing/lowering changes update grammar/ISA specs. Wire changes update the
format spec. CLI changes update [the CLI reference](../reference/cli.md).
Repository moves update [the layout reference](../reference/repository-layout.md).

The offline migration library provides `normalize_typed_storage` for mapping a
verified program's ordinary names to exact source types. A reaching-definition
analysis joins branches and computes loop fixed points, preserves zero-iteration
paths, stops terminated paths, and respects provably constant Boolean short-circuit
conditions. It checks named bounded-region captures as reads. A split is rejected
when an affected read may reach a different source type or an undefined value.
The transform changes name references only, preserving prefix nodes, constants,
lexical binders, and fuel profiles; increased name/local capacity still requires
separate target checks. This helper is not enabled implicitly by migration,
production evolution, or timing entrypoints.


Grammar production `generation_stages` separates construction policy from union
membership. Compilation retains union costs and, for restricted grammars, builds
initial/mutation feasibility tables. Generation follows the request stage at all
recursive choices; shared CPU/GPU donor requests select mutation. Reconstruction
and crossover use the union contract, while frame caches and replay provenance
retain stage identity. Default unrestricted grammars reuse their existing tables.


Executable-root checks on an immutable compiled grammar retain successful
reachability certificates per root. Copies share a synchronized certificate cache;
checking one executable root does not admit a different unsupported root. This
avoids repeatedly walking the grammar during request and membership validation.

Native AST verification constructs node-index diagnostic paths only when a check
fails. Successful structural and type passes transfer their completed annotations
to the caller without copying them; the checks and diagnostic contents are unchanged.

Runtime cache identities serialize the original AST structure directly with an
empty-pool marker, then append every decoded constant. This preserves the prior
identity bytes without copying nodes, names, and nested metadata just to exclude
transport tokens from structural serialization. The structure-only helper is
not a complete program identity on its own.

Typed verification maintains local and lexical-binder environments in numeric
index order, so scope signatures do not allocate and sort temporary copies per
expression. Exact scope annotations use the same order and retain the existing
assignment, branch-intersection and loop-visibility rules.

Constant identity encoding has a direct scalar path with the same canonical
singleton-domain bytes as the JSON artifact encoder. Float formatting and string
escaping still use the canonical JSON routines; list values still use decoded
payload encoding. Both membership comparison and runtime cache identities use
this shared encoder, including signed-zero and invalid-value handling.

Contextual (non-seed-replayable) generation adds ordinary names only when a node
or bounded-region capture references them. It remaps compiled input/local slots
to native name indices consistently, including shared template expansions. Initial
seed-replayable generation retains its existing complete name table. Both paths
retain native type, membership, lowering and contextual-frame checks.

Variation certification always validates before table compaction. When compaction
removes no name or constant entries, its order-preserving algorithm proves that
the AST and indices are unchanged, so certification reuses that analysis. Changed
tables still trigger fresh analysis. Native metadata is supplied by compaction
itself rather than rebuilt a second time.

Mixed population analysis chooses its exact root contract from the return type
produced by native verification inside membership reconstruction. That verified
AST, including requested exact scopes, feeds the selected root matcher and witness
builder directly. Every configured root is still validated, root types remain
unique, and the selected grammar rules and lowering checks must pass. No attached
genome metadata is trusted to select a root or bypass native verification.

Membership matching stores active recursion and successful first-production
decisions in a single per-match table. An active sentinel rejects zero-node alias
cycles; failed entries are erased because rejection can depend on alias ancestry.
Successful entries retain the production used by canonical witness reconstruction.
The matching step and depth checks are unchanged.

Variation analysis initializes each site’s available native bindings from its
first occurrence and intersects bindings only for subsequent linked occurrences.
This avoids repeating the first lookup without changing multi-occurrence scope
constraints or compatibility keys.

Exact native scope capture reuses its most recently captured scope only after
comparing every local and binder ID/type pair. This avoids temporary scope-key
allocations for adjacent expressions; hashes alone never authorize scope reuse.

Parallel donor preparation shares an immutable destination analysis across its
seed workers. Payload-producing workers stage string/list registrations privately;
after joining, the pool validates read/write conflicts and commits the batch, or
replays sequentially without publishing any conflicting staged writes. See
[payload staging](payload.md#internal-donor-staging) and
[GPU reproduction](gpu-reproduction.md) for ownership and fallback details.

Grammar membership compares semantic-fuel profiles without temporary maps after
native verification has established unique supported events. Profile order is
irrelevant, while event presence and cost must match exactly. Impossible leaf
node kinds are rejected before profile comparison; grammar frame/step accounting
and production order remain unchanged.

AST text and structural/cache keys write decimal fields directly into strings,
using `to_chars` instead of per-field stream formatting. Existing delimiters,
field order, signed values and constant encodings are preserved; the floating
constant bit representation remains fixed-width hexadecimal.

Native fuel-profile verification builds diagnostic paths only when reporting an
error. A node-index flag array detects duplicate profile owners after bounds
checking; each event is compared with the earlier events in its small profile.
The supported event set bounds successful scans, and unsupported or duplicate
events still fail in their original order. Error codes, paths and messages remain
unchanged; unprofiled ASTs do not allocate the owner flag array.

Native AST cache-key constants use direct decimal encoding for integer/token
fields and fixed 16-digit lowercase hexadecimal encoding of Float bits. This
preserves the existing stream-produced key bytes, including signed zero and NaN
payload bits, without allocating a stream for every constant. Runtime identity
continues to decode payload values separately; registry tokens alone do not
certify runtime equivalence.
