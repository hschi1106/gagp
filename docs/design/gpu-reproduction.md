# GPU Reproduction

## Purpose

GAGP reproduces on the GPU only through the resident phase engine
(`representation: resident`, the default of `gagp run`). The population lives
on the device as fixed phase slots; tournament selection, typed crossover,
mutation, elitism and lowering to bytecode run as GPU kernels, and evaluation
reads the lowered programs without a host round trip. `HostPhasePopulation` is
the CPU reference engine with the same operators; GPU and CPU runs of the same
seed follow the same operator schedule, and the parity tests compare them.

A grammar the resident profile declines runs as the native AST representation
(`representation: native_ast`). Native AST reproduction always runs on the host
(`cpp/src/evolution/repro/backend.cpp`); evaluation still uses the selected
engine. Earlier releases also had a GPU backend for native ASTs and CPU/GPU
reproduction ablations; they were removed in release 3.0 because the resident
engine covers the same grammars faster.

## Resident engine


This is the representation of the research mainline; the
[research mainline overview](research-mainline.md) explains how it fits
together. This section remains the owning implementation contract.

`representation: resident` (the default search of `gagp run`) requests a
run-owned genotype: on the device with GPU evaluation and GPU reproduction, or
on the host with the CPU reference engine. It uses one host thread. If the
profile declines the grammar, or cold import declines an initial member,
`evolve_population` reruns the same search with `native_fallback_config`
(native AST, host reproduction, unchanged evaluation engine) and records the
reason in `EvolutionResult::resident_declined`; `gagp run` prints it and writes
it to `result.json` ([search_config.md](../../spec/search_config.md#resident-admission)).
Only admission declines fall back; policy errors still fail. This profile admits one fixed root region template with
independent whole-phase holes, identity scopes and unit variable-phase resource
charges. A grammar without templates whose entry is an expression is
admitted as one whole-program phase: the entry expression is the only variable
phase, grammar inputs lower to preset locals in evaluator input order, and the
lowered code is completed by the native program terminator
(`patch_whole_program` in `phase_layout.hpp`). Phase expressions may use all
eight value types, any catalog operation whose arity matches, grammar inputs,
finite constant domains of any type and Int ranges.
A Program entry without templates is admitted the same way, with the whole
Program as its phase and no appended terminator: statements (`assign`,
`return`, `if_stmt`, `for_range`, blocks), up to 16 grammar locals and named
input targets lower exactly as the native compiler does, including the
first-use local order and for-range temporary reuse (`compile_phase_tree` in
`phase_compile.cuh`). Statement programs are limited to 512 nodes. A child that
reads a local before native definite assignment would define it
(`phase_locals_defined`) is rejected: crossover tries its next site, mutation
keeps the crossed child, and initialization draws again. Sites of a
nonterminal with `"variation": false` count toward site depth but are never
chosen. Physical and site-depth budgets ignore authored resource charges;
projected budgets still require unit charges.
Whole-program phases may also use `let` binders. Binder numbers are static
(`compile_phase_grammar` in `phase_grammar.hpp`): every nonterminal is entered
under exactly one environment, a list of binder numbers whose root of scope
size n is 0..n-1. A `let` binds one more than the largest visible number, a
traversal binds the next three, and scope maps of references, templates and
holes project the enclosing environment, so a bound reference's operand is
fixed in the profile and generation never renumbers. A grammar that reaches a
nonterminal under two environments, or needs a binder number of 16 or more,
declines the profile. A number may be rebound while a scope map hides its outer
owner; lowering saves and restores the owner's local. Lowering mirrors the
native compiler: the binder temporary is allocated before the value, and
temporaries are recycled LIFO when the body ends. Programs that contain a
binder or a checked conversion keep the native per-instruction fuel table. A
`let` bind fuel charge prices its binding store. Let binders inside the phases
of the region-template layout above decline the profile.
Whole-program phases may also contain the four traversals (`traverse`,
`traverse_reverse`, `traverse_range`, `traverse_range_reverse`) as
`PhaseTreeKind::Traverse`, whose operand indexes the profile's region table
(direction, range, binders, result types and per-event fuel costs). Lowering
reproduces the native prefix exactly, including its aliasing of stored sources,
zero-cost events and length facts, which the phase compiler tracks per binder
number through lets and `if` conditions comparing a length with zero.
Templates and holes are zero-node wrappers (`PhaseExpressionKind::Alias`): a
site on a template production passes to its first physical node, and a site is
mapped back to its production through the wrapper chain, so production roots
must resolve to distinct nodes. A hole repeated in its template body is
admitted only when its fill is fixed (no reference and only single-value
constants). A genome uses the region-template layout only when it has
templates, exactly one bounded region and the grammar has no `let` or traversal
primitive; every other admitted genome is one whole-program phase.
Lowering tables are fixed: at most 8 open traversals, 16 lets, 16 loops and 16
analysed value `if`s on one path, and 64 locals. The profile bounds each over
every tree within the physical depth budget (a traversal holds at most nine
temporaries, a let or loop one, next to the inputs and grammar locals) and
declines a grammar that could exceed one, so variation never produces a program
it cannot lower.
Whole-program phases may also contain bounded regions (`PhaseTreeKind::Region`,
at most 8 operands plus phases, 8 captures and 4 regions per program). Operands
lower in place; each phase is a separate program whose locals start with its
bindings, and a phase child sees only those bindings (`child_scope`), so
variation inside a phase uses the phase's own lexical scope. Input, local and
lexical captures resolve to caller locals exactly as the native compiler does.
The phase compiler appends one record per region after the main code
(`PhaseCompileResult`), and `patch_whole_program` assembles them into the
native `BoundedRegionSegment`s, which `verify_bounded_region_segment` checks.
Region phases may not reach inputs, grammar locals, statements or nested
regions, matching the native compiler. Grammars whose only
member is one fixed program (no nonterminal reference and no constant choice)
use the native fallback.
Fixed skeleton charges must be at most one node/level without depth resets.
The profile conservatively enforces physical whole-program node/depth budgets,
including zero-charge administrative nodes; weighted/reset skeletons and inputs
whose physical size exceeds those budgets explicitly fall back. Import
independently checks grammar membership; type compatibility alone is insufficient.
In the region-template layout, repeated holes, lexical capture, nested
templates, projected budgets and mixed root contracts decline this profile. Declared execution capacities are preserved.

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
counts may include children subsequently changed by mutation.
Results must identify this profile and cannot be presented as identical evolving
populations to native AST reproduction. GPU memory is bounded by live population and
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

### Shared operator bodies and the CPU reference engine

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

The synchronous batch steady state (`schedule.mode: steady_state`) runs on both
owners through `run_phase_population_steady` in `evolve.cpp`. Each owner
implements `begin_steady` / `make_batch` / `evaluate_batch` / `batch_nodes` /
`commit_batch`: children of one round occupy offspring rows `0..batch-1`, the
operator kernels take the child count separately from the parent population,
the GPU owner lowers and evaluates the batch through a second packed program
set (`ProgramSet batch`, reusing identical-member results through the completed
fitness path), and `place_children` copies the placed rows into the population.
Staged owners allocate the batch buffers per round (inside the stage bank when
stage storage is active), lower the batch, then `stash_children` copies the
child rows compactly into persistent buffers and the batch buffers are released
before evaluation, so the bank serves the payload workspace; `place_stashed`
places from the stash. Member programs are not lowered after `begin_steady`. Each
evaluation launch has a large latency floor, so `batch` below the population
size is slower than the generational loop; the default `batch` is therefore the
population size (see [research mainline](research-mainline.md)).

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

### Grammar-invocation depth and parsimony

`EvolutionConfig::gpu_phase_site_depth` optionally caps per-phase nesting of
independently mutable grammar invocation roots (1–15; zero disables the policy).
Physical grammar node/depth and per-phase GPU capacities remain separate checks.
Cold import annotates and checks every initial member; crossover checks the
spliced site's annotated depth, and mutation samples from a joint physical-node,
physical-depth and invocation-depth feasibility table. Synthetic nodes inside a
production consume physical resources but no invocation level. The profile is
`resident-gpu-v4-site-depthN`. It requires a resident owner and cannot
combine with authored projected-resource budgets. This counts private construction
sites, rather than treating canonical projected resources as interchangeable.

`gpu_phase_site_parsimony` independently uses the number of mutable grammar
invocations to break fitness/wrong ties. It requires a task metric with wrong
counts, keeps the original index as the final tie break, and adds
`-site-parsimony` to the profile. It changes selection; it is not a performance
shortcut. Device metrics are checked against independent canonical derivations
in the owner regression. Matching a source's counts on audited programs does not
establish equivalent RNG, donor filtering or search distributions.

### Optional CPU training reference verification

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

### Optional batched native GPU import/export transport

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
### Owner-bound packed executable carrier

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
exact 17-bit expression, 7-bit depth, 4-bit scope-count and 4-bit projected
depth fields; the remaining 24 bits stay zero because binder numbers are static.
These cover existing admitted limits (at most 65536 source expressions plus an
entry wrapper, depth 64, 15 lexical slots); this does not narrow supported
grammars or change RNG/operator semantics. Invalid expression IDs are rejected
before assignment, and reservation subtraction is checked before unsigned
arithmetic. The16-byte task layout is instantiated only by differential tests,
not exposed as a runtime experiment setting. Test coverage includes complete
node/origin/constant/fuel/status/step equality, large expression IDs, maximum
depth and an eight-slot reference. This reduces pending-array traffic, not the
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
GPU variation, evaluation and physical capacities are unchanged. The native AST
path keeps its existing allocation behavior. The device's current pool
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
