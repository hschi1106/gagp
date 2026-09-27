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
the population, clamped to 128–4096 entries. This retains parent analyses through
preparation and decoding while allowing bounded headroom for generated children;
entries remain exact-runtime-identity keyed and eviction does not relax admission.

Parent analysis warm-up uses bounded batches of at most 128 programs with up to
20 readers (limited by hardware concurrency) in the GPU backend. Each reader reconstructs native
membership, lowering and variation sites without writing the payload registry or
compatibility registry. After all readers join, the owning thread commits results
in population order and interns compatibility IDs in site order. Exact decoded
runtime keys are recomputed before cache lookup, including payload liveness.
Payload registration during parent warm-up is prohibited. Populations below
32 members use the serial path. Failed analyses propagate in population order;
no worker survives the call, including exception unwinding.

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
prefetch. Up to eight workers process independent pools while preserving the
returned job/seed order. Each pool stages payload writes; only a successful atomic
commit publishes results. On conflict or unsupported batching it returns no result,
and the caller must replay its original complete interleaved preparation. Merely
generating all pools sequentially before later assembly would not preserve payload
collision behavior. Single-seed pools keep their existing caller-registry path.
Batches are limited to 128 jobs, 64 seeds per job and at most 1,048,576 declared
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
128–4096-entry warm-handoff capacity and never accepts caller-supplied provenance.
Unavailable handoffs, enclosing transactions, failed remapping or changed reads
use full compacted-population analysis. No-removal compaction skips the extra
analysis stage.

Donor-pool calls inside an active staged payload transaction use sequential generation in the caller's view. Optional cross-pool batching declines in that situation. This keeps uncommitted destination values visible and leaves all donor writes owned by the enclosing transaction, including rollback. Worker-local transactions are used only outside an enclosing transaction.

Mutation preprocessing may preview donor work for a bounded window of 128 parents. A copied RNG reproduces the existing site-shuffle and donor-seed schedule; only the anticipated subtree-mutation site creates a pool job. Bounded pool workers generate in parallel, then commit payload transactions atomically. The ordinary loop consumes results in its original parent/site/seed order, checking the preview against the actual schedule. If speculative work declines or fails, it publishes no writes and the loop generates donors through the existing per-site path. Populations below the window size and crossover preparation bypass prefetch. This changes scheduling, not proposal weights, seeds, fallback decisions or acceptance validation.

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
Public preprocessing accepts no such handoff. Retention is capped by the
analysis-cache capacity; oversized populations and sequential/enclosing-scope
warm-up paths use ordinary preprocessing. Registry IDs are still published in
population order. Tests compare candidate fields, identities and donor ordering
with public preprocessing, including a changed unused String constant payload
that must invalidate the saved identity.

Prepared backend state also retains an opaque parent-certificate continuation
bounded by the warm-analysis capacity. It owns the exact immutable packed source
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
