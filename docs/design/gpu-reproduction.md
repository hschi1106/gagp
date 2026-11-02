# GPU Reproduction Backend

## Purpose

GPU reproduction implements tournament selection, typed-subtree crossover,
and mutation from compiled-grammar contracts. The public behavior matches CPU
reproduction; child-for-child RNG identity across backends is not promised.

## Inputs

The backend receives:

- the immutable `grammar-definition-v2` compilation;
- the exact generation request and search limits;
- verified parent ASTs with reconstructed membership witnesses;
- the completed fitness vector;
- mutation, selection, and seed settings.

There is no grammar-config mode, specialized candidate bucket, LinearRec binder
array, ASGP phase table, or DP-pattern metadata.

## Pipeline

### Prepare

Host preparation reconstructs each parent, groups logical replacement sites,
assigns dense compatibility IDs, computes destination node/depth/template
budgets, and generates contextual donors from the same compiled grammar.
Crossover-only preparation omits mutation donor pools. Mutation preparation uses
the shared seed schedule to anticipate the single candidate pool that each child
may read, including the constant-to-subtree fallback, and builds only those pools.
All candidate rows and donor-seed draws remain in their original order. The GPU
still makes the mutation decisions, samples the candidate and donor, and performs
the splice; anticipation only avoids preparing data that cannot be consumed.

Repeated template-hole occurrences remain one atomic site.

GPU reproduction supports at most 1024 physical AST nodes per program. Compiled
crossover and mutation allocate their shared origin workspace for the configured
physical node limit, rather than reserving the maximum for every launch. Source
name and constant tables must each fit their device capacity before host packing;
the union-capacity cap used for offspring must never truncate an input buffer.

An optional projected offspring budget is separate from physical allocation limits.
When each native-kind/fuel-profile pair has a unique resource charge throughout the
grammar, cached analysis can certify exact local replacement allowances. Packed
candidates carry projected node cost, carried/reset depth, and destination limits;
device selection checks both directions before choosing a pair. Grammars without
this sufficient certificate retain all candidates admitted by the physical and
grammar contracts, with projected admission checked on the complete copied-back
child. This prevents unsafe pruning when canonical ancestor matching changes costs.
Budgeted donor preparation already checks the complete spliced destination.

Finite constant values and Int/Float range descriptors remain cached per grammar.
Constant domain descriptors also carry the mutation policy. The default resamples;
`flip` complements a selected Bool and atomically updates its logical copies;
`keep` returns applied without changing the child or generating host proposals.
Both policies retain candidate eligibility and never request subtree fallback.
Numeric `add` descriptors retain the outer membership range and a separate delta
range. The kernel samples and applies the delta directly, checking signed overflow
or Float/nonmember results before publishing all logical copies. Rejected sums
keep the old value without a fallback. Optional Float `gpu_grid_steps` samples
equally weighted closed grid indices on-device, preserving its explicit backend
sampling law without a large finite host table.
Float intervals are sampled directly on the device using the same separately
rounded interpolation as the CPU; no host proposal table narrows scalar ranges.
Optional Float quantization is also performed on-device after interval sampling,
with the declared scale retained in the immutable domain descriptor. CPU/GPU
sampling agrees bit-for-bit for a shared RNG word, including signed zero.
For numeric `sample_from`, these cached descriptors describe the sampling subset;
the compiled grammar retains the outer membership domain for child acceptance.
Bounded sequence domains use fresh host-prepared proposals: each preparation
samples one value per population member per sequence domain, including length
and every scalar element, from the full declared domain. The device selects
among those proposals and performs the constant replacement. Proposals are not
an enumerated replacement grammar and are not cached for the whole run; there
is no CPU reproduction fallback. This can correlate children that select the
same proposal, just as a shared contextual donor pool can. CPU/GPU random
trajectories are not required to match.

Each immutable proposal snapshot records its seed and population count and
retains the shared grammar table. Live preparations retain their own snapshots;
run-level payload root collection includes every live snapshot and nested
StringList payload. Capacity checks reject more than one million prepared
values or more than 256 MiB of the sequence expansion bound, before sampling.

Preparation may overlap GPU evaluation because it depends only on immutable
population/grammar state. Tournament selection still waits for fitness.

### Pack and upload

Packing verifies grammar/search identities and flattens AST nodes, names,
constants, lexical/traversal/bounded-region metadata, site groups, contracts,
domains, and donors. It prescans capacities and never truncates a compiled
payload. General bounded-region plans are transported as ordinary v2 AST
metadata; authoring package files are not required.

Buffers are explicitly compiled mode. Low-level entry points reject a mismatched
mode before allocation, pointer access, or kernel launch. Arena buffers are
reused when capacities permit.

### Select and vary

Selection consumes the completed fitness vector and produces parent pairs.
Crossover samples compatible logical sites, checks independent destination
budgets, and replaces every physical occurrence in the selected logical group.
Mutation either regenerates an admitted subtree from packed contextual donors
or resamples a mutable constant using its compiled finite/range domain or the
current preparation's bounded-sequence proposals.

The device preserves prefix validity, lexical ID remapping, metadata ownership,
atomic occurrence groups, and explicit provenance for copyback. A failed
attempt returns the certified parent according to the shared fallback contract
and updates the corresponding counter.

### Copy back and certify

Compact device results are copied to host storage, decoded as
`ast-prefix-v2`, structurally/type verified, and reconstructed against the
active compiled grammar. A child that cannot satisfy the complete contract is
rejected. No release-1 side table is decoded during copyback.
Projected admission always uses reconstructed canonical costs, including for
constant mutation. Prepared data and reusable GPU resources cannot be used with
a different projected budget. Failure returns the certified parent and records
the resource rejection; it does not substitute CPU reproduction for a GPU operation.

## Correctness

CPU and GPU backends share:

- tournament selection semantics and crossover-before-mutation order;
- exact type/scope/template compatibility keys;
- atomic repeated-hole handling;
- destination node, depth, and template budgets;
- constant domains and contextual donor frames;
- accepted-child AST verification and grammar membership;
- variation counter meanings.

The GPU uses fixed transport limits for programs, metadata, names, constants,
and total padded storage. Capacity rejection is explicit and does not silently
change the grammar or execute reproduction through an old host path.

## Overlap and timing

`--repro-overlap on` schedules preparation around GPU evaluation. It may hide
some prepare/preprocess/pack wall time; selection, variation, copyback, decode,
and certification remain ordered after their dependencies. Timing field names
and aggregation rules are defined in
[the timing reference](../reference/timing.md).

Variation counters classify operator outputs, including both crossover
children and subsequent mutation outputs. They are not final-population counts.
Performance claims require measured benchmark artifacts; this design contract
does not assert a cutover speedup.


Donor generation selects the grammar's `mutation` generation stage, on both CPU
and GPU preparation paths. Stage-specific feasibility and frame-cache entries
prevent initial-only alternatives from supplying a donor or a false budget
minimum. Request identity, including stage, is checked when reusing GPU run
resources. Device selection/splicing and decoded-child union membership remain
unchanged; the stage does not partition crossover compatibility.


Compiled device splicing rebuilds name and constant tables from surviving node
references in first-use order. Removed parent entries consume no output slots;
repeated copies share equal values, with Float equality preserving signed-zero
bits. Packing reserves up to the union of two source tables, bounded by the
existing 128-entry device capacities. Search node/depth limits are unchanged.
Metadata-only names continue through the existing sidecar reconstruction by
value; device splice selection and node materialization stay on GPU. True table
exhaustion remains transactional and returns the original parent.

## Constant-branch donor preparation

When the shared mutation schedule selects a constant operation and the certified
parent stream has at least one constant group, preprocessing omits its subtree
donor pool. The GPU still executes the constant operation; only its NoGroups
outcome can fall through to subtree mutation. Empty-group streams retain donor
preparation for that fallback. Host donor seeds are consumed for omitted pools
as well, preserving subsequent candidate sampling.

Prepared donor pools share a bounded admission cache only within one destination
and variation site. Its decoded AST identity includes the full donor structure,
constant values, inputs and fuel. Repeated identities reuse the same full-child
admission result; generation and per-seed retry order remain unchanged. At most
256 identities are retained, and failed seed slots are preserved when forming
the GPU pool. No admission certificate crosses a pool boundary.

Within a budgeted donor pool, destination membership and the certified variation
site are resolved once and owned by the pool. Holding the analysis keeps site
references valid even when accepted children evict the parent from the analysis
cache. Retry seeds, per-donor admission checks, and the pool-scoped decoded-AST
admission cache are unchanged.

GPU preparation sizes its worker-owned variation analysis cache to four times
the population, clamped to 128–4096 entries. This retains some parent analyses through
preparation and decoding while allowing bounded headroom for generated children;
entries remain exact-runtime-identity keyed and eviction does not relax admission.

Parent analysis warm-up uses bounded batches of at most 128 programs with up to
20 readers (limited by hardware concurrency) in the GPU backend. Each reader reconstructs native
membership, lowering and variation sites without writing the payload registry or
compatibility registry. After all readers finish a batch, the owning thread commits results
in population order and interns compatibility IDs in site order. Exact decoded
runtime keys are recomputed before cache lookup, including payload liveness.
Payload registration during parent warm-up is prohibited. Populations below
32 members use the serial path. Failed analyses propagate in population order;
no worker survives the call, including exception unwinding. The call owns one
worker team reused across batches; its barrier completes every callback before
payload validation and ordered publication. Exceptions wait for all readers
before propagating. GPU compilation, parent compaction and private complete-child
admission use the same operation-owned team mechanism.

Private child admission uses batches of at most 256 children. Its operation-owned
team first reconstructs independent device child ASTs and metadata into indexed
slots, then joins before collecting them in child order and running full admission.
Metadata reconstruction reads only immutable prepared tables and private outputs;
it does not publish payloads or choose variations. Enclosing payload scopes and
unmatched source/context ownership keep reconstruction sequential. Speculative
failures repeat on the ordered path, preserving the original error position.
The team remains capped at 20 workers; each batch bounds reconstruction and
admission storage, including an odd discarded crossover sibling.

Donor pools may evaluate independent seeds with at most four concurrent workers.
The pool resolves and owns the destination analysis once; workers share that
immutable certificate while keeping separate VariationContexts, admission caches,
and retry sequences. The caller collects seed results in order.

The compiled mutation-entry graph conservatively identifies reachable String or
typed-list construction across all stages and expanded children/references.
Payload-free workers need no registry transaction. Payload-producing workers use
private staged writes and captured reads, then join before the pool commits all
transactions atomically. Conflicting values or changed read dependencies discard
all staged writes and replay the entire pool sequentially in seed order. This
preserves collision behavior and cross-seed dependencies. Full membership,
resource acceptance, retry limits, and failed seed slots remain intact.

Short-lived donor seed workers validate candidate membership, canonical projected
resources and execution lowering without constructing future variation sites.
Membership and witness reconstruction run once: over-budget children skip lowering,
while admitted children still check lowering and the instruction limit. The CPU
and sequential fallback retain their full analysis cache for subsequent reuse.
The full-analysis oracle tests preserve admission decisions and retry ordering.

Budgeted donor generation also uses a certified site's projected replacement
allowance as an early rejection filter. The site is recovered from the parent
analysis rather than trusting caller-provided allowance fields. This is enabled
only by the existing grammar-wide, context-independent resource-charge
certificate. A rejection consumes the same retry seed and records the same
pool-local negative admission. Passing this filter does not admit the donor:
complete child membership and execution validation remain required. Grammars
with ambiguous resource interpretations retain full reconstruction for every
uncached attempt.

A separate bounded resource-invariance checker compares derivations within given
roots using a relaxed tree-grammar product. It drops constant-domain restrictions,
lexical mappings and linked-hole equality, retaining only disjointness conditions
also enforced by membership. Alias closure is finite; a least fixed point admits
only productive pairs. Charge conflicts then propagate through productive pairs.
A root is certified only when productive and conflict-free. Work/state/edge/group
and closure-storage limits conservatively return no certificates. The stronger
checker is shared by immutable grammar copies and keyed by root ID. For certified
roots, expression donors are reconstructed in the destination nonterminal/frame
before their charges participate in early rejection; mutation-entry provenance is
not sufficient. A failed destination match falls back to whole-child analysis,
since a different enclosing production may accept that child. Surviving proposals
still receive complete admission. Before product construction, partition refinement merges bisimilar states of the
relaxed weighted grammar. Initial partitions include native labels and resource
charges; refinement uses each ordered child's set of reachable partition IDs.
Only a fixed point is merged, preserving recursive languages and node costs.
This reduces certificate startup work without changing donor admission rules.

Application host work defaults to one CPU thread. `GAGP_HOST_THREADS=2..20`
opts into the worker paths described below. On 1T, overlap preparation executes
on the caller after GPU eval launch; decode admits children in source order in
the run's existing VariationContext, so the following mutation preparation can
reuse their analyses. It does not speculate into discarded worker contexts.
Admission, payload validation and GPU variation kernels remain unchanged.

GPU copyback prepares changed-child ASTs in bounded batches of 128. With at least
32 proposals, an owned prepared-source/context continuation can perform complete
child admission using at most 20 readers, capped by hardware concurrency. Workers
use separate variation contexts and payload snapshots; they perform ordinary
native/grammar certification, lowering, root-contract and resource checks.
Every worker joins before atomic read validation. Provenance and physical metadata
remain checked in the original child order before results and admission counters
are published. Failed speculation or changed payloads falls back to ordinary
ordered admission. Enclosing payload scopes and callers without the owned
continuation keep deferred analysis warming and the existing ordered path.
Worker cache/registry IDs do not escape into the owning context; only certified
children and the five admission-result counters are transferred.

The donor module provides the speculative multi-pool API used by bounded mutation
prefetch. Up to 20 workers process independent pools while preserving the
returned job/seed order. Each pool stages payload writes; only a successful atomic
commit publishes results. On conflict or unsupported batching it returns no result,
and the caller must replay its original complete interleaved preparation. Merely
generating all pools sequentially before later assembly would not preserve payload
collision behavior. Single-seed pools keep their existing caller-registry path.
Batches are limited to 1024 jobs, 64 seeds per job and at most 1,048,576 declared
physical donor nodes across their outputs; exceeding a bound declines speculation.

### Population order across overlap modes

The evolution loop supplies GPU reproduction with fitness aligned to the original population order in both direct and overlap modes. Candidate shuffles, donor seeds and tournament indices therefore refer to the same members for a fixed seed. A separate ranked view identifies the best member for reporting. Previously, direct execution prepared fitness-sorted parents while overlap prepared original-order parents, changing seeded trajectories when fitness differed. The correction changes the direct GPU trajectory to match overlap; it does not change evaluation order, tournament distribution, or CPU reproduction ordering. Lower-level prepared reproduction still requires callers to supply fitness and genomes aligned with the prepared source order.

Parent analyses are first warmed on the original tables, preserving validation
of unused constants and payloads. Private parent compaction processes batches of
at most 128 members with up to 20 readers outside enclosing payload transactions.
Read conflicts fall back to sequential compaction and errors retain source order.

When stable compaction removes table entries, its owning preparation call can
transport the validated analysis: physical nodes, types, binders, witness and
resource charges remain unchanged; native scope name indexes are remapped,
unused input names are removed from expression scopes, duplicate scopes are
interned again, and affected compatibility keys are rebuilt. Program-root input
contracts remain unchanged. Exact runtime identities and payload reads are
revalidated before ordered cache publication. Existing compatibility IDs are
reused only in their original live context registry. Transport is bounded by the
warm-handoff ownership policy below and never accepts caller-supplied provenance.
Unavailable handoffs, enclosing transactions, failed remapping or changed reads
use full compacted-population analysis. No-removal compaction skips the extra
analysis stage.

Donor-pool calls inside an active staged payload transaction use sequential generation in the caller's view. Optional cross-pool batching declines in that situation. This keeps uncommitted destination values visible and leaves all donor writes owned by the enclosing transaction, including rollback. Worker-local transactions are used only outside an enclosing transaction.

Mutation preprocessing may preview donor work for a bounded window of up to 1024 parents (at least 128 parents are required). The preview stops before its next selected parent would exceed the declared donor-node storage bound; the next window resumes at that parent. A copied RNG reproduces the existing site-shuffle and donor-seed schedule; only the anticipated subtree-mutation site creates a pool job. Bounded pool workers generate in parallel, then commit payload transactions atomically. The ordinary loop consumes results in its original parent/site/seed order, checking the preview against the actual schedule. If speculative work declines or fails, it publishes no writes and the loop generates donors through the existing per-site path. Populations below 128 members and crossover preparation bypass prefetch. This changes scheduling, not proposal weights, seeds, fallback decisions or acceptance validation.

The donor preview is bypassed when mutation is disabled or fewer than two hardware threads are reported, since no parallel donor work can result. The ordinary loop still consumes its established site and donor-seed schedule.


The compiled preprocessing path obtains each parent's materialized runtime identity
from the same variation-analysis lookup that validates it. This avoids separately
serializing and hashing the parent immediately before cache lookup. The identity
is still decoded on each lookup; the optional output adds no persistent identity
memo and does not trust mutable genome provenance or bypass payload validation.

Empty donor seed pools return before hardware-concurrency discovery or any
preparation. Crossover and unselected mutation sites still preserve their random
schedule and candidate metadata, but do not pay donor setup costs when no donor
is requested.

Identity checks at packing and prepared-source validation read existing ASTs
through the runtime identity API. They do not copy AST tables into temporary
genomes just to compute a key. The genome overload forwards to the same AST
implementation; full decoded-constant, metadata, input-name and fuel validation
still runs for each check, including detection of changed payload registry values.

For the private owned packing continuation, pools of at least 128 donors compute
these complete identities with up to 20 operation-owned workers in chunks of 16.
The caller joins the workers before consuming results and reports errors in the
original donor order. This adds one byte and one exception pointer per donor,
plus worker-local identity strings; it does not cache or omit decoded identities.
Public packing and callers with an active payload scope keep the sequential path
so transaction reads remain on their owning thread.

Table compaction takes ownership of its working genome. Callers retaining their
source pass an lvalue and receive a compacted copy; certification transfers its
already-owned genome with a move. Native index checks, survivor order, remapping,
and metadata refresh remain the same in both paths.
When all name and constant entries are referenced, compaction stops after its
index-validation pass and metadata refresh. Stable survivor order makes remapping
the identity operation in this case, so no replacement tables are allocated.

Mutation prefetch retains each previewed parent's analysis and identity for the
current bounded window. Identity/analysis reads run in one payload snapshot;
before the ordered parent loop consumes them, it validates that snapshot against
the registry. Changed reads fall back to normal analysis. An enclosing payload
transaction disables this handoff and keeps the caller's original payload view.
No mutable public preparation or packing check is bypassed.

The private mutation preparation pass consumes the compact, certified output of
crossover decoding directly. It does not copy and compact that entire population
again. The original population warm/validation call still runs, including payload
reads; ordinary external crossover preparation retains full compaction. No public
caller can select the internal already-compacted mutation path.

Grammar membership and changed-child classification compare bounded region plans
by their fields after validating both plans. They no longer build canonical JSON
objects just to compare plans. All fields, including execution limits, progress,
rank/domain and window descriptors, participate; validation still rejects invalid
enums and noncanonical inactive fields. The JSON encoding remains unchanged.

The private backend now carries warm-up analyses, runtime identities and sealed
read-only payload snapshots into preprocessing. This continuation requires the
same population ASTs, grammar, requests and context; table compaction that
changes the representation triggers another warm-up. Each consumer revalidates
payload reads before reuse and falls back to ordinary analysis on a mismatch.
Public preprocessing accepts no such handoff. By default retention is capped by
analysis-cache capacity. Setting `GAGP_POPULATION_HANDOFF=1` instead retains one
analysis/read snapshot per population member in the prepared generation, even
when the FIFO cache evicts it. The persistent cache keeps its 128–4096-entry cap;
the continuation adds memory proportional to the current population and its
analysis sizes, and releases that ownership with prepared generation state.
This prevents oversized populations from repeating warm analysis serially during
preprocessing and parent certification. The same exact-identity, owner and
payload-read checks apply. Sequential/enclosing-scope warm-up paths still use
ordinary preprocessing. Registry IDs are still published in
population order. Tests compare candidate fields, identities and donor ordering
with public preprocessing, including a changed unused String constant payload
that must invalidate the saved identity.

Prepared backend state also retains an opaque parent-certificate continuation
following the warm-analysis ownership policy above. It owns the exact immutable packed source
set, its variation context, freshly built compact-parent metadata and warm
analyses/read snapshots. Decode reuses it only when source/context owners match,
row counts agree, and the payload snapshot remains valid; otherwise normal parent
certification runs. Public standalone pack/decode has no continuation by default.
This avoids rebuilding parent identities and native metadata after preparation.
Child verification, splice provenance checks and copied-fallback comparison remain
unchanged. A 128-member GPU oracle compares enabled/disabled continuations and
checks metadata freshness plus fallback from unusable payload snapshots.

Private packing of that same unchanged population can reuse warm parent runtime
identities after revalidating their payload read snapshots. It still compares
those identities with preprocessing output and retains all shape, candidate,
capacity and donor checks. Public packing always recomputes identities. Missing
or invalid snapshots also take the ordinary identity path. Tests compare physical
nodes/names/contracts with public packing and reject forged identities, including
an old identity after a payload change.

Immediate direct reproduction enters the private executor after building its own
prepared state. The source population remains owned and unchanged throughout
that call, so it does not run the public replay identity comparison again.
Preparation still validates imports, execution fuel, grammar requests, resource
ownership, packed shapes and payload identities. Public prepared replay (including
caller-supplied overlap state) retains full validation against scored genomes.
A direct-versus-replay oracle checks offspring, variation counters and host RNG
consumption; malformed imported ASTs and mismatched fuel are still rejected.

The evolution loop also has a private owned overlap continuation in
`cpp/src/evolution/repro/owned_overlap.hpp`. Evaluation and preparation share one
const population owner; the continuation captures its configuration and consumes
fitness once, without exposing mutable prepared data. Source payload values,
including unused constants, are snapshotted before preparation. If that snapshot
changes before execution, preparation is repeated with the same seed and full
validation. Public lifecycle overlap/replay APIs retain their existing source
checks. The old population and prepared state are released before the generation
timer ends and before the payload sweep.

For GPU crossover preparation with a projected offspring budget and subsequent
mutation, nonlocal resource-invariance analysis can run concurrently with parent
preparation. It is joined within preparation, retains the same roots and proof
work limits, and uses a separate certificate-cache mutex so executable-root
lookups can proceed. No proof work is moved outside the generation.

Device reproduction arrays share one allocation with individually aligned typed
slices. Their capacities and transport checks are unchanged. Growth replaces the
allocation and clears the uploaded-domain owner; sufficient capacity is reused.
Pinned host staging retains separate allocations. Its reuse predicate compares only pair, node, name and constant capacities used by staging; donor/domain dimensions do not force a staging reallocation. Allocation and copyback use the same predicate.

Private donor prefetch can reuse the destination analysis already held by its
owned preparation window after checking its payload snapshot. Public donor APIs
still reconstruct/lookup their own destination analysis. Within each fixed
destination/site/frame admission cache, a newly generated scalar donor can reuse
its freshly rebuilt native key when that key is at most 256 bytes. Longer keys
and payload-backed donors retain decoded runtime identities; no input artifact
metadata is trusted and all original seeds/retries are retained.

Parallel child admission can also reuse the validated parent's root contract.
The parent snapshot is checked before entering the worker's payload scope and
again before publishing its result. Snapshot checks deliberately reject reuse
inside an active scope; checking there would silently disable the optimization.
Every child still receives membership, native/lowering and budget validation.
Invalid parent evidence falls back to ordinary parent analysis.

### Experimental native GPU phase population

For a checkpoint-specific Chinese explanation with old/new examples and evidence
limits, see [GPU phase variation comparison](gpu-phase-variation-comparison.zh-TW.md).
This section remains the owning implementation contract.

`GAGP_GPU_PHASE_POPULATION=1` requests a run-owned GPU genotype in the existing
GPU evaluation + GPU reproduction modes. It uses one host thread. The default
native AST backend remains the explicit fallback, with the reason recorded in
`EvolutionResult`. This profile admits one fixed root region template with
independent whole-phase holes and pure Int/Bool/IntList expressions, scalar
Int/Bool constants, identity scopes and unit variable-phase resource charges.
Fixed skeleton charges must be at most one node/level without depth resets.
The profile conservatively enforces physical whole-program node/depth budgets,
including zero-charge administrative nodes; weighted/reset skeletons and inputs
whose physical size exceeds those budgets explicitly fall back. Import
independently checks grammar membership; type compatibility alone is insufficient.
Repeated holes, lexical capture, nested templates, projected budgets and mixed
root contracts decline this profile. Declared execution capacities are preserved.

Selection, subtree crossover, fresh subtree mutation and phase lowering execute
on GPU. Private double buffers retain site contracts across operators and
successive generations. Only live compiled phase code returns to the native
GPU evaluator; changed offspring do not undergo AST reconstruction between
operators. Every program/case is evaluated. One history-best AST per generation
and requested final exports normally materialize a canonical derivation cache
inside their respective generation/final-output timing boundaries. The optional
construction-based export below retains the private owner membership invariant
without materializing that reverse-parsed cache. Cold import, allocation
and compilation are reported as initialization, and included in search wall time.

This changes reproducible RNG streams and the variation distribution: tournaments
sample with replacement; sites are expression nonterminal occurrences within
phases; crossover tries at most eight compatible sites; mutation uses the actual
remaining whole-program node and phase depth budget. Fixed root structure is not
a variation site. History-best ranking retains source order on fitness ties,
instead of the reference ranking's serialized program key; tournaments retain
the first drawn candidate on ties, as the legacy GPU tournament already did.
Every accepted phase remains in its original nonterminal and
scope. It does not change numeric, fuel or evaluator semantics. Changed/unchanged
counts classify final children against their selected parent; crossover fallback
counts may include children subsequently changed by mutation. Both overlap modes
use the same dependency schedule here because host donor preparation is absent.
Results must identify this profile and cannot be presented as identical evolving
populations to legacy reproduction. GPU memory is bounded by live population and
grammar capacities, without ancestral proof/source retention.

The physical whole-program grammar budget may be at most 2048 nodes and 64
levels. Each variable phase still has its independent 1024-node buffer; a larger
whole-program budget does not enlarge those buffers. Cold import explicitly
declines any phase exceeding that capacity. Variation checks the sum of all
variable phases plus fixed nodes against the authored whole-program budget,
fresh mutation donors fit both the remaining whole-program budget and the
remaining destination-phase capacity, and lowering bounds compacted
code/constants by the whole-program sum rather than a single phase's capacity. These are backend resource limits, not ASGP logical-depth
limits; a 2048-node grammar does not establish equal search spaces.

Evolution attempts this independently checked phase owner before creating the
generic GPU reproduction resources. The generic kernel's 1024-node request
preflight remains enforced if phase admission declines; no larger request is
silently passed to that kernel. Initial case/AST payload roots are retained
before admission, and generic run/domain roots join retention when that backend
is created. This ordering does not replace GPU reproduction with CPU work.

The phase owner accepts up to 16384 members, subject to its existing 12 GiB
genotype allocation bound. If the estimated genotype buffers, configured payload
scratch cap and 2 GiB of kernel-launch headroom would exceed currently free
device memory, it allocates reproduction buffers only during reproduction.
Completed lowering is copied into the existing host executable owner before
those temporary device buffers are released. Evaluation retains only the current
genotype, and its private payload workspace is released before reproduction.
This changes storage lifetime, preserving the configured scratch cap, population,
cases, RNG, operators and execution limits. All allocation, release and lowering
costs remain in search wall time; this heuristic does not guarantee that a given
card has sufficient memory. `GAGP_GPU_PHASE_STAGED_BUFFERS=1` forces this lifetime
policy for diagnostics; `0` retains all buffers, and an unset value selects it
from the memory estimate. Both diagnostic policies receive full offspring and
CPU/GPU evaluation checks in `gagp_test_gpu_phase_population`.

`GAGP_OWNED_PHASE_PACK=1` additionally reuses executable verification in that
private owner. A nonconstructible borrowed view is valid only for a synchronous
owner evaluation. Cold native verification certifies the unchanged root, plan,
bindings and fixed phases; the checked device phase compiler certifies each new
phase's instruction construction, local types, result type and stack capacity.
The owner incorporates those bounds before publishing a new executable population.
Packing still checks transport capacities and semantic fuel. Ordinary external
`eval_programs` always performs full native verification. No public boolean or
serialized certificate bypasses that boundary. The conservative cold stack bound
may overestimate later requirements; it cannot underestimate new phase stacks.

Experimental evaluator `GAGP_COORDINATE_EXECUTOR=1` applies only after ordinary
or owned executable verification and typed direct-phase capability proof. It
requires one Int coordinate/result, literal inclusive/exclusive bounds, one or
two coordinate-offset requests, and no preparations or bound operands. It uses
unboxed Int frames but preserves lazy recursion, base predicate before memo
lookup, linear memo keys, entry/instruction fuel, boundary phases, and original
frame/cell capacity errors. Other structures use the existing generic executor.
No task name, expected output or particular offset/domain size participates in
selection. This is independent of reproduction representation.

`GAGP_OWNED_PHASE_CAPS=1` reuses typed-view/root capability in that same private
owner. Cold bytecode analysis proves the fixed pieces for the grammar's ordered
input types. The GPU phase compiler's closed, typed operation/forward-branch
construction proves the corresponding capability for every new phase. Runtime
case types must match the recorded input signature, and actual payload tables
are still checked; a different case signature uses ordinary capability analysis.
Stack/local thresholds remain checked each evaluation. This does not turn
unsupported expressions or captures into admitted view programs.

`GAGP_COUNTED_SITE_DRAW=1` counts eligible nonterminal/scope sites and draws a
single uniform rank, instead of a reservoir RNG draw at each visited node. The
site distribution remains uniform and filtering/capacities are unchanged; RNG
consumption and therefore evolutionary trajectories differ. Selection and both
variation operators still execute entirely on GPU. Quality comparisons must
identify this setting.

`GAGP_GPU_UNIFORM_PHASE_SITES=1` is an opt-in search policy for the native GPU
phase owner. Each crossover draws one variable phase uniformly before its bounded
retry loop; every destination and compatible donor draw stays in that same phase.
Each mutation that passes its existing probability gate independently draws one
phase uniformly, then a site within it. Existing nonterminal/scope matching,
physical/projected resource checks, retry count, and GPU execution remain in
force. Only the exact value `1` enables this policy; unset or `0` preserves the
existing genome-wide uniform-site draws and their RNG consumption. The owner
captures the setting at construction, and its profile gains `-uniform-phase`.

This changes the site distribution and RNG trajectory: phase probability becomes
uniform instead of proportional to eligible site count. It does not introduce
ASGP's crossover probability gate or its single-destination, depth-filtered donor
selection. Native CPU reproduction (including GPU evaluation with CPU
reproduction) is unchanged. Comparisons must disclose these operator differences;
this flag does not establish equivalent convergence or equal search work. The
existing `gagp_test_gpu_phase_population` exercises enabled-policy membership,
full-case CPU/GPU fitness parity, counted/reservoir selection, and exact
unset-versus-zero trajectories. Quality/performance adoption requires separate
measurements. The benchmark search-contract metadata names uniform-phase draws
and active projected depth caps, while keeping their alignment flags false:
native grammar sites, retries, physical limits, and admissibility still differ.

`GAGP_GPU_SOURCE_CROSSOVER_GATE=1` independently enables a fixed 0.7
crossover probability in the native GPU phase owner. The default remains one
attempt per child. The gate is drawn after parent/donor selection and before
phase/site selection; a skip clones the selected parent, including deferred-copy
mode, then continues through the existing mutation pass. Skips are unchanged
unless mutation changes them, and are neither crossover attempts nor failed
crossover fallbacks. Exact `1` enables the constructor-captured policy; unset or
`0` consumes no additional RNG draw. Profiles gain `-source-cross07` before the
optional `-uniform-phase` suffix.

The search-contract audit marks only the crossover probability item matched by
this option. It changes RNG trajectories and does not align donor filtering,
retry policy, elite work, native CPU reproduction, or the other search rules.
The population regression covers disabled trajectory parity, attempted/skipped
mixtures with mutation disabled, eager/deferred clones, projected resource
admission, and full-case CPU/GPU fitness. This is an experimental search choice,
not a default or a convergence-equivalence claim.

#### Shared operator bodies and the CPU reference engine

Per-child selection and variation live in `repro/gpu/phase_operators.cuh` as
`__host__ __device__` bodies: tournament, reservoir/counted site draws,
crossover with its gate and retries, subtree mutation (generation, splice,
in-place copy), offspring comparison and phase lowering. The GPU kernels in
`phase_population.cu` are thin wrappers; `HostPhasePopulation`
(`phase_population_cpu.cpp`, a plain C++ translation unit) runs the same bodies
serially or on opt-in host workers. Both owners admit the same input, use the
same per-child seeds and operation order (crossover, mutation, elites,
comparison, swap, lowering) and reuse completed results only for genotypes
identical to a parent. `evolve_population` drives both through one shared
generation loop, so ranking, selection fitness, verification, history and
export are common code. `gagp_test_phase_population_cpu_reference` requires
identical final populations, fitness, wrong counts and champions for several
policies, host thread counts and GPU engine variants.

Research population schedules (`PhaseSchedulePolicy`: islands and an async
discrete-event simulation, [search config](../../spec/search_config.md#population-schedules-research))
exist only in the CPU reference owner. Islands run the same bodies on shifted
row ranges inside `reproduce`; async uses `start_child` / `evaluate_child` /
`commit_child` per worker row, driven by `run_phase_population_async` in
`evolve.cpp` instead of the shared generation loop. The GPU owner rejects
those two schedules.

The synchronous batch steady state (`schedule.mode: steady_state`) runs on both
owners through `run_phase_population_steady` in `evolve.cpp`. Each owner
implements `begin_steady` / `make_batch` / `evaluate_batch` / `batch_nodes` /
`commit_batch`: children of one round occupy offspring rows `0..batch-1`, the
operator kernels take the child count separately from the parent population,
the GPU owner lowers and evaluates the batch through a second packed program
set (`ProgramSet batch`, reusing identical-member results through the completed
fitness path), and `place_children` copies the placed rows into the population.
Member programs are not lowered after `begin_steady`. Measured on the formal
G64 cohort, each evaluation launch has a large latency floor, so `batch` below
the population size is slower than the generational loop
(`~/gagp/logs/async-island-eval-20261031/README.md`).

The semantic policy (`PhaseVariationPolicy`, [search config](../../spec/search_config.md))
is a constructor argument of both owners. Exact GPU engine variants remain
process settings captured at construction:

- `GAGP_GPU_WARP_VARIATION=1` runs crossover and mutation with one warp per
  child. Lanes execute the serial control flow redundantly; site counting,
  subtree scans, splice copies and constant renumbering are lane-parallel, while
  random draws and depth checks keep their serial order. This bounds the
  per-generation tail set by the largest generated subtree.
- `GAGP_GPU_CHOICE_TABLES=1` (with warp variation) replaces the weighted
  production loop of subtree generation by host-built threshold tables
  (`phase_choice_tables.hpp`). For each nonterminal, depth and logical budget
  the eligible set changes only at sorted minimum costs, and the sequential
  IEEE FP64 draw is monotone in the 53-bit random value, so integer thresholds
  select exactly the production of the serial loop without device FP64 work.

#### Grammar-invocation depth and parsimony

`EvolutionConfig::gpu_phase_site_depth` optionally caps per-phase nesting of
independently mutable grammar invocation roots (1–15; zero disables the policy).
Physical grammar node/depth and per-phase GPU capacities remain separate checks.
Cold import annotates and checks every initial member; crossover checks the
spliced site's annotated depth, and mutation samples from a joint physical-node,
physical-depth and invocation-depth feasibility table. Synthetic nodes inside a
production consume physical resources but no invocation level. The profile is
`native-gpu-phase-v4-site-depthN`. Explicit requests require the resident GPU
owner and fail on unsupported policies or initial members; they cannot combine
with authored projected-resource budgets. This counts private construction
sites, rather than treating canonical projected resources as interchangeable.

`gpu_phase_site_parsimony` independently uses the number of mutable grammar
invocations to break fitness/wrong ties. It requires a task metric with wrong
counts, keeps the original index as the final tie break, and adds
`-site-parsimony` to the profile. It changes selection; it is not a performance
shortcut. Device metrics are checked against independent canonical derivations
in the owner regression. Matching a source's counts on audited programs does not
establish equivalent RNG, donor filtering or search distributions.

#### Optional CPU training reference verification

`EvolutionConfig::cpu_fitness_verification` is disabled when `top_k=0`. Otherwise
it evaluates the leading `top_k` resident GPU candidates on the same training
cases at generations divisible by `interval` and at final evaluation.
`near_zero_error >= 0` additionally triggers when the leading summed fitness is
at least its negative. The threshold concerns the configured fitness, including
auxiliary tuple summaries, rather than a universal correctness threshold.

The CPU reference exports and compiles only those candidates, applies the same
fuel, penalty, metric and reduction lanes, canonicalizes reference scores to the
ranking precision, corrects their scores/wrong counts, then re-ranks the complete
population before GPU selection and reproduction. Candidates outside the bounded
subset retain GPU scores; a corrected leader can therefore be replaced by an
unverified candidate. `verification_best_unverified` explicitly counts that case.
This is partial verification relative to the native CPU runtime, not an
all-candidate or all-input exactness guarantee. It uses the CPU evaluator's
existing per-case discard-only payload overlay.

`EvaluationTiming` separately records verified programs/cases/errors/timeouts,
score/wrong corrections, leader changes, unverified leaders, maximum raw score
delta and total verification wall time. Preparing raw-score diagnostics, export, compilation and re-ranking are
included in that cost and in search wall time. GPU case counters remain GPU
counters. Enabling verification on an unsupported owner, a CPU reproduction path
or a legacy metric fails explicitly; it never silently disables verification or
replaces GPU reproduction.

`checkpoint_generations` plus `evaluation_checkpoint` provide opt-in diagnostic
population snapshots after evaluation and before variation, in source population
order. Generation G denotes final evaluation. The sorted, unique requested
indices must be in 0..G and cannot request G when final evaluation is skipped.
Callbacks must finish using payload handles during the call. Native populations
export only requested checkpoints; callback/export cost is charged to the run.
They are intended for diagnostics, not headline timing.

#### Optional batched native GPU import/export transport

`GAGP_BATCH_PHASE_IMPORT=1` flattens admitted live genes once, uploads compact
arrays, and scatters them into the existing fixed-capacity device owner. The
external grammar admission, skeleton checks, bytecode checks, and device site
annotation remain required. `GAGP_BATCH_PHASE_EXPORT=1` gathers the final live
population once before independent host AST reconstruction and canonical grammar
admission. History still exports its selected individual normally. No execution,
selection, RNG, or variation rule changes. Both operations stay within their
original cold-initialization/final-result timing boundaries. Transient transport
memory is bounded by live population capacities and released within the call;
reported genotype device bytes include the transport allocation peak. Single
member export and ordinary transport remain available for reference checks.

`GAGP_REUSE_PHASE_GRAMMAR=1` compiles each immutable grammar/nonterminal/depth
phase profile once per cold population importer. The local cache owns a
`shared_ptr<const CompiledGrammar>` and rejects foreign grammar identity. It
shares only grammar compilation, never AST witnesses or admission results; all
external individuals still undergo full independent canonical admission and
bytecode verification. The cache is released after import and cannot grow across
generations. This does not change supported grammar combinations or operators.
`GAGP_PHASE_CAP_BUCKETS=1` partitions an owner-verified population when only
some programs meet the direct phase stack bound (16). It retains the existing
input-type, payload, locals and root capability checks. Two launches share the
same packed program tables and scatter fitness and counters directly to the
original population indices. Large-stack members run the generic view kernel;
no candidate or case is removed. Shared scratch is sized for the larger required
frame layout and reused only after the preceding launch completes on the same
stream. Index construction, upload and both launches remain inside evaluation.

`GAGP_PHASE_REPRO_THREADS` selects 1, 2, 4, 8, 16, 32, 64 (default), or 128
threads per native phase reproduction block. It changes scheduling only, not
selection, RNG, variation or resource limits. Small blocks expose more independent
program work on low populations; no CPU workers are created. The compiler offset
scan uses one 256-thread tiled block, retaining bounded checked compiler results
and the same compacted program layout.

`GAGP_REUSE_IMPORTED_EXECUTABLE=1` supplies the evaluator's actual input order
when each external individual is admitted and compiled. The cold GPU owner
moves that already compiled executable into its population instead of verifying
and compiling the same AST a second time. GPU bytecode verification, skeleton
checks, capability proofs and independent output admission remain required.
Secondary imports read cached grammar profiles by const reference and omit
copies that would immediately be discarded; the first import retains the full
profile tables needed by the owner and export adapter. This changes neither
execution nor variation semantics and does not cache external admission.

`GAGP_GPU_STATIC_WEIGHTS=1` pre-normalizes mutation production weights only
when a grammar-table proof shows that a maximum-weight alternative has the
minimum required node cost at every supported depth. Such an alternative is
feasible whenever any alternative is feasible, so its dynamic normalization
factor cannot change with the remaining node budget. Unsupported tables retain
the existing scan and division. This removes repeated maximum scans/divisions;
it does not change selection weights, RNG draws, node/depth admission or grammar
membership. Tests compare complete constructed nodes/constants across different
budgets and depths, including the unproved fallback case.

`GAGP_BUCKET_WORKSPACE=1` lets compact and generic capability buckets use
separate grid sizes while sharing scratch sequentially. Frame allocation uses
the larger required byte span, and memo storage uses the larger thread count.
The planner reserves at least one generic block and constrains the sum of these
allocations to the existing 512 MiB workspace ceiling. Declared per-program frame
and memo capacities are unchanged. This removes a common worst-case frame
stride from compact-bucket concurrency; it adds no program/case skipping.
`GAGP_BOUND_COMPARE_PHASE=1` recognizes verified typed phases consisting of one
non-parameter local load, one Int constant and an Int LT/LE/GT/GE comparison,
with either operand order and optional Return. Exactly one matching binding is
required; captures and extra bindings retain the interpreter. The direct phase
uses the same double conversion and every original instruction fuel charge.
The recognized form is a general bytecode condition, independent of task names,
answers or particular comparison constants. Tests include operand reversal,
large integer rounding and fuel/return boundaries against CPU execution.
#### Owner-bound packed executable carrier

Initial packed-table construction reuses the private owner's cold GPU bytecode
verification and recorded stack bounds through `OwnedGpuPrograms`. The borrow
is synchronous, after every imported executable passes the existing verifier
and before any reproduction or program mutation. External import validation,
capacity checks, and subsequent checked GPU lowering remain unchanged; no public
verification flag is accepted. The phase-population regression checks packed
and ordinary initialization with import reuse on/off, external mutation,
empty/oversized populations, and CPU/GPU scores after reproduction.

`GAGP_OWNED_PHASE_PACKED=1` retains one private packed root/region/binding
layout inside `NativePhasePopulation`. Cold admission still verifies every
external AST and bytecode. A checked unique slot-to-phase map binds each GPU
compiler result to its original phase. Each generation preserves fixed phase
storage and replaces current variable `DInstr`/constant ranges directly; it no
longer expands them into `Instr` plus a separate fuel vector merely to pack
those values back into `DInstr`. Stack bounds and scalar/add/comparison hints
are refreshed after replacement, with stale fields cleared first. Static
structure, original fuel costs, limits and full population/case evaluation remain.

`OwnedGpuPrograms` can borrow this carrier only from the private run owner for
a synchronous evaluation. No mutable pointer, public certificate constructor or
serialized admission shortcut is added. Native bytecode is materialized on
`programs()` access or before fallback capability analysis for a session whose
input types differ from the owner's proof. A returned reference view must be
requested again after reproduction. External packing continues full validation.
Specialization switches are fixed for an owner's lifetime; instantiate a new
owner when changing constant/add/comparison or generic-phase configuration.

Cold packing is charged to initialization; current-generation integration and
per-evaluation carrier copying/upload remain charged normally. This path retains
D2H/H2D transfers and host metadata; it is not device-resident executable reuse.
Storage contains the fixed prefix and current variable phases only. Buffers may
retain their bounded live high-water capacity, never ancestral code or ASTs.
Tests compare each generation against independently verified GPU and CPU
execution, exercise changed-input fallback before reference materialization,
and check stale optimization hints across growth, shrink and constant changes.

The native GPU grammar generator stores each pending task in eight bytes using
exact17-bit expression,7-bit depth,4-bit scope-count and24-bit scope-map fields.
These cover existing admitted limits (at most65536 source expressions plus an
entry wrapper, depth64, eight lexical slots); this does not narrow supported
grammars or change RNG/operator semantics. Invalid expression IDs are rejected
before assignment, and reservation subtraction is checked before unsigned
arithmetic. The16-byte task layout is instantiated only by differential tests,
not exposed as a runtime experiment setting. Test coverage includes complete
node/origin/constant/fuel/status/step equality, large expression IDs, maximum
depth and lexical permutations. This reduces pending-array traffic, not the
kernel's reported maximum local frame: other native operations still require
that frame. Measured improvement is modest and concentrated in reproduction.


`GAGP_GPU_FUSED_DONOR_SITES=1` emits private mutation-donor site metadata while
constructing grammar nodes. The admitted variation profile has identity scopes
and no zero-node aliases, so a single immediately selected reference record
identifies the next emitted node's nonterminal, entry expression and parent scope.
Non-reference descendants start without a site. This avoids reparsing the newly
generated tree and scanning its productions solely to recover construction facts.
A distinct mutation-kernel specialization removes the annotation pending stack;
the compact eight-byte generation task representation is unchanged.

External import still uses independent annotation and admission. Differential
tests compare fused sites against the annotation oracle, every generated node,
constant and RNG step, followed by 32 generations of scoped/unscoped variation and
independent grammar/CPU validation. This switch changes neither the search
profile, grammar support, random draws nor splice/resource validation.


Three native owner policies reduce temporary reproduction work without changing
logical sites, RNG draws, genotype nodes, grammar scope or execution fuel:

- `GAGP_GPU_DEFERRED_CROSSOVER_COPY=1` keeps the selected parent immutable while
  attempting the splice. On success it materializes the other phases once; on
  exhaustion it materializes the complete parent. A failed speculative output
  cannot escape. This avoids copying the changed phase before overwriting it;
  it does not retain shared ancestors between generations.
- `GAGP_GPU_FUSED_SPLICE_DEPTH=1` validates structural depth while constructing
  the replacement output, eliminating the second output traversal. Capacity
  rejection and source-site metadata remain unchanged. This is fused metadata
  construction, not incremental bytecode compilation.
- `GAGP_GPU_COMPACT_COMPILE_FRAMES=1` lowers using twelve-byte temporary frames
  holding an immutable node index, stack/jump offsets, argument tags and child
  count. Signed sixteen-bit fields cover the native owner's 1024 nodes and 4096
  instructions. The helper retains its 4096-node contract and falls back to
  ordinary-width indexed frames when code capacity exceeds 32767. Every node,
  type, branch, capacity and fuel check still executes. No narrowed compiler
  limit, cached executable or persistent shared genotype is introduced.

These settings are captured when constructing the native population owner.
Lowering frames borrow nodes only until the kernel returns; the private source
and bytecode output buffers remain disjoint. Three-way compiler tests compare
copied-node, indexed and compact frames against independent CPU instruction,
constant and fuel output, including malformed input and large-buffer fallback.
Owner tests compare exact genotypes, full-case fitness and operator accounting
for 32 generations with each policy. Initialization, transfers, packing and final
materialization remain included in their existing timing boundaries.


`GAGP_GPU_CONSTRUCTIVE_EXPORT=1` fixes an AST-only export policy for a native
population owner's lifetime. `ProgramGenome::derivation` is optional cache data;
this policy returns it empty rather than reverse-parsing an already constructed
member solely to attach a canonical witness and optional compiled cache. The
private `NativePhasePopulation::Impl::finish_export` is the only publication path
that uses this ownership argument. It does not accept an externally asserted
validity bit or issue an admission certificate.

The cold fixed-skeleton comparison serializes constants in fixed-node traversal
order, copying their exact `Value` identities. Parent-dependent constant-pool
interning order and duplicate table entries therefore do not force fallback.
This normalization applies only to the comparison artifact, after each input's
ordinary admission; fixed values, plans, bindings and fuel remain part of the
key. The runtime AST and grammar membership contract are unchanged. Regression:
`gagp_test_gpu_phase_layout` checks reordered pools and rejects changed constants
(including integers above 2^53 and payload identities), plans, bindings and fuel.

Cold import admits the immutable fixed skeleton, phase contracts and initial
members. The admitted GPU generator samples grammar productions and constant
domains. Crossover and mutation preserve exact nonterminal/identity-scope sites;
physical node/depth checks enforce the materialized search limits. This owner
does not support a separate projected offspring budget; authored projected
resource charges are not interchangeable with these materialized limits.
Only these checked operators write the private live genotype. Export still
performs every `export_native_phase_layout` expression-origin/opcode/arity/fuel,
constant-index, lexical-index and fixed-skeleton transport check and metadata
compaction. Thus membership follows from construction; this is distinct from a
canonical derivation witness. External AST import and explicit membership APIs
retain their ordinary strict validation, including on a modified export.

Tests clear supplied derivation caches before independent all-member admission
and CPU evaluation over 32 generations, compare member/bulk ASTs and metadata
against canonical-cache export, and reject a modified external replay. Full
frozen-population snapshots are independently decoded, admitted and evaluated
on CPU/GPU by an archived runtime with certificates disabled. Report process
wall and post-search audit cost as well as search wall: callers that explicitly
need a canonical witness or an executable cache still pay to construct it.

### Identity-checked import reuse

`GAGP_REUSE_IMPORT_CERTIFICATE=1` lets native phase import retain an existing
private admission certificate. Import still calls `reconstruct_derivation`,
which checks grammar, request and full runtime identity before using the sealed
witness/VerifiedAst; public derivation fields cannot authorize an import. A
missing or stale certificate runs ordinary admission. The bytecode reuse lookup
also checks actual evaluator input order and fuel; on a miss it lowers normally.
Registry-dependent constants do not qualify for this scalar executable reuse.
This removes duplicate cold admission/lowering after population initialization,
not the external validation boundary, phase-profile checks or GPU packing checks.
The phase-layout test covers forged public witness fields, changed AST constants,
changed grammar limits and reordered evaluator inputs.

`GAGP_COLD_PAYLOAD_PROOFS=1` additionally uses short read snapshots around
replayed-population admission and native layout-import membership. Together with
`GAGP_REUSE_IMPORT_CERTIFICATE=1`, registry-backed constants, including empty
IntLists, can retain and reuse the same private membership proof. Each lookup
still checks grammar, request, and exact decoded payload identity; a miss runs
full membership, AST, resource, and lowering validation. The scope reads only
that genome's constant closure and ends before GPU transport or generations.
It may retain one copy of each distinct resident payload in that closure;
public registry bytecode executable reuse remains disabled. An unchanged read-only
snapshot permits publication; changed reads or any staged writes discard the
candidate and retry ordinary admission. No staged writes are committed, and an
existing enclosing scope keeps its original transaction responsibility. This
flag defaults off for same-binary cold-time A/B measurements; all work remains
inside the existing admission timing boundary. `gagp_test_grammar_membership`
covers identity changes, inherited scopes, discarded writes, and conflict retry.

`GAGP_COLD_IMPORT_EXECUTABLES=1` optionally retains immutable bytecode already
lowered during cold admission, including registry constants. It requires the
existing `GAGP_COLD_PAYLOAD_PROOFS=1` read-only snapshot and
`GAGP_REUSE_ADMISSION_COMPILE=1`. The existing import-certificate and imported-
executable settings retain reuse across initialization and owner construction.

`GAGP_FUSED_COLD_EXECUTABLE=1` lets that same admission return its already
validated immutable executable, avoiding another full identity serialization.
The private accessor runs only after the current invocation either checked the
complete certificate identity or constructed a fresh certificate. It still
checks the actual evaluator input order. Registry constants additionally require
the current read scope and exact raw tags/bits/markers; speculative artifacts
remain inside the existing snapshot until its reads validate. Scalar-only
admission can return its private artifact without a registry transaction.
Missing artifacts and mismatched input order retain ordinary lowering. Public
registry lookup and GPU bytecode/physical-capacity verification are unchanged.
Tests: `gagp_test_grammar_membership`, `gagp_test_gpu_phase_population`, including
stale scalar provenance, registry aliases, disabled flags, reordered inputs and
multi-generation full CPU/AST checks on staged and window paths.
A private cold-import lookup checks the current grammar, request,
complete decoded AST/constant identity, exact raw constant tokens/markers,
evaluator input order and fuel inside that snapshot. Only validated, write-free reads publish the candidate; writes or
conflicts discard it and retry ordinary admission/lowering. Public mutable-AST
executable reuse still rejects registry constants. The artifact owns its copied
constant tokens exactly like ordinary bytecode and does not freeze registry
contents for execution. This removes a second cold lowering, changes no evolved
phase or generation execution, and retains at most the current cold certificates'
compiled artifacts plus exact constant-pool copies. The default is off; allocation and lookup remain in normal
initialization timing.

### Experimental pure-measure evaluation reuse

`GAGP_MEASURE_PHASE_CACHE` leaves native reproduction and the evaluation pipeline
unchanged. In the existing view-profile fitness kernels (compact and generic), each block may precompute
its current program's Int preparation phase for sequence lengths 2..64. The
private typed-view proof and sequence-window shape with sequence slot 0 are required; all declared bindings must be Measure,
all constants Int/Bool, and the allowed scalar instructions have forward-only
control flow. Captures, state, payload operations and other shapes decline.
The shared table is rebuilt for every program and launch, never shared across
executable identities. Its allocation, validation and preparation stay timed.

All program/case pairs are evaluated. A cache entry stores the original VM
result/error and exact instruction fuel spent. Insufficient remaining fuel,
lengths outside the cache, and precomputation timeouts use ordinary execution.
No precision, operation, error, grammar, RNG or search-rule change is intended.
The 64-entry range bounds only optional reuse, not the supported input domain.

Preparation uses the same per-phase dispatch as an ordinary invocation, including
large-stack generic execution. Dynamic shared memory is reserved only when the
optional table fits the device limit; otherwise ordinary execution is retained.

`GAGP_HYBRID_PHASE_EXECUTOR` keeps the existing typed/direct-root and structural
region proofs but admits a phase stack bound up to the existing MAX_STACK (64).
Each phase requiring more than 16 slots calls the original IntListViews tagged
VM, with the same binding, fuel, error and result checks. Other phases and region
frames can remain compact. This does not reduce a resource limit or alter a
program. The normal generic backend remains available when the existing type,
payload, local-capacity or structural proof fails.

`GAGP_PER_PHASE_STACK_BOUND` preserves the private GPU compiler's bound for each
changed phase. Fixed phases retain their validated cold-import bound. The owner
still keeps the whole-program maximum for population admission; no public
metadata is trusted and no declared stack capacity is reduced. The diagnostic
`GAGP_GENERIC_PHASE_VM` override takes precedence over compact admission.

Three optional evaluator changes compose with those proofs:

- `GAGP_GPU_WINDOW_LEAF_DIRECT=1` executes the evolved base phase of an intrinsic
  zero/singleton child without writing and rereading its global frame. Carried
  state evaluation, the child depth check, entry fuel and base errors keep their
  original order. It does not replace the base expression.
- `GAGP_WINDOW_SOA=1` stores unboxed window-frame fields in planes across case
  lanes. The allocated bytes, frame limits and logical frame contents are
  unchanged; generic regions keep their existing representation.
- `GAGP_DYNAMIC_PROGRAM_QUEUE=1` lets an idle physical block claim the next
  complete program. That block retains its workspace until its program ends;
  results use original population indices. Every case and the original fitness
  reduction order are retained. Queue allocation and initialization are timed.

These options change execution and storage only. Native selection, crossover,
mutation, RNG draws, grammar membership, population, cases and numeric/fuel
semantics remain unchanged. The fixed benchmark's complete-search timing is
`evolve_wall_ms` (cold evolution setup, all generations and final evaluation);
input parsing and post-search CPU audit/export remain visible separately in
process wall time, as in the existing comparison contract.


Native GPU phase construction also admits `singleton(Int)`,
`concat(IntList, IntList)`, and finite domains containing only empty `IntList`
constants. Nonempty registry-backed constants still require the ordinary
fallback. Empty literals use the canonical registered value; current phase
constants enter the existing per-evaluation payload collection. Construction,
variation and lowering remain GPU operations. Allocating list operations and
empty list literals clear the private typed-view proof, so evaluation uses the
existing payload-aware GPU VM; they never inherit a scalar/view capability
certificate. This includes list and tuple carriers when all their remaining
productions and the fixed region skeleton satisfy phase admission. Tests:
`gagp_test_gpu_phase_compile` and `gagp_test_gpu_phase_layout`.

The integer-codepoint `is_letter(Int)` overload is also admitted to phase
construction/lowering and the CPU/GPU typed view interpreters. The shared
capability proof requires an Int input and Bool output; the operation preserves
the view certificate and uses the builtin's ASCII classification with ordinary
operation fuel. The Char overload remains on the generic executor. Boundary
coverage lives in `gagp_test_cpu_execution_session`, with GPU phase proof and
bytecode comparison in `gagp_test_gpu_phase_compile`.


## Completed fitness of exact unchanged offspring

`GAGP_GPU_UNCHANGED_FITNESS_REUSE=1` is fixed when a native GPU phase
population is constructed. Its post-elite device comparison proves every tree
node, origin, constant tag/bits/flag equal to the selected prior parent. Scores
are eligible only for finite, fully scored rows from a successful completed GPU evaluation, for the same
immutable fitness-session revision and either that generation or its immediate
successor. Reinitialization (including a failed attempt), newly resolved payload backing, another session, a
missing evaluation, or absent exact equality forces ordinary full evaluation.
Selection, RNG, GPU variation and lowering are unchanged. No CPU reproduction,
approximate program equality, output prediction or cached payload result is used.

The GPU skips only certified rows. Host copyback restores their exact scalar
fitness, wrong and logical error/timeout/case counters. `fitness_reused_programs`
and `fitness_reused_program_cases` expose inherited observations; physical root
calls and actual payload replay remain separate. Failed/unscored evaluations
never supply reused scores. Internal complete counters are retained when this
option is active, while callers requesting no diagnostics still receive no
case-counter vector. `gagp_test_gpu_phase_population` covers exact multi-generation
AST/CPU parity, repeated evaluation, elite replacement, reset/fuel/policy changes,
separate sessions and unscored generation gaps; host scope guards are tested in
`gagp_test_fitness_contract`.

`GAGP_GPU_PARENT_FITNESS_CACHE=1` additionally indexes every previous-generation
genotype on the GPU. It requires unchanged-fitness reuse and is also fixed at
owner construction. A post-elite child that differs from its selected parent
may inherit another parent's completed score only after every phase node,
origin, constant tag, raw numeric bits and boolean flag compares equal. The
fixed region skeleton and decoding context belong to the same private owner.
Hashes locate candidates; hash equality alone never authorizes reuse. Up to64
open-addressing probes are attempted; collisions or saturation can decline
reuse and retain ordinary evaluation. The table is rebuilt before stage-storage
overwrites the previous-generation trees. Its buffers are outside the shared
scratch bank and included in resident-byte accounting (256KiB at P16384).

The same successful-score/session-revision/generation guards above still apply.
Index construction and exact lookup are charged to reproduction time. Selection,
variation, RNG and offspring remain unchanged. The phase-population test includes
forced hash collisions, full-table misses, signed-zero and individual genotype
field differences, plus full CPU/AST comparisons across generations and session
changes. This opt-in is experimental; admission and measured gains are recorded
per immutable study attempt.


## Native GPU allocation scope

`GAGP_GPU_CUDA_POOL=1` enables a dedicated CUDA stream-ordered allocation pool
only during the native phase path, including its cold import. Selection, RNG,
GPU variation, evaluation and physical capacities are unchanged. Generic and
overlap paths keep their existing allocation behavior. The device's current pool
is never changed. Native operations use the existing default stream; this scope
does not promise ordering for arbitrary external streams.

The 16 GiB release threshold retains unused pool backing between native stages;
it does not reduce or replace the independent 16 GiB evaluation-workspace budget.
Live genotype and workspace storage can exceed the retention threshold. Scope
completion synchronizes and trims unused backing. Session buffers that outlive
the native owner retain shared ownership; their later matching free releases
the final pool. Pool creation, retirement and cleanup remain inside timed evolve
calls. Unsupported pool devices and allocation errors remain explicit failures.

Allocation ownership lives in `runtime/gpu/allocation_pool.cu`; wrappers are
used by native tree buffers, fitness scratch and host packing. The C++ scope
header is private and CUDA-header-free. `gagp_test_cuda_allocation_pool` checks
stream ordering, dirty reuse, nested scopes, late frees, error forwarding and
unchanged device-pool selection. `gagp_test_gpu_phase_population` compares pooled
and ordinary native searches with full final AST/champion/fitness/wrong/CPU
checks, including private staged-list evolution and an escaped fitness session.


## Single-elite GPU reduction

`GAGP_GPU_LINEAR_ELITE=1` is fixed at native population construction. With one
elite, a 256-thread GPU reduction selects the greatest fitness and breaks ties by
lowest source index, then copies its phase slots into offspring index 0. This
replaces quadratic rank scanning with linear work; selection RNG and variation
are unchanged. No elites and multiple elites retain existing behavior. NaNs are
rejected by the existing reproduction precondition. The ordering oracle covers
98 populations through 16384, including signed zero and infinities. Native
generation comparisons cover complete AST/fitness/wrong trajectories, CPU
evaluation, ties, infinities and the zero/multiple-elite fallback.


## Shared storage for sequential GPU stages

The staged native path uses one backing for reproduction scratch and evaluation
workspace. Pooled staged owners activate it automatically; `GAGP_GPU_STAGE_STORAGE=1`
also enables it with the metadata pool disabled. The configured evaluation cap
remains the configured payload budget (16 GiB in the twofold study). Another
64 KiB covers allocator alignment only. At most 256 transient
allocations are tracked; unsupported capacity or lifetime overlap is an error.

The current population stays outside this backing. After variation and exact
parent comparison, a GPU kernel copies active offspring nodes, origins, sites
and constants into that fixed current storage. It is inside reproduction kernel
timing. All scratch retires before the next stage. This prevents surviving
offspring from fragmenting a freed workspace allocation. Default-stream
ordering, RNG, operators, AST/depth/phase limits, scoring and replay are unchanged.
Physical reserved/peak stage bytes are reported separately from logical genotype
allocation sizes and the whole-card sampled VRAM peak.
