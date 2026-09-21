# GPU Reproduction Backend

## Purpose

The GPU reproduction backend moves one full generation of selection and variation off the CPU hot path while preserving the public GP contract:

- public program representation remains prefix `AstProgram`
- public selection remains tournament-based and controlled by `selection_pressure`
- public crossover remains `typed_subtree`
- public mutation remains the single public mutation path controlled by `mutation_subtree_prob`

The backend is designed to reduce one-generation benchmark cost and to support overlap with GPU fitness evaluation. It is a performance implementation detail, not a new public GP dialect.

Canonical timing names and CLI/JSON output mapping are documented in
[timing.md](../reference/timing.md).

## Public Controls

The public control plane is:

- `--repro-backend {cpu|gpu}`
- `--repro-overlap {on|off}`

The benchmark script exposes four formal modes built from those controls:

- `cpu`
- `gpu_eval`
- `gpu_repro`
- `gpu_repro_overlap`

`gpu_repro_overlap` overlaps reproduction input preparation with GPU fitness evaluation. The overlap changes timing visibility, not the public reproduction API.

## Pipeline

One `gpu` reproduction pass is split into the following stages.

### 1. Prepare Inputs

Source files:

- [gpu.cpp](../../cpp/src/evolution/repro/gpu.cpp)
- [prep.cpp](../../cpp/src/evolution/repro/prep.cpp)
- [types.hpp](../../cpp/include/gagp/evolution/repro/types.hpp)

The host extracts:

- the current population
- scored fitness values
- backend configuration limits

It then computes the preprocessing data needed by device kernels:

- subtree end positions
- typed crossover candidate ranges
- typed donor pool entries, bucketed by result type for subtree mutation

If `--grammar-config PATH` is active, this preprocessing stage filters typed candidate ranges to grammar-allowed subtrees and builds the donor pool with the selected `grammar-config` search-space controls. Checked-in `grammar-config` presets remain accepted as compatibility input and are translated before use. Generated `compact` configs with legacy `num_list_mode=both` also seed exact numeric-list fixture inputs as `Any` for search-space compatibility. Runtime execution remains the full public grammar superset with exact fixture values; the config only controls search-space generation.

When ASGP forms are enabled by the grammar config, donor preprocessing may
synthesize conservative ASGP donors. ASGP-DC donors may appear in the `Int`,
`Float`, and `String` donor buckets; the `String` slice traverses a `String`
source as chars and rebuilds strings with `singleton(index(...))` and
`concat`. ASGP-DP1D and ASGP-DP2D donors may appear in the `Int`, `Float`,
and `String` donor buckets; the `String` DP slices use `concat` transitions
over memoized dependency results. These donors use the same host-side typed expression generator
as CPU subtree mutation and are still subject to normal donor-size,
grammar-config, and metadata packing limits.

This stage is reported as:

- `repro_prepare_inputs_ms`
- `repro_preprocess_ms`

### 2. Pack

Source files:

- [pack.cpp](../../cpp/src/evolution/repro/pack.cpp)
- [pack.hpp](../../cpp/include/gagp/evolution/repro/pack.hpp)

The host flattens the typed AST population into bounded GPU-friendly arrays:

- plain node buffers
- per-program metadata
- candidate tables
- compact name id tables
- compact constant tables
- `LinearRec` binder side tables for parent and donor programs
- ASGP-DC, ASGP-DP1D, and ASGP-DP2D binder/spec side tables for parent and
  donor programs
- donor pool buffers

This stage is reported as `repro_pack_ms`.

### 3. Upload

Source files:

- [arena.cu](../../cpp/src/evolution/repro/gpu/arena.cu)
- [launch.cu](../../cpp/src/evolution/repro/gpu/launch.cu)

Packed host buffers are uploaded into a reusable device arena. The same arena is retained inside the process and grown only when capacity is insufficient.
Before sizing and packing, GPU reproduction compacts each AST's name and constant tables to entries referenced by live nodes. Decoded children and fallback parents are compacted again before becoming the next population so stale table entries from prior crossover or mutation rounds cannot accumulate past fixed kernel scratch limits. A decoded child is also rejected and replaced by its selected fallback parent if any live `String` or typed-list constant lacks a host payload, preventing payload-token-only ASTs from entering later fitness or replay output.

This stage is reported as `repro_upload_ms`.

### 4. Device Selection And Variation

Source files:

- [selection_kernels.cuh](../../cpp/src/evolution/repro/gpu/device/selection_kernels.cuh)
- [variation_kernels.cuh](../../cpp/src/evolution/repro/gpu/device/variation_kernels.cuh)

The device executes two kernel families:

- tournament selection kernel
- variation kernel

Selection preserves the same high-level tournament semantics as the CPU path:

- round-based selection
- chunk size controlled by `selection_pressure`
- without-replacement within each round
- chunk winner chosen by best fitness inside that chunk

The GPU kernel still emits one mating pair per thread, but its per-round permutation is an internal device implementation detail rather than a shared host/device plan. CPU and GPU are not required to use identical RNG streams or identical within-round permutations as long as they preserve the same public tournament contract.

Selection also chooses a typed crossover site pair for each mating pair by scanning the bounded candidate tables for parent A and parent B, finding a compatible typed-subtree key, and picking one candidate with that key from each parent. The packed key includes result type, visible scope signature, binder/scheme identity, ASGP phase identity, and ASGP-DP dependency arity so device-side crossover does not exchange same-result-type roots from incompatible lexical or phase contexts.

Variation then applies the same high-level order as the CPU backend:

- every pair first attempts `typed_subtree` crossover
- each resulting child independently samples mutation from `mutation_rate`
- if a child mutates, `mutation_subtree_prob` chooses subtree mutation vs constant perturbation

GPU subtree mutation uses a type-bucketed donor pool keyed by the selected
crossover-site type. The donor pool can include conservative ASGP-DC,
ASGP-DP1D, and ASGP-DP2D donors when the active grammar config enables the
matching ASGP form and its required value, list, or builtin features. ASGP-DC,
ASGP-DP1D, and ASGP-DP2D currently cover conservative `Int`, `Float`, and
`String` targets. Constant
perturbation is applied directly to the packed child constant table after
crossover.

Variation produces packed child buffers plus child metadata such as:

- node count
- max depth
- builtin usage marker
- validity bit

The device-side child metadata parser understands the release 1.0.0 structured-expression node set (`BoundVar`, `MapList`, `FilterList`, `LinearRec`, ASGP-DC, ASGP-DP1D, and ASGP-DP2D) so valid structured children are not rejected solely because they contain structured forms.

These kernels are reported as:

- `repro_kernel_ms`
- `repro_selection_kernel_ms`
- `repro_variation_kernel_ms`

### 5. Copyback

Source files:

- [copyback.cu](../../cpp/src/evolution/repro/gpu/copyback.cu)

The backend copies back only live child regions rather than fixed-capacity slabs. Host-side pinned staging is reused across generations to keep D2H cost stable.

Copyback also returns the selected parent and candidate indices for each pair. Decode uses this context to rebuild structured side-table metadata that is not represented directly in `PlainNode`.

This stage is reported as `repro_copyback_ms`.

### 6. Decode

Source files:

- [pack.cpp](../../cpp/src/evolution/repro/pack.cpp)

The host rebuilds `ProgramGenome` children from copied-back packed buffers. This includes:

- name id lookup
- AST node reconstruction
- constant reconstruction
- `LinearRec` binder side-table reconstruction
- ASGP-DC, ASGP-DP1D, and ASGP-DP2D binder/spec side-table reconstruction
- program key regeneration
- fallback to the selected parent if the child is marked invalid or decode fails
- fallback to the selected parent if host-side lexical binder validation finds
  an escaped `BoundVar` or missing structured-expression metadata

For `LinearRec`, decode keeps base binders outside the replaced range, shifts base binders after the replaced range, and inserts donor binders whose root lies inside the donor range. Donor binder names are remapped through the copied child name table, and binder-only names are appended on the host when they were not referenced by any copied AST node.

ASGP metadata decode follows the same replacement context. It keeps ASGP
side-table entries outside the replaced range, shifts surviving entries after
the replaced range, inserts donor entries whose root lies inside the donor
range, and remaps binder names and DP boundary constants through the copied
child tables. The same path handles donor-pool ASGP-DC, ASGP-DP1D, and
ASGP-DP2D donors produced by GPU subtree mutation.

This stage is reported as `repro_decode_ms`.

## Overlap Model

Source files:

- [evolve.cpp](../../cpp/src/evolution/evolve.cpp)
- [evolve_cli.cpp](../../cpp/src/cli/evolve_cli.cpp)

When `repro_overlap` is enabled and `--engine gpu --repro-backend gpu` is active, the implementation starts reproduction preparation in a background task while GPU fitness evaluation is running.

The overlapped portion is:

- `repro_prepare_inputs_ms`
- `repro_preprocess_ms`
- `repro_pack_ms`

The non-overlapped tail remains:

- upload
- device kernels
- copyback
- decode

Because of this, overlap is only valuable when the hidden preparation work is large enough and the CPU-side preparation does not noticeably slow concurrent GPU evaluation.

## Arena Reuse

Source files:

- [gpu.cpp](../../cpp/src/evolution/repro/gpu.cpp)
- [internal.hpp](../../cpp/src/evolution/repro/gpu/internal.hpp)
- [arena.cu](../../cpp/src/evolution/repro/gpu/arena.cu)

The backend keeps a process-local runtime cache containing:

- device arena buffers
- pinned host staging buffers

This changes the timing profile:

- `repro_setup_ms` is usually a first-use or growth cost
- `repro_teardown_ms` can remain zero in steady state

This reuse is required for overlap to be worthwhile; otherwise repeated `cudaMalloc` / `cudaFree` and host allocation churn dominate short runs.

## Correctness Model

The GPU reproduction backend is not required to reproduce the exact same child sequence as the CPU backend. It is required to preserve the public GP contract:

- children must decode into valid `ProgramGenome` objects or fall back deterministically
- compiled children must remain legal under the same runtime/compiler rules
- reproduction still follows the same public high-level order: select -> typed crossover on each pair -> child-level mutation
- public CLI semantics and benchmark accounting remain stable

Fitness parity remains a CPU/GPU requirement for evaluation. Reproduction identity is not a parity contract.

## Current Bottlenecks

The backend no longer spends meaningful time inside its device kernels on normal-size runs. The remaining cost is mostly host-side:

- `repro_decode_ms`: packed child reconstruction and program key rebuild
- `repro_preprocess_ms`: subtree/candidate/donor preprocessing
- `repro_pack_ms`: flattening host ASTs into bounded upload buffers

`repro_copyback_ms` and arena lifecycle cost are much smaller after live-region copyback and reusable staging were added.

In overlap mode, improvement is limited when:

- `repro_decode_ms` dominates the post-eval tail
- background preparation contends with CPU resources needed by evaluation orchestration

## How To Read Timings

For fixed-population benchmarks built on `gagp_evolve_cli --generations 1 --skip-final-eval on`:

- compare `total_ms` first
- then inspect `eval_ms`
- then inspect reproduction subphases

For overlap mode:

- do not add all reproduction subphases and expect them to equal `repro_ms`
- `repro_ms` is the residual wall-clock cost after any hidden work
- steady-state generation timings are more meaningful than cold-start single-shot runs

## Related Documents

- [development guide](../guides/development.md)
- [architecture](architecture.md)
- [payload model](payload.md)


## Compiled grammar preparation and execution

Compiled CPU reproduction owns one `grammar::VariationContext` per generation. It retains
an immutable grammar/request, validates all parents, and preserves tournament selection
and crossover-then-child-mutation order. For an odd CPU population, a fresh tournament
supplies the last pair's extra parent; the unused second child is discarded. Legacy CPU reproduction ablations are rejected
in this mode. The existing legacy grammar-config path remains the migration reference.

Host `preprocess_population(..., VariationContext&)` uses one registry for numeric
compatibility IDs, samples logical sites without replacement and stores every physical
occurrence of shared holes. Each occurrence addresses a checked slice of formal
binder IDs; separate donor slices preserve the source formal mapping. These tables
remain host-owned for metadata reconstruction during copyback. Candidate records
carry separate node/depth/template budgets
and source measurements. Per-site donor offsets address contextual donors that fit those
budgets; they do not use the legacy nine-type buckets. Donor payloads exclude standalone
expression envelopes and preserve local names and decoded constant values when packed.

Preparation retains grammar ownership and materialized population/donor identities.
Packing rejects stale preparation, checks spans and contracts, and prescans capacities
instead of truncating data. Compiled program metadata has an actual candidate count;
padded candidates are invalid. Current transport caps are 512 nodes, 128 names/constants
and a combined 256 MiB budget for padded buffers, constant-mutation tables and
logical source-snapshot storage (excluding allocator bookkeeping/capacity slack).
These caps are independent of CPU evolution's grammar limits. Generic packing retains
complete immutable parent/donor AST snapshots, including all nested sidecars and
original name/constant indices, through copyback. Copies of prepared inputs share
these snapshots; later population edits cannot change them.

Public synchronous compiled GPU preparation and execution run the complete two-pass
operator pipeline. Prepared inputs retain their variation context and reject changed
source programs, requests or operator settings. Crossover outputs pass acceptance
before a second preparation analyzes their actual mutation sites; mutation is skipped
when its configured rate is zero. Preparation reserves up to 128 names/constants so
variation can introduce table entries beyond the parents' current occupancy.
Compiled asynchronous preparation uses the same worker lifecycle as legacy mode.
The worker owns a population snapshot and its mutable variation context, and returns
both packed buffers and context through the future. It performs no CUDA work.
The main thread joins before selection, mutation, copyback or payload sweeping;
prepared source snapshots and donor payloads remain live through both passes.
The low-level allocation/upload/launch/copyback path now supports compiled
crossover and mutation passes. It validates dimensions before CUDA access, bounds the device arena
to 512 MiB, uploads constant domains/groups/origins/streams and donor contracts, then
runs numeric selection and atomic crossover. Two control lanes per selected pair
produce independent children. Missing candidates or a failed splice publish an exact
base copy; successful splices publish explicit provenance. Selection uses reciprocal
replacement budgets and actual candidate counts, with -1 for a missing pair.
The internal pass decoder performs cached native/grammar acceptance and classifies
operator attempts and child outcomes. Selection publishes per-pair contract/budget
rejection counts from its first candidate enumeration; rank selection does not count
them again and padding is excluded. Copyback reduces these rows into the retained
variation context. Configuration copies retain shared immutable grammar ownership.
A compiled GPU evolution run owns `GpuReproRunResources`, shared by synchronous
preparation, the overlap worker and both operator passes. Its immutable constant
domains, exact values and expression-to-domain map are materialized lazily once.
Per-pass groups, node origins and stream metadata retain that owner rather than
copying the domain arrays. Reusing a resource with a different grammar or request
is rejected; each generation still owns a private mutable variation context.

The payload lifetime manager includes all materialized domain values in every sweep,
including constants absent from the current population or fitness cases. This also
retains strings reachable through StringList domain values. Async preparation finishes
before the evolution loop sweeps the registry. Standalone resource users must include
these roots when performing their own sweeps.

The GPU arena retains the last uploaded domain owner. Static domain/value copies and
finite-value validation run once per owner and arena lifetime; arena recreation clears
this state. Dynamic occurrence/group/origin/stream/donor arrays upload every pass.
Production mutation retains constant-time device shape/index checks without scanning
all finite-domain values for each child. Direct low-level helper calls keep full
validation unless their caller explicitly supplies validated-domain state.

Variation counters accompany timing as integer counts: `crossover_attempts`,
`mutation_attempts`, `contract_rejections`, `budget_rejections`, `generation_rejections`,
`acceptance_rejections`, `fallback_children`, `unchanged_children`, `changed_children`.
They apply to compiled CPU and GPU operators; legacy operators report zero. Contract
and budget rejections count candidate pairs considered. Fallback is a subset of unchanged
outputs. Crossover classifies both outputs (including an odd-population discarded child),
and a later mutation classifies its output separately. These counts must not be interpreted
as final-population diversity or fitness improvement. Host donor preparation failures also
increment generation rejections on its worker context.


The generic splice transport records a base parent, destination candidate, source
kind/index/range and occurrence count in `PackedChildSplice`. Device occurrence
arrays and child records belong to the cached arena. Child records are zeroed
before a launch and copied to pinned staging with the other fixed child metadata.
A copyback view borrows that staging until the next resize or copyback; neither
an arena nor a staging pointer may survive a capacity change. Cache reuse requires
the same contract mode and sufficient actual donor and occurrence counts.

The atomic device splice utility checks every ordered disjoint span and final
length before touching output storage. Per-node origin records distinguish base
nodes from each copied source occurrence. The host metadata reconstruction helper
consumes an already-spliced node stream: it relocates surviving metadata, copies
source metadata per occurrence and maps lexical captures and introduced binder IDs.
It does not select an operator or insert, remove or reorder nodes. Reconstruction
is transactional and leaves the supplied child unchanged on an error. Production
copyback resolves this metadata, verifies native structure and grammar membership,
and records acceptance or an exact source-copy fallback before the next operator pass.

Compiled preprocessing also records each parent's and contextual donor fragment's
logical constant groups. Fixed template constants are excluded, repeated logical
occurrences share one group, and separate groups remain distinct even when their
nodes share a constant-pool slot. Domain rows preserve finite declaration order and
duplicates, exact typed payload handles, and signed 64-bit integer endpoints.
Per-stream origins refer to global group IDs; group node positions are stream-local.
The device grammar RNG matches host rejection sampling, including full-width ranges.
A device constant-mutation helper now samples and mutates complete logical groups,
compacts live constants together with caller-supplied metadata roots, and reports
NoGroups, Invalid or Capacity separately. Failure leaves published nodes, constants
and root remaps untouched. Float equality uses exact bits, preserving signed zero
and NaN payloads. Mutation dispatch uses identity parent indices, selects its probability/branch and
logical group or contextual donor on device, and falls through to subtree mutation
when a constant branch has no groups. A padded odd-population lane copies its source
without a mutation attempt. One `GpuReproRunResources` owner shares immutable constant
domains across both passes, synchronous or overlapped preparation, and later generations.

The packed-source reconstruction entry point resolves device provenance against the
prepared candidate/donor tables before rebuilding metadata. It checks the selected
source interval, destination occurrence count, numeric compatibility and replacement
budgets, including the destination's donor-pool slice. A rejected descriptor leaves
the child untouched. Production decode uses this entry point before cached native and
grammar acceptance; malformed provenance or changed fallback copies are rejected.

The production two-pass sequence uses crossover followed by decode/acceptance
and analysis of the actual offspring, then a device mutation pass. This preserves
operator order and avoids deriving mutation sites from stale parent structure. The
extra preparation is accounted under existing preprocessing/packing timing. Repeated
launches add only each pass's
selection and variation durations to kernel_ms, avoiding accumulation of earlier
passes twice.

Metadata-only constant roots are prepared per stream from DP boundary-constant
references, deduplicated in source order, and transported in a checked flat array.
The device compactor pins those values; identity/constant-mutation reconstruction
restores source sidecars against the compacted table. `CompiledMutationOutcome`
distinguishes skipped, constant, subtree, no-site, no-donor, invalid and capacity
results. The internal decoder checks source provenance and physical offsets, reuses
certified parents, and calls cached native grammar acceptance for performed changes.
A reported skip/fallback must contain an exact copy of the source node and value
tables; corrupted output is rejected instead of silently replaced. Crossover
classifies the discarded odd sibling, while mutation classifies only actual offspring.
Selection counts are copied once per crossover pair. Before aggregation, the decoder
checks that contract and budget rejections together do not exceed the product of the
actual selected parents' candidate counts; padding does not enlarge this bound. Invalid
counts reject before changing run statistics. The complete backend carries these
counters through both operator passes and synchronous or overlapped preparation.


## Compiled evolution stress coverage

`test_compiled_payload_evolution_gpu.cpp` exercises all eight public result types
through lexical captures in five modes (CPU, GPU evaluation, synchronous GPU
reproduction, overlapped GPU reproduction, and CPU evaluation with GPU reproduction).
Singleton and five-member populations run four generations with mutation enabled;
every final child passes native verification, grammar membership, successful typed
execution and independent CPU/GPU fitness comparison. These are small exact-payload
fixtures, not payload-capacity boundary measurements.

`test_compiled_evolution_stress_gpu.cpp` runs three seven-member structured populations
for three generations in those five modes. It checks repeated template occurrences,
one-coordinate memoization, two-coordinate/two-request memoization and a nested
initial-state region. Final-child checks cover exact region metadata and semantics,
with per-generation crossover/mutation counts and final CPU/GPU fitness parity.
The 2D test explicitly compiles a 128-cell variant of the catalog's 1024-cell grammar;
production GPU packing continues to reject declarations above its capacity.
