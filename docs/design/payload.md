# Payload Values: Strings and Lists

`String`, `IntList`, `FloatList` and `StringList` values have contents of
variable size, but `gagp::Value` must stay a small, trivially copyable struct
that moves through VM stacks, constant pools and CUDA transfers. This document
explains how such *payload values* are represented on the host and on the
device, and where exactness ends. The semantics of the operations are in
[builtins_base.md](../../spec/builtins_base.md) and
[builtins_runtime.md](../../spec/builtins_runtime.md).

## Two layers

1. A `Value` holds a compact **token**: in `Value.i`, the upper 16 bits are the
   encoded length and the lower 48 bits a deterministic hash
   (`pack_container_payload`, `container_len`, `container_hash48` in
   [value.hpp](../../cpp/include/gagp/core/value.hpp)).
2. A process-global **payload registry** maps `(tag, token)` to the real
   contents ([payload.hpp](../../cpp/include/gagp/runtime/payload/payload.hpp),
   [payload.cpp](../../cpp/src/runtime/payload/payload.cpp)), under one mutex.

Construct values with `payload::make_string_value` and
`make_int_list_value` / `make_float_list_value` / `make_string_list_value`.
They compute the token, register the contents and return the `Value`. The
typed-list constructors are strict: an `IntList` accepts only `Int` elements,
and so on; nothing is widened or narrowed.

Registry maintenance:

- `lookup_index` copies one element under the mutex; it fails for an absent
  payload or an index out of range.
- `retain_only(roots)` keeps the payload closure reachable from a set of
  values, including strings inside a `StringList`. The evolution loop roots the
  current population, the cases and the grammar's constant domains.
- `clear()` drops contents; existing tokens then have no payload.

## On the GPU

Device code never reads the host registry.

1. `FitnessSessionGpu::init` uploads the cases, their payloads and the
   expected answers once per session.
2. Each `eval_programs` call gathers the tokens the evaluated programs need,
   fetches missing ones from the registry into a session cache, and flattens
   that closure (including strings inside `StringList`s) into sorted, read-only
   tables (`build_payload_pack` in `fitness_gpu.cu`).
3. One grid-stride kernel evaluates every (program, case) pair. New strings and
   lists created during execution live in per-lane scratch.

The launch picks one execution profile for the whole batch, reported in
`FitnessEvalResult::execution_profile`:

| Profile | Used when | Storage for new payloads |
| --- | --- | --- |
| `int-list-views-typed` | A forward type-flow proof (`cpp/src/runtime/view_profile.hpp`) shows lists are only read through `len`/`index`/`slice` and never escape | None: lists are offset/length views into the read-only tables |
| `extended-payload` | Programs construct strings or lists | A per-lane arena: 8192 list values, 1024 list entries, 8192 string bytes, 1024 string entries (cumulative, including intermediates) |
| `mixed` | Everything else | Bounded per-thread scratch |

Further exact specializations are enabled automatically when a population
qualifies: private `IntList` arenas without registry hashing, adjacent-span
concatenation, warp-cooperative copies, fused concatenation, and typed
interpreters for small phases (`fitness_gpu.cu`, around the
`DRegionWorkspace` setup). Each is admitted only after a proof over the whole
population; otherwise the generic path runs. Their suffixes are appended to
the execution profile. A session memoizes the bounded-tuple allocation proof
per code block, keyed by the complete code, constants and local bounds (blocks
that read `IntList` constants are always analyzed afresh).

Transport buffers freed after an evaluation are kept in power-of-two buckets
per device (blocks up to 256 MiB, at most 1 GiB cached) and reused by the next
evaluation (`device_allocation.cu`). The cache is released when an allocation
fails and before the resident engine measures free memory.

### Overflow replay

Running out of physical scratch is not a semantic error. The kernel marks the
affected programs, and they are re-run on the GPU with `ArenaMixed`: storage in
a bounded global workspace, starting at 65536 entries and list values per
invocation (at least the task's `list_allocation_limit`) and doubling as
needed. Replay runs 32 threads per program over the original lane chunks with
the same reduction order, so its scores equal those of an unlimited run.
Replay scratch is limited to 512 MiB beyond the payload workspace (16 GiB,
`payload_workspace_budget.hpp`); if a single block cannot fit, the evaluation
fails rather than scoring a physical failure. Logical limits never grow.
Replayed programs, cases and launches are counted in `FitnessEvalResult` and
the run timing.

### Region workspace

Bounded regions keep frame and memo cells in explicitly owned global device
slices (512 MiB, reused across evaluations; memoized answers are never carried
between programs). Payload scratch stays live through all descendants and memo
hits; a region with a scalar result may restore the scratch counters once it
completes.

## Exact path and fallback

While the contents are available, container operations are exact on both
CPU and GPU: `concat`, `slice`, `append`, `reverse` build the real value;
`find` and `contains` inspect strings; `index` returns the element with its
type (`Char` for a `String`, `Int`/`Float`/`String` for the typed lists).

When contents are unavailable, results are deterministic but opaque:

- string `concat`, `slice`, `reverse` and every `index` return a
  `FallbackToken`;
- list operations return a token of the correct list tag with a deterministic
  hash and length;
- `find` and `contains` return a `ValueError`.

Contents can be unavailable when a token was built directly
(`Value::from_int_list_hash_len` and similar) without `payload::make_*`, after
`clear()` or `retain_only()`, or when a string result does not fit GPU
scratch. Fallback values compare as mismatches in fitness, so CPU and GPU
still agree, but fallback is not semantics-preserving.

Region phases may return a fallback token where the verifier expects a
nominal type; siblings must agree on the actual tag and `combine` must keep
it. Tests of direct execution must distinguish a fallback from a type error
and check consumed fuel, because both can receive the same fitness penalty.

## CPU fitness overlay

Each CPU fitness case runs inside a discard-only payload overlay: generated
strings and lists are visible until the case is scored and then released,
while case, answer and program payloads stay. The overlay allows 8 MiB of
generated data per case; exhausting it is a `ValueError` and the normal case
penalty. This is a CPU resource bound, not a claim that the GPU arena has
equal capacity.

Grammar variation can stage payload writes in a `StagedPayloads::Scope`
(`cpp/src/runtime/payload/staging.hpp`): writes stay private until committed
under the registry lock, and a conflicting write rejects the whole batch.

## Limitations

- **Collisions.** Tokens are 16-bit length plus 48-bit hash; two different
  contents with the same tag, length and hash alias the same registry key, and
  a later registration overwrites the earlier one. This is a compactness
  trade-off, not a collision-proof design. Private `IntList` arenas compare
  contents and do not depend on the hash.
- **Shallow hashing.** List hashes combine element `Value`s, not recursively
  expanded contents. Nested lists are not part of the value domain.
- **Length encoding.** Lengths above 65535 saturate in the token; the payload
  itself keeps the true length.
- **Global lifetime.** The registry is process-global; callers that sweep it
  must root every value they still need.

## Reading order

1. [cpp/include/gagp/core/value.hpp](../../cpp/include/gagp/core/value.hpp)
2. [cpp/include/gagp/runtime/payload/payload.hpp](../../cpp/include/gagp/runtime/payload/payload.hpp)
3. [cpp/src/runtime/payload/payload.cpp](../../cpp/src/runtime/payload/payload.cpp)
4. [cpp/src/runtime/cpu/builtins_cpu.cpp](../../cpp/src/runtime/cpu/builtins_cpu.cpp)
5. `cpp/src/runtime/gpu/fitness_gpu.cu` and `cpp/src/runtime/gpu/device/`
