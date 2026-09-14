# Typed grammar definitions

This document describes the internal `grammar-definition-v1` construction format.
The production CLI still accepts the existing [grammar-config](grammar_config.md)
format. CLI integration, materialized derivation validation, and structured execution
descriptors are pending during the staged migration. A compiled grammar
is a construction artifact, not a runtime execution mode.

## Types and entry

A definition is a strict UTF-8 JSON object. Duplicate keys (including equivalent
Unicode escapes), unknown schema keys, invalid Unicode, non-finite or unrepresentable
numbers, and incompatible versions are errors. JSON nesting is limited to 256 levels;
each source file is at most 16 MiB, and an import graph has at most 256 files.

The exact value types are `Int`, `Float`, `Bool`, `Char`, `String`, `IntList`,
`FloatList`, and `StringList`. There is no implicit numeric conversion, `Any`, generic
list, tuple, or closure. Entry is `{"nonterminal":"Expr","type":"Int"}`. Its type
must agree with the referenced nonterminal, and its lexical scope must be empty.
Optional `inputs` is an ordered array of `{"name":"n","type":"Int"}` declarations;
input names and lexical names occupy separate namespaces. Optional `locals`
declares ordered `{name,type}` mutable program variables; local names cannot conflict
with input names. `{"local":"i"}` reads a declared local. These declarations establish
construction types, not initialization; native verification and runtime checks still
govern a materialized program.

Required root fields are `format_version`, `entry`, `search_limits`, and
`execution_limits`. Optional construction fields are `imports`, `inputs`, `locals`,
`nonterminals`, and `templates`. Imported modules can omit root settings.
Only the root supplies effective input/local/search/execution/entry settings.

## Imports and identity

`imports` is an array of local relative file paths resolved against the declaring
file. Absolute paths, network URLs, embedded NULs, cycles and conflicting definitions
are rejected. Shared imports are evaluated once. Files have no executable hooks.

Nonterminals and templates have stable `id` strings matching
`[A-Za-z_][A-Za-z0-9_.-]*`. Definitions default to `operation:"define"`; a duplicate
ID in the same resource category is an error. `operation:"replace"` replaces an
imported resource completely. `operation:"extend"` is available only for
nonterminals and permits exactly `id`, `operation`, and `alternatives`; it appends
alternatives without changing type/scope. Alternative IDs must remain unique within
the nonterminal. An override must explicitly import every preceding writer through
its dependency graph; sibling overrides cannot depend on traversal order.

Resolved export includes `resolved:true`, `catalog_version`, `normalization_version`,
merged definitions and `source_content`, and contains no `imports`. Source content
retains canonical transitive module content (including overridden definitions) with
import paths removed. Resources, alternatives and template holes are sorted by ID;
JSON object keys are sorted, while other arrays retain their semantic order. Source
content is deduplicated and sorted by canonical bytes. Relocating or renaming the
same imported files does not change identity. Modifying transitive content does.

Canonical numbers use finite binary64 values with enough digits for round-trip;
strings use UTF-8, escaping quotation marks, backslashes and control characters.
The grammar identity is lowercase SHA-256 of the complete canonical resolved export,
including `grammar-definition-v1`, `gagp-primitives-v1`, and normalization version `1`.
Export/parse/export is byte-idempotent. The compiler recomputes identity from the
resolved model rather than trusting separately supplied cached strings or hashes.

## Productions, expressions and scopes

A nonterminal has `id`, `type`, `scope` and a nonempty `alternatives` array.
Optional `category` defaults to Expression and also supports Program, Block and
Statement. For structural categories, `type` is the enclosing program result contract,
not a runtime value produced by a statement or block. Entry may be Expression or
Program, and its optional category must match the referenced nonterminal. Each
alternative has `id`, a positive finite `weight`, and `expression`. Its stable
production ID is `nonterminal-id/alternative-id`. Numeric IDs follow sorted stable
IDs and are meaningful only with the grammar identity.

A scope is an ordered array of `{name,type}` bindings. Binding names match
`[A-Za-z_][A-Za-z0-9_]*`; duplicates and lexical shadowing are rejected. A reference
may require a narrower scope than its caller, but every required name must be
visible with its exact declared type. The compiler stores numeric scope mappings.

An expression is exactly one of:

- `{"constant":{"type":"Int","values":["0","1"]}}`.
- `{"input":"n"}` or `{"bound":"x"}`.
- `{"ref":"Expr"}`.
- `{"signature":"add(Int,Int)->Int","args":[{"ref":"Expr"},{"ref":"Expr"}]}`.
- A template invocation or hole as defined below.

Signatures resolve exact catalog keys once. Enabling `index(String,Int)->Char` does
not enable `index(IntList,Int)->Int`. Arguments must match the selected signature;
leaf primitives use the constant/input/bound expression syntax, so a catalog leaf
cannot bypass its domain or scope contract. No ASGP or LinearRec primitive is
available to new authors.

Region-bearing signatures require `bind`, an object keyed by the zero-based region
argument index. Each value is an array of names in catalog binder order. For example,
`let(Int,Int)->Int` has argument 1 as its body and requires `"bind":{"1":["x"]}`.
The initializer cannot see `x`; the body can. `traverse` similarly declares element,
index and accumulator bindings only in its step body. These two contracts currently
compile but fail explicitly when execution is requested; general runtime support
arrives in subsequent migration stages.

## Constant domains

A domain has exactly `type` and one of `values` or `range`. `values` is a nonempty
array of exact typed values. Int values use canonical decimal strings within signed
64-bit bounds, preserving values beyond JSON binary64 integer precision. Float values
are finite JSON numbers, Bool values are JSON booleans, Char values are strings
containing exactly one Unicode scalar, and String values are strings. Each typed-list
value is an array of its element encoding; empty lists and strings are valid. Nested
or heterogeneous lists are rejected.

`range` is available only for Int and contains two inclusive decimal-string endpoints
in ascending order. The full signed 64-bit range is valid. Constant domains own their
decoded values and never identify strings/lists solely by runtime payload tokens.

## Templates and fixed regions

A template has `id`, `type`, `scope`, `holes` and `body`. Each hole has `id`, `type`
and `scope`. Templates and holes accept the same optional category contract as
nonterminals, and category must match at every invocation/substitution. The body refers to a slot with `{"hole":"body"}`. Each declared hole
must occur at least once. Repeated occurrences identify the same logical hole;
materialization copies its selected derivation and charges every copy against the
AST budget. The body's non-hole structure is fixed; a nonterminal
reference must occupy a declared hole, and a fixed constant has exactly one value.

Invocation is `{"template":"Local","holes":{"body":{"ref":"Narrow"}}}`.
Every hole must be filled, with its exact result type. The argument compiles under
the hole's declared scope, not the caller's entire environment. Each hole scope
must be available where the hole occurs in the body, including any region binders
introduced there. Template scope similarly restricts the fixed body's external
lexical bindings. Template expansion cycles, missing/unknown holes, type mismatch,
and capture of undeclared bindings are errors.

Compiled expressions retain template IDs, fixed flags, typed hole contracts and
scope mappings. Template/hole wrappers are construction metadata and consume no
materialized AST nodes. They are not runtime closures or package dispatch.

## Limits and productivity

Search limits serialize separately as `{"max_nodes":20,"max_depth":8}`. Current
compiler capacities are 65,536 nodes and depth 256; both limits must be positive.
Execution limits currently serialize as `{"fuel":100}`, a positive integer no larger
than 2,147,483,647. Search settings never substitute for execution fuel. Additional
structured execution bounds will be specified with their primitive descriptors.

Compilation is bounded to 4,096 nonterminals, 4,096 templates, 65,536 productions,
and 65,536 compiled expressions. It rejects every nonterminal lacking a finite
derivation within the requested joint depth/node budget. Recursive grammar rules
are legal when productive. A fixed point records minimum nodes separately for each
available depth; independently minimal depth and node counts are insufficient to
establish joint feasibility. A primitive/leaf contributes one node and one depth
level; a nonterminal reference and construction wrapper contribute neither.

Compiled consumers receive const numeric tables indexed by nonterminal, exact
result type and lexical context, decoded domains, scope mappings and cached minimum costs. JSON parsing,
import resolution and catalog string matching do not belong in per-candidate or
GPU paths.

## Examples and current integration status

[custom_integer.json](../configs/grammar_definitions/custom_integer.json) selects
Int constants/input/addition and a lexical region. Its `Narrow` nonterminal can use
only the declared `x` binding, while `Expr` has no lexical bindings. It compiles to
five productions; requesting execution currently rejects the declared `let`
primitive until its runtime stage is implemented.

[invalid_scope.json](../cpp/tests/fixtures/grammar/invalid_scope.json) moves the
reference to `Narrow` into the let initializer. Compilation rejects it because `x`
is not visible there. Both examples use construction limits distinct from fuel.

The compiler additionally bounds distinct lexical contexts to 4,096 and total
context-index entries (nonterminal, production and binding-map entries) to
4,194,304. Context indexes include only nonterminals whose required bindings are
available with exact types; their binding maps are prepared during compilation.

## General structured contracts

The declaration catalog also resolves two parameterized families. It instantiates
exact state/result types once and stores a grammar-local numeric contract ID.
Both families reject execution until their verified rank descriptors and general
runtime implementations are available; declaring an arbitrary request expression
does not establish a progress proof.

`{"family":"recur","state_types":["String","Int"],"result_type":"StringList","requests":3}`
declares a bounded recursive region with 1–4 exact typed state slots and 1–8
statically ordered request sites. Its argument order is initial state values,
Bool base predicate, exact-result base body, request-major next-state regions,
then exact-result combine body. Every region sees the current state slots. Only
the combine body additionally sees one exact-result binding per request. Initial
state expressions do not see these binders. Before execution, a separate bounded,
checkable decreasing-rank descriptor must constrain recursive transitions.

`{"family":"memo","dimensions":2,"result_type":"FloatList","requests":3}`
declares one memoized recurrence family with 1–4 integer coordinate slots, 1–8
ordered dependency requests and any exact result type. Arguments are initial
coordinates, exclusive coordinate extents, boundary body, Bool base predicate,
base body, then combine body. Region binders are coordinates, followed only in
the combine body by ordered dependency results. Dependency offsets, monotone rank,
frame/cell capacities, boundary/base/memo ordering and charging parameters must
be separately specified and verified for materialized execution. There are no
separate 1D/2D evaluators in this contract.

An expression selects a contract with `structured` instead of `signature`, for
example `{"structured":{"family":"recur","state_types":["Int"],"result_type":"Int","requests":1},"args":[],"bind":{}}`.
That abbreviated example is intentionally invalid: it omits all five required
arguments and their explicit region binder names. `args` and `bind` follow the
same exact typing and scope validation as ordinary region-bearing signatures.
Unknown families, excess dimensions/requests, wrong arity, and a request region
that refers to a combine-result binder are rejected.

## Structural control catalog

The catalog records the existing AST categories independently of value types:
Program, Block, Statement and Expression. They are syntax categories, not new
runtime values. Exact control declarations cover `program(Block)->Program`,
`block_nil()->Block`, `block_cons(Statement,Block)->Block`,
`if_stmt(Bool,Block,Block)->Statement`, `for_range(Int,Block)->Statement`, and
independent `assign(T)->Statement` and `return(T)->Statement` signatures for each
of the eight value types. Assignment and ForRange require a target/index name.
Their categories and prefix arities are checked against native node descriptors.
Structural productions use `{"control":"return(Int)->Statement","type":"Int","args":[{"constant":{"type":"Int","values":["7"]}}]}` and analogous exact control
keys. Named controls require `name` referring to a declared local of the exact
assignment/index type. Other controls reject a target name. Each child must match
its required syntax category; structural children carry the same enclosing result
type. Return's exact value type must agree with that contract. Categories constrain
nesting, so a block cannot be substituted into a value-expression argument.

Numeric category indexes remain separate from exact value-expression type indexes.
A reference, template or hole preserves both category and type. No Block or Statement
is treated as an `Any` value. Local declaration names and control targets are resolved
once into numeric local IDs; materialization maps them into the native AST name table.

## Membership, derivation and cache contracts

An AST's native validity and membership in a selected grammar are separate claims.
A materialized program executes independently of grammar files. Admission into a
constrained evolutionary population requires a validated derivation from its entry
nonterminal; a matching result type alone is insufficient. Each production choice
must belong to the required nonterminal, each leaf must belong to its exact domain
or visible binding, and every template skeleton and hole contract must match.
The joint structural budget applies after template materialization.

Derivation metadata must identify the canonical grammar hash, schema/catalog/
normalization versions, stable and numeric production/nonterminal IDs, expression
or slot ID, logical derivation instance, template instance, scope mapping and
materialized prefix-AST span. A repeated hole shares its logical choice within a
template instance; every materialized occurrence retains that relationship and its
own span. Metadata is evidence to validate against the actual AST, not permission
to trust stale annotations. External ASTs without provenance may execute, but
population admission requires successful derivation reconstruction or a precise
membership error. Goal 03 implements materialization/serialization and Goal 04
implements variation/reconstruction acceptance.

Generation/derivation cache identity includes canonical grammar content and all
schema/catalog/normalization versions, generator/RNG version, requested nonterminal,
exact context/binding mapping, search and execution limits, fixture input-schema
identity and the configured payload-seeding policy. Changing production weights or
transitive content invalidates construction caches. Node-analysis/variation caches
also include materialized AST content, scope/provenance identity and destination
budgets. Numeric IDs without their owning grammar identity are never cache keys.

Runtime bytecode cache identity instead includes materialized AST semantics,
decoded constant payload contents, serialized execution descriptors/limits and
compiler/ISA semantic versions. It excludes arbitrary grammar paths, package names,
and weights that do not change the materialized program. Grammar or cache metadata
never selects a runtime evaluator by package name. These contracts must remain true
when later stages add concrete artifact fields and acceptance checks.

Source shape validation runs before override resolution discards definitions; an overridden unknown key or malformed constant is still an error.

## Staged materialization policy

The internal generator identifies its sampling algorithm as `typed-derivation-v1`
and its RNG as `splitmix64-rejection-v1`. Bounded integer sampling uses rejection
before modulo reduction; the complete signed 64-bit domain is supported without
signed overflow. Weights are normalized by the maximum eligible weight before
summing in binary64. Excluded productions are never restored as a fallback.

A Program entry already contains its native envelope. An Expression entry reserves
four nodes (PROGRAM, BLOCK_CONS, RETURN, BLOCK_NIL) and three prefix depth levels
from the total search budget before deriving its expression. If this envelope plus
a finite expression cannot fit, generation fails with a budget diagnostic. Metadata
reports logical sampling steps, derived nodes and total materialized AST size
separately, and retains search limits separately from execution fuel.

Children receive a random allowance between their minimum feasible node cost and
the remaining budget after reserving their siblings' minimum costs. Unused allowance
returns to later siblings. Alias-only cycles are redundant derivations of the same
AST: selection excludes paths that revisit a nonterminal without materializing a
node, including through transparent template/hole wrappers. Alias chains are iterative;
recursive productions that materialize nodes retain normal depth/node accounting.
The current additional logical-step capacity is 1,048,576 and reports exhaustion.

Each template instance plans a common budget for every logical hole. Its depth is
the tightest of all occurrences, and its reservation accounts for every copy.
Feasibility analysis applies this shared constraint rather than independently choosing
different derivations for different occurrences. The hole is sampled once; later
occurrences copy its native subtree, constant references and logical origin IDs.
Forwarded holes preserve their enclosing slot identity. Unused reserved nodes are
released after the first materialization determines the actual subtree size.

Fixed skeleton origins identify their template instance. Hole occurrence records
identify shared slots and physical AST spans; copied production-choice records remap
parent indexes and spans. Native verification is mandatory before returning a generated
program. Execution preflight follows the entry's reachable productions and instantiated
bodies; an unused imported template does not request execution of its primitives.
Population integration is still being implemented before the Goal 03 production
interface is finalized.


### Generated artifacts (staged)

`grammar-generated-v1` records the resolved grammar and hash, generator/RNG versions,
runtime semantic version `gagp-native-1.0.0`, canonical unsigned 64-bit decimal seed, ordered input schema and its SHA-256 hash,
exact return type, search limits, execution fuel, and `domain-only-v1` payload policy.
That policy samples only declared constant domains; expected outputs do not inject
additional constants. The artifact also stores a native `ast_shape` with an empty
constant pool and a separate `constants` array. Each constant uses the singleton
constant-domain encoding, retaining full signed 64-bit values, signed floating zero,
Unicode scalar values and complete typed payload contents without registry IDs.

Derivation metadata contains logical-step, derived-node and lowered-instruction counts
plus numeric rows:

| Array | Columns |
| --- | --- |
| nodes | expression, production, nonterminal, logical instance, template instance, slot, fixed (0/1) |
| choices | nonterminal, production, parent choice, AST begin, AST end |
| templates | template ID, parent instance |
| holes | template instance, slot, AST begin, AST end |

AST spans are half-open. Missing numeric identities use `4294967295`.
Same-version replay compiles the embedded resolved definition, checks its declared and
optionally required grammar identity, regenerates from the seed, and compares the
entire canonical artifact. Constants, provenance, schemas, limits or version identities
that disagree with the regenerated request fail with a diagnostic. A changed request
that is itself valid and regenerates the recorded program can pass; replay establishes
internal reproducibility, not authenticity of the original request. Arbitrary varied
programs require a separate membership validator.

Materialized decoding is independent of the recorded generator and resolved grammar.
It restores payloads from content and verifies the native AST against the recorded
input schema and return type. It establishes native validity, not grammar membership.
The contract-preserving decoder returns the recorded search limits, execution fuel,
input schema and return type alongside the AST; it checks the recorded structural
limits as well as global capacities. Callers executing the decoded program must use
the recorded execution fuel. Both
paths accept at most 256 MiB of artifact JSON and 512 JSON nesting levels. The AST
evaluation CLI applies these bounds before dispatching by artifact format. Materialized ASTs are limited to 65,536
nodes and prefix depth 256; structural numeric fields must be signed 32-bit integers
before conversion by the native AST codec.


### Materialized membership and population generation (staged)

`require_membership` validates native structure, exact input/return typing and full
materialized node/depth budgets, then matches the entry's compiled productions.
Expression entries require their exact single-return envelope. Referenced constants must belong
to their declared typed domains, including integer range endpoints and signed floating
zero; opaque payload IDs are insufficient. Input/local leaves retain their declared
names and exact types. Fixed template skeletons must match, and repeated or forwarded
holes must have equal materialized subtrees. Equality resolves names and constant
contents, so table compaction and duplicate constant-table entries do not change it.
Membership does not depend on generation seed or attached provenance. It does not
certify the accuracy of supplied provenance; artifact replay checks that separately.

Matching excludes active zero-node alias cycles, caches positive nonterminal matches
only within one grammar/AST invocation, and reports capacity exhaustion at 1,048,576
matching steps or 4,096 grammar frames. Negative matches are not cached because they
can depend on the current alias ancestry. Generation runs this membership gate before
accepting a result. Its cost belongs inside initialization timing.

The compiled-grammar overload of population initialization requires a positive size,
exact case input names/types (order-independent), and an expected return type matching
the entry. Individual seeds are `seed + index` modulo 2^64. Each member retains its
immutable derivation metadata. The domain-only policy ignores expected values when
sampling constants. The existing grammar-config population and reproduction paths
remain available as the migration oracle; selecting compiled initialization does not
imply that legacy reproduction enforces the new grammar.


`grammar-population-v1` bundles an initial population with exactly four root fields:
`format_version`, `grammar_hash`, `count`, and `members`. Members are complete
`grammar-generated-v1` objects in population order. The population must contain
1..65536 members and fit in 256 MiB. Each member must have the same grammar identity.
Encoding requires attached immutable provenance and checks exact same-version replay
before accepting each member; changed ASTs with stale provenance fail. Decoding checks
root fields/count, required grammar identity when supplied, and each member's complete
replay. This format reconstructs generated initial populations; varied populations
require a future artifact version with variation provenance.

The existing one-AST CLI evaluation path can consume a generated artifact as a
materialized program. It verifies the case schema, applies the recorded fuel, and
rejects a conflicting explicit fuel override. This path does not certify generation
provenance and does not route the population into legacy reproduction.


Before accepting a generated program, generation lowers it through the native compiler
and its bytecode verifier, records the ordinary instruction count, and enforces the
implementation capacity of 1,048,576 lowered instructions. This count is separate from
logical sampling steps and materialized AST nodes. Typed definitions cannot lower to
legacy specialized segments. Compilation and membership verification are included in
initialization timing; later compilation caches do not move this work outside the gate.

Generated-program runtime compilation identities include the runtime semantic version,
materialized AST structure, exact constant contents, ordered input names and actual
execution fuel. Grammar file paths, production weights, seed and derivation metadata
are excluded. The identity supports ordinary native grammar programs; specialized
legacy metadata is rejected. Legacy migration genomes keep their existing cache path.
Materialized artifact execution requires the recorded runtime semantic version even
when the generator is unavailable; an incompatible runtime version is rejected.


Finite-domain membership encodings are compiled once alongside owned constant domains.
Lookup uses exact singleton-domain canonical JSON, including typed payload contents and
signed floating zero. This index belongs to one immutable compiled grammar and does not
change domain order or production sampling weights. Integer ranges retain direct
inclusive endpoint checks.


### Contextual generation requests

`GenerationRequest` names a compiled nonterminal ID, its exact result type, an ordered
visible lexical environment, and a remaining full-native-program node/depth budget.
The budget must be positive and no larger than the compiled grammar limits; expression
requests include the four-node/three-level envelope. The requested nonterminal must
be an Expression or Program, and its minimum feasible derivation must fit. Generation
reports an actionable diagnostic before sampling when these conditions do not hold.
The default API constructs the compiled entry request with empty lexical scope and the
full grammar limits, retaining its sampling sequence.

Visible bindings require unique valid names and exact public value types, with capacity
4096. Every required nonterminal binding must appear by name and exact type; extra
bindings are permitted. The returned scope mapping indexes required bindings into the
caller's ordered environment, preserving lexical identity without type-only matching.
Global input/local declarations remain those of the compiled grammar. Native verification
still checks their actual availability in the materialized program. Actual bound-value
execution remains unavailable until the general lexical runtime; a reachable Bound node
fails preflight explicitly. Scoped rules that do not consume a bound value can still
be generated and their scope contracts are retained.

Generation, genome-generation and population-initialization overloads consume the same
request. Membership has a corresponding request-aware overload; its default overload
checks the grammar entry and full limits. Executability preflight follows the requested
nonterminal, so an unrelated unsupported default entry does not block another executable
request. All recursive production choices remain within that request's structural budget.

Generated artifacts record `request` with `nonterminal`, `type`, `visible_environment`
and `scope_mapping`; the request budget is the artifact's `search_limits`. Exact replay
reconstructs the request and validates the complete rematerialized artifact, including
scope mapping and requested return type. Materialized execution still checks native
validity independently of generation provenance. Immutable genome provenance retains
the complete request for later constrained variation.
