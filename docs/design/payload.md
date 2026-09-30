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

The GPU fitness path launches one kernel over the full accepted population. An opt-in, structurally proven integer/list view profile is described below.

The finer `StringOnly` / `ListOnly` labels are kept for experiment tooling and offline bucket studies rather than the production eval dispatch tree.

Exact string/typed-list builtins use bounded per-thread scratch. CPU and GPU share the release 2.0.0 direct-list tags (`IntList`, `FloatList`, `StringList`). When exact string output materialization will not fit in GPU per-thread scratch, GPU string operations use the fallback path. Direct-list operations preserve the list tag and compact hash/length token even when the exact expanded payload cannot be materialized in thread-local scratch.

There is no legacy payload-free/payload-bearing bucket split. The whole launch
uses `Mixed` or the proven view profile, and evaluates every program/case pair.

### Proven integer/list execution profile

`cpp/src/runtime/gpu/view_profile.hpp` checks forward type flow after full ordinary
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

`GAGP_COMPACT_FRAMES=1` uses 72-byte frames for at most one state, or 88-byte
frames for at most two states, when every segment also needs at most one
preparation and two results; other shapes use 264-byte generic frames. Frame
and memo capacities do not decrease. The 512 MiB workspace limit controls launch
concurrency, never evaluation coverage. `gagp_test_bounded_region_gpu` checks the
fast path, generic fallback, numeric boundaries and exact fuel/capacity exhaustion.


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
