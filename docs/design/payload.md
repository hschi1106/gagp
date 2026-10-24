# C++ Runtime Payload Model

This document explains how C++ runtime container values work under `cpp/` and how the exact and fallback paths interact.

## Adjacent immutable private IntList storage

`GAGP_GPU_PRIVATE_ADJACENT_CONCAT=1` permits an admitted private compact,
contiguous IntList evaluator to join adjacent immutable spans by returning a
combined span. Interleaved compact cells decline this option. Type, encoded
length, logical allocation charges and original capacity/overflow checks still
run in their original order; the optimization never increases the admitted
list budget or removes a replay. No public payload identity changes.

The arena tracks ordinary reserved cells separately from physically written
cells. Singleton and non-adjacent concat append to physical storage. A generic
allocation synchronizes the physical cursor to the ordinary boundary. Returning
from a scalar region conservatively restarts at its saved ordinary boundary;
older live spans remain below that boundary. Handles are private to the proven
whole program; memoized IntList states are excluded by existing admission.

Ownership and operation checks live in `device/builtins_device.cuh`; the
workspace admission and profile marker are in `fitness_gpu.cu`. These paths are
relative to `cpp/src/runtime/gpu/`. `gagp_test_extended_payload_gpu` contains an
independent CPU-vector trace and an append/prepend positive witness retaining
all immutable prefixes with64 actual cells and2143 reserved/charged cells.
`gagp_test_gpu_phase_population` compares ON/OFF fitness, wrong counts and case
coverage throughout evolving private-list owners, alongside its CPU audits.
Actual test results and search speed remain separate study evidence.

## Private compact-cell interleaving

`GAGP_GPU_PRIVATE_CELL_SOA=1` changes only the physical layout of an admitted
private compact IntList arena. Within each physical CUDA block, lane `t` stores
its logical cell `i` at `block_base + i * block_width + t`. Each lane retains
the same logical capacity; total allocation, list handles, allocation charges,
overflow/replay decisions and frame/memo ownership keep their existing rules.
Program scheduling cannot transfer a block's scratch columns to another block.

Private spans carry a stride for compact local cells. Immutable boxed input and
answer tables remain contiguous. Singleton, concat, slicing, indexing, and
resident scoring respect the stride; restoring the logical cell cursor restores
the same lifetime boundary. Cooperative contiguous copying is used only when
both source and target are contiguous. A strided copy uses the ordinary owner
loop. The option declines when compact private-list admission is unavailable,
and `execution_profile` records `private-cell-soa` or `private-cell-soa-declined`.

The ownership/boundary checks are in `private_intlist_offset_checks.cuh` through
`gagp_test_extended_payload_gpu`: one-lane and 32-lane boxed/compact storage,
contiguous/interleaved local cells, nested windows, wide integers, resident
distance scores, cursor restoration and logical/physical overflow. The
`gagp_test_gpu_phase_population` private-owner evolution fixtures also enable
the option while retaining their CPU and genotype checks. Test commands and
measured speed are recorded separately; enabling this layout is not a speed
claim.

## Private short-pattern edit distance

`GAGP_GPU_SHORT_EDIT_DISTANCE=1` enables an exact scoring path for private
IntList Levenshtein fitness. Session initialization prepares a sorted dictionary
of full signed Int64 values and 64-bit position masks from the same decoded,
immutable answer cache used for GPU upload. Each original case retains its own
descriptor, so input-case permutations still select the correct answer. Session
reinitialization clears/rebuilds the tables and invalidates prior fitness reuse.
Preparation and upload remain inside the ordinary initialization timing; these
immutable answer tables do not reduce the configured payload workspace cap.

Whole-sequence distance for answers of at most 64 elements uses a one-word
bit-vector update, with the global-alignment boundary. Longer answers retain
the original full DP; inputs are never truncated. Equal and empty sequences
have exact shortcuts after the existing answer-length and penalty guards.
The result still uses the same capped/discounted conversion and wrong-count
rules. Evaluation fuel, payload allocations, errors, genotype operators and RNG
are unaffected by this post-execution scoring calculation.

Descriptors must match the expected raw tag/token/marker and decoded length,
with in-bounds dictionary entries. Internal/private-marker answers decline
dictionary preparation and keep ordinary scoring. The runtime does not trust
caller-supplied program metadata for this optimization. The word update follows
[Myers (1999)](https://doi.org/10.1145/316542.316550), applied here to full-width
integer equality rather than a byte alphabet.

The implementation is in `short_edit_distance.hpp`, `short_edit_patterns.hpp`
and `device/short_edit_device.cuh` under `cpp/src/runtime/gpu/`. Runtime tests
include `gagp_test_extended_payload_gpu` for 0/63/64/65-element labels, wide
integers, capped/discounted scores, case ordering and reinitialization, and
`gagp_test_gpu_phase_population` for native CPU checks of evolving list phases.
Actual test results and full-search speed remain separate evidence.

## Why This Layer Exists

`gagp::Value` must stay compact and trivially copyable so it can move through:

- VM stacks
- bytecode const pools
- CPU/GPU runtime boundaries
- CUDA host/device transfers

That means `Value` does not directly own `std::string` or `std::vector<Value>` payloads.

Instead, `String`, `IntList`, `FloatList`, and `StringList` values use a two-layer model:

1. `Value` stores a compact packed token
2. the payload registry stores the real host-side container contents keyed by that token

## Base Container Representation

In [cpp/include/gagp/core/value.hpp](../../cpp/include/gagp/core/value.hpp), `String`, `IntList`, `FloatList`, and `StringList` use `Value.i` as:

- upper 16 bits: encoded length (`pack_container_payload` masks to 16 bits;
  host payload factories clamp the length before packing)
- lower 48 bits: deterministic hash

Helpers:

- `pack_container_payload()`
- `container_len()`
- `container_hash48()`
- `from_string_hash_len()`
- `from_int_list_hash_len()`
- `from_float_list_hash_len()`
- `from_string_list_hash_len()`

This compact representation is the public runtime transport form for containers.

## Payload Registry

The host-side registry lives in:

- [cpp/include/gagp/runtime/payload/payload.hpp](../../cpp/include/gagp/runtime/payload/payload.hpp)
- [cpp/src/runtime/payload/payload.cpp](../../cpp/src/runtime/payload/payload.cpp)

It stores:

- `unordered_map<PayloadKey, std::string>` for strings
- `unordered_map<PayloadKey, std::vector<Value>>` for lists

Where `PayloadKey` is:

- `ValueTag`
- packed `Value.i`

The registry is process-global and protected by a single mutex.

## Main APIs

### Construction

- `payload::make_string_value(s)`
- `payload::make_int_list_value(elems)`
- `payload::make_float_list_value(elems)`
- `payload::make_string_list_value(elems)`

These functions:

1. compute the packed token
2. register the real payload in the host registry
3. return the compact `Value`

The typed-list constructors are strict: `make_int_list_value()` accepts only
`Int` elements, `make_float_list_value()` accepts only `Float` elements, and
`make_string_list_value()` accepts only `String` elements. Construction fails
instead of silently widening, narrowing, or creating a public generic list.

Use these when exact container behavior should be available later.

### Manual registry access

- `register_string()`
- `register_list()`
- `lookup_string()`
- `lookup_list()`
- `lookup_index()`
- `clear()`
- `retain_only()`
- `stats()`

`clear()` drops the registry contents without invalidating existing `Value` tokens. After that, the same `Value` may still exist, but exact payload lookup will fail.

`lookup_index()` copies one selected value under the registry mutex rather than
copying a complete container. It returns false for absent payload, invalid arguments,
or an index outside the registered payload. CPU `INDEX` checks logical bounds first
and uses its existing fallback token when this lookup cannot provide an element.
String lookup preserves the frozen CPU byte behavior; the inherited difference
from the normative character contract is recorded in the
[semantic coverage ledger](../reference/grammar-migration/semantic-coverage.md#inherited-utf-8-string-discrepancy).
`retain_only()` keeps only the live payload-token closure reachable from a root value set; for list payloads this recursively keeps any referenced element payloads such as strings inside a `StringList`.

Compiled GPU evolution also roots the immutable constant-domain values owned by its
run resource. These include grammar constants absent from cases and the current
population. Preparation materializes them once, and subsequent generation sweeps
keep their full payload closure. Shared C++ ownership of a table alone does not keep
its registry tokens resident; standalone callers performing sweeps must append the
run resource's payload roots too.

### Snapshot export

- `snapshot_strings()`
- `snapshot_lists()`

These export registry contents into flat vectors.
They remain useful for diagnostics and offline tooling, but the production GPU fitness path no longer snapshots the full registry at session start.

### By-token lookup

- `lookup_string_packed()`
- `lookup_list_packed()`

`lookup_list_packed()` takes the typed-list `ValueTag` as part of the lookup key, so `IntList`, `FloatList`, and `StringList` payloads with the same compact token do not alias across tags.
These let the GPU fitness session lazily fetch only the payload tokens it actually needs for the current accepted population.

## GPU Session Handoff

The GPU runtime does not read the host registry directly from device code.

Instead:

1. `FitnessSessionGpu::init()` caches shared-case payload tokens for the current session and lazily preloads only those shared tokens
2. `FitnessSessionGpu::eval_programs()` gathers the payload tokens actually needed by the accepted program subset
3. missing tokens are fetched from the process-global payload registry by packed token and inserted into a session-local host cache
4. `build_payload_pack()` in `cpp/src/runtime/gpu/fitness_gpu.cu` flattens only that needed-token closure into:
   - `DStringPayloadEntry` plus one contiguous byte buffer
   - `DListPayloadEntry` plus one contiguous `Value` buffer
5. the compact pack is uploaded for the current evaluation run

The needed-token closure includes strings referenced from exact `StringList`
payloads. This is required because `index(StringList, i)` returns a `String`
value that later string operations such as `concat` may need to materialize on
device.

This separation is important:

- the host registry is convenient for CPU exact behavior and test setup
- the GPU runtime consumes compact, read-only per-eval payload tables instead of the full registry
- shared case locals and expected answers are packed separately from payload snapshots
- device lookup for global payload entries is done against compact sorted tables

## GPU Payload Flavors

The default device path uses the generic `Mixed` eval kernel family.

Programs are still classifiable into four fine-grained payload flavors in `cpp/src/runtime/gpu/host_pack_gpu.cu`:

- `None`
- `StringOnly`
- `ListOnly`
- `Mixed`

The current GPU fitness path selects one execution profile for the population.
One grid-stride kernel covers all programs; workspace limits concurrent blocks,
not evaluation coverage. A single launch is not an architectural requirement.
An opt-in, structurally proven integer/list view profile is described below.

The finer `StringOnly` / `ListOnly` labels are kept for experiment tooling and offline bucket studies rather than the production eval dispatch tree.

Exact string/typed-list builtins use bounded per-thread scratch. CPU and GPU share the release 2.0.0 direct-list tags (`IntList`, `FloatList`, `StringList`). When exact string output materialization will not fit in GPU per-thread scratch, GPU string operations use the fallback path. Direct-list operations preserve the list tag and compact hash/length token even when the exact expanded payload cannot be materialized in thread-local scratch.

There is no legacy payload-free/payload-bearing bucket split. The whole launch
uses `Mixed` or the proven view profile, and evaluates every program/case pair.

### Experimental private IntList arena offsets

`GAGP_GPU_PRIVATE_INTLIST_OFFSETS=1` requests `private-intlist-offset-v1` in
the existing GPU arena VM. Admission is population-wide and fail-closed: every
root, phase and region must use only Int/Bool/IntList operations from the view
subset plus integer Singleton/Concat, with IntList outputs permitted. Container
equality and memoized regions with any IntList state are excluded. Unsupported
programs, policies, missing payloads or absent global-arena storage retain normal
execution and report `private-intlist-offset-v1-declined` in the execution profile.

This opt-in profile has **collision-independent local content semantics**. It
does not promise equivalence to canonical hash-registry aliasing under a hash
collision. External inputs, answers and constants retain canonical transport
identities; external IntList `Value.b` markers are normalized before execution.
Only complete, integer-valued resident payloads can enter the profile. Generated
lists use invocation-private offsets marked by `Value.b`, with distinct arena
and immutable-table origins, checked bounds, and no hash/registry insertion.
Active private arenas allocate only list cells and required frame/memo storage;
unused string storage, local registries, and legacy/epoch indices are omitted
from allocations and workspace-budget accounting. Their pointers remain null
through initial execution and physical-overflow replay. Capacity fields and
logical allocation accounting retain their existing meaning.
Descriptors remain inside the invocation through resident content scoring;
neither serialization nor public bytecode/program ownership accepts a proof token.
Exactness and wrong-case counts compare contents, including when equal contents
have different private offsets. Content scoring also ignores the historical
saturated-versus-masked transport length difference for a 65536-element list;
legacy token-based Exact/wrong counts can therefore differ at that boundary
even without a hash collision. Exact and Levenshtein support use the allocation
policy below; TupleSummary has a separate opt-in described next.
Set the opt-in before session and native-population initialization so expected
payloads and the cold capability are prepared.
Exact/Levenshtein admission requires a positive logical allocation limit at most
65536 and external
payloads at most 65536 elements. Private generated lists may reach 65537 elements:
a host constant containing 65536 elements has clamped transport length 65535,
so concatenating it with a one-element constant charges exactly 65536 cells
while copying 65537 elements. This exhausts the entire logical budget; further
nonempty allocations fail, scalar-region rewind does not refund that charge,
and zero-charge Slice uses the masked length and cannot grow the backing.
A descriptor escape bit means actual length `65536 + low16`, allowing local
lengths 65536/65537 and immutable-table length 65536 only. Larger private handles
are rejected. Generated `container_len` keeps the existing low-16-bit behavior
for Len/Index/Slice and empty-Concat decisions. At logical limit 65536, private
GPU replay reserves at least 65537 physical value cells so this valid boundary
needs only the ordinary initial-overflow replay. Other external sizes decline
the profile rather than narrowing an offset or length.

`GAGP_GPU_PRIVATE_TUPLE_SCORE=1` extends this private profile to TupleSummary.
It compares signed 64-bit fields directly, validates the expected tuple width,
and counts correctness using the primary field only. Auxiliary fields contribute
the same weighted/capped error as ordinary tuple scoring. All existing whole-
program type/support proofs and external payload bounds remain required.
Positive logical caps through 65536 retain their existing charges. A zero cap
is preserved, rather than replaced by an implicit allocation policy.

Zero-cap admission additionally calls `gpu/private_tuple_policy.hpp` on current
bytecode. A forward dataflow proof checks all branch paths and proves every
Concat allocates at most three cells. Inputs and slices may keep their original
length through 65536. Region base/combine/boundary results establish an inductive
three-cell bound for child-result bindings. Unknown or larger concatenations
decline the complete private profile and use ordinary evaluation, including when
a later slice would shorten the final result. This prevents unbounded zero-cap
intermediates from exceeding the private descriptor's representable length.
The resident owner materializes current phase bytecode for this additional
proof each evaluation; an initial/typeflow-only certificate is insufficient.
The execution profile records `private-tuple-score` or its declined form.
Workspace reuse, typed phases and private window execution apply only after
these checks succeed. Fuel, limits, complete cases and GPU overflow replay keep
their existing contracts. The new flag defaults off.

Singleton and nonempty Concat retain element copying and logical allocation
charges. The required positive allocation policy already makes ordinary arena
Slice alias immutable windows; private Slice preserves that rule. Empty Concat, fuel, frame/memo limits, logical-limit errors, physical
overflow diagnostics and replay remain in the existing VM. Scalar region return
rewinds cannot retain a list descriptor; list-valued results keep their backing.
The native GPU reproduction owner proves fixed root/segment structure on cold
import and composes it with an explicit capability certificate from every fresh
GPU-lowered phase on every generation. Initial-population admission alone never
authorizes later offspring. This execution capability is a type/support proof,
not grammar membership; the native reproduction owner retains its separate
grammar construction and variation invariants.

`GAGP_GPU_COMPACT_INTLIST_CELLS=1` additionally requests eight-byte signed
integer cells for invocation-local storage, only after the complete private
IntList capability above succeeds. Immutable case/constant/answer tables remain
boxed `Value` arrays. The existing VM uses typed spans to read either backing;
ordinary boxed payload lookup rejects compact local descriptors. All 64 integer
bits, element offsets, encoded numeric list lengths, actual content lengths,
logical allocation charges, fuel, rollback, overflow/replay, and case coverage
are identical to the boxed private profile. The flag defaults off and reports
`compact-intlist-cells` or `compact-intlist-cells-declined` in the execution
profile. It changes physical cell bytes and accounted workspace concurrency;
there is no new VM, reproduction backend, search rule, or payload transport.

`GAGP_GPU_TRIVIAL_PHASES=1` optionally bypasses phase stack/local setup inside
an admitted private IntList arena invocation. Verified phases with at most eight
locals and bindings qualify only for `PushConst` or one non-Parameter binding's
`Load`, optionally followed by `Return`. Constant phases may declare unused
bindings: missing or incorrectly typed unused captures remain unobserved. The
single-Load form selects one unique non-Parameter binding by destination local
among up to eight declared bindings; unused bindings retain lazy capture errors.
Every instruction keeps its original fuel check and charge in order, including partial consumption on
timeout; final expected-tag and result-phase fallback-token checks are retained.

Constants preserve their complete canonical `Value`, and loads preserve their
original frame value. Neither path eagerly resolves payloads, converts constants
to views, changes list registration/identity, or extends payload lifetime. The
shortcut has its own metadata and runs only behind both the private-arena proof
and this flag; declined view/generic profiles retain ordinary execution. Metadata
is reset and rebuilt against the current phase code/constant offsets after every
GPU generation. Set the flag before native population initialization. Profiles
append `-trivial-phases` or `-trivial-phases-declined`; the flag defaults off.

[Static phase counts](../reference/trivial-private-phase-counts.json) show why the
existing zero-binding scalar-constant shortcut misses these populations. These
are structural artifact counts, not dynamic call frequencies or speedup evidence.

`GAGP_GPU_PRIVATE_BOUND_PHASES=1` optionally reuses the existing Bound-view
scalar compare/add shortcuts inside an admitted private arena. The existing
`GAGP_BOUND_COMPARE_PHASE=1` and `GAGP_BOUND_ADD_PHASE=1` metadata matchers retain
their strict binding counts: one non-Parameter binding compared with an Int
constant, or two non-Parameter loads followed by Add, optionally with Return.
Only used Int operands qualify; other tags execute the ordinary phase VM.
Unused captures remain lazy. Full-width comparisons, wrapping Int addition,
ordered instruction fuel checks and final result types remain unchanged. Current
metadata is refreshed after every GPU generation. This independent flag defaults
off and profiles append `-private-bound-phases` or `-private-bound-phases-declined`.
The typed Bound-view VM remains limited to its existing view profile.

`GAGP_GPU_PRIVATE_WARP_COPY=1` optionally cooperates on private compact IntList
copies without changing arena layout, case ownership or VM scheduling. Only
checked compact destinations in CUDA global allocations qualify; boxed global
IntList sources and compact local sources are both supported. Thread-local
pointers must never be shared through this helper. On Volta and later, the helper
captures the currently active lanes and uses that opportunistic group only. It
requires at least four participants and copies spans of at least 256 cells;
smaller spans, smaller groups, older devices and the default-off setting retain
serial copies. Small-span owners still participate in all group collectives
before copying their own small span serially.

The group enumerates qualifying owners and broadcasts each owner's checked
source, destination, length and source representation. Participant ranks copy
consecutive cells of one owner at a time; lanes need not have equal sizes or
matching VM positions. An initial `__syncwarp(mask)` orders prior owner writes
before foreign reads, and a final barrier completes foreign writes before owners
resume. Sources are immutable during the copy; append destinations are disjoint
from existing source spans and other cases. The captured mask remains fixed for
all collectives, so later arrivals form independent groups or copy serially.
The synchronization follows NVIDIA's [warp intrinsic participation and ordering contract](https://docs.nvidia.com/cuda/cuda-programming-guide/05-appendices/cpp-language-extensions.html#warp-functions).
Logical allocation, fuel, descriptor identity, physical limits and replay remain
unchanged; every source cell is copied. Profiles append `-private-warp-copy` or
`-private-warp-copy-declined` when requested. Preparation and copy costs remain
inside ordinary evaluation timing.

`GAGP_GPU_PRIVATE_TYPED_PHASES=1`, together with
`GAGP_GPU_PRIVATE_GLOBAL_OFFSETS=1`, optionally runs private phases through the
existing 64-bit typed phase loop. Current complete private typeflow and successful
normalization of every IntList case value and packed root/phase constant are
required, including empty lists. Any unconverted canonical list, including an
actual65536 input or constant, declines this shortcut for that evaluation;
expected-answer-only payloads do not enter the phase loop and do not disqualify
it. Normalization and its uploads remain inside ordinary evaluation timing and
are repeated against each fresh packed table. External programs/cases are unchanged.

Only phases with at most eight locals and a verified stack bound of sixteen use
the typed loop; larger phases retain the generic private VM. Scalars retain all
64 bits. List words preserve private global/local origin and escaped backing
length, while Len/Index/Slice retain the existing encoded numeric length rules.
The list adapter calls the existing checked private builtins for allocation,
logical charging and physical-capacity replay. Missing or incorrectly typed
parameter captures fail only on Load after its fuel charge; Store clears that
local's capture-type guard. Unused captures remain unobserved. The same loop
retains instruction order, wrapping arithmetic, branch fuel and protected numeric
operations; its ordinary Bound-view policy remains unchanged. Profiles append
`-private-typed-phases` or `-private-typed-phases-declined`; the flag defaults off.

`GAGP_GPU_PRIVATE_MEASURE_CACHE=1`, together with
`GAGP_MEASURE_PHASE_CACHE=1`, extends the existing block-local sequence-Measure
preparation cache to admitted private IntList arenas. The default is off. Current
CPU-verified or GPU-lowered private typeflow must succeed; the block then admits
only a single Int preparation whose declared bindings are all Measure slot zero,
whose constants are Int/Bool, and whose forward-only scalar instruction/builtin
whitelist cannot observe captures or payloads. Other phases execute normally; private phases already handled by the scalar-constant
or enabled trivial constant/Load shortcut skip cache construction.
The cache is rebuilt from current phase code for every program block, including
initial tiles and replay; it never crosses a program or generation boundary.

Lengths 2 through 64 are evaluated by the same arena phase VM using an empty
payload state and the same private/trivial dispatch settings. Cached results
retain all 64 integer bits, exact non-timeout errors and spent instruction fuel.
Timeouts do not populate entries. A hit requires sufficient remaining fuel;
misses, lengths outside the table and insufficient fuel execute the original
phase, preserving partial consumption and error ordering. The original region
entry fuel, expression evaluation, logical allocation, full cases and reductions
remain unchanged. Cache storage follows all score slots in dynamic shared memory: untiled arenas
retain every logical lane slot, while tiles use physical lane slots. Cache fit
admission conservatively reserves the original logical score width; unavailable
shared capacity declines caching.
Profiles report `-private-measure-cache` when enabled for private arenas, or
`-private-measure-cache-declined` when its prerequisites fail. This reports cache
availability; individual ineligible phases still use the ordinary VM.

`GAGP_GPU_PRIVATE_GLOBAL_OFFSETS=1` additionally prepares admitted immutable
IntList inputs and packed root/phase constants as global-origin offsets into
this evaluation's payload table. This opt-in host step runs only after complete
private eligibility; it checks identity, tag, encoded/actual length and backing
bounds. Lists up to 65535 elements qualify. Actual 65536-element globals remain
canonical so saturated/masked transport lengths retain their existing Len,
Index, Slice and empty-Concat behavior. Unresolved values retain ordinary lazy
lookup; unsupported populations still decline private execution normally.

The existing prepared-case buffer owns the upload through initial evaluation
and overflow replay. Session cases and external/owned ASTs remain canonical;
all offsets are prepared again against the current table on every evaluation,
including fresh GPU generations and reinitialized sessions. Host preparation
and buffer upload remain in reported pack/upload and total evaluation timing.
Answers retain canonical scoring transport. No device operation, numeric value,
fuel, logical allocation charge or physical replay policy changes. Profiles add
`-private-global-offsets` or `-private-global-offsets-declined` when requested;
unset and other values leave the prototype off. The extended payload test covers
both modes, canonical length boundaries, mixed origins, lazy captures and
full-case CPU parity; native-owner tests compare both modes over 32 generations.

`GAGP_GPU_PRIVATE_WINDOW_EXECUTOR=1` selects the existing two-window driver
for admitted private IntList arenas only when every packed segment has IntList
state zero, an optional carried Int, IntList result, two same-source window
requests, one InteriorCut Int preparation, no memo and no bound operands. The
carried state may only copy itself or run its verified Int request expression.
Unsupported or mixed shapes keep the generic driver and report
`-private-window-declined`; admitted shapes report `-private-window`. The default
is off. Current private typeflow excludes fallback-token phase results and nested
region calls, including after every GPU-lowered generation.

Private execution retains the existing tagged compact frames, capacities,
workspace accounting, payload cells and resident scoring; unboxed/SoA frame
formats remain view-only. Initial arenas and replay retain the same driver
capability. Every evolved predicate, base body, cut, request expression and
combine still runs with original instruction and entry fuel. Existing
`GAGP_GPU_WINDOW_LEAF_DIRECT=1` may execute an intrinsic terminal child's base
phase without storing its frame, after the original Slice, carried-state
expression, declared depth check and child entry charge. Encoded lengths,
logical allocation, physical overflow/replay, lazy captures, errors and scalar
region rewind remain in the original phase VM and arena lifetime.

### Proven integer/list execution profile

`cpp/src/runtime/gpu/view_profile.hpp` checks forward type flow after ordinary
bytecode verification. With `GAGP_VIEW_PROFILE=1`, a population containing bounded
regions can use `IntListViews` only when every root/phase is supported, all required
list payloads are present and contain integers, and final outputs are scalar.
Supported observations are Len/Index/Slice; list equality/output, other containers,
Float, backward jumps and unsupported instructions use the generic whole-batch
fallback. Programs are never discarded. `FitnessEvalResult::execution_profile`
reports the selected path.

Read-only lists then use a private offset/length descriptor into the immutable
launch payload array. The descriptor never escapes the evaluator. Slices need no
copy or hash, removing copy-pool losses for this subset. The public Value and
serialized payload representations stay the same.

`GAGP_PREPARED_CASE_VIEWS=1` additionally prepares immutable captured list
inputs once per evaluation. After the same complete-payload/type-flow proof,
`FitnessSessionGpu` maps case IntList tokens to this launch's offsets while
preserving their packed lengths. It uploads this separate table only when a
region declares an IntList parameter; list state alone does not need it. Repeated
phase bindings then use the existing idempotent view conversion. Original case
values and presence masks remain available for ordinary kernels. Public container
markers are cleared during session initialization, missing or unproved captures
retain ordinary lazy errors, and prepared descriptors never escape the evaluator.
Table construction is included in `pack_ms`, allocation/upload in `upload_ms`;
changing constant payloads rebuilds offsets on the next evaluation.

`GAGP_TYPED_VIEW_PHASE=1` uses unboxed int64/view payloads for proven phases with
at most 8 locals and verified stack bound 16. It retains ordinary numeric
conversions, protected division, checks and every semantic fuel charge. Larger
phases use the ordinary interpreter. Independently, verified small phase bounds
select smaller tagged interpreter arrays; `GAGP_GENERIC_PHASE_VM=1` disables that
storage optimization for controls.

`GAGP_SORT_CASES=1` stably orders cases by total input IntList length and traverses
them in strided lane order. It applies only to proven view execution with integer
answers/penalty and a conservative exact accumulator bound. All cases remain;
other profiles retain their original reduction order. Sorting and its upload are
charged to session initialization, once per session.

`GAGP_CONSTANT_PHASE=1` separately recognizes fully verified phases containing
one scalar constant push and an optional return, with no locals or bindings.
Int/Bool/Float/Char results can bypass the interpreter. The original instruction
fuel is charged, including an explicit return, and the expected result tag is
checked. Other phases use their ordinary evaluator; this optimization does not
narrow numeric precision or remove necessary validation.

`GAGP_COMPACT_FRAMES=1` uses 72-byte frames for at most one state, or 88-byte
frames for at most two states, when every segment also needs at most one
preparation and two results; other shapes use 264-byte generic frames. Frame
and memo capacities do not decrease. The 512 MiB workspace limit controls launch
concurrency, never evaluation coverage. `gagp_test_bounded_region_gpu` checks the
fast path, generic fallback, numeric boundaries and exact fuel/capacity exhaustion.

Further opt-ins specialize direct phase/root execution, verified window regions,
unboxed window frames and bound scalar Add phases. Their support and cost
contracts are maintained in the [fixed benchmark guide](../guides/fixed-asgp-benchmark.md#architecture-experiments-2026-10-01-second-round).
They do not change cases, numerical precision or resource/fuel limits.

## Exact Path vs Fallback Path

Container builtins in the CPU and GPU runtimes follow the same high-level policy while exact materialization is available:

1. try exact payload lookup
2. if exact input lookup or exact output materialization is unavailable, return an opaque fallback result

Exact path:

- `concat` builds the real concatenated string or typed list
- `slice` builds the real sliced string or typed list
- `append` builds the real typed list with one additional element
- `reverse` builds the real reversed string or typed list
- `find` and `contains` inspect exact string payloads
- `index(String, i)` returns `Char`
- `index(IntList, i)` returns the integer element value
- `index(FloatList, i)` returns the float element value
- `index(StringList, i)` returns the string element value

Fallback path:

- string `concat`, `slice`, and `reverse` return `FallbackToken`
- list `concat`, `slice`, `append`, `prepend`, and `reverse` return the corresponding direct-list tag with deterministic hash/length
- `index` returns `FallbackToken`
- `find` and `contains` return `ValueError` when exact string payload lookup is unavailable

This fallback is deterministic and parity-friendly, but not fully semantics-preserving.
The design target is:

- exact CPU/GPU parity when both sides have exact payload access
- deterministic fallback when exact payload access or materialization is unavailable
- CPU/GPU parity for compact direct-list hash/length results even when expanded list payloads exceed bounded GPU materialization capacity.

## When Exact Payload Can Be Missing

Exact payload lookup is not guaranteed.

Common cases:

- tests or helper code directly create `Value::from_string_hash_len()` / `Value::from_int_list_hash_len()` / `Value::from_float_list_hash_len()` / `Value::from_string_list_hash_len()` without calling `payload::make_*()`
- random constant generation creates container tokens directly
- registry state was cleared with `payload::clear()`
- registry state was pruned with `payload::retain_only()` and the token was not part of the retained live-root closure
- GPU exact output materialization exceeds bounded per-thread scratch

Because of this, callers must not assume every `String`, `IntList`, `FloatList`, or `StringList` value has a recoverable payload behind it.

## `index` Return Type Behavior

`index` is type-stable on exact typed-list payloads.

On exact payload lookup:

- `index(String, i)` returns `Char`
- `index(IntList, i)` returns `Int`
- `index(FloatList, i)` returns `Float`
- `index(StringList, i)` returns `String`

On fallback:

- string and typed-list indexing return `FallbackToken`

This means the exact result type is predictable from the typed-list tag, while missing payloads still produce the deterministic fallback token.

## Bounded-region results and ownership

General bounded regions use the ordinary payload tables and one invocation's
shared per-thread scratch. Host packing gathers constants from every phase,
including boundary phases. Frame values and memo cells contain compact `Value`
references in explicitly owned global device workspace slices. Payload scratch
remains live through all descendants, memo hits, and the returned payload. Workspace
slices are reused between invocations only after their frame/memo references are no
longer needed; this does not extend or shorten payload scratch lifetime. A scalar result permits restoring the invocation's scratch
counters only after the region has completed.

The bytecode verifier proves exact nominal phase types. At runtime, terminal and
combine result phases also accept internal fallback tokens produced by ordinary
payload operations. Sibling results must agree in their actual tags before the
next request runs, and combine must retain that tag. Predicates, preparations,
request expressions and child state entry keep exact tag checks. These rules are
shared by the CPU and GPU adapters; they do not expose a public fallback type.
Compact typed-list overflow retains its list tag, while indexing an unavailable
payload can produce an opaque fallback result even for a nominal scalar type.

Direct execution tests must distinguish a successful fallback from a type error
and check consumed fuel. Both can receive the same fitness penalty, so aggregate
fitness equality alone does not establish fallback compatibility.

## Typed-List Hashing Is Shallow

`payload::make_int_list_value()`, `payload::make_float_list_value()`, and `payload::make_string_list_value()` hash list elements with a shallow helper.

Nested lists are not part of the public typed-list contract. If helper or compatibility code ever constructs container-valued elements, the hash includes the nested container's packed token rather than a recursive deep re-hash of the full nested payload.

This keeps hashing cheap and stable across CPU/GPU paths, but it means list identity is based on element `Value` identity, not fully expanded recursive content.

## Collision Risk

Container identity is not collision-free.

Risk sources:

- container hashes are truncated to 48 bits in the packed transport form
- registry keys are based on `(tag, packed-token)`

If two different payloads produce the same tag + length + hash48, they alias to the same registry key. Later registration overwrites earlier contents for that key.

This is a compactness/performance tradeoff, not a cryptographic or collision-proof design.

## Design Tradeoffs

Benefits:

- compact `Value`
- cheap stack and bytecode transport
- CPU/GPU-friendly representation
- deterministic fallback when exact payload is unavailable

Costs:

- process-global registry lifetime
- possible token collision
- fallback behavior is not fully semantics-preserving
- fallback results are opaque and intended to compare as mismatches in fitness/equality paths
- typed-list hashing is shallow

## Files To Read Together

For the full runtime picture, read these in order:

1. [cpp/include/gagp/core/value.hpp](../../cpp/include/gagp/core/value.hpp)
2. [cpp/include/gagp/runtime/payload/payload.hpp](../../cpp/include/gagp/runtime/payload/payload.hpp)
3. [cpp/src/runtime/payload/payload.cpp](../../cpp/src/runtime/payload/payload.cpp)
4. [cpp/src/runtime/cpu/builtins_cpu.cpp](../../cpp/src/runtime/cpu/builtins_cpu.cpp)
5. GPU mirrors under `cpp/src/runtime/gpu/`

## Internal donor staging

Parallel donor generation uses an internal thread-local `StagedPayloads::Scope`.
String and typed-list writes stay private, and lookups see those writes before
capturing a committed-registry value or absence. Repeated reads reuse the captured
value. Registry enumeration remains committed-only; clearing or pruning within a
scope is rejected. Normal runtime calls outside a scope retain their existing
behavior.

After workers join, a single registry lock protects validation and commit. Read
dependencies must still match the committed registry and the other workers;
conflicting writes, including different floating-point bit patterns, reject the
whole batch without publishing staged entries. The caller then reruns the donor
pool sequentially. Successful commits publish the complete string/list closure.
Scopes cannot nest or be reused, and commit requires completed worker scopes.

A sealed read-only staging snapshot may revalidate its captured dependencies
without committing or consuming them. The check holds the registry mutex and
compares exact strings and typed-list elements, including Float bits. It rejects
writes, unsealed/active snapshots, conflicts, and an enclosing thread-local scope.
Successful earlier commit does not prevent another read validation. Consumers
must separately ensure their AST/request ownership has not changed; payload
validity alone does not certify an arbitrary mutable genome.

## CPU fitness temporary payloads

Each CPU fitness case executes and scores inside a discard-only payload overlay.
Generated string/list payloads are visible until scoring finishes, then released;
committed case, answer, and program payloads remain available. The overlay limits
cumulative generated data plus registry keys to 8 MiB per case. Concat checks
its prospective output before allocating it. Exhaustion produces a Value error
and the normal case penalty; instruction fuel is unchanged. This is a CPU
fitness resource boundary, not a claim that the GPU list arena has equal capacity.
Direct execution APIs keep their existing persistent result-payload ownership.
Configured sequence fitness resolves generated GPU payloads inside
`d_execute_bytecode_impl`, before its per-invocation payload state is destroyed.
Expected payloads are uploaded with shared case payloads. The metric owner is
`core/fitness_policy.hpp`; CPU and CUDA scoring share the distance recurrence
and numeric transforms. See [fitness](../../spec/fitness.md) for capacities and
configuration semantics.

GPU fitness programs that construct strings/lists use the separate
`extended-payload` profile when a view executor is not admitted. Its per-case
arena has 8192 list values, 1024 list entries, 8192 string bytes and 1024 string
entries. These are cumulative allocation limits, including intermediate
results, not maximum output lengths. Physical exhaustion is detected separately
from a semantic runtime error and triggers GPU replay of the affected programs.
Replay uses `ArenaMixed`, pointer-backed invocation storage in a bounded global
workspace. Thirty-two physical threads execute the original logical lane chunks
and the same canonicalized warp reduction order. Successful replay replaces the
program's score and diagnostics; preliminary physical failures never become
fitness penalties. Scalar/view kernels keep their existing storage.

Replay starts with 65536 entries and list values per invocation, with at least
the configured logical cell limit, and 8192 string bytes. It doubles physical
capacities when needed while reducing concurrent program blocks to stay within
512 MiB of additional replay scratch. Logical policy limits never grow. If one
32-thread block cannot fit or a CUDA allocation fails, the whole fitness call
fails explicitly rather than returning a scored physical error or using CPU
execution. Programs/cases/launches replayed are reported separately from final
case counts through `FitnessEvalResult`, evolution timing, and benchmark JSON.
Kernel timing includes all replay kernels; total evaluation timing also includes
overflow detection, scratch allocation, and transfers. No host worker threads
are added. This remains a bounded evaluator, not an unlimited allocation promise.

Focused validation: `ctest --test-dir cpp/build -R
'gagp_test_payload_staging|gagp_test_extended_payload_gpu' --output-on-failure`.
The GPU test covers full 1024-case list construction, tuple/string entry counts,
capacity errors, clamped subordinate memo coordinates and a 200-cell memo case.
It also forces an 8192-cell overflow below the 65536-cell logical bound, checks
selective replay and final counters against CPU execution with 512 reduction
lanes and discounted list fitness, and verifies logical errors do not replay.

The optional fitness logical list-cell budget is independent of storage bytes
and registry deduplication; its contract is in `spec/fitness.md`.
`runtime/payload/staging.hpp` owns the invocation-local CPU counter, established
by CPU fitness or a direct benchmark audit before execution. CUDA initializes
the same counter in each invocation's payload state. Neither counter persists
across cases. GPU `DResult::payload_capacity_exceeded` distinguishes physical
list storage exhaustion from a logical budget failure.

In the logical list budget profile, CUDA slices register exact-content window
aliases instead of copying cells. A nonnegative private entry offset addresses
the invocation's append-only list arena; a negative offset encodes
`-(case_table_offset + 1)` into the immutable packed case/constant table. These
offsets never escape the invocation or modify public payload identity. Repeated
identical registrations reuse their first resident entry; logical allocation
charges still occur on every singleton and nonempty concat execution.

CPU builtins may borrow staged list contents through the private `read_list`
helper while constructing a fresh result. Registry-backed inputs still use a
locked copy; no pointer into the shared registry is exposed. Reads finish before
registration, which can replace an existing staged payload. Fresh builtin list
results transfer their vector storage into the overlay through
`make_owned_list_value`, retaining typed validation, content hashing, collision
checks, and cumulative byte charges on every registration. Public constructors
and payload identities are unchanged.
`GAGP_GPU_PAYLOAD_INDEX=1` is an optional execution-private lookup prototype for
GPU arena replay. The pointer-backed `IndexedArena` flavor indexes existing canonical
`(list tag, packed hash/length)` entries with bounded open addressing at at most
half occupancy. Initial evaluation retains the ordinary extended evaluator;
its large per-thread arena must not be enlarged with an additional local index,
which can exceed CUDA local-memory reservation limits. Ordinary payload/view
flavors keep their original storage.
The index preserves the first registered identity, including duplicate keys and
negative-offset aliases; public hashes, full-content hashing, list allocation
charges, fuel, and comparisons are unchanged. When a scalar bounded region
discards temporary list entries, the index is rebuilt from the surviving prefix
only if the entry count changed. This preserves probe chains and first-match
identity when later allocations reuse discarded entry slots. Slots are cleared
per invocation, and replay slot memory counts against its 512 MiB scratch budget. This prototype
removes linear entry lookup/dedup scans, not content hashing. Its initialization
cost is included in kernel timing, and performance requires a measured flag-0
versus flag-1 comparison. The execution profile adds `-list-index` only when the
indexed replay actually runs. `gagp_test_extended_payload_gpu` covers collisions,
duplicates, aliases and full-case CPU parity with the flag both off and on.

`GAGP_GPU_GLOBAL_PAYLOAD=1` optionally routes initial generic extended-payload
evaluation through the same pointer-backed indexed arena executor as replay.
It retains the original physical capacities (1024 entries, 8192 list values and
8192 string bytes per invocation) and the logical fitness block width. Thirty-two
physical threads process those logical lanes; a persistent GPU program queue
reuses a bounded set of blocks across the population. Scalars in the same
population use the same original row mapping and generic VM. View-specialized
populations remain on their admitted path.

Initial payload, index, frame and memo buffers share a 512 MiB scratch budget.
The ordinary cached region workspace is released before this allocation, and
initial scratch is released after synchronization before any replay scratch is
allocated. Preparation/allocation, execution, transfers and releases remain
inside total evaluation timing; allocations are included in upload timing and
all GPU launches in kernel timing. Physical overflows retain the existing GPU
replay behavior and counters. The execution profile adds `-global-arena`.
`gagp_test_extended_payload_gpu` compares both initial routes across logical and
physical capacity boundaries, and checks a 96-program mixed scalar/list queue,
logical 512-lane reduction and the overlap callback. This setting changes no
CPU threading or reproduction backend.

`GAGP_GPU_GLOBAL_WINDOW_ALIASES=1` enables exact list-window aliases in both
ExtendedMixed and global arena evaluation, even when the logical list budget
is disabled. Despite the historical flag name, global initial evaluation is
not required.
The source backing must resolve to the immutable packed launch table or the
invocation's append-only local or global arena. This backing remains valid through
scoring. The flag propagates to that call's replay; it does not affect legacy
Mixed evaluation. Defaults and implicit aliases under a positive logical
allocation budget are unchanged. Content hashing, first-key
lookup, fuel and logical allocation charges stay unchanged. The profile adds
`-window-aliases`. The extended-payload GPU test compares exact full outputs
with the flag off/on at logical limit zero and checks that aliases avoid the
physical copying that otherwise triggers replay on either initial executor.
A raw ExtendedMixed regression also covers nested aliases of immutable table
payloads and local Concat allocations for IntList, FloatList and StringList,
including empty slices, full cell storage, and full registry errors.

Diagnostic fixed-ASGP results separately report `initial_kernel_wall_ms`,
`initial_kernel_event_ms` and `replay_kernel_event_ms`. Initial wall timing
includes launch/wait and any host overlap callback. Initial event timing is
populated only when a host overlap callback enables CUDA events; otherwise it
is zero. Replay event timing sums the replay CUDA events. The existing
`kernel_ms` retains its compatibility behavior: wall time including replay
allocation and retry orchestration without overlap, summed kernel event time
with overlap. Total evaluation time includes all allocations and frees.

`GAGP_GPU_TILED_REPLAY=1` schedules replay as `(program, logical warp)` tasks
on the bounded 32-thread arena blocks. Each task keeps the original logical
lane's consecutive case partition and writes its canonical warp subtotal.
A final GPU kernel sums these subtotals in the original warp order. Counters
and physical-overflow flags reset once per affected program before tiles run;
all cases are reevaluated, and retry counters count full program passes.
Replay CUDA event timing includes reset, evaluation, and final reduction.
The 512 MiB scratch limit includes tile partials, queue and program indices.
Tiled replay starts with 4096 entries and at least 65536 value cells, doubles
entries alone until 65536, then follows the existing combined growth sequence.
The default replay route and CPU/reproduction behavior remain unchanged.

`GAGP_GPU_FUSED_LIST_CONCAT=1` enables an exact Concat implementation in the
extended and arena payload evaluators. It resolves both list operands once,
reuses those first-match results through logical charging and capacity checks,
and performs canonical hashing while copying values. Hash element order and
bits, empty-list handling, allocation accounting, registry registration and
fallback/error behavior are unchanged. The profile adds `-fused-concat`.
This setting adds no payload arrays and does not change Slice or index clearing.
The extended-payload test compares both implementations for all three typed-list
kinds, missing payloads, empty operands, first-match collisions, capacity errors,
logical limits, and full CPU/GPU list-output fitness parity.

Initial physical-overflow counts are retained per program in
`FitnessEvalResult::initial_payload_overflow_cases` before replay replaces final
status counters. `GAGP_GPU_OVERFLOW_DIAGNOSTICS=1` additionally records a bit mask
per original logical warp: bit `lane % 32` marks a lane containing at least one
initial physical overflow. Masks use original program order and logical lanes,
not arena worker IDs, and are disabled before retries. The optional storage is
64 KiB for 1024 programs with 512 logical lanes and is included in the arena
scratch budget. This diagnostic adds atomics/copyback and may reduce arena worker
count at a memory-budget boundary; it is not a zero-cost timing profile.

Fixed-ASGP snapshots emit per-program `initial_payload_overflow_cases` and, when
the flag is enabled, `initial_payload_overflow_logical_lanes`, raw `lane_masks`,
affected `lanes`/`tiles`, and `lane_replay_cases`/`tile_replay_cases` (all fields
carry the `initial_payload_overflow_` prefix). The latter count the original case
chunks covered by affected lanes or tiles, not extra cases actually executed.
They estimate selective replay opportunities; replay still replaces entire
programs. Logical allocation failures do not set these diagnostics. The existing
extended-payload GPU regression checks dense 33/512-lane masks, unaffected
programs, logical failures, and sparse lanes preserved across multiple retries.

`GAGP_GPU_ADAPTIVE_REPLAY=1` starts GPU replay with 4096 registry entries,
`max(16384, policy.list_allocation_limit)` value cells and 8192 string bytes
per physical lane. A declared positive list budget therefore sizes the initial
value arena upfront; zero-budget policies retain the smaller 16384-cell start.
Physical failures
carry entry/value/byte reason bits independently from semantic errors and
initial overflow diagnostics. Retries double only capacities exhausted by
unresolved programs; unknown reasons conservatively grow all three. Each retry
still evaluates the complete unresolved programs and uses the original logical
lane reduction. Logical allocation budgets, fuel and numeric behavior do not
change. Scratch accounting includes the reason array within the arena budget.
The default sizing/growth route is unchanged. Snapshot diagnostics report final
replay entry/value/byte capacities; replay timing and case counters include all
attempts. Tests independently exhaust entries, values and bytes and compare
fitness and statuses with CPU evaluation, retaining unaffected program rows.

This sizing rule trades concurrency for avoiding repeated full-program work.
The 2026-10-15 coarse P64/C1024 snapshot screen before this adjustment found
that an unconditional 16384-cell start took three replay passes instead of one
for FilterOdd (3859 to 4989 ms total evaluation) and NegativeToZero (5358 to
6458 ms), while IsSorted improved from 722 to 643 ms with one replay pass.
All scores and statuses matched. These observations motivate using the declared
budget; they are not measurements of the adjusted rule or formal speedup claims.
The regressions retain zero-budget value/byte growth and separately verify that
positive-budget replay starts at the declared capacity with exact CPU parity.
`GAGP_GPU_PAYLOAD_WORKSPACE_MIB` overrides the physical arena scratch cap for one
GPU fitness evaluation. The default is 512 MiB; explicit values must be decimal
integers in `[64,16384]` (for example `2048`, `8192`, or `16384`). Empty, signed, fractional,
unit-suffixed, whitespace-containing, out-of-range, and overflowing values return
an explicit `Value` error. No device/system setting is changed. The chosen cap is
reported as `payload_workspace_budget_bytes` in fitness results and fixed-ASGP
snapshots.

The same cap applies to initial global arenas and all replay attempts; their
large buffers never coexist. Arena frames, memo storage, entries, value cells,
string bytes, indexes, overflow diagnostics, queues, and replay indices/partials
are included in this scratch accounting. Allocation uses only complete physical
32-thread workers that fit after metadata, with overflow-safe capacity arithmetic;
if even one worker cannot fit, evaluation fails explicitly. Larger caps increase
concurrency without changing per-case logical allocation limits, fuel, program
selection, or case coverage. Ordinary resident inputs/program tables, output
buffers, CUDA context, and other execution workspaces are additional device
memory, so total device usage can exceed this arena cap. With enough pending
work, 8/16 GiB caps can consume approximately 8/16 GiB of arena scratch. Treat those
as explicit per-run A/B settings, not a new default or an established speedup.
The parser and exact-cap arithmetic are checked by `gagp_test_fitness_contract`.

`GAGP_GPU_REUSE_PRIVATE_WORKSPACE=1` optionally retains one session-owned device
allocation for admitted private compact IntList arena cells, frames and memo
storage. Initial evaluation and synchronized replay repartition that same
allocation; there are no separate retained stage buffers. Every stage charges
the entire retained capacity, including unused capacity, plus its current
metadata, queues, indices and partial scores against the configured arena cap.
Reduced headroom or a larger request evicts the old allocation before replacing
it. Existing worker counts, logical capacities and complete case coverage stay
unchanged. Dirty storage retains the VM's existing per-invocation initialization.

The default is off; unrelated profiles evict retained arena storage and use their
existing paths. Reinitialization releases it on its owning device before device
selection. Destruction also selects its owning device and restores the caller's
device. Completed calls may reuse storage; an active pooled evaluation rejects
same-session evaluation/reinitialization from its overlap callback. The backing
stays alive through that callback and all initial/replay synchronization. Profiles
append `reuse-private-workspace` or `reuse-private-workspace-declined`. Results
report retained bytes, successful backing allocations in that evaluation, and
the maximum accounted pooled arena bytes. Allocation, eviction and transfer
costs remain in normal evaluation/session timing.

`GAGP_GPU_SMALL_INT_HASH=1`, together with `GAGP_GPU_FUSED_LIST_CONCAT=1`,
optionally replaces the shallow element-hash calculation for Int values in
[-256,255] with an exact 512-entry table lookup. The 4 KiB table is initialized at
compile time in device global memory and uses read-only cached loads; it is not
CUDA constant memory and needs no per-evaluation upload or initialization.
Other values and tags use the original hash routine. Outer list hashing,
payload allocation, fuel, and numeric semantics are unchanged. The execution
profile adds `-small-int-hash`. The extended payload GPU test checks every table
entry, adjacent bounds, full-width integer patterns, all other tags, typed-list
Concat errors/capacity/collisions, and full-case CPU fitness with the flag off/on.

The initial global arena uses 1024 registry entries, 8192 Value cells, and 8192
string bytes per physical lane within the configured workspace cap. Snapshots
record `initial_payload_entry_capacity`, `initial_payload_value_capacity`, and
`initial_payload_physical_blocks` (zero when the global arena is unused).
Original GPU overflow replay and its adaptive capacities remain available.
Tests retain entry-only and value-only overflow, unaffected programs, and final
case/error/timeout coverage at these fixed initial capacities.

`GAGP_GPU_TILED_INITIAL_PAYLOAD=1` independently forces the existing replay
warp-tiling mechanism for initial global-arena evaluation. The value `auto`
enables it only when the maximum `IntList` input length is at least 32 elements
(one warp). This immutable session maximum is derived during initialization
from effective packed local bindings, after duplicate bindings overwrite earlier
values and out-of-range locals are ignored. Scalar inputs, shorter or empty
`IntList` inputs, other list tags, list answers, and program-created lists do not
activate `auto`. The threshold is a data-derived performance heuristic, not a
correctness or supported-input limit; program/task names do not affect it. Tasks
are program × logical warp; each still evaluates the original lane's complete
case partition.
The same per-lane accumulation, ascending-lane warp reduction, and ascending-warp
final reduction preserve canonical fitness at both full and partial logical
warps. Initial counters are initialized once before all tiles. An additional
8 bytes per task of partial scores is included in the initial scratch cap and
freed with that arena before any replay allocation. Reduction is enqueued before
the overlap event/callback, and its cost is included in initial kernel timing.
The original overflow replay route remains unchanged. The default is off; the
result/snapshot records `initial_payload_tiled`, and the execution profile adds
`-initial-tiled` when active. `1` still forces tiling, and unset, `0`, or other
values retain the untiled launch. Tests compare off/forced/auto full-case Discounted
fitness, status counts, original-lane diagnostics, fuel boundaries, and callbacks
at 33 and 512 logical lanes, input lengths 31/32, normalized input selection,
and fixed initial-capacity/replay coverage. Sorted arena tests also check auto with
overflow replay and callbacks.

`GAGP_GPU_CONCAT_CACHE=1` with `GAGP_GPU_FUSED_LIST_CONCAT=1` enables a
32-slot direct-mapped Concat result cache only in pointer-backed initial/replay
arenas. Exact keys contain both resolved backing pointers, their lengths, and
the typed-list tag; public token equality alone cannot produce a cache hit.
Entries retain the full canonical hash and immutable local result backing.
Hits preserve the original logical charge, physical value-capacity check,
reserved cell count, and ordinary first-match registry registration. They skip
only copying and hashing, leaving unused reserved cells inaccessible. Missing
payloads, type errors, entry exhaustion and physical replay retain their normal
paths. The cache clears per invocation and whenever scalar-region rollback
rewinds either list entries or cells, including a cell-only rewind after dedup.
Cache storage counts against the configured payload workspace cap; initialization
and allocation remain inside the existing measured evaluation stages.

`GAGP_GPU_CONCAT_CACHE_DIAGNOSTICS=1` optionally reports `concat_cache_hits`,
`concat_cache_misses`, and `concat_cache_skipped_cells` in snapshot/generation
records. Counts include failed initial attempts and all replay attempts; they
count cache probes after the normal value-capacity check, including probes whose
later registry registration fails. Three atomics per arena invocation aggregate
these counters only when diagnostics are enabled. No diagnostic atomics run by
default. The execution profile adds `-concat-cache` when an arena route runs.
`gagp_test_extended_payload_gpu` covers repeated operands, cache and canonical
identity collisions, global/local shadowing, typed errors, capacity, rollback,
case lifetime, CPU fitness and unchanged physical-overflow replay.

`GAGP_GPU_SORT_ARENA_CASES=1` independently sorts arena cases by total effective
input IntList length, stably preserving ties. Eligibility requires Levenshtein
with Capped proximity, finite integral `abs(penalty)`, and
`case_count * max(1, abs(penalty)) < 2^40`. Unit edit distances, capped scores,
and all error ceilings are then integral; every partial sum stays exactly
representable under the 48-bit canonical reduction. Summary parameters are not
used by Levenshtein; TupleSummary, Discounted, and all other metrics retain their
original order. This guard does not depend on program output type or widen the
existing view-executor proof.

The separate permutation is captured and uploaded at session initialization,
with construction/upload included in initialization timing. It adds four bytes
per case of immutable session input storage (4096 bytes for 1024 cases), outside
the arena scratch cap just like existing input tables. Initial global evaluation
and both arena replay flavors consume it without changing contiguous logical
case partitions, case multiplicity, or the original input/expected pairing.
Local initial evaluation remains unsorted. Overflow lane masks continue to refer
to logical execution lanes: with global sorting these contain sorted case
positions, not the original case positions. Result/snapshot fields
`arena_cases_sorted` and `arena_case_order_bytes` disclose usage/storage; active
arena sorting adds `-arena-sorted` to the profile. Default is off. Tests cover
full 1024-case CPU fitness/status parity at 33/512 lanes, tiled/non-tiled initial
evaluation, indexed/nonindexed replay, and excluded fractional/Discounted policies.


### Optional private arena index epochs

`GAGP_GPU_PAYLOAD_EPOCH_INDEX=1` selects a 64-bit epoch/entry index for indexed
GPU arena execution and replay. Unset, `0`, or other values retain the 32-bit
index. This changes physical index maintenance only: canonical first-match
identities, payload lookup, logical allocation, fuel, case coverage and native
GPU reproduction remain unchanged. Active execution profiles append `-epoch`
after `-list-index`; this is an experimental setting, not a measured speedup claim.

Each physical lane owns an index and unsigned epoch counter across sequential
cases, program-queue tasks and case tiles. Workspace allocation initializes both
once inside evaluation timing. A new case advances the epoch; slots from older
epochs are empty. Unsigned wrap clears that lane's table before reusing epoch 1.
Scalar-region rollback advances the epoch and rebuilds surviving entries in
registration order, preserving collision probe chains and first-match semantics.
Scoring finishes while the current case's payload remains resident; error exits
need no cleanup before the next epoch. Separate replay allocations initialize
independent tables/counters. No index state crosses workspace ownership.

Workspace budgeting includes eight bytes per index slot (two slots per entry)
and four bytes per physical lane for the counter, instead of four bytes per slot.
Allocation/initialization failure follows the ordinary workspace failure path.
The additional lookup traffic and reduced resident block capacity can offset the
avoided per-case clears; performance requires a full-case comparison.

Validation: `gagp_test_extended_payload_gpu` includes direct legacy/epoch
collision, rollback, error reuse and wrap tests, plus epoch-enabled CPU parity
for queue reuse, initial case tiling, scalar rollback and growing arena replay.

Tiled arena evaluation stores each logical warp's 32 partial scores at tile-local
indices and reduces them in the original order. Its dynamic shared allocation
reserves those 32 slots plus any active phase cache. Untiled arena evaluation
still reserves all logical fitness lanes, independent of its physical warp size;
shrinking untiled storage to the 32 physical threads is unsafe. The phase cache
starts after the score slots. Case partitions and canonical reductions are unchanged.

### Optional coalesced fitness counters

`GAGP_GPU_COALESCED_COUNTS=1` accumulates integer case, wrong, error, timeout
and fallback counts per physical lane, then combines them within each physical
warp before updating the program's counters. Partial warps and lanes with no
cases remain covered. Unset or other values retain per-case atomic updates.
The profile appends `-coalesced-counts`; logical case partitions, floating-point
fitness accumulation/reduction, payload overflow reporting and replay resets
are unchanged. This is an execution optimization, not a search-rule change.

GPU accumulator canonicalization also returns integral values in the closed
interval `[-2^48, 2^48]` directly: the existing 48-bit quantization retains all
such integers exactly. Fractional and larger values use the original rule.
`gagp_test_fitness_accumulator_gpu` compares both rules at the device bit level;
fitness parity covers partial physical warps and errors/timeouts, and the native
phase-population test compares complete counters across changing private lists.

### Optional exact reuse of repeated integer cases

`GAGP_GPU_INTEGER_CASE_REUSE=1`, fixed at GPU session initialization, groups
identical effective bindings and expected values. It requires at least128 cases,
at least fourfold repetition, Exact or Absolute/Capped fitness, integral error
ceilings, and a bounded total absolute score below `2^40`. Every evaluation must
also pass the existing whole-population Int/Bool/list-view type-flow proof;
generic, Float, fractional, TupleSummary and arena routes retain ordinary work.

The typed view kernel executes one representative with its integer multiplicity.
Fitness, wrong, errors and timeouts preserve every original case's contribution.
Integral weighted sums are exact under the original48-bit accumulator, permitting
a smaller physical block without changing a score. No examples are discarded,
no labels are changed, and no result is reused across programs or generations.
Fuel and logical resource limits remain per invocation. The profile appends
`-integer-case-reuse` or `-integer-case-reuse-declined`; `physical_case_count` and
evolution `physical_program_cases` disclose initial physical work separately
from the original logical case counts and overflow replay work.

Host fitness-contract tests cover multiplicities, differing targets/missing
bindings and rejected policies. The native GPU phase-population regression
compares32 changing generations against full-case GPU and independent CPU scores.

### Guarded integer arithmetic and simple private phases

GPU integer division and modulo use a shared helper in
`integer_arithmetic.cuh`. Zero, unit and signed-power-of-two divisors retain
their protected/wrapping rules; operands that both fit signed32 use a32-bit
operation after the overflow cases have been handled. Other operands retain
full64-bit arithmetic. The device oracle compares all four variants against
the CPU rules, including `INT64_MIN`, negative divisors and full-width values.

`GAGP_GPU_SIMPLE_PHASES=1` optionally executes a proven single-operation private
phase directly. Fresh metadata recognizes one to three bound/literal operands,
their optional type checks, one admitted integer/list primitive and optional
Return. Complete private typeflow and global IntList normalization are required;
other phases retain the existing VM. Each original instruction consumes its
fuel in order, used parameters retain lazy name/type checks, and list primitives
use the same allocation, charging and overflow-replay helpers. No phase result
is memoized and reproduction is unchanged. Metadata is cleared and rebuilt
after every generation. The default is off; profiles append `-simple-phases`
or `-simple-phases-declined`. `simple_phase_count` counts admitted static phases,
not dynamic invocations. Native phase-population checks compare enabled/disabled
execution, low-fuel behavior and independent CPU scores over changing generations.

### Optional shape-only root result cache

`GAGP_GPU_SHAPE_RESULT_CACHE=1`, fixed at session initialization, partitions
cases by all effective scalar bindings, assignment bits, tags and IntList
lengths. At least128 cases and fourfold repetition are required, with at most256
groups. Fresh complete typed-view and direct-root admission is followed by a
closed primitive check over every phase. Element reads, container identity
operations, list-key memoization and non-scalar results decline. Unresolved or
saturated65536-cell backing extents and public offset descriptors also decline.
A verified literal-true base predicate needs only its base body and boundary:
cut/request/combine phases remain unreachable, retaining the original predicate
fuel and timeout checks. A content read in a reachable phase still declines.

For an admitted program, a block executes the original root once per input
group, with the original invocation fuel, and stores only its scalar result or
error in shared memory. Each original case then uses its own expected answer,
status counters, original reduction lane and ordered canonical score update.
Fractional scoring is therefore retained without a new weighted reduction.
Private payload descriptors are never cached. No result survives a program or
generation boundary. Existing repeated-case scheduling must still offer at
least fourfold additional work reduction before this cache is used.

The profile appends `-shape-result-cache` or `-shape-result-cache-declined`.
`shape_result_programs` and `shape_result_groups` disclose admission;
`physical_program_cases` counts actual initial root executions across the
population, while `physical_case_count` retains the earlier case-scheduling
count. Logical case counters and overflow replay remain separate. Native tests
cover different list contents/labels, content-dependent exclusions, fractional
scoring, fuel boundaries, saturated payload extents and changing populations.

### Optional512-thread view register specialization

`GAGP_GPU_RELAXED_VIEW_REGISTERS=1` selects an additional typed direct-root
kernel compiled with a512-thread launch bound when its actual physical block
fits. It gives the compiler a different register/occupancy tradeoff. The actual
block width, logical case lanes, instruction/fuel semantics, score reduction,
operators and workspace cap are unchanged. Wider blocks retain the original
1024-thread-capable kernel. Arena execution retains its original32-thread bound.
Profiles append `-view-reg512` or `-view-reg512-declined`. Dedicated differential
tests cover enabled/disabled512 execution and the1024 fallback; changing-generation
CPU audits exercise the specialization as well. A resource or speed improvement
must be measured; the launch-bound change alone is not evidence of a gain.


### Optional projected shape inputs

With `GAGP_GPU_SHAPE_RESULT_CACHE=1`, session-fixed
`GAGP_GPU_PROJECT_SHAPE_INPUTS=1` can omit unused caller locals from input-shape
keys. Fresh direct-root LOADs always remain dependencies. Each reachable phase
adds Parameter bindings only when its bytecode LOADs that binding; a verified
literal-true base predicate excludes its unreachable cut/request/combine.
Boundary dependencies remain included. Complete existing typeflow is still
required; this dependency check cannot grant admission to a missing or wrongly
typed parameter that the existing profile declines.

The session retains partitions by64-bit input mask, including declined masks,
with at most64 partitions and4MiB total GPU group/representative metadata.
GPU metadata is uploaded again only when an admitted partition is added.
Reinitializing a session clears every partition. Program metadata selects its
own group offsets/count; shared result storage fits the largest admitted group.
Actual root executions remain in `physical_program_cases`;
`shape_result_groups` reports the maximum admitted count in that evaluation.
No scalar result/error survives a program or generation boundary. Per-case
labels, logical coverage, original ordered score updates and the additional
fourfold-saving threshold remain unchanged.

### Optional private arena compilation geometry

`GAGP_GPU_PRIVATE_LAUNCH_BOUND` accepts0(default),32,512 or1024. The setting
chooses a separately compiled indexed-arena kernel for admitted private lists;
both initial evaluation and indexed payload replay use the selected bound.
It changes the compiler's register allocation constraint. Physical launches
remain32 threads, with the same workspace, logical score lanes, case order,
fuel and allocation limits. Nonprivate programs keep the ordinary specialization.
Invalid values return a configuration error; profiles append
`-private-launch32/512/1024` when the private arena path applies, otherwise
`-private-launch-declined`. Static register counts and runtime gains require
measurement; this option does not imply higher occupancy or faster execution.

`gagp_test_bounded_region_gpu` compares CPU results, counters, low fuel, overflow
replay and private window boundaries for512/1024. The phase-population regression
also compares these kernels across changing GPU-generated populations.

### Optional small phase register banks

`GAGP_GPU_REGISTER_PHASE_STACK=1` adds a typed phase implementation for fresh
verified stack bounds1..4 and at most8 locals. It uses explicit64-bit fields for
four operand slots and eight local slots, retaining the original bytecode,
branching, builtin allocation, fuel and error ordering. Used input presence and
private parameter type checks remain lazy. Other phases retain the array-backed
VM; generic/Float programs do not acquire typed admission through this flag.
The profile appends `-register-phase-stack` or its `-declined` suffix. Fixed
fields permit compiler register allocation; they do not guarantee a resource or
speed improvement. Device storage-oracle, low-fuel, private list, hybrid stack
and changing-generation CPU/GPU tests cover this path.

### Optional physical view blocks with fixed logical score lanes

`GAGP_GPU_VIEW_PHYSICAL_BLOCK` accepts0(default),32,64,128,256 or512. Typed view
execution can use up to that many physical threads while retaining the original
logical fitness lane count. Each physical thread visits separate logical lanes;
each logical lane retains its original ordered case accumulation. Strided case
iteration advances by logical lanes, and shared score storage reserves every
logical lane before the original warp/final reduction. Private arenas retain
their existing32-thread execution and replay rules.

This setting changes concurrency and scratch slot geometry, not case coverage,
score reduction, fuel, reproduction or search parameters. Workspace allocation
uses physical threads; complete logical score slots remain in shared memory.
The profile records `-physical-viewN` or `-physical-view-declined`;
`physical_blocksize` records the initial launch width while benchmark
`gpu_blocksize`/CPU-audit reduction lanes retain the configured logical width.
Tests cover partial case counts, sorted integral and ordered fractional scoring,
32/64/128/256 physical widths,1024 logical lanes, invalid settings and fresh
GPU-produced populations.


### Optional dense arena case scheduling

`GAGP_GPU_DENSE_CASE_LANES=1` applies to untiled global arenas only when
`0 < cases <= logical lanes`. Physical lanes visit consecutive case ordinals,
then scatter their scores to the original logical lanes. Unsupported counts
and tiled initial evaluation retain the ordinary mapping. The profile appends
`-dense-case-lanes` or `-dense-case-lanes-declined`.

`GAGP_GPU_DENSE_CASE_TILES=1` applies the same case mapping to tiled initial
arena evaluation and tiled overflow replay. Each program schedules
`ceil(cases / 32)` tiles instead of `ceil(logical lanes / 32)`, provided
`0 < cases <= logical lanes`. The `tuple` mode additionally requires admitted
private lists and their tuple-summary scoring policy. Other scoring policies
keep the ordinary tiles, using the same global setting across tasks.

A case ordinal `k` maps back to logical lane
`ceil((k + 1) * logical lanes / cases) - 1`, retaining the existing arena case
order. Dense tiles reserve a program-by-logical-lane score buffer inside the
configured payload workspace budget. Every slot is cleared before initial or
replay use, including reused storage. `reduce_dense_case_tiles` reconstructs
the original ordered lane-to-warp and warp-to-total canonical reductions;
physical tile order is not a replacement score reduction. Private arena entry,
value, fuel, overflow, error and reproduction limits remain unchanged.
The execution profile appends `-dense-case-tiles` or its declined suffix.

Tests in `gagp_test_extended_payload_gpu` and
`gagp_test_gpu_phase_population` compare ordinary/dense execution with an
independent CPU reference, fractional scores, partial warps, unsupported case
counts, low fuel, overflow replay, dirty storage and changing populations.
Dense scheduling is experimental; fewer tiles do not establish a speedup.

### Optional case-based view geometry and narrow compilation

`GAGP_GPU_CASE_VIEW_BLOCK=1` caps admitted typed-view physical blocks at
`max(32, next_power_of_two(cases))` when `4 * cases < logical lanes`.
The `hybrid` mode retains this sparse rule. For other positive case counts it
caps blocks at128 only when no packed phase has a bound-add operand; phases
with bound addition retain the logical width. An explicit
`GAGP_GPU_VIEW_PHYSICAL_BLOCK` remains an upper bound. These rules inspect
current case counts and compiled features, without task names or test labels.
Both modes preserve original logical lanes, case accumulation and reduction.
Profiles report `-case-view-blockN` or `-case-view-block-declined`.

`GAGP_GPU_VIEW_BOUND128=1` selects a separate128-thread compilation bound only
for an admitted direct-root view launch of at most128 physical threads.
Larger launches and other execution paths retain the existing kernels. When
this specialization is selected, the optional relaxed512 compilation is
reported as declined. The profile appends `-view-bound128` or its declined
suffix; no register, occupancy or timing improvement is implied.

`gagp_test_bounded_region_gpu` and `gagp_test_gpu_phase_population` cover
physical/logical width separation, bound-add versus non-add admission,
fractional scoring, low fuel and fresh populations.

### Optional compilation of an admitted private layout

`GAGP_GPU_SPECIALIZE_PRIVATE_LAYOUT=1` exposes an already admitted compact
private-list layout as compile-time facts in a separate indexed-arena kernel.
It requires the32-thread compilation bound, compact contiguous cells rather
than cell SoA, private global offsets, warp copy, trivial/typed/bound phases
and the private window executor. `PayloadWorkspaceStorage::prepare` already
clears coordinate/unboxed/SoA view modes and private per-lane registries;
the specialization makes those existing facts visible to inlined device code.
Immutable external tables and runtime type, boundary, fuel and allocation
checks remain in use. Unsupported layouts use the ordinary kernel.
Initial evaluation and overflow replay use the same guarded dispatch.

Profiles append `-private-layout-specialized` or its declined suffix.
`gagp_test_gpu_phase_population` checks32 fresh generations against CPU and
ordinary execution, dirty reusable storage and unsupported-layout fallback;
`gagp_test_extended_payload_gpu` checks mixed/public payload fallback. These
are execution checks, not a claim about every intermediate evolved individual
or a guaranteed performance benefit.
