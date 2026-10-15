# C++ Runtime Payload Model

This document explains how C++ runtime container values work under `cpp/` and how the exact and fallback paths interact.

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

- upper 16 bits: saturated length
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
integers in `[64,8192]` (for example `2048` or `4096`). Empty, signed, fractional,
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
work, 2/4 GiB caps can consume approximately 2/4 GiB of arena scratch. Treat those
as explicit per-run A/B settings, not a new default or an established speedup.
The parser and exact-cap arithmetic are checked by `gagp_test_fitness_contract`.

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

`GAGP_GPU_INITIAL_PAYLOAD_SCALE` is an optional bounded initial-global-arena
experiment. Its only accepted values are `1` (default), `2`, `4`, and `8`. It
multiplies the initial 1024 registry entries and 8192 Value cells per physical
lane; string bytes remain 8192. It does not enable global payload evaluation
itself. Every allocation still fits the same configured workspace cap; larger
lanes can reduce physical-worker concurrency. Original GPU overflow replay and
its adaptive capacities remain available, with no CPU fallback or changes to
case coverage, logical allocation limits, hashes, fuel, or scoring.

The option is captured at evaluation entry. Snapshots record
`initial_payload_entry_capacity`, `initial_payload_value_capacity`, and
`initial_payload_physical_blocks` (zero when the global arena is unused); an
explicit nondefault scale adds `-initial-scaleN` to the execution profile.
Compare full-search time: avoided duplicate initial work competes with reduced
concurrency, and initial evaluation assigns one worker per program whereas tiled
replay can distribute its logical lanes across workers. Replay time is therefore
not all avoidable overhead. Parser tests cover malformed values; GPU parity tests
cover entry-only and value-only overflow, successful larger initial capacity,
remaining replay, unaffected programs, and final case/error/timeout counts.

`GAGP_GPU_TILED_INITIAL_PAYLOAD=1` independently applies the existing replay
warp-tiling mechanism to initial global-arena evaluation. Tasks are program ×
logical warp; each still evaluates the original lane's complete case partition.
The same per-lane accumulation, ascending-lane warp reduction, and ascending-warp
final reduction preserve canonical fitness at both full and partial logical
warps. Initial counters are initialized once before all tiles. An additional
8 bytes per task of partial scores is included in the initial scratch cap and
freed with that arena before any replay allocation. Reduction is enqueued before
the overlap event/callback, and its cost is included in initial kernel timing.
The original overflow replay route remains unchanged. The default is off; the
result/snapshot records `initial_payload_tiled`, and the execution profile adds
`-initial-tiled` when active. Tests compare flag off/on full-case Discounted
fitness, status counts, original-lane diagnostics, fuel boundaries, and callbacks
at 33 and 512 logical lanes, plus the initial-capacity/replay matrix.

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
