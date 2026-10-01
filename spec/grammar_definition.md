# Typed grammar definitions

This document defines the production `grammar-definition-v2` construction
format. It is the sole release-2 authoring and search-space input for generation,
membership, CPU/GPU variation, and evolution. Release-1 `grammar-config` inputs
must be converted offline as specified in [grammar_config.md](grammar_config.md).

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
including `grammar-definition-v2`, `gagp-primitives-v3`, and normalization version `1`.
Export/parse/export is byte-idempotent. The compiler recomputes identity from the
resolved model rather than trusting separately supplied cached strings or hashes.

## Productions, expressions and scopes

A nonterminal has `id`, `type`, `scope` and a nonempty `alternatives` array.
It may declare Boolean `variation` (default `true`). When false, derivation
choices for that nonterminal are excluded from subtree crossover/mutation site
selection on both CPU and GPU. Descendant nonterminals retain their own policy;
constant perturbation is governed separately by constant domains. Generation,
membership and execution do not change. This policy is part of grammar identity.
Extensions cannot change it; use an explicit definition/override.
Optional `mutation_entry` names the nonterminal used to construct a subtree donor
when this nonterminal is selected for mutation. The referenced rule must have the
same exact type, category and ordered lexical scope. Ordinary construction, even
inside a larger mutation donor, still uses this rule's own productions. The
redirected request uses the mutation generation stage and the destination's
physical and projected budgets. Redirection is applied once, not recursively.
Destination membership and crossover remain unchanged; generated donors outside
the destination language are rejected by normal offspring admission. This permits
fixed construction values with broader standalone replacement distributions.
The field participates in grammar identity and cannot be changed by an extension.
Optional `category` defaults to Expression and also supports Program, Block and
Statement. For structural categories, `type` is the enclosing program result contract,
not a runtime value produced by a statement or block. Entry may be Expression or
Program, and its optional category must match the referenced nonterminal. Each
alternative has `id`, a positive finite `weight`, and `expression`. Its stable
production ID is `nonterminal-id/alternative-id`. Numeric IDs follow sorted stable
IDs and are meaningful only with the grammar identity.

An alternative may opt into a nonempty string `crossover_group`. For a site
reconstructed through that alternative, group identity replaces the nonterminal
ID and enclosing template/slot IDs in the crossover compatibility key. Exact
result type, category, formal scope and available ordinary locals must still
match. Ungrouped alternatives retain the original exact contract and never match
grouped alternatives. Different groups never match. This permits explicitly
interchangeable rules or holes while distinguishing alternatives with different
structural roles. Grouping does not alter construction, mutation donor selection,
atomic repeated-hole replacement, budgets or destination membership admission;
an exchanged child outside its destination language is still rejected. Authors
should group mutually admissible languages to avoid rejected offspring. The
authored group is part of grammar identity and shared CPU/GPU site analysis.

Grouped alternatives may additionally declare `crossover_scope: "closed"`
(default `"exact"`). Canonical site analysis checks every physical occurrence
for lexical closure, including REGION_VAR references and lexical bounded-region
captures. A reference is internal only if its declaration lies inside the
replaced subtree. Proven closed sites in the same group may exchange across
different formal lexical contexts; ordinary local availability and exact result
type/category still match. Sites with an external lexical capture retain exact
scope matching and cannot match a proven closed site. The actual AST determines
closure; the authored policy alone never certifies it. Mutation retains the full
destination frame and its donor rules. CPU splice and GPU metadata reconstruction
check the incoming payload before omitting external binder remapping. Atomic
replacement, budget checks and destination membership remain in force. Without
a crossover group this scope policy is rejected.

An alternative may also declare `generation_stages`, an array containing distinct
`"initial"` and/or `"mutation"` strings. Omission enables both stages; an empty
array retains the production for membership but excludes it from generation.
Unknown, duplicate or non-string stages are errors, including in overridden
source definitions. This field filters every recursive production choice, not
only the entry rule. Each alternative retains its own expression and constant
domains, so stages may use different terminal ranges or skeletons without changing
the nonterminal's type/scope identity. Positive weights are normalized over the
feasible alternatives enabled for the selected stage.

Membership, derivation reconstruction and crossover compatibility use the union
of all alternatives, including membership-only productions. Generation stages do
not impose an additional runtime validity restriction. Constant perturbation is
not production generation and retains its separately defined domain policy.
Compilation checks union productivity and computes stage-specific minimum costs;
a nonterminal may be unproductive in one or both generation stages. A generation
request fails if its selected stage cannot fit the budget, even when the union
would fit. Disabled alternatives never provide an alias exit or a template-hole
minimum. Unrestricted grammars share their existing cost tables and retain their
sampling order and RNG consumption.

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

A concrete expression may declare an optional nonempty `fuel_events` object. Its
keys are semantic event names from the native fuel contract and its values are JSON
integers in `0..2147483647`. Constants, signatures, controls, executable bounded
structured expressions, inputs, bound values, and local values own materialized
nodes and may declare a profile. Nonterminal references, template invocations, and
holes are construction wrappers and cannot declare one; the concrete expression
selected through the wrapper owns any profile. An event must be supported by the
resolved native node kind. Unknown events, unsupported events, booleans, fractional
costs, negative costs, overflow, and empty profiles are errors.

Profile presence is semantic. With no `fuel_events`, the compiler retains the
ordinary instruction fuel behavior. With a profile, an explicitly declared zero is
preserved, and supported events omitted from the object use the native profiled-event
default cost of one. The compiler stores authored entries in canonical event order,
generation attaches them to the physical node, and membership requires the same
profile presence and event-to-cost map at that node. Repeated template holes must
copy an identical profile to every physical occurrence. Event object ordering alone
does not affect membership or grammar identity after canonicalization.

Region-bearing signatures require `bind`, an object keyed by the zero-based region
argument index. Each value is an array of names in catalog binder order. For example,
`let(Int,Int)->Int` has argument 1 as its body and requires `"bind":{"1":["x"]}`.
The initializer cannot see `x`; the body can. `traverse` similarly declares element,
index and accumulator bindings only in its step body. Closed regions generate,
verify and execute on CPU. `traverse_reverse` reverses iteration order;
`traverse_range` and `traverse_range_reverse` take sequence, start index, begin,
end, seed and step arguments, with bindings on argument 5. Whole-sequence variants
bind argument 3. Ranges clamp both endpoints to the sequence length and use a
half-open interval. String elements have type Char; all eight accumulator types
are supported. Experimental native regions are explicitly rejected on GPU.

Scope mappings project ordered formal bindings onto native binder IDs. Repeated
shared holes preserve one logical derivation while alpha-renaming introduced
binders and mapping captured bindings to each occurrence's environment. Membership
checks binder roles and traversal direction, accepting consistent alpha-renaming.
The v2 catalog adds traversal signatures; numeric signature IDs and canonical
identity are versioned accordingly.

## Constant domains

A domain has `type` and exactly one of `values`, `range`, or `sequence`.
Only Float ranges additionally accept `quantization_scale`. `values` is a nonempty
array of exact typed values. Int values use canonical decimal strings within signed
64-bit bounds, preserving values beyond JSON binary64 integer precision. Float values
are finite JSON numbers, Bool values are JSON booleans, Char values are strings
containing exactly one Unicode scalar, and String values are strings. Each typed-list
value is an array of its element encoding; empty lists and strings are valid. Nested
or heterogeneous lists are rejected.

`range` accepts Int or Float and contains two endpoints in ascending order.
Int endpoints are decimal strings; membership and sampling are inclusive, and
the full signed 64-bit range is valid. Float endpoints are finite JSON numbers;
membership is inclusive and rejects nonfinite values. A numeric Float interval
containing zero admits both signed zeros; finite `values` distinguish their signs.
Constant domains own their
decoded values and never identify strings/lists solely by runtime payload tokens.

Float sampling consumes one RNG word, uses its upper 53 bits divided by `2^53`,
and interpolates into `[minimum, maximum)`. Equal endpoints return the first
endpoint exactly, including its zero sign. Arithmetic rounds each operation
separately on CPU and GPU. When endpoint subtraction overflows, interpolation
uses separately rounded `minimum * (1-fraction) + maximum * fraction`; otherwise
it uses `minimum + fraction * (maximum-minimum)`. A result rounded to the upper
endpoint is replaced by the adjacent Float toward the minimum; a result below
the minimum is clamped to the minimum. This defines finite, reproducible samples
even for the full finite binary64 interval and adjacent/subnormal endpoints.
GPU scalar mutation samples this interval on-device without a finite host
proposal table. This range constructor does not imply additive perturbation.

An optional positive finite `quantization_scale` applies
`round(sample * scale) / scale` after interval sampling, with ties away from zero
and each arithmetic operation separately rounded. For example,
`{"type":"Float","range":[-8,8],"quantization_scale":1000}` constructs
three-decimal values, including signed zeros and both endpoints. It samples a
continuous interval before rounding, not uniformly among grid points; endpoint
cells retain their half-width. Both endpoints must round-trip through
quantization unchanged and have rounded grid-index magnitude at most
`2^50`. That bound retains enough precision to recover the integer grid index
from a generated binary64 value. Membership additionally requires quantizing the
value to leave it unchanged. Omission disables quantization; explicit zero,
negative, nonfinite, off-grid or oversized configurations are rejected. The
option is valid inside a FloatList element domain and is included in grammar
identity and replay. It does not add a perturbation policy.

Int and Float range domains may additionally declare `sample_from`, a same-type
numeric domain used for generation and constant resampling. The outer range and
its optional quantization still define membership. For example, a Float range
`[-100,100]` can sample from `{"type":"Float","range":[-8,8],"quantization_scale":1000}`
while accepting other finite values in the outer interval. Sampling does not
clamp or re-quantize existing member values.

The sampler is a non-nested domain with either finite `values` or a numeric
`range`, optionally quantized for Float. Compilation proves it stays inside the
outer membership domain: finite values are checked individually and range bounds
must be contained. For quantized outer support, a non-singleton sampler range
must use the same scale; a singleton must be a member. Different-scale range
inclusions are rejected unless expressed as checked finite values. Recursive
`sample_from`, type mismatches, non-range outer domains and escaping samplers
are rejected. The option also works on numeric element domains within typed
sequences. GPU preparation retains outer grammar identity while flattening the
scalar sampler into its cached range/value descriptor. Default domains retain
their original draws and membership behavior.

A constant domain may declare `mutation: "resample"` (the default), `"keep"`, or
`"flip"`. This controls only the selected logical constant mutation, not initial
generation, subtree donors, membership, or candidate selection. `keep` is an
accepted unchanged child: it remains eligible for selection and does not trigger
subtree regeneration or count as fallback/rejection. `flip` requires a Bool domain
containing both `false` and `true`, and complements the selected value without a
replacement draw. Repeated occurrences of one logical constant change atomically;
fixed template constants remain ineligible. CPU and GPU apply the same policies.
GPU `keep` needs no value/proposal table and preserves existing payload roots.
Unknown policies, non-Bool or one-sided `flip`, and `mutation` declarations inside
`sample_from` or sequence element domains are rejected. Policy is part of grammar
identity and replay; concrete constant artifacts cannot carry a mutation policy.

Numeric range domains also accept a mutation object, for example:

```json
{"type":"Float","range":[-100,100],
 "sample_from":{"type":"Float","range":[-8,8],"quantization_scale":1000},
 "mutation":{"kind":"add","range":[-1,1],"gpu_grid_steps":65535}}
```

The mutation object's `range` describes an additive delta with the same type as
the outer domain. Int endpoints use canonical decimal strings and are sampled
uniformly, inclusively; Float endpoints are finite JSON numbers and use the same
continuous half-open sampler as Float construction (including exact singleton
ranges). The result is added to the current value without quantization or
resampling from `sample_from`. This requires numeric range membership; finite
membership sets and quantized outer Float ranges are rejected. Construction may
still use a quantized `sample_from` domain.

Optional `gpu_grid_steps` is Float-only and must be an integer in
`[1,4294967295]`. CPU deltas remain continuous; GPU deltas instead select an
equally weighted integer index `i` in `[0,steps]` and interpolate the closed
delta range using `i/steps`. Both endpoints are included, multiplication and
addition round separately, and overflowing interval width uses the weighted
endpoint form. Thus `[-1,1]` with 65535 steps has 65536 equally weighted indices,
including both endpoints and no zero. This is not rounding a continuous draw;
it preserves a distinct declared GPU sampling law. Omission uses continuous
sampling on both backends. Backend policy and grid size participate in grammar
identity and replay.

Signed Int overflow, nonfinite Float sums, and sums outside the outer membership
range retain the previous value as an accepted unchanged result. They do not
clamp to an endpoint, retry the draw, fall back to subtree mutation, or count as
acceptance rejection. The original value must already satisfy membership. Each
selected logical constant uses one delta, shared by all of its lowered copies;
fixed constants and independent logical origins are unaffected.

`sequence` contains exactly `length` and `element`. `length` is a pair of inclusive
integer endpoints in `[0,65536]`, in ascending order. `element` is a constant
domain: String requires Char, IntList requires Int, FloatList requires Float,
and StringList requires String. Other outer types are rejected. This permits a
StringList of bounded strings but never nested lists. String lengths count
Unicode scalars, including NUL, rather than UTF-8 bytes. Sampling chooses the
length uniformly and samples each element independently using its domain.
Membership checks both bounds and every element under its own domain semantics.

Sequence compilation rejects an expansion bound above 16 MiB. A finite scalar
element costs eight units; a finite String costs eight plus its decoded byte
length, maximized over its domain. A sequence costs eight plus maximum length
times its element bound. This bounds construction; it neither changes runtime
payload semantics nor reports actual allocator memory. Fixed template constants
still require singleton `values`. Saved AST constants remain concrete singleton
encodings, never domain generators.

CPU constant mutation samples the domain directly. GPU preparation samples fresh
sequence proposals, one per population member per sequence domain, from the full
declared domain and preparation seed. Device mutation selects and installs a
proposal. Shared proposals can correlate children; backend RNG trajectories are
not required to match. They are preparation-local, not a run-level restricted
grammar. Live immutable proposal snapshots retain payload roots across overlap.
Preparation rejects more than one million total values or a sequence expansion
bound above 256 MiB before sampling; it never truncates a domain or switches GPU
reproduction to CPU. Finite values and integer-range tables retain their existing
cached behavior and RNG consumption.

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
The `entry_fuel` field of a bounded `RegionPlan` charges region-frame entry and is
independent of a bounded expression's optional node-level `fuel_events` profile.

Concrete materialized expressions may additionally declare
`"resource_charge":{"nodes":1,"depth":1,"resets_depth":false}`. All three
fields are required; nodes must be an integer in `[0,65536]`, depth an integer
in `[0,256]`, and resets_depth a Boolean. Omission means one node, one depth
level and no reset. This annotation is permitted on constants, bindings, locals,
control nodes, executable primitives and bounded structured nodes, including
fixed template bodies. References, template invocations, holes and abstract
nonmaterialized primitives cannot own a charge.

`project_derivation_resources` reconstructs canonical grammar membership and
projects these charges over the verified materialized tree. Node charges sum;
depth charges accumulate along a path and a reset starts depth accounting anew
at that node. Earlier ancestor peaks still count. Each physical occurrence of a
repeated hole contributes its own charge. Implicit expression envelopes have
the default unit charge; use an explicit Program production to author different
structural charges. Attached provenance is not trusted for this calculation.
Ambiguous derivations use the canonical reconstructed membership witness, not
an asserted seed history. Charges participate in grammar identity and do not
change program ASTs or execution fuel.

This projection is currently an accounting interface. The serialized
`search_limits`, generation feasibility and production variation limits still
count physical nodes/depth; resource annotations alone never authorize a larger
search space. Enforcing separate projected budgets remains pending integration.

`joint_resource_frontier` computes nondominated structural construction costs
within both a request's physical limits and supplied projected node/depth limits.
Each row retains physical node count and projected node/carried-depth/reset-depth
costs from one derivation. Production stages apply. A repeated template hole
selects one shared cost alternative at its tightest physical depth, rather than
mixing alternatives independently across copies. Reference cycles use a bounded
fixed point over physical depth. Unproductive joint budgets return an empty set;
state-capacity or construction-work exhaustion throws rather than silently
discarding feasible alternatives. The default retained-state cap is 1,000,000;
construction work is capped at 64,000,000 steps. This is derivation-based structural
feasibility, not native validity or contextual-frame admission; ambiguous
membership choices must still be resolved when enforcing a projected budget.

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
five productions and supports CPU generation, membership and execution.

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

The coordinate progress DSL uses a full permutation of the coordinate axes,
each with direction `+1` or `-1`, as a lexicographic rank. Direction `+1` means
that increasing that coordinate increases rank; `-1` reverses it. Each declared
request is a constant signed integer offset vector. At its first nonzero
coordinate in rank order, the offset must decrease rank. Zero vectors and
nondecreasing requests are invalid. Later coordinates may move in either
direction: for example, rank `[(0,+1),(1,+1)]` admits `[-1,+2]` and `[0,-1]`.
All coordinates have finite bounds, so strictly decreasing in-domain requests
cannot form a cycle. A boundary request terminates before expanding more edges.
An arbitrary expression or a runtime-only decrease assertion is not a proof.

Coordinate domains explicitly distinguish inclusive and exclusive upper bounds.
Exclusive equal endpoints represent an empty domain; inclusive equal endpoints
represent one coordinate. Reversed bounds are invalid. Duplicate offset vectors
are rejected unless the descriptor explicitly allows repeated ordered requests;
allowing duplicates does not change the decreasing-rank requirement. Rank axes
must cover every dimension exactly once, directions must be exactly `+1` or
`-1`, and every request must have exactly one offset per dimension. The existing
capacities of four coordinates and eight requests also apply to this DSL.

For a nonempty Cartesian domain, every in-domain coordinate plus every declared
offset must be representable as an `Int`, including requests that leave the
domain. Descriptor validation rejects potential overflow before execution.
An empty Cartesian domain has no outgoing requests. Storage cardinality is
checked separately against a caller-supplied bound using overflow-safe arithmetic;
an empty domain requires zero cells. A full inclusive `Int` domain has more cells
than a 64-bit allocation count can represent and cannot be allocated as a dense
table. These checks do not themselves enable structured execution; the materialized
execution and serialization requirements above still apply.

Static progress validation is independent of the domain bounds: a serialized
rank and its offsets can be checked before runtime extent expressions are
evaluated. Once those expressions produce bounds, invocation validation checks
domain ordering and coordinate-addition safety. Sparse memo capacity counts
successful stored results, not every coordinate in the Cartesian domain; a large
domain alone must not cause premature memo exhaustion.

The bounded-region drivers use explicit fixed-layout frames with state, prepared
values, ordered child results and a continuation index. Each entered frame pays
the declared entry charge before state/boundary/base evaluation. Boundary and base
handling precede memo lookup, so their results are not cached. A nonterminal memo
miss prepares its per-frame values once, then evaluates requests depth-first in
declared order. Request construction and its errors precede the frame-capacity
check; a rejected child pays no entry charge. Combine evaluation and result checks
precede memo-capacity failure. Only successful nonterminal combines are stored.
All phases share the remaining invocation fuel and propagate their first error.
Logical frame/cell bounds are independent of retained reusable scratch allocation.
The compiled execution contract exposes this driver on CPU and GPU with the same
declared frame, memo and semantic-fuel limits.

Sequence progress uses a ranked sequence state and ordered half-open windows whose
endpoints are the beginning, end, or one of at most four prepared interior cuts.
An interior cut is an exact `Int` clamped to `[1, length-1]`. Length zero or one
selects the base body before preparing cuts. A request from beginning to end is
invalid because it retains the whole source. Every other endpoint pair is proper
for length at least two, including empty or reversed windows. Resolution preserves
endpoint order for the sequence slice operation. Duplicate windows need explicit
allowance. This permits user-defined multiway decompositions and overlapping proper
windows; it does not encode a divide-and-conquer package identity.

The compiled `RegionPlan` shape uses exact public state/result/parameter types,
ordered preparations and requests, explicit execution limits, and one progress
proof. Coordinate slots project `Int` state slots; every projected next state must
use its own checked constant-offset constructor. A ranked sequence must use a
proper-window constructor from that same source. An arbitrary request expression
is permitted only for an unranked state slot and must have that slot's exact type.
Memoization requires coordinates covering every state slot, so its key omits no
changing state. Nonmemoized plans have a canonical zero cell limit.
Materialized plans require a frame-entry charge in `[1, INT_MAX]`; thus every
visited state is metered even when its phase instructions have zero charges.

Invocation operands consist of initial states followed by at most eight additional
`Int` bound operands. Each coordinate bound is explicitly a literal or an operand
reference. New memo extent arguments can therefore define exclusive zero-based
domains, while a translated template can state inclusive literal bounds without
overflow-prone endpoint conversion. All-literal domains are checked statically;
actual dynamic bounds still require invocation validation. Plan version 1 admits
up to four state slots, eight requests, four preparations and 32 lexical parameters.

Phase inputs name typed slot banks explicitly. State and captured parameters may
be selected by any phase. Prepared values are available only to later preparations,
request expressions and combine; child results are available only to combine.
Sequence measure slot zero is the current ranked source length. A phase's explicit
binding list selects from these banks, allowing isolated template phases without
implicit access to every available slot. Captured locals must preserve set/unset
state; an unused capture must not raise an eager `Name` error. Native materialization,
phase bytecode verification and codecs preserve these contracts before execution.

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
keys. Named controls require exactly one of `name` (a declared local) or
`input_name` (a declared input) of the exact assignment/index type. Other
controls reject both target fields. Each child must match
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
exact context/binding mapping, generation stage, search and execution limits, fixture input-schema
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
Population generation and replay preserve the same compiled production identities,
budgets, and membership contract.


### Generated artifacts

`grammar-generated-v2` records the resolved grammar and hash, generator/RNG versions,
runtime semantic version `gagp-native-2.0.0`, canonical unsigned 64-bit decimal seed, ordered input schema and its SHA-256 hash,
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

Offline migration of a complete `grammar-generated-v1` member is a materialization
route, not generator replay. The migration reader requires its release-1 semantic,
generator, RNG and payload-seeding versions, verifies its grammar and input-schema
hashes and provenance shape, and takes the exact input/return contract, search limits,
fuel, AST shape and detached constants from the member. A plain release-1
`ast-prefix` artifact instead requires an exact `fitness-cases` schema plus explicit
fuel, maximum-node and maximum-depth values because it does not embed that contract.

Both exact program routes emit `grammar-materialized-v2`. The envelope records target
runtime `semantic_version` `gagp-native-2.0.0`, a content hash and format identity for
the release-1 source, exact schemas and limits, an `ast-prefix-v2` shape whose native
constant table is empty, and exactly one detached `constants` array using the lossless
singleton-domain codec above. Decoding rejects an incompatible semantic version, a
missing detached pool, or constants present in both locations. This contract preserves
materialized values and behavior; it makes no claim that a release-1 seed regenerates
the same program under v2. A `grammar-population-v1` container has no whole-container
conversion: callers must extract and migrate each complete materialized member.


### Materialized membership and population generation

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
sampling constants. There is no production grammar-config population or reproduction
path; the release-1 reader is isolated in offline migration tooling.


`grammar-population-v2` bundles an initial population with exactly four root fields:
`format_version`, `grammar_hash`, `count`, and `members`. Members are complete
`grammar-generated-v2` objects in population order. The population must contain
1..65536 members and fit in 256 MiB. Each member must have the same grammar identity.
Encoding requires attached immutable provenance and checks exact same-version replay
before accepting each member; changed ASTs with stale provenance fail. Decoding checks
root fields/count, required grammar identity when supplied, and each member's complete
replay. This format reconstructs generated initial populations; varied populations
require a future artifact version with variation provenance.

The existing one-AST CLI evaluation path can consume a generated artifact as a
materialized program. It verifies the case schema, applies the recorded fuel, and
rejects a conflicting explicit fuel override. This path does not certify generation
provenance.


Before accepting a generated program, generation lowers it through the native compiler
and its bytecode verifier, records the ordinary instruction count, and enforces the
implementation capacity of 1,048,576 lowered instructions. This count is separate from
logical sampling steps and materialized AST nodes. Typed definitions lower only to
the general release-2 AST and bytecode forms. Compilation and membership verification are included in
initialization timing; later compilation caches do not move this work outside the gate.

Generated-program runtime compilation identities include the runtime semantic version,
materialized AST structure, exact constant contents, ordered input names and actual
execution fuel. Grammar file paths, production weights, seed and derivation metadata
are excluded. Specialized release-1 metadata is rejected. Migrated materialized
programs use the same release-2 compilation identity after conversion.
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
execution is supported within materialized lexical regions. A standalone request
that consumes an external binding requires a materialization frame; generation
rejects such a reference when no frame supplies it. Scoped rules that do not consume
a bound value can still be generated and their scope contracts are retained.

Generation, genome-generation and population-initialization overloads consume the same
request. Membership has a corresponding request-aware overload; its default overload
checks the grammar entry and full limits. Executability preflight follows the requested
nonterminal, so an unrelated unsupported default entry does not block another executable
request. All recursive production choices remain within that request's structural budget.

Generation requests default to the `initial` stage. CPU subtree mutation and GPU
donor preparation select `mutation` through the shared donor request constructor.
Contextual feasibility tables and analysis cache provenance distinguish the
selected stage, including when available locals and budgets otherwise match.
The stage does not enter crossover compatibility IDs. Both modes still require
native verification and union membership for accepted children.

Generated artifacts record `request` with `nonterminal`, `type`, `visible_environment`
and `scope_mapping`; a non-default stage adds `generation_stage: "mutation"`.
Omission means `initial`, preserving the default canonical encoding. The request
budget is the artifact's `search_limits`. Exact replay
reconstructs the request and validates the complete rematerialized artifact, including
scope mapping and requested return type. Materialized execution still checks native
validity independently of generation provenance. Immutable genome provenance retains
the complete request for later constrained variation.

### Contextual lexical variation

Variation reconstructs each selected nonterminal's ordered formal binder environment
from the compiled scope mappings and the materialized regions. Each physical
occurrence retains its own native IDs. Compatibility compares the formal contract,
exact types and available ordinary locals; physical binder IDs do not make otherwise
equivalent sites incompatible. A shared logical hole is replaced at all occurrences,
with captured references mapped separately for each occurrence. Introduced donor
binders are alpha-renamed as needed, including collisions with destination captures.

An isolated donor frame may supply native binder IDs aligned with its request's
visible environment. IDs must be unique, valid and disjoint from declarations inside
the donor. Generation reserves those IDs when allocating new region bindings.
Private verification and lowering project captured references onto fresh temporary
input names without changing node positions. This projection includes external
lexical captures in bounded-region parameter metadata, not just REGION_VAR nodes.
Captures of bindings introduced inside the donor remain lexical; both lexical
region declarations and bounded phase declarations must be disjoint from the
external frame IDs. The stored donor retains REGION_VAR nodes and lexical capture
metadata and requires its frame; this does not extend the standalone artifact
input contract or permit unbound references in materialized execution.


The v2 primitive catalog also supplies `check_int(Int)->Int` and
`check_list(T)->T` for T in IntList, FloatList and StringList. They lower to the
checked-value expressions specified in grammar.md. They validate a value without
introducing a binder, allocating a payload or enabling any structured runtime form.


## Materialized bounded structural productions

The `bounded` structured family supplies a complete verified RegionPlan instead
of an unproved recursion declaration. Its exact expression keys are `structured`,
`captures`, `phases` and `args`. `structured` has exactly `family: "bounded"` and
`plan`; the plan uses the exact schema in bytecode_format.md, including canonical
int64 decimal strings, explicit progress constructors, memo policy and execution
limits. Contract identity includes every plan field. The earlier `recur` and `memo`
shape-only declarations retain their staging behavior.

`captures` is positional to plan.parameter_types. Each row has exactly one key:
`input`, `local`, or `bound`, whose value is a declared grammar identifier. Input
and local captures resolve to ordinary native name-table entries; bound captures
resolve through the occurrence's lexical environment. Types must agree exactly,
and duplicate capture references are invalid. Captures snapshot caller local state;
they are not extra evaluated arguments. An explicitly captured local may remain
unset, and only reading the corresponding phase parameter raises Name.

`phases` contains every phase in canonical order: predicate, base, preparations,
request expressions, combine, and coordinate boundary when applicable. Each row has
`argument` and `bindings`; the argument equals the initial state count plus extra
bound operand count plus phase ordinal. Bindings are ordered objects with `bank`,
`slot` and `name`. Banks are `state`, `parameter`, `prepared`, `result`, `measure`.
The shared plan determines slot visibility and exact types; names and sources must
be unique within a phase. Empty phases still have an explicit row with empty
bindings. At most 49 bank bindings can occur in a phase.

`args` follows the native flat child order: initial states, additional Int bounds,
then the phase bodies above. Initial expressions use the surrounding lexical
environment. Each phase uses a closed environment containing only its declared
binding names. Ordinary lexical primitives may introduce further local bindings.
Global input/local expressions and nested structured recursion are forbidden in
phase bodies, including when reached through nonterminal references or template
instantiations. Abstract template holes are checked through concrete invocations;
unresolved holes in a concrete phase remain invalid.

Materialization assigns fresh native phase binder IDs and emits one
BOUNDED_REGION node with its checked arity. Repeated template holes preserve the
whole plan and independently freshen introduced phase declarations, while mapping
captured lexical bindings to each occurrence's environment. Membership verifies
all plan fields, capture references, phase sources and exact types. Witness and
variation scope mappings use formal positions rather than requiring identical
physical binder IDs across occurrences. Lowered instruction budgets count both
root and isolated phase bytecode; a phase cannot evade the budget by residing in a
segment table.

### Explicit input write targets

A named control expression (`assign` or `for_range`) specifies exactly one of
`name` (a declared local) or `input_name` (a declared input). The target's declared
exact type must match the control signature. Input writes update the same native
slot subsequently read by an `input` expression, for that execution only. They
do not mutate the external case data. Other controls reject both target fields.
Local declarations still cannot conflict with input names.

### Production variation eligibility

An alternative may set `variation` to `true` (default), `false`, or `"unbound"`.
The owning nonterminal must also enable variation. `false` excludes the selected
production's site without excluding descendant choices. `"unbound"` excludes
that site when its materialized subtree contains a non-fixed lexical bound
reference. Fixed references in template implementation bodies do not count;
references supplied through holes do. Membership reconstruction, not imported
provenance, determines fixed origins. This policy controls both subtree mutation
and crossover candidate enumeration, and leaves constant perturbation unchanged.

## Experimental internal evolution profiles

An explicitly selected experimental evolution profile may retain immutable
derivations or executable fragments instead of materializing complete ASTs after
each operator. Legacy operator sampling and RNG draw order apply to the legacy
mode; they are not mandatory internal representation choices. Such profiles must
state their grammar/shape support, operator distribution, resource admission and
fallback boundaries. Type compatibility alone never proves nonterminal membership.
Scope, constant domain, logical repeated holes, root contract and construction
bounds remain required. Import/checkpoint metadata is untrusted; live in-process
proof reuse requires immutable ownership and a controlled construction boundary.
The restricted executable-fragment experiment is defined in
[the fixed benchmark guide](../docs/guides/fixed-asgp-benchmark.md#owned-fragment-prototype).
