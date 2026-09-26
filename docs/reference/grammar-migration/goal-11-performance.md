# Goal 11 performance preservation

Current status on 2026-09-24: Goal 11 remains incomplete. The user replaced exhaustive
performance acceptance with the representative protocol recorded in the local
Goal 11 plan and the maintained [experiment protocol](../../guides/experiment-protocol.md):
at most six workloads, one outer warm-up,
three measured pairs, and a 5% degradation allowance. Older strict-gate requirements
and results below are historical records, not current acceptance requirements.
Source-budget generation and variation checks are implemented. Scoped donor,
template, placement and replacement-boundary evidence is recorded below. The
representative performance gate and final-candidate integration remain pending;
a universal arbitrary-offspring closure proof is not an additional acceptance gate. The latest retained p1024 candidate measures 6172.874035 ms CPU and
441.599888 ms GPU, a 13.978432x speedup in one diagnostic pair. The recent
frozen-reference check was 41.527071x; the candidate retains 33.66% of
that speedup and still shows a material regression. Reference and latest
candidate were not measured in the same pair. The latest candidate uses private
prepared-parent certificates, warm identities during packing, and immediate private
execution after owned preparation.
Targeted GPU backend ownership-boundary and evolution CPU/GPU parity checks pass;
final representative acceptance and final integration remain pending.
The evidence does not support completing Goal 11.

The draft manifest contains six logical workloads and twelve measurement rows,
not the superseded eleven-mode exhaustive matrix. Its schema and dependency
hashes were revalidated in `current-mapping-evidence-audit.json` under the
representative artifact root. All 24 retained-population output hashes match
their recorded evidence, covering 5,376 members. Those checks used an earlier
binary; they establish neither arbitrary offspring closure nor current-binary
final integration. Six earlier direct/overlap comparisons agree in populations,
fitness and counters. Historical entries below retain their original scope and
must not be read as current requirements or current-candidate measurements.

The partial DP2D profile now completes two real evolution generations on both CPU
and Snoopy GPU (`goal-11-dp2d-profile-02`). It preserves all 64 frozen members and
uses source node/depth limits 80/7 with physical limits 531/141 and fuel 20000.
Administrative constants are fixed template nodes; coordinate and phase mutation
use explicit donor construction rules without helper alias sites. CPU recorded
30/33 mutation attempts and GPU 32/36, with 32 crossover attempts per generation.
Both runs recorded zero generation rejections, acceptance rejections and fallback
children. GPU reproduction kernel time was positive in both generations. These
are functional diagnostics, not paired timing or identical-trajectory evidence.
Fresh DC, DP1D and cross-backward DP2D root donor alternatives remain missing;
this partial profile cannot establish complete source search equivalence.

Follow-up `goal-11-dp2d-profile-03` adds source-shaped DP1D backward1 and DP2D
cross-backward donor constructions: coordinates 4 and (2,2), solve value 1,
and dependency-plus-one / sum-of-dependencies transitions. Their domain bounds
and derived memo/frame capacities match the frozen adapter. Separate destination
rules admit literal mutation and recursive addition, while root mutation uses
the fixed construction entry. Two-generation CPU and GPU runs completed with
zero generation/acceptance rejections and zero fallback children. These runs do
not prove that every donor branch was sampled. DC construction, complete source
crossover language/scope closure and independent donor equivalence checks remain
pending; this is still a partial profile, not final representative acceptance.

`goal-11-dp2d-profile-04` corrects a source-contract mistake in profiles 02/03:
the frozen mutation and crossover implementations both exclude every subtree
inside an ASGP phase body. Those earlier profiles incorrectly allowed phase
subtree replacement. Profile 04 disables those sites while retaining mutable
phase constants and restores the fixed dependency/addition shapes. It also adds
the DC root constructor (source [1,2,3], index(xs,0), split 1, left+right), giving
root donor weights 0.82 literal and 0.06 for each structured family.

Its initial generic DC frame bound 65535 failed GPU admission (capacity 128);
the failure is retained under `initial/`. The profile's reachable DC sequence
length is at most four (initial length three, replacement length zero to four),
so four live frames suffice. The corrected profile completes two CPU and two GPU
generations without generation/acceptance rejection or fallback children. This
does not establish final equivalence: DC Map/Filter sequence donors and original
cross-site compatibility still need integration. In particular, template-hole
contracts currently distinguish coordinate sites that need comparison with the
source compatibility rules. Earlier successful execution is not evidence that
those operator eligibility contracts were correct.

`goal-11-dp2d-profile-05` adds recursive DC sequence membership and the frozen
replacement mix: 0.65 ordinary IntList literal, 0.175 identity Map over [1,2],
0.175 true Filter over [1,2]. Map's bound body is excluded from subtree sites;
Filter's Bool constant remains eligible. The traversal templates follow the
transition lowering's fuel events and charge only original source nodes. Two CPU
evolution generations completed. A separate 16-member sequence construction probe
explicitly covered both Map and Filter; CPU and GPU fitness vectors matched
exactly with one warmup and one measured call each. This is same-candidate parity,
not frozen-source equivalence or performance acceptance. Cross-site compatibility,
scheme restrictions and independent source donor checks remain pending.

`goal-11-dp2d-profile-06` uses explicit production crossover groups: ordinary Int
coordinates/literals share a group; structured roots separate DC, DP1D arity one,
DP2D arity one/two, Map and Filter. Default exact nonterminal/template contracts
remain unchanged for ungrouped grammar alternatives. Focused native tests verify
cross-rule/cross-hole exchanges, distinct authored groups, empty-group rejection,
and the existing atomic/default contract behavior. All 23 repository checks pass.
The grouped profile also completes two GPU evolution generations. An independent
comparison against the source candidate matrix is still required; the authored
groups and successful execution alone do not certify source search equivalence.

The independent initial-population check is now complete under
`goal-11-candidate-matrix-01`. A probe linked to frozen b049183 production libraries
collects typed-expression candidates and applies the original ASGP phase exclusion.
A separate current probe reconstructs grammar variation sites and maps them back
through the independent source expansion layout. For all 64 frozen DP2D members,
both select source nodes [3,4,5] (192 total sites), and all 36,864 cross-member
compatibility decisions agree, including 20,480 compatible pairs. This certifies
the initial population's site/compatibility contract only. Generated donor shapes,
their mutable constants and subsequent reachable populations remain separate
obligations; no timing acceptance follows from this check.

`goal-11-generated-donors-01` obtains one actual frozen-generator sample for each
of seven families: Int literal, DC, DP1D, DP2D, IntList literal, Map and Filter
(first covering seeds 0, 1, 2 and 23). Their 14 subtree candidates, all 196 pair
compatibility decisions, 14 mutable constant occurrences and seven projected
node/depth comparisons agree with the current profile. Administrative constants
are not counted as mutable source constants.

The unmodified DC lowering initially failed membership: it specializes frames to
the current source length three, while the evolving profile reserves four for
all reachable replacement lists. The comparison explicitly changes only that
descriptor's frame count from three to four. This normalization is currently
probe-local; importing DC populations needs supported integration before broad
mapping acceptance. These seven samples do not prove arbitrary offspring closure,
runtime output/fuel equivalence or performance preservation.

DC frame reservation is now integrated in the source lowering and final-candidate
import path (`goal-11-dc-frame-import-01`). `--minimum-dc-frames 4` imports the
unchanged frozen DC donor into the evolving profile without probe-local AST edits.
The GPU evaluates it to the expected value 6 (fitness zero); output records the
reservation. The eight-result-type native DC comparisons preserve values, errors
and exact fuel boundaries after increasing capacity. Adapter contract tests and
23 migration tool tests pass. The option only raises source-derived DC capacity,
does not alter DP plans or fuel, and rejects current artifacts/source-less bytecode.
This closes the import plumbing gap; profile-wide reachable-length proofs and
all selected workload mappings remain necessary for final acceptance.

The second selected profile, `goal-11-nested-profile-01`, admits all 64 original
nested-binder members, including the captured [10] literal without adding it to
the random replacement distribution. It reuses traversal templates with scoped
nested sequence/index rules. Initial candidate positions [3,4,8,9,11] match the
frozen source. An initial implicit expression envelope incorrectly added three
levels to offspring depth and caused rejected donors; an explicit source Program
envelope fixes this. The corrected profile completes two CPU generations with
zero generation/acceptance rejections or fallback children. Earlier CPU/GPU runs
are retained as failed mapping diagnostics, not acceptance evidence.

One concrete compatibility gap remains: the old scheme keys allow closed outer
and inner Map roots to exchange, while the target formal context includes an
unused outer binding and separates them. The corrected profile has not yet had
its GPU check; source-equivalent closed-subtree exchange needs integration before
this workload can enter final timing.

The six logical workloads are now fixed in
[`goal-11-representative-selection.json`](goal-11-representative-selection.json):
the canonical anchor at p64 and p1024, and p64 mixed exact payloads, nested
binders, metadata stress and DP2D. All source population and case hashes were
verified; the selection retains 1,344 members with their original multiplicity.
It is a preselection record, not a runnable or passing timing manifest. Multiple
measurement scopes belong to these same six inputs.

Inspection of the actual canonical p64 snapshot confirms that it includes loops,
Map/Filter and LinearRec, as well as ordinary scalar/container operations. The
earlier narrow Float SimpleExp demo grammar cannot stand in for that frozen
population. Final mapping must cover its full typed and structured membership;
the existing literal and budget packages alone do not establish this.

The optional native `offspring_resource_budget` is now enforced at the shared
CPU/GPU child-acceptance boundary. A physically smaller child with higher authored
cost is rejected; a valid replacement can repair an initially over-depth parent.
GPU prepared calls and reusable run resources reject a changed budget. Focused
validation in `goal-11-offspring-budget-01` passed three native tests, including
actual GPU reproduction and overlap, plus 23 migration-tool and 23 repository tests.
The 556-file source snapshot, commands and logs are sealed there. This is child
admission only: budget-aware generation/selection and initial-budget admission
remain to be integrated before claiming source-search equivalence.

The old local `goal-11-local-reference-01/resume-02` runner was intentionally cancelled
after the user revision. Its process exited with status 130 and the retained
`cancelled-by-revised-goal.json` explains the cancellation. Partial trials remain
diagnostic and are not final-candidate acceptance.

Workload manifests select the revised protocol with
`"acceptance_protocol":"representative-2026-09-23"`, `warmup_blocks: 1`, and
`measured_blocks: 3` (or 5 for one targeted recheck). Steady sessions use exactly
one initialization warm-up and one measured call, avoiding nested measured repeats.
The runner carries the protocol into trial evidence; the auditor checks it against
the workload manifest. Reports use point estimates with `Q >= 0.95` and absolute
ratios `>= 1/1.05`, without bootstrap confidence intervals. Missing protocol metadata
retains the original frozen rules. The focused tool suite passed 23 tests, including
a complete synthetic runner/audit/report flow and bounded-recheck threshold cases.

The native resource-flow integration now retains resource indexes in generated and
reconstructed witnesses. Variation sites expose reconstructed subtree costs, and
contextual donors expose payload costs without counting their standalone envelope.
This does not yet enforce projected budgets. The exact 552-file snapshot and logs
are sealed under `goal-11-resource-flow-01`: 117 native tests (19 GPU), 79 tool tests,
and 23 repository tests passed. Selected and canonical derivations can still have
different costs in an ambiguous grammar, so attached provenance is not an admission
certificate. This snapshot predates the revised measurement-tool changes above.

Budgeted construction and donor generation are now connected to native population
initialization, CPU mutation and GPU donor preparation. Initial and offspring
budgets are separate; imported single/mixed populations reconstruct membership
before initial-budget admission. Construction uses at most 64 attempts and records
the accepted seed for ordinary replay. Donors are checked by splicing into the
certified destination and reconstructing the whole candidate, including ambiguous
ancestor choices. Exhaustion is a reported generation failure, not a proof of
infeasibility. No original production alternative is deleted, but conditioning
changes the sampling distribution and is not a claim of old RNG equivalence.

Evidence is under `goal-11-budgeted-generation-01` and `-02`. CPU reproduction and
the compiled GPU backend (including overlap and canonical-cost ambiguity) passed
in the first run. One new resource test initially used a mismatched test fuel;
only that test file changed, and the focused rerun passed. Tool/repository checks
passed 23/23 each. Both source snapshots, failure, correction and commands are
retained. Certified source profiles/origin mapping, GPU crossover candidate budget
selection and final representative performance acceptance remain incomplete.

CPU crossover now checks both complete canonical child costs before reservoir
selection, preventing a pair that fits only one direction from being selected.
Membership and projected-budget rejections are counted separately. Two focused
tests passed on Snoopy; the source delta and native output are retained under
`goal-11-crossover-budget-01`, based on the generation-02 snapshot. The optional
exact admission path's preprocessing overhead is not yet measured; GPU candidate
selection and the source-profile mapping are still pending.

Certified local projected-budget filtering now runs inside GPU candidate selection.
The sufficient certificate requires one resource charge for each native-kind and
exact fuel-profile pair throughout the compiled grammar. Candidate packing retains
projected donor costs and destination node/carried/reset limits. CPU selection uses
the same bounds; uncertified grammars keep exact CPU child checks and GPU copyback
admission, rather than unsafe early pruning. The original alternatives remain
available. This is generic support, not a certified frozen-source profile.

`goal-11-projected-selection-01` seals the 556-file source and focused results:
four native tests passed, including two executed GPU tests, plus 23 tool and 23
repository tests. CUDA checks cover each bound, untouched-surrounding failure,
zero charges, 64-bit values and reverse-direction rejection. End-to-end checks
verify packed allowances, unchanged unit-cost behavior, and absence of unsafe
filters for conflicting charges. Descriptor size and optional preprocessing costs
still require final representative performance measurement. Source-profile/origin
mapping and source physical-capacity closure remain required before acceptance.

## Candidate and reference identities

An actual selected-population profile now admits all 64 frozen `dp2d-p64`
members, preserving their multiplicity. It composes source literals with the
DP2D diagonal-backward expansion, literal coordinates, the captured 0..1 domains,
four memo cells/five frames, and a non-replaceable Program envelope with source
depth resets. The candidate passes initial projected admission at 80 nodes and
retains the seven-level offspring limit separately from physical limits 531/141.
CPU fitness agrees for all 64 members with the frozen reference. Evidence and
the concrete profile are under `goal-11-dp2d-profile-01`. This certifies initial
membership/admission only: the full source donor/operator closure is still
missing, so the profile is not accepted for evolving performance measurements.
The reference ran locally and the candidate on Snoopy; these diagnostic times
must not be compared.

The old reference wrapper rejected the revised 1+1 inner session counts. A new
isolated `representative-reference-adapter-01` changes only those minimum-count
checks, retaining defaults and the frozen b049183 production archives. Its CLI
source/header are the Goal 01 wrappers (the added public case-parser forwarding
function only); the snapshot serializer is rebuilt from its retained source.
Build commands, source/binary/library hashes and initial missing-header/stale-object
link failures are retained. The rebuilt wrapper successfully produced the 1+1
DP2D correctness observation. Historical binaries/results were not overwritten.

Nonterminals may now declare `variation:false` to suppress replacement of that
choice while retaining evolvable descendants. Generation and membership remain
unchanged; constant mutation remains a separate policy. Both reproduction backends
consume the shared filtered site analysis. The legacy grammar-config converter
uses this on Program roots, preserving the source's expression-only subtree
replacement boundary rather than allowing whole-program swaps.

`goal-11-variation-policy-01` records the source/spec delta. The focused grammar
reproduction test passed (site exclusion, descendant retention, unchanged seeded
construction, identity and Boolean validation), and all 23 repository checks
passed. The converter's package-equivalence test verifies disabled Program roots.
This supplies one missing source eligibility control; it does not yet certify
all frozen profile production/scope rules or final performance.

The new data-defined `source_literals.json` package exposes all eight ordinary
source literal types with separate initial/mutation productions. Numeric members
retain full finite representable ranges while construction uses source sampling;
Float keeps the 1000 quantizer and 65535-step GPU perturbation grid. Bool flips;
nonnumeric constants keep their value. Sequence lengths and Char alphabets follow
the independently inspected frozen generator and donor branches. This is a
reusable component for profile composition, not a full generator/search mapping.

`goal-11-source-literals-01` records the source delta and targeted test. An initial
nested String domain incorrectly included a mutation policy, which the schema
rejected. Only the package data changed to remove that nested policy; the rerun
in `fixed/` passed in 1.22 seconds. It compiles all eight roots, generates each in
both stages, and checks stage domains, perturbation policy and numeric membership
outside construction bounds. No GPU/runtime implementation changed in this step.

GPU reproduction's physical node cap is now 1024, above the independently derived
531-node conservative expansion bound for 80 source nodes. Compiled kernels use
dynamic shared origin storage proportional to the requested node capacity. This
removes the earlier node-cap obstruction without changing projected admission;
name/constant and other metadata limits still require profile-level closure.

The 515-node integration probe exposed a host packing overflow: source tables
larger than the device cap were copied into buffers sized by a capped union.
Packing now rejects oversized source tables before copying. The probe separately
checks this rejection and performs real GPU crossover/mutation of 515-node input
trees with zero fallback, preserving projected cost and evaluated value. The
unchanged direct kernel tests passed; corrected preparation and backend tests
passed, followed by the backend assertion check for zero fallback.
`goal-11-expanded-capacity-01` retains the failing source/test log, corrected
`fixed/` snapshot, and final assertion delta/log in `final/`. A diagnostic ASan
build could not initialize CUDA (out-of-memory), so no ASan pass is claimed.
This is correctness evidence, not a performance measurement or complete source
profile certification.

The four checked-in compatibility packages now carry source resource charges:
one node/depth unit for each source operation or operand, zero for expansion-only
helpers. The existing eight-result-type matrix independently compares projected
nodes and expression depth against `legacy_budget_metrics` for LinearRec, DC,
DP1D and DP2D. Expression roots compare their payload depth, excluding the native
four-node envelope. No runtime fuel charge was changed.

`goal-11-package-resources-01` retains a 556-file source snapshot and the exact
focused CTest command/output on Snoopy. All three tests passed: grammar package
contracts, CPU legacy equivalence, and actual GPU package results (4.32 seconds
total test time). This establishes package expansion accounting, not full legacy
search-profile equivalence or final performance acceptance.

The subsequent `control-reset/` source delta annotates structural nodes emitted
by the legacy grammar-config converter with `{nodes:1, depth:0, resets_depth:true}`.
A mapped ordinary source program now matches both whole-program node count and
source expression depth. The focused legacy package equivalence test passed after
rebuilding the changed converter (1.30 seconds); package runtime files were not
changed by this follow-up. This does not yet supply the frozen profile's complete
production eligibility or stage-specific search rules.

The final-candidate benchmark adapter now accepts paired
`--source-max-total-nodes` and `--source-max-expr-depth` options separately from
the physical grammar limits. It enforces source node admission for initial
populations, leaves their projected depth unbounded as in the frozen source,
and supplies both projected limits to offspring generation/variation. Frozen
reproduction captures are checked against source limits; grammar identity and
physical-limit checks remain mandatory. Every result records the two kinds of
limit in `source_resource_budget`. These options consume authored charges; they
do not certify that an arbitrary supplied grammar reproduces a frozen profile.

The paired workload runner admits these options only on a v2 candidate with a
grammar and both positive limits, and includes them in workload identity and
commands. `goal-11-benchmark-budgets-01` retains the adapter source delta and
passing focused benchmark contract test on Snoopy. It checks larger physical
limits, source node rejection, initial over-depth admission and result metadata.
The 23 migration-tool tests also passed, including option routing and malformed
budget rejection. This changes benchmark plumbing, not measured performance.

- Candidate parent revision: `6fe14ca9a30b6ee7f6c3fa56d944ece0c0c41fbc` plus the
  uncommitted Goal 11 diff described below.
- Candidate Release build:
  `/home/hschi1106/gagp-artifacts/grammar-migration/goal-11-release-r64`.
- Reference revision: `b04918307eb69ec0f08c6bf5b03a0fae9399dbbf`.
- Reference Release build:
  `/home/hschi1106/gagp-artifacts/grammar-migration/b049183/build-r64`.
- Both optimized CUDA builds target architecture 89 and use
  `--maxrregcount=64`.
- Toolchain: GCC 11.4.0, CMake 3.22.1, CUDA 12.2.140. Driver and final device
  telemetry must be captured again with the accepted run.

The frozen release-1 evolution/evaluation matrix has 208 configurations and 4,632
timing rows. Its completed baseline contains 29,520 processes. The reproduction
matrix has 34 configurations, 68 rows, and 1,224 baseline processes. The paired
protocol requires three warmup and at least fifteen measured randomized blocks for
both revisions, followed by the independently audited bootstrap gates.

## Honest migration measurement boundary

`gagp_final_candidate_bench` is an opt-in benchmark target linked only to the
transition library. It directly decodes the frozen `migration-population-v1` and
`migration-reproduction-v1` records, preserves exact scalar bits and payloads, and
lowers supported release-1 ASTs through the offline transition path. It supports:

- `steady`, which executes the same materialized old program after v2 lowering and
  is the exact program-level runtime mapping;
- `run`, which additionally requires a supplied compiled grammar and reconstructs
  honest membership before current evolution;
- `repro-steady`, which validates the captured parents, subtree tables, candidate
  fields, donor tables, capacities, and reproduction settings before running current
  CPU or GPU grammar reproduction.

The adapter does not feed retired candidate or donor decisions into the current
backend. CPU and GPU reproduction share the current compiled-grammar operator
contract, and their children need not equal the retired children. GPU reproduction
prepares immutable host inputs once outside measured steady calls, using the same
first `std::mt19937_64` value as the production path. Preparation wall time and its
input, preprocess, and pack components are reported separately.

A valid one-member fixed-seed fixture exercises current CPU `repro-steady` end to
end and verifies deterministic children across repeated calls and fresh processes.
All 17 AST-backed p64 frozen families passed CPU steady evaluation during adapter
development. The bytecode-only `bounded_fallback` family has no source AST or type
provenance and cannot participate in grammar evolution. `bool_char`, `mixed_exact_payloads`, and
`metadata_stress` contain multiple parent return types; splitting them changes the
old mixed-population selection semantics. Such replacements must remain explicitly
mapped workloads and cannot be described as the same frozen evolution work.

The timing tools now accept `migration-workloads-v2`. A pair has common trial
metadata and separately hashed before/after artifacts and arguments. For steady
reproduction, each role explicitly chooses `donor_replay` or `adapter`; this keeps
the old CPU donor-tape replay on the reference side and current CPU grammar
reproduction on the candidate side. The evidence audit rebuilds role-specific
commands and validates both hashes, artifacts, donor counts, raw timings, and process
inventory. A separate mapping inventory records whether each proposed pair has the
same cases, population, and effective work. Rows marked non-comparable cannot enter
the speed gate.

The first inventory is
`/home/hschi1106/gagp-artifacts/grammar-migration/goal-11-mapped-01/inventory.json`
(SHA-256 `f5b3e25ed583b971245e064322784c616d6e440a40ab35d8a6d5d26dc0007b7e`).
It covers all 18 historical families, 242 historical configurations, and all 80
Goal 07 compiled workloads. It finds that the Goal 07 profiles provide domain and
feature coverage but do not reuse the historical cases or population members; all
such direct pairs are therefore marked non-comparable. This prevents those earlier
one-block pilots from being promoted into Goal 11 gates. Compatibility-package
mapping with the historical case schemas was checked separately. The four maintained
runnable compatibility roots require inputs and therefore reject the frozen no-input
case schemas for linear, DC, DP1D, and DP2D.

Five new artifact-only roots remove that schema mismatch for `linear_int`,
`linear_float`, `linear_string`, `dc_int`, and `dc_float`. They reuse the Goal 8
constant-source package constructions, the exact frozen 1,024-case files, fuel
20,000, and empty input schemas. All five definitions validate, generate deterministic
p64 and p1024 populations, and complete a one-generation CPU evolution check on the
historical cases. Their manifest is
`goal-11-mapped-01/noinput/manifest.json` (SHA-256
`e5403af231cb395465525eb6958bdf4967d613903f0672a8668b4adcb4221dfa`);
its sidecar validates 33 file hashes. These are mapping/correctness artifacts, not
timing evidence.

Artifact-only DP1D and DP2D roots similarly replace the package input leaves with
frozen Int constants while retaining the six general bounded/memo alternatives and
their Solve/Transition search spaces. Both empty-input definitions validate, generate
p64 and p1024 populations, and complete one CPU evolution generation against exact
copies of the historical 1,024-case files. Their manifest is
`goal-11-mapped-01/noinput-dp/manifest.json` (SHA-256
`b56f653ee92f55934cfbcf6be27dd21118bca8571a5d7886074f7ebc8d4b70e4`),
with 31 validated file hashes.

The canonical `simple_exp_1024` anchor now has a current v2 artifact mapping with
the exact historical 1,024 cases, `x: Float` input, `Float` result, fuel 20,000,
and the historical 80-node/7-depth limits. Its deterministic p64 and p1024
populations replay byte-identically and each completed one CPU evolution generation.
The mapping manifest is `goal-11-mapped-01/simple-exp/manifest.json` (SHA-256
`6d9963f80c515a89b3fa01c7f2415e3b6f6b1ddea753d8ad95bcf9c1fc808288`),
with a sidecar covering 26 retained files. This establishes the current anchor
artifact; formal before/after timing remains pending.

Six artifact-only traversal roots cover `int_list_map`, `int_list_filter`,
`float_list_map`, `string_list_filter`, `string_list_empty_map`, and
`nested_binders`. They use exact copies of the corresponding frozen cases and
retain nontrivial typed source, predicate or transform, seed, and bounded-step
choices. Every definition validates; all p64 and p1024 populations replay
byte-identically; and each p64 population completed one CPU evolution generation.
The manifest is `goal-11-mapped-01/traversal/manifest.json` (SHA-256
`61dc691417981309719f2eb46ebd36316b73d87d3639eb2d53615f0a43234204`),
with 38 validated retained files. These are behavioral search-space mappings.
They do not assert equal legacy members, and the nested root cannot reproduce the
retired numeric binder identity as an equal v2 derivation.

The bounded-fallback runtime row does not need a synthetic grammar replacement.
The benchmark-only adapter now decodes its exact frozen ordinary bytecode and
index-addressed cases, including the unmaterialized String token, and sends those
programs directly through the current CPU or GPU evaluator. It rejects removed
specialized opcodes and nonempty legacy DC/DP segments, and permits only steady
runtime evaluation because the snapshot has no AST or grammar provenance. Reference
and candidate p64 CPU/GPU probes produced identical fitness; candidate p1024 CPU/GPU
probes also executed all 18 calls with the frozen result. Their manifest is
`goal-11-mapped-01/bounded-fallback/manifest.json` (SHA-256
`eb350cc9750ec63e52d63496f54ce476910edf99d5357ce2a298e59d014fc80c`).
Those timings are excluded because the devices were contended; the probes establish
exact runtime work and result identity only.

The three mixed-return historical families now have nine current exact-return-type
partitions: Bool and Char; IntList, FloatList, String, and StringList payloads; and
Int, Float, and IntList metadata stress. Each partition retains unmodified historical
case rows, and all eighteen p64/p1024 populations replay byte-identically and complete
one CPU evolution generation. The manifest is
`goal-11-mapped-01/mixed/manifest.json` (SHA-256
`aac28a80efef383e3505cc1839f34f7175a90af6352b1d2feebc0320e8466893`),
with 236 validated files. Partitioning changes tournament competitors, pairing,
crossover eligibility, and trajectories, so it is not whole-population equivalence.
The metadata Int and IntList roots also require 96 nodes and depth 32; retained
probes show their current derivations are infeasible at the historical 80/7 bounds.

Matching reference-side material now covers the same nine typed partitions at p64
and p1024. Each partition preserves historical programs and exact typed case rows,
keeps reproduction parent, fitness, subtree, and candidate rows aligned, and retains
the complete 576-row typed donor pool. All 18 captures passed the optimized plain,
instrumented, donor-replay, and 3+15 steady workflow; their children match
byte-semantically, verify, and replay generates no replacement donor calls. The
manifest is `goal-11-mapped-01/mixed-reference/manifest.json` (SHA-256
`97f96ef6f331399da827a58f426e9b8c17464f35353e04eb4dcdb2fc20b18f0d`).
This evidence does not erase the partitioning gap: tournament competitors, pairing,
crossover eligibility, source multiplicities, and child trajectories differ from
the original whole mixed-family workloads.

The original metadata Int and IntList reproduction captures retained 80-node/7-depth
limits and therefore could not pair with their 96-node/32-depth current grammars.
Four replacement reference captures now use the exact current limits at p64 and
p1024. Plain, instrumented, replay, and 3+15 steady children are byte-identical, all
verification records pass, and replay generates no new donor calls. Their manifest is
`goal-11-mapped-01/reference-repro-mixed-96/manifest.json` (SHA-256
`6f9893d798c1fd2ada651e2f8b606553074396f2295df68db5d0cbb0c6b51efa`).

The five no-input, two DP, and six traversal families have equal-limit reference
reproduction captures at 96 nodes and depth 32 for both population sizes. Capture
dimensions, all 576-donor pools, plain/instrumented/replay children, verifier results,
zero replay-generated calls, and 3+15 steady samples passed. The manifest is
`goal-11-mapped-01/reference-repro-96/manifest.json` (SHA-256
`97ef15e45c82e7ec9cf3fe38c3c56789b57f0a3b7fc1caa24f29026e42c6fe18`).
These records complete the role-specific reproduction inputs; they are not GPU timing
evidence.

The frozen `migration-workloads-v2` manifest contains 302 mapped pairs: 184 evolution,
72 steady evaluation, and 46 steady reproduction. Its comparison records classify
72 exact runtime replays, 140 typed search-space pairs, and 90 typed-partition pairs.
Validation independently rehashed every role artifact, confirmed equal arguments,
exact cases and limits, unique pair IDs, the required comparison schema, and 3,048
role/mode commands across all seven valid compiled-grammar modes. Representative reference `run`,
`steady`, GPU `repro-steady`, and CPU donor-replay commands also passed. The manifest is
`goal-11-mapped-01/full-v2/manifest.json` (SHA-256
`4ab4a7e9564991d12c3ce04fc16372512b583e6448482e06d0d042b80dec3fba`);
its validation report has SHA-256
`9492b4bf8e67562a59594934d55b7bd60396d9fb66382ce493c791c1e0cd84fd`.
The independently retained command reconstruction has SHA-256
`409ef4deeb38904de8f7c7276b2fa979e676a2e87677a21c21a54c37ecc97c73`
and fixes the complete 54,864-process count for three warmup and fifteen measured
blocks.

Goal 09 removed the release-1 specialized candidate-bucket and donor-coupling paths.
The four corresponding combinations (`cpu_gpu_candidates`,
`gpu_gpu_candidates`, `cpu_gpu_coupled_donor`, and
`gpu_gpu_coupled_donor`) remain explicit non-gating inventory in the manifest; the
backend rejected them when this archived manifest was produced.
The seven retained modes cover CPU, GPU evaluation, GPU reproduction, overlap,
CPU-evaluation/GPU-reproduction, and CPU/GPU evaluation with GPU-style selection.
Both adapters passed all fourteen role/mode commands for a frozen p64 evolution pair.
That functional-smoke report has SHA-256
`90cca671855b1df0db3a27df3d4458f682dcd18c79b3c84176276e869a06af9b`;
its durations are excluded from performance evidence.

The current candidate additionally restores `gpu_candidates` as a generic compiled
CPU ablation. It uses the same bounded candidate preparation, compatibility and
budget rules, and hash-ranked feasible pair selection as GPU reproduction, while
retaining CPU tournament selection and ordinary post-crossover mutation. All copies
of a template hole are spliced atomically with capture remapping, and children pass
normal grammar membership checks. Preprocessing, including donor generation, stays
inside the measured operation. No specialized legacy runtime or AST path is restored.
The two candidate-ablation evaluation combinations therefore execute again. The archived seven-mode manifest remains unchanged and
must be replaced only after the search-space mapping is repaired. Functional mode
availability alone is not performance or equal-work acceptance.

The candidate-ablation implementation passed all 110 local native tests, 75 tool
checks, and 23 repository checks. Its reproduction tests cover atomic repeated
holes, named locals, more sites than the GPU preparation cap, non-entry requests,
odd population sizes, deterministic owned/reference replay, unchanged operator RNG
scheduling, and rejection of invalid parents. Diagnostic/timed outcome comparisons
pass for nine modes with both final-evaluation settings (18 comparisons), and for
three CPU-only modes (six comparisons). CPU-only reproduction tests also pass.
Commands, source hashes, and raw logs are retained in
`goal-11-candidate-ablation-01`. These are correctness results, not accepted timing
samples; the remote runtime pilot predates this ablation change.

The subsequent `gpu_coupled_donor` implementation restores the remaining two
combinations. It preserves CPU crossover selection and records its original-parent
sites, then uses precomputed contextual donors at those same sites for subtree
mutation. Its constant branch mutates the crossed child normally. Preparation covers
all CPU-selectable sites, rather than truncating them to the GPU candidate cap.
The contextual pool uses the compiled GPU preparation's four donors per site;
this replaces the legacy type-bucket policy and therefore still needs effective-work
and performance comparison. A local-scope regression found that preparation and
crossover must use the same certified, compacted parent representation; the initial
failure and repair evidence are retained in `goal-11-coupled-ablation-01`.
All eleven frozen mode combinations are now executable locally. The archived
seven-mode manifest and remote pilot candidate remain unchanged. Neither restored
mode availability nor correctness tests repair the rejected search-space mapping.
The combined local candidate passed 110 native tests, 75 operational tool tests,
and 23 repository checks. Diagnostic/timed outcomes match for all eleven modes
with final evaluation both on and off (22 comparisons); CPU-only builds pass eight
such comparisons and the reproduction regression suite. Tests check unchanged
CPU crossover when mutation is disabled, post-crossover constant mutation, no
changes outside the coupled subtree, repeated holes with lexical captures,
non-entry requests, invalid-parent rejection, and deterministic RNG scheduling.
`goal-11-coupled-ablation-01` retains complete commands, logs, and source identities.

The 72 steady-evaluation pairs replay exact programs and cases and can support an
isolated runtime comparison. The 140 typed search-space and 90 typed-partition pairs
are not established equal-work comparisons. Matching cases, dimensions, seeds, and
limits does not preserve search constraints or initial program semantics. Their
archived `gate_eligible` flags are operational declarations, not acceptance evidence;
these mappings need repair before any full-flow performance claim.

The frozen reference CPU donor-tape path uses the optimized instrumented mutation
binary because the corresponding plain binary intentionally cannot consume donor
tapes. The steady replay interval excludes donor capture, donor generation, and
diagnostic serialization. The validation smoke record preserves both the successful
replay and the plain binary's expected rejection.

The checked [mapping summary](goal-11-mapping-summary.json) retains the compact input
and artifact hashes, completed mappings, and acceptance flags.

## Repairs made before timing

Population replay previously parsed every large artifact twice, serialized and
reparsed every member, and repeatedly compiled the same embedded grammar. Replay now
parses the population once, compiles the embedded grammar once when needed, and
validates each member structurally against one canonical definition. Recursive JSON
comparison preserves exact object/array shape and distinguishes negative zero.
Unknown nested fields, member grammar changes, payload clearing, required and
embedded grammar routes, byte-stable re-encoding, and the legacy
`population-seeds` diagnostic are tested.

On the fixed p1024 restricted-combine CPU diagnostic, process wall time fell from
the Goal 09 observation of about 32.43 seconds to 4.89 seconds; the measured evolution
body remained about 1.56 seconds. This single unpaired diagnostic is useful cause
evidence, not a performance gate result.

Contextual donor generation previously recomputed the same grammar fixed-point cost
table for each donor. `VariationContext` now owns a bounded FIFO cache keyed by the
exact local-availability bitmap and effective depth. Grammar identity is implicit in
the worker-owned context. Request-specific node feasibility is still checked after a
lookup, and generated donors, RNG consumption, derivation provenance, search limits,
and acceptance checks are unchanged. CPU mutation and GPU reproduction preparation
use the cache. Tests compare cold and cached fixed-seed donors, distinct local/depth
keys, eviction, and impossible node budgets.

Legacy DC/DP transition lowering also emitted `INT_MAX` frame and memo capacities.
The CPU could allocate those lazily, but the GPU packer correctly rejected them
against its fixed 128-frame and 128-cell workspace. DP lowering now proves its memo
capacity from the inclusive literal coordinate-domain cardinality and its DFS frame
capacity from that cardinality plus one boundary frame. DC lowering uses the exact
packed length for a direct constant sequence and retains the full 65,535-element
bound for a dynamic sequence, which remains honestly unsupported by the fixed GPU
workspace when it cannot be proven smaller. Focused Debug and Release tests cover
constant, empty, length-one, and dynamic DC; multi-request DP1D/DP2D execution; and
safe `UINT32_MAX` capacity saturation. Functional probes for `metadata_stress`,
`dc_int`, `dc_float`, `dp1d`, and `dp2d` executed the
current GPU path at p64 and p1024 without capacity rejection and matched their CPU
fitness arrays exactly. The p1024 commands, hashes, fitness identities, and structural
counts are retained in `goal-11-mapped-01/gpu-bound-functional/manifest.json`
(SHA-256 `ff5225cc65ccca980f8d4a7143d8680c403e7ea066bd7a3585a9daa8535d899a`).
Device contention excludes their observed times from the performance gate.

## Correctness evidence

The Debug focused integration set passed 12/12 tests, covering the final-candidate
adapter, legacy boundary, donor generation, mutation, crossover, reproduction,
bounded variation, GPU reproduction preparation, population/member artifacts,
generation CLI, evolve CLI contract, and region artifacts. The same focused set
passed 12/12 in the Release candidate. The Goal 11 migration runner tests pass,
including v1 compatibility, role-specific v2 commands and hashes, old CPU donor
replay versus current CPU adapter routing, raw-evidence audit, and recovery. Full
Debug validation passed 109/109 native tests, 74/74 operational tool tests, and
23/23 repository checks. The explicit GPU gate passed 3/3 with no skip: VM smoke,
fitness CPU/GPU parity, and evolution CPU/GPU parity all executed their CUDA paths.
A later benchmark-only correction canonicalized captured fitness exactly as the
production evolution loop does; the adapter integration test then passed in both the
Debug and Release builds. The subsequent finite-capacity audit added terminal DC,
adversarial-cut execution, multi-request DP, and saturation cases; the expanded
boundary test passed in both builds.

The retained test-evidence manifest is
`goal-11-mapped-01/test-evidence-final/manifest.json` (SHA-256
`adaf9a701d1315d32b67242136c5a05694768de4345eb14f531d62486adba43c`);
its 68-file sidecar has SHA-256
`a98467606648b13c3d71010768c8bc338bd44df2e2c231dafd1aa7629abedd95`.
It records the exact commands, timestamps, exit statuses, raw output hashes, changed
source identities, candidate binary identities, and pre/post GPU telemetry. The GPU
records prove functional CUDA execution only; their durations remain excluded from
performance acceptance.

## Timing blocker

### Effective-work diagnostics

With `GAGP_BUILD_BENCHMARKS=ON`, `gagp_final_candidate_stats` accepts the same
`--action run` arguments as `gagp_final_candidate_bench`. It intercepts the actual
candidate evolution loop in a separate executable and records every evaluated and
reproduced population: size/depth histograms, unique-program count, verifier counts,
requested fitness evaluations, and requested case scores. Final evaluation is
included when enabled. Existing per-generation fitness progress and variation
counters remain in the result. These observations support workload comparison;
they do not by themselves establish equivalence of partitioned populations.

Its JSON includes `diagnostic_only: true` and `population_observations`. The timing
extractor rejects diagnostic output. Observers are absent from the timed binary;
all diagnostic timings are excluded. `gagp_test_final_candidate_stats` compares
non-timing results against the plain benchmark for seven modes with final evaluation
both enabled and disabled, and checks every population histogram and NFE count.

A twenty-generation canonical p64 diagnostic compared all seven operational modes
against the plain candidate benchmark; every non-timing result matched. Both old
and current runs requested 1,280 fitness evaluations, but the frozen initial native
AST mean is 35.078125 nodes versus 6.171875 in the proposed mapping (49 five-node and
15 ten-node members). Node counts alone are not invariant across lowering versions;
inspection of `simple_exp.grammar.json` independently confirms the replacement
restricts the original mixed full-language population to Float input/constants and
clipped addition/multiplication. It therefore does not preserve the frozen search
space. The evidence is `goal-11-effective-work-01/work-audit.json` under the artifact
root, with hashes of all old/current observations. The canonical evolution mapping
is rejected for acceptance; all original required rows remain pending rather than
being dropped. The full mapping needs reconstruction, including mixed parent/donor
workloads and the four legacy ablation combinations.

### Remote preparation on snoopy

The isolated remote workspace is `/home/hschi1106/gagp-goal11-20260922` on
`snoopy` (RTX 3090, driver 560.35.05). Both reference and candidate were rebuilt
with GCC 11.4.0, CUDA 12.6.85, Release, architecture 86, and
`--maxrregcount=64`. The reference uses the original Goal 01 fixture-parser wrapper
and separately retained measurement adapters; its runtime and evolution sources
remain at the frozen reference revision. No RTX 4090 timing is combined with this
host's measurements.

Remote Release suites passed 33 reference and 109 candidate native tests, plus 74
tool and 23 repository tests. All 265 transferred input hashes and 22 representative
role/mode commands passed. Both assertion-enabled Debug suites completed successfully.
The initial fixed runtime replay stopped on a parity failure; the repaired replay
completed as recorded below. Preparation evidence and commands are retained at
`snoopy-20260922/readiness.json` under the artifact root. The initial missing-wrapper
build failure and smoke-selector failure are retained alongside their repairs.

The user cleared the desktop GPU load on 2026-09-22; subsequent idle samples
reported 0% utilization. The initial runtime preflight
stopped at `simple_exp_1024-p1024-steady-b256`. The first 66 pairs passed the
existing absolute CPU/GPU tolerance of `1e-9` and exact same-backend cross-version
comparison. At pair 67, CPU results remain identical across versions, but GPU
results differ at population indices 212, 253, and 685. Inspection of candidate
bytecode finds 72, 73, and 83 root locals respectively, exceeding the GPU limit of
64. Indices 202 and 727 additionally exhibit CPU/GPU differences in the reference
itself; these remain under investigation. The five-program candidate probe
reproduces all five GPU penalty results. No tolerance was widened and no row was
removed. Evidence is retained in `runtime-preflight-01`, `runtime-preflight-02`,
and `parity-probe-02` beneath the remote artifact directory. The queued calibration
terminated at its preflight prerequisite and produced no timing acceptance result.

The local-slot repair reuses compiler temporaries only after their lexical or
loop lifetime ends. Named locals and active enclosing captures retain distinct
storage. The three failing programs fall from 72/73/83 locals to 20/31/20 before
the subsequent fuel repair. Remote `slots-validation-01` passed all 109 tests in
both Release and Debug; `runtime-slots-01` completed all 72 fixed-evaluation pairs
with exact same-backend cross-version fitness. Its two baseline CPU/GPU exceptions
at indices 202 and 727 are explicitly retained for both block sizes.

Direct GPU probes then exposed a separate Map/Filter migration fuel defect:
extra traversal bookkeeping charges and Map body/output-load ordering differed
from the frozen compiler. The transition adapter now checks the source before
storing it, uses uncharged administrative traversal setup, preserves Map's
body/store/load/append order, and charges Filter output stores only on acceptance.
No production specialized node or runtime path was restored. Six frozen-reference
Map/Filter examples have exact fuel boundaries 14, 15, 15, 74, 63, and 75; all 18
GPU comparisons below, at, and above those boundaries match value/error and fuel.
The original two fallback probes now also match their reference remaining fuel
(19502 and 19450 from 20000). These diagnostic probes ran on the original local
GPU host and are not timing samples.

The combined repair passed all 109 local native tests. New regression cases cover
sibling traversal storage, live enclosing bindings, sequential loops retaining
named variables, and frozen Map/Filter fuel boundaries. Raw probe results,
`map-filter-comparison.json`, and `local-fuel-ctest.log` are under
`snoopy-20260922`. A separate remote source/build (`candidate-fuel`, `fuel-release`,
`fuel-debug`) preserves the earlier measured binaries; both remote Release and Debug
suites passed 109/109 tests. `runtime-fuel-01` completed all 72 pairs with exact
same-backend cross-version fitness (144 fresh candidate commands); reused reference
outputs and their hashes are retained. Earlier slot-only results are not final-candidate
performance evidence.

Same-binary reference calibration completed on this host. The initial 15-pair
run (`calibration-trials-02`) retained all 252 processes and produced 14 passing
and seven rerun rows. Its independent 30-pair rerun (`calibration-trials-03`)
retained all 462 processes; the 21 audited rows had confidence-interval lower
bounds at least 0.9819395. The comparator still reports pending because some
same-binary CPU ratios are slightly below one. These calibration measurements
are not candidate performance acceptance results.

The canonical fixed-program runtime pilot (`runtime-pilot-trials-01`) completed
without profiling against the combined repair. All eight rows passed evidence
auditing: four CPU rows pass and four GPU rows require reruns after fifteen paired
blocks. GPU median times before/after are 3.097/3.251 ms (p64/b256),
1.629/1.738 ms (p64/b1024), 41.206/51.199 ms (p1024/b256), and
25.273/41.997 ms (p1024/b1024). These results do not meet performance acceptance.
An independent thirty-pair confirmation (`runtime-pilot-trials-02`) completed
with the same binaries and inputs. All eight rows passed evidence auditing:
four CPU rows pass and all four GPU rows have confirmed regressions. GPU median
times before/after are 3.098/3.251 ms (p64/b256), 1.630/1.739 ms (p64/b1024),
41.145/51.060 ms (p1024/b256), and 25.274/42.028 ms (p1024/b1024).
The full performance matrix remains pending.

Diagnostic `nsys` traces in `snoopy-20260922/runtime-nsys-01` retain all four
GPU workload pairs, using the recorded binaries and arguments with distinct
output paths. Each trace contains 18 launches (three warmups, fifteen trials).
For p1024/b1024, mean kernel duration increases from 23.978 to 30.812 ms;
aggregate `cudaMalloc` plus `cudaFree` API duration increases from 2.956 to
127.557 ms. For p1024/b256 the corresponding allocation/free totals increase
from 3.205 to 142.253 ms. These instrumented values diagnose overhead and are
not acceptance timings.

The session-owned bounded-region workspace repair retains the same allocation
layout across calls and releases it before replacing a changed layout. Local
native tests pass 111/111, repository checks pass 23/23, and the remote focused
bounded-region GPU test passes, including stale-memo and workspace-resizing
checks. The isolated remote build adds only this repair and test to
`candidate-fuel`; it does not include the later local mixed-root/ablation work.
In `runtime-workspace-01`, three alternating-order diagnostic pairs per workload
(three warmups and fifteen measured calls per process) give old/repaired median
call times of 2.930/2.919 ms (p64/b256), 1.605/1.600 ms (p64/b1024),
51.086/42.099 ms (p1024/b256), and 42.097/32.914 ms (p1024/b1024).
This is not the frozen statistical acceptance protocol.

The repaired p1024/b1024 trace reduces aggregate allocation/free API duration
from 127.557 to 6.721 ms over 18 calls; mean kernel duration remains 30.886 ms.
Original traces record 1024 reference blocks versus 99 candidate blocks at
blocksize 1024, with 64 registers per thread in both. The candidate workspace
budget constrains the whole mixed population's grid, which merits investigation
for the remaining kernel gap. This observation does not establish causality.
Raw traces, process commands, source hashes, patch, and validation log are retained
under the diagnostic artifact directories. Kernel regression and full acceptance
remain unresolved.

The follow-up `runtime-schedule-01` experiment separated ordinary blocks from
budget-limited region workers within the same mixed kernel. Its trace confirms
1024 blocks instead of 99, still at 64 registers per thread. Three rotating-order
diagnostic groups compare reference/workspace-only/scheduled candidates:
2.763/2.892/2.898 ms (p64/b256), 1.493/1.591/1.594 ms (p64/b1024),
41.108/41.747/43.609 ms (p1024/b256), and 25.217/32.779/34.479 ms
(p1024/b1024). The experiment passed 111 local tests and the focused remote
GPU test, but made the p1024 kernel slower. Its scheduler implementation was
reverted; the workspace-only repair and additional output-order parity checks
remain. Raw commands, experimental patch, trace, and results are archived.
Increasing the grid alone is therefore not a supported repair for this loss.

`runtime-family-probe-01` partitions all 1024 frozen programs into disjoint
diagnostic groups by their legacy structure (440 ordinary, 257 traversal,
321 linear-recursion, six recursive/memoized regions). These smaller launches
are diagnostic only and cannot substitute for the full-population gate.
Reference/workspace-only kernel medians are 4.679/4.725, 4.319/4.494,
11.543/12.582, and 7.370/19.316 ms respectively. Individual probes of all six
region programs locate the largest gap at original index 252, a one-coordinate
memoized recurrence: 8.408/18.888 ms. Index 551 measures 0.602/1.837 ms and
751 measures 0.464/0.804 ms; the other three are near 0.3 ms in both binaries.
The next repair investigation therefore targets generic region execution,
including its phase and frame/memo access costs, rather than increasing the
population grid. Program-index membership, derived diagnostic snapshots, and
all process commands/results are retained; frozen acceptance inputs are unchanged.

The next repair sizes the GPU phase preset array by binding count: up to four
bindings use a four-entry instantiation, while larger phases retain the full
49-entry capacity. This avoids constructing unused presets at every phase call
without changing phase execution or its lazy capture checks. Focused tests cover
4, 5, and 32 bound parameters, missing/wrong-type captures, exact results, payload
fallback, and fuel boundaries. The isolated `candidate-phase` source extends
`candidate-workspace`; it still excludes later local evolution/migration work.
Three alternating-order diagnostic pairs in `runtime-phase-01` give
workspace-only/repaired call medians of 19.172/13.002 ms for program 252,
42.024/39.650 ms for full p1024/b256, and 32.842/27.367 ms for full p1024/b1024.
All underlying fifteen-trial samples and warmups are retained. This repair is
retained for validation, but the three-pair diagnostic is not performance
acceptance, and the remaining full-matrix gates are still pending.
The repaired local candidate passes all 111 native tests and 23 repository
checks; the isolated remote candidate passes both bounded-region GPU tests.
Its full-p1024/b1024 `nsys` trace reports an 18-launch mean kernel duration of
25.307 ms. Trace, binary/source hashes, repair patch, and test logs are retained
in the same diagnostic directory; instrumented runs remain excluded from gates.

Two subsequent scratch-sizing experiments were not retained. In
`runtime-phase1-01`, a one-binding preset specialization slightly improved the
isolated recurrence but had mixed p1024/b256 results. `runtime-frame-01` used
the same bytecode interpreter with 16 stack slots and eight locals only for
verified, nonbranching phases of at most 16 instructions and eight locals.
Other phases kept full capacity. It passed all 111 local tests and the focused
remote GPU test, including instruction/local-capacity boundaries, captures,
and branching fallback. Three alternating-order diagnostic pairs gave
phase-only/compact-frame call medians of 13.015/10.516 ms (program 252),
39.563/40.713 ms (p1024/b256), and 27.287/26.442 ms (p1024/b1024).
Since b256 became slower in all three pairs, the implementation was reverted.
The b256 traces retain identical 399-block grids and 64 registers per thread;
these observations do not explain the regression. Both experimental patches,
raw measurements and traces are archived; additional phase boundary tests remain
in the repository. The retained implementation is still the four-binding preset
specialization plus session workspace reuse, with full acceptance pending.

The retained follow-up interleaves region workspace slots across threads within
each block. Each thread still owns its frames/memo entries, and the workspace
budget, frame/cell capacities, program ordering, and fuel rules are unchanged.
The new 2,053-case test covers divergent depths, memo hits, multiple cases per
thread, two programs, repeated evaluation, block sizes 256/1024, and fuel 6/100.
It exposed a 1024-thread launch resource failure in the uncapped local build;
an explicit 1024-thread kernel launch bound fixes that build without relying on
the benchmark's external register cap. Local native tests pass 111/111,
repository checks pass 23/23, and both focused remote GPU tests pass.

After a three-pair exploration (`runtime-stride-01`), fifteen alternating-order
diagnostic pairs in `runtime-stride-02` compare the phase-only and strided
candidates. Call medians before/after are 2.908/2.893 ms (p64/b256),
1.597/1.595 ms (p64/b1024), 39.602/39.541 ms (p1024/b256), and
27.328/26.279 ms (p1024/b1024). Paired bootstrap ratio intervals (10,000
resamples, seed 42) are [1.00355, 1.00966], [0.99861, 1.00548],
[0.99692, 1.00462], and [1.03727, 1.04287], respectively. The strongest
evidence is the p1024/b1024 improvement; the middle two rows remain near parity.
These candidate-to-candidate diagnostics do not establish frozen-reference
absolute-time or CPU-relative Q acceptance. The isolated remote source still
predates later local evolution/migration work; final full-matrix timing remains
pending. Raw samples, build/source/binary identities, initial launch failure,
test logs, and profiling records are retained in the two artifact directories.

`runtime-reserve-01` measures a further host-only packing repair: reserve the
checked sum of root instruction/constant counts before appending entries, avoiding
repeated vector growth. All existing fuel/region checks still run on every call;
there is no identity cache or pointer-based validation bypass. Five alternating
diagnostic pairs give packing medians before/after of 0.1063/0.1019 ms
(p64/b256), 0.1069/0.1004 ms (p64/b1024), 2.3891/2.2707 ms (p1024/b256), and
2.3726/2.2461 ms (p1024/b1024). Overall call medians are 2.8800/2.8834,
1.5930/1.5840, 39.9646/39.5223, and 26.3032/26.1997 ms, respectively.
This small packing improvement is retained; overall timing includes noise and
these five diagnostic pairs do not establish frozen-reference gate acceptance.
The isolated remote `candidate-reserve` extends `candidate-stride` only.
Validation passes: 111 local native tests, 23 repository checks, and the focused
remote bounded-region GPU test. Raw commands, all samples, source/binary hashes,
patch, and test logs are retained in the diagnostic directory.

The complete current implementation was subsequently deployed as
`candidate-integrated-01`, including mixed-root evolution/CLI support, restored
ablations, offline typed-storage normalization, and all retained runtime repairs.
`integrated-01/source-identity.json` records 534 source files; the archive SHA-256
is `dbdd159318d3e3ee9110606c1dd7fc82c66d9186c969f7c0f6dee579393dd9df`.
Every extracted file was checked before building. Both sm86 Release and Debug
builds pass 111/111 native tests, with no skipped GPU tests; Debug compiler flags
retain assertions. The remote tools suite passes 75/75 and repository checks
23/23. An initial tool-test skip caused by the external build directory was
resolved using the expected `cpp/build` path and rerunning the suite.

All 72 frozen runtime pairs were replayed on the integrated Release binary:
144 CPU/GPU commands agree exactly with the archived same-backend results.
Input hashes and all reused result hashes were checked against the prior sealed
evidence; the previously documented reference CPU/GPU exceptions remain intact.
These runs use one warmup and two measured calls solely for semantic verification,
not timing acceptance. The Release benchmark SHA-256 is
`3da7577c33f175bfbe7e903b65fb4c1b953a35fc88ae76d5975d869cec38a4f8`.
Build logs, test logs, commands, hashes, and results are under `integrated-01`.
This closes the remote integration-test gap; it does not resolve frozen grammar
mapping or establish full-matrix performance acceptance.

The archived seven modes and mapped search spaces are operational coverage, not
proof of the unchanged acceptance contract. Restoring four historically frozen modes
requires a replacement full-flow manifest and measurements. Likewise,
equal CLI limits do not prove equal effective work for changed initial populations
or partitioned reproduction. These issues remain pending alongside timing; neither
the manifest's `gate_eligible` flags nor successful command reconstruction resolves
them.

### Frozen search-space audit and mixed-root repair

`goal-11-search-space-audit-01` inspects all 34 source-backed frozen populations
(18,496 programs) through exact migration and native verification. All migrated
ASTs verify, and root-type histograms remain unchanged. The two source-less
bounded-fallback populations are explicitly outside AST inspection. Canonical p64
contains seven root types: 16 Int, 10 Bool, 7 FloatList, 12 String, 8 Float,
5 StringList, and 6 IntList. Its exact migrated mean AST size is 57.15625 nodes,
compared with 35.078125 before migration. Canonical p1024 has 17 programs with
multiple assigned types for one name; 304 migrated programs exceed the frozen
80-node search bound, with a maximum of 214 nodes. These are concrete obstacles
to full-flow mapping, not grounds to discard programs or increase frozen limits.

The first repair adds finite mixed-root request sets to variation and its bounded
analysis cache. Differently typed parents can exchange shared compatible subtrees
without partitioning selection or admitting a generic root type. Every accepted
child retains its parent's exact root contract; domain membership and common
resource limits remain enforced. Shared GPU preparation now analyzes each parent
against that set. Native evolution now carries the full request set through
initialization, global tournament selection, GPU resources, and overlapped
reproduction. Focused tests exercise all eight exact types across eleven modes,
including heterogeneous expected-value CPU/GPU parity and rejection of reused
GPU state with a changed root set. The private benchmark accepts
`--population-roots RootA,RootB`. Production evolution and generation CLIs now
expose the same root selection, including fixed mixed-population artifact replay.
Complete frozen grammar mapping remains pending. The fixed-program runtime pilot does not use this new path.
The variation-layer change passed all 110 native tests and 23 repository checks;
CPU-only crossover, analysis-cache, and reproduction tests also passed. The mixed
fixture exchanges a shared Bool nonterminal between Int and Float roots, executes
both children, mutates them repeatedly, checks shared GPU candidate contracts,
rejects forged provenance and domain mismatches, and prevents a valid member of
another root from replacing its parent. Evidence is retained in
`goal-11-mixed-roots-01`.

### Original host

Both visible RTX 4090 devices are continuously occupied by unrelated
`dj_transcribe` jobs owned by another user. At the latest check, both devices reported
100% GPU utilization and about 20.2 GiB and 17.0 GiB used. The active jobs include
both long-running and recently started processes, so the condition is not a reliable
idle gap. These processes are not modified or terminated.

Running the 54,864 paired processes under this contention would violate the
frozen protocol's uncontended-hardware and variance requirements. No observed timing
under the current load may be entered as accepted evidence. Once an uncontended GPU
is available, rebuild the final diff, rehash every artifact and binary, run the full
mapped matrix without profiling, audit raw evidence, calculate every row's paired
confidence intervals, rerun inconclusive rows to thirty measured blocks, and repair
any persistent regression before committing Goal 11.

Native mixed-root evolution integration passed all 110 native tests, 75 tool
tests, 23 repository checks, and three CPU-only focused tests. Logs and source
hashes are retained in `goal-11-mixed-evolution-01`. This correctness evidence
does not resolve the frozen grammar mapping or performance gates.

Production mixed-root CLI integration passed all 110 native tests, 75 tool tests,
23 repository checks, and four CPU-only CLI/artifact tests. The generation CLI
creates all eight exact types and replays them with an embedded or required grammar;
the evolution CLI reproduces the same output from generated and saved mixed inputs.
Unknown, duplicate, missing, or malformed roots are rejected. Evidence is retained
in `goal-11-mixed-cli-01`. The frozen mixed full-language mapping remains unresolved.

The follow-up storage-identity audit (`goal-11-local-types-01`) also counts input
initialization and ForRange variable writes, which the earlier assignment-only
count of 17 omitted. Canonical p64 has 15 programs with conflicting storage types;
p1024 has 200, including 158 that retype an input and 33 that retype a loop variable.
A diagnostic type-based rename still passes native verification and matches sampled
results/errors for 46,080 p64 and 614,400 p1024 comparisons (1024 cases at fuel 0,
30, and 20000). This is not an accepted transformation: an independently constructed
loop counterexample changes Bool(true) to Float(7) after renaming, although both ASTs
verify. A migration regression now preserves the original shared-slot behavior for
zero, one, and two loop iterations. Compiler-lowering and migration-boundary tests
pass. A safe mapping must preserve storage identity or prove reaching-definition
and loop-carried-read equivalence; case-level fitness agreement alone is insufficient.

`goal-11-storage-proof-02` replaces the unguarded diagnostic rewrite with the offline
`normalize_typed_storage` helper. It proves reaching definition types through
branches and loop fixed points, includes zero-iteration paths, stops after Return,
and respects provably constant Boolean short-circuit evaluation. Named bounded-region
captures are checked as reads. The original loop counterexample and loop-carried
read/capture hazards are rejected. All 15 canonical p64 conflicts and 200 p1024
conflicts now satisfy the proof; 660,480 case/fuel comparisons retain identical
results/errors. All 34 source-backed populations (18,496 programs) pass normalization
and native verification with no node-count change; compiled local count is at most
38, within 64. The two bytecode-only populations have no AST and are explicitly
outside this normalization audit; their runtime gates remain required.

This helper is not automatically enabled in timing or production entrypoints.
It preserves the already-migrated AST node count, so the frozen 80-node mapping
problem remains unresolved. Complete grammar/search membership and final GPU
performance acceptance are still pending. Validation: 111 native tests, the
CPU-only normalization test, and 23 repository checks passed. Proof regressions
cover branches, name collisions, early Return, short-circuit control flow, named
captures, unchanged error codes, and every fuel limit from 0 through 150 in the
accepted fixtures. The earlier conservative proof (`goal-11-storage-proof-01`)
and rejected unguarded rewrite evidence remain archived.


The physical-budget audit (`goal-11-budget-audit-01`) covers the same 34
source-backed populations, totaling 18,496 members. Snapshot/case hashes and every
TSV aggregate were rechecked before sealing the artifact. The two bytecode-only
populations are outside this AST audit, not excluded from runtime acceptance.

| Diagnostic metric | Frozen maximum | Migrated maximum | Frozen above nominal | Migrated above nominal |
| --- | ---: | ---: | ---: | ---: |
| Physical nodes (nominal 80) | 80 | 214 | 0 | 323 |
| Full AST depth (nominal 7) | 15 | 22 | 2,009 | 8,379 |
| Consecutive expression depth (nominal 7) | 8 | 19 | 88 | 5,539 |

These are representation diagnostics, not acceptance counts. The frozen
`genome.cpp` metadata excludes statement/block ancestry from expression depth.
The frozen generator emits a leaf even when its recursive depth parameter reaches
zero, and its final acceptance checks node count and verification rather than
metadata depth. Consequently, neither the full-depth metric nor the diagnostic
expression-depth metric can simply be substituted for that generator parameter.
The current grammar request counts the complete materialized AST, subtracting a
four-node, three-level envelope for expression roots. Template copies consume
physical nodes under `spec/grammar_definition.md`.

This establishes that copying the numeric depth setting does not establish equal
search limits. It does not justify raising the candidate limits or removing
oversized programs. Equivalent complete grammar/search mapping remains required;
all formal performance gates remain pending. The artifact summary records hashes
of both implementations and the normative specification, and the mapping summary
links the sealed measurements.

The benchmark population wrapper now forwards `--population-roots` to native
generation. `goal-11-mixed-wrapper-01` records all 78 tool tests and 23 repository
checks passing without skips. Native integration coverage verifies all eight exact
types in a deliberately reversed root order, per-member seeds, byte-identical
embedded-grammar replay, the omitted-option single-entry default, and invalid-root
rejection without overwriting an existing output. This repairs the tool entrypoint
for mixed-population experiments; it is not evidence of complete frozen grammar
mapping or a performance pass. Native runtime sources were unchanged by this repair.


`runtime-leaf-01` evaluated a generic one-instruction region-phase fast path for
constant/local reads. It retained preset validation, lazy captured-parameter type
checks, instruction fuel order, and result-type checks. The CUDA Release experiment
passed all 111 native tests; local Debug leaf-phase parity coverage also passed.
Five randomized paired diagnostic blocks (seed 42), each with three warmups and
15 steady samples, produced these median call times against `integrated-release-01`:

| Population | Block size | Integrated ms | Leaf experiment ms |
| ---: | ---: | ---: | ---: |
| 64 | 256 | 2.886537 | 2.898923 |
| 64 | 1024 | 1.591330 | 1.596163 |
| 1024 | 256 | 39.441769 | 39.154050 |
| 1024 | 1024 | 26.199403 | 26.137035 |

All paired fitness vectors were identical. The experiment was not retained because
its small changes were mixed and both p64 medians were slower. These diagnostic
blocks are not the frozen-reference statistical gate and do not establish a formal
regression or acceptance. The initial benchmark setup failures (missing benchmark
target, then CUDA compiler not discovered) are archived separately and supplied no
timing samples; measured binaries used the same explicit CUDA compiler, sm86,
Release configuration, and register cap as the integrated reference.

Retained tests cover leaf versus explicit-Return phases, all eight exact result
types, binding counts 1/2/4/5/8/9/32, missing/wrong-type captures, and exact versus
insufficient instruction fuel. They check intended CPU errors and CPU/GPU fitness
parity. The production phase interpreter was restored after the experiment.


The host-only `goal-11-workspace-layout-audit-01` examines compiled descriptors for
34 source-backed populations (18,496 members), with input hashes checked before
compilation. It records the current allocation formula and a size-only model for
separate arrays containing the population's maximum live state/preparation/result
slots, plus continuation indices. No execution capacities or runtime code change.
Canonical p64 contains no bounded regions. Canonical p1024 has six programs with
regions, with maxima of 17 frames, 16 memo cells, two state slots, one preparation,
and two requests. Current storage costs 5,256 bytes per worker; the modeled arrays
cost 1,940 bytes. At the unchanged 512 MiB workspace ceiling this corresponds to
399 versus 1,024 blocks at block size 256, and 99 versus 270 at block size 1024.

This identifies a possible general storage-layout repair for further investigation,
not a measured speedup. Alignment, indexing, thread isolation, capacity errors,
payload behavior, and GPU performance still require implementation and verification.
The earlier rejected scheduling experiment remains relevant: more concurrent blocks
alone do not establish a benefit. The ongoing integrated-reference timing run is
independent of this local descriptor audit.


`goal-11-compact-workspace-01` implemented the storage model as a local
experiment, subsequently rejected after the diagnostics below. Population maxima determine separate frame-value banks and continuation
indices; memo key width follows the maximum state count. The same interpreter
accesses production strided banks or the raw result probe's fixed frame representation
through a frame view. Allocation reuse still replaces the entire layout when its
byte sizes change, and the aggregate 512 MiB ceiling is unchanged.

Local Debug validation passed all 111 native tests with GPU tests executed, 78 tool
tests, and 23 repository checks. Added coverage expands, shrinks, and regrows all
banks within one session, using the maximum 4 states, 4 preparations, and 8 requests,
with distinct state/preparation values and memoized duplicate requests. Existing
2053-case thread isolation, capacity exhaustion, exact result/error, and payload
suites also passed. Source hashes and the experimental patch are archived. This is
not performance acceptance: the ongoing remote integrated pilot measures the previous
archived layout. Separate optimized-build correctness and timing remain required
before retaining this experiment in the accepted candidate.


Compact storage subsequently passed all 111 native tests in an isolated Release
build on local RTX4090 GPU0 (CUDA 12.2, sm89, register cap 64). Five randomized
paired blocks, each with three warmups and 15 steady samples, matched all fitness
vectors but did not show consistent timing benefits. An additional isolated build
retained the original worker grid while using compact storage:

| Population / block | Original / compact ms | Original / compact, original grid ms |
| --- | ---: | ---: |
| 64 / 256 | 2.119234 / 2.121324 | 2.123374 / 2.119474 |
| 64 / 1024 | 1.259352 / 1.264632 | 1.260163 / 1.262282 |
| 1024 / 256 | 28.947045 / 30.753889 | 28.915895 / 29.901547 |
| 1024 / 1024 | 19.999408 / 19.967478 | 19.963658 / 20.436029 |

Separate nsys traces for p1024/block256 show 399 versus 1024 blocks, both at 64
registers/thread, with six profiled kernel launches averaging 27.657428 versus
30.086904 ms. The fixed-grid variant also passed its focused GPU test but did not
remove the slowdown. These local measurements are diagnostic and are not compared
numerically against RTX3090 gate timings. GPU0 was explicitly selected; GPU1's
unrelated load was recorded in telemetry. No formal confidence or acceptance claim
is made from these five-block experiments.

Both storage variants were rejected and the four runtime files were restored to
the pre-experiment hashes. Maximum-slot and layout-reuse tests remain. Complete
sources, optimized build caches, raw paired results, commands, binary identities,
and nsys traces are archived under `goal-11-compact-workspace-01`. The independent
remote integrated pilot continues against its unchanged archived binary.


`goal-11-phase-workspace-01` is a separate, currently unaccepted experiment: reuse
one VM stack/local array pair across non-reentrant phases of a bounded invocation.
Stack depth, local validity, and expected-type masks are still initialized on every
phase call. Ordinary VM calls retain owned arrays; Value construction and all
capacities remain unchanged. A compile-time assertion prohibits reusable storage
in an interpreter that can enter bounded execution.

Four focused Debug GPU tests passed, including new raw-result regressions that
check NameError rather than a stale local value, Timeout before that missing-local
read at insufficient fuel, exact remaining fuel, and a Float result in a slot that
held an Int capture constraint in a previous phase. Full integration, optimized-build
validation, and performance measurements remain pending. The running remote
integrated pilot does not measure this experimental implementation.


The phase-workspace experiment subsequently passed all 111 native tests in both
Debug and isolated Release builds, plus 78 tool tests and 23 repository checks.
Thirty new randomized local RTX4090 paired diagnostic blocks (seed 42), with three
warmups and 15 samples per session, produced the following call-time results.
Intervals use 10,000 paired bootstrap resamples of block medians. These are GPU
absolute-time comparisons against the integrated candidate, not frozen-reference
CPU/GPU Q gates.

| Population / block | Integrated ms | Phase workspace ms | A | Paired 95% interval |
| --- | ---: | ---: | ---: | --- |
| 64 / 256 | 2.119239 | 2.052188 | 1.032673 | [1.031526, 1.033318] |
| 64 / 1024 | 1.261353 | 1.244098 | 1.013869 | [1.012527, 1.015532] |
| 1024 / 256 | 28.961629 | 28.790660 | 1.005938 | [1.001821, 1.008656] |
| 1024 / 1024 | 19.965918 | 20.058383 | 0.995390 | [0.993190, 0.997756] |

Every paired fitness vector matched. The last row is a measured local regression,
so this experiment remains unaccepted and requires repair. The five-block initial
sample and all 30-block raw results are retained. Separate nsys traces show unchanged
block grids and 64 registers/thread: profiled p64/block256 mean kernel time falls
from 2.109943 to 2.048594 ms, whereas p1024/block256 changes from 27.865914 to
28.108932 ms in its six-launch trace. Profiled timings are not acceptance samples.

A grouped-array-only control, without sharing phase storage, passed two focused
GPU tests but did not reproduce the improvement: five-block medians were
2.119794/2.114405, 1.261141/1.261851, 28.963439/29.013588, and
19.953996/19.960263 ms (integrated/control in the same row order). Its first launch
was rejected by the idle check before any timing samples; the idle telemetry and
failed launch log were preserved, and the next attempt ran after GPU0 was idle.
This control remains isolated in the artifact tree. No current remote integrated
pilot measurement is attributed to either experimental implementation.


Phase attribution of the full-reuse 30-pair sample places the p1024/block1024 loss
mostly in the kernel (+0.085208 ms median phase time), with +0.009284 ms in packing;
these phase medians are not summed into total time. Separate 18-launch nsys traces
retain 99 blocks of 1024 threads and 64 registers/thread, with mean kernel times
18.200959/18.536772 ms for integrated/full-reuse. This motivated two isolated controls:
reuse only locals, or reuse only the operand stack. Both passed focused exact-result
and fitness GPU tests. Locals-only retained the large-block loss in its five-pair
sample; stack-only showed four non-regressing point estimates and was extended to
30 new randomized paired blocks.

| Population / block | Integrated ms | Stack-only ms | A | Paired 95% interval |
| --- | ---: | ---: | ---: | --- |
| 64 / 256 | 2.118747 | 2.051647 | 1.032705 | [1.031863, 1.033622] |
| 64 / 1024 | 1.261803 | 1.242979 | 1.015145 | [1.014107, 1.017138] |
| 1024 / 256 | 28.972654 | 28.787455 | 1.006433 | [1.005077, 1.008499] |
| 1024 / 1024 | 19.967104 | 19.957093 | 1.000502 | [0.996457, 1.003673] |

The stack-only variant is the current experimental working-tree candidate. Each
phase still owns and constructs its local array; only the operand stack is shared
within one bounded invocation, with stack depth reset per phase. Its Debug and
Release builds each passed all 111 native tests without GPU skips, alongside 78
tool tests and 23 repository checks. The optimized benchmark hash remained identical
to all 30-pair measurement records after the complete Release build. Current runtime
headers match the archived measured source bytes.

These local results justify further remote validation, not final acceptance. The
last interval spans 1, and this diagnostic lacks CPU/Q and the complete frozen
matrix. The original full-reuse variant, grouped-array control, locals-only control,
all raw comparisons, and the selected stack-only source remain separately archived
under `goal-11-phase-workspace-01`. `selected-summary.json` identifies the current
choice; the original `summary.json` still describes the superseded full-reuse
experiment. The ongoing remote integrated pilot continues against its older binary.


The broader stack-only diagnostic now covers all 72 frozen runtime workloads,
with five randomized paired blocks per workload and 15 measured samples after
three warmups per process. All 360 paired fitness vectors match. Fourteen rows
have A below 1; the lowest is 0.982426 for
`string_list_empty_map-p1024-steady-b1024` (7.449789/7.583050 ms).
Other low estimates include FloatList Map (0.984000), IntList Map (0.985362),
and StringList Filter (0.986878), all at p1024/block1024. These observations
weaken the four-row selection evidence and are not acceptance results.
All 1,446 sealed files were hash-verified after the runner terminated.

A new independent 30-pair run of every one of the 72 workloads is running under
`goal-11-phase-workspace-01/local-runtime-matrix-30`, with seed 20260923 and the
same pinned binaries and inputs. The five-pair evidence remains immutable in
`local-runtime-matrix-01`; no rows were removed. This extension is still a
GPU-only diagnostic against the integrated candidate, not the frozen-reference
CPU/GPU Q gate. Remote validation preparation does not establish acceptance of
the stack-only candidate.


Phase attribution of the five-pair matrix places the largest losses in GPU kernel
time: StringList empty Map adds 0.127421 ms kernel time against a 0.133261 ms
call-time difference; FloatList Map adds 0.121391 ms kernel time against
0.128550 ms call time; IntList Map adds 0.115000 ms against 0.121281 ms.
Packing deltas in these rows are below 0.001 ms in magnitude. Phase medians are
summarized independently and must not be added to reconstruct call time.
`local-runtime-matrix-01-phases.json` preserves all 72 rows, including improving
rows. This locates a potential kernel cost; it does not establish its cause or
replace uninstrumented 30-pair confirmation and subsequent nsys diagnosis.


The independent 30-pair extension completed all 2,160 pairs. Verification checked
8,646 sealed files, process return codes, pinned binary identities, raw fitness
vectors, and all recorded medians. All paired fitness vectors match. Paired
10,000-resample bootstrap intervals (seed 42 per row) classify 58 rows as faster,
11 as slower, and three as uncertain; 13 point estimates are below 1. All 11
intervals entirely below 1 are at p1024/block1024. The stack-only candidate
therefore requires repair and is not accepted despite its canonical-row gains.

| Workload (p1024/block1024) | Integrated ms | Stack-only ms | A | Paired 95% interval |
| --- | ---: | ---: | ---: | --- |
| StringList empty Map | 7.457628 | 7.581777 | 0.983625 | [0.982823, 0.984905] |
| FloatList Map | 7.917933 | 8.045050 | 0.984199 | [0.983715, 0.985066] |
| IntList Map | 8.180112 | 8.291881 | 0.986521 | [0.985652, 0.987193] |

All 72 rows, including the other eight confirmed losses, remain in
`goal-11-phase-workspace-01/local-runtime-matrix-30-analysis.json`.
These are local GPU-only comparisons against the integrated candidate, not
frozen-reference CPU/GPU Q acceptance. The diagnostic runner exited successfully
before nsys was started for the three lowest-A workloads and their block256
controls. The profiling script's lock was separately verified to reject a live
paired runner without starting a profiler or creating profile output.


All six before/after nsys profile pairs completed with matching fitness and verified
artifact hashes. Each trace contains 18 launches, gridX 1024 and 64 registers/thread;
block size and dynamic shared memory match before/after. At block1024, mean kernel
times change from 7.084316 to 7.200214 ms (StringList empty Map), 7.568044 to
7.722076 ms (FloatList Map), and 7.853046 to 7.938010 ms (IntList Map). Their
block256 controls instead improve slightly: 6.787255/6.748806,
7.713722/7.683187, and 8.105710/8.063715 ms. These traces support a block-size-related
kernel cost, not a changed launch grid or register count; they do not establish
an instruction-level cause. Raw resource fields are retained in
`local-runtime-matrix-30-nsys-analysis.json`. Profile timings remain diagnostic only.


An isolated ABI control removes the extra phase-workspace argument from ordinary
VM calls by selecting the workspace parameter type at compile time. It passed both
bounded-region GPU tests. Its complete five-pair, 72-workload diagnostic preserves
all 360 fitness vectors but retains the large-block regression pattern: FloatList
Map A=0.983754, StringList empty Map A=0.984283, IntList Map A=0.986992.
Twelve point estimates are below 1. Thus this control does not repair the observed
loss and is not promoted. Sources, build, test log, and sealed trials remain under
`source-stack-abi`, `build-stack-abi`, and `local-runtime-stack-abi-05`.
The next isolated control restores separately declared ordinary stack and locals
arrays; no new control has replaced the working-tree candidate. The previously
prepared remote stack bundle is held pending repair rather than treated as an
accepted next candidate.


The separate-array control also passed both focused GPU tests and all 360 paired
fitness comparisons, but retained 13 A point estimates below 1. StringList empty
Map A=0.982577, FloatList Map A=0.985505, and IntList Map A=0.987213 at
p1024/block1024. Thus neither the workspace-argument control nor the ordinary-array
layout control repairs the measured loss. Both remain isolated archived experiments.

The stack-sharing experiment is now rejected and reverted in the working tree.
Both runtime headers were restored byte-for-byte from the integrated baseline;
new exact-result/fuel/mask-isolation tests remain. Earlier paragraphs describing
stack-only as the selected candidate are historical experiment stages, superseded
by this decision. The prepared remote stack bundle is superseded and must not be
deployed. The still-running remote integrated pilot already measures the restored
runtime, so it does not need restarting because of this reversion. Full frozen
performance acceptance remains incomplete.

After restoring the integrated runtime, a complete Debug rebuild and all 111
native tests passed, including 19 GPU-labeled tests with no skips. All 78 tool
tests also passed. Build/test logs and hashes are linked by
`goal-11-phase-workspace-01/reversion-summary.json`.


The user has explicitly authorized implementing and verifying equivalent budget
translation. The old search restrictions, complete populations, execution fuel,
and all performance gates remain unchanged. Physical representation bounds must
be derived from the mapping, rather than chosen from the largest observed member.
Source admissibility and target physical feasibility require separate evidence;
generation and variation must retain the source search restrictions. Increasing
target limits by itself is not acceptance. The original physical-budget audit is
historical evidence and remains immutable; its statement that translation was not
yet authorized is superseded by this approval.


The offline migration module now exposes `legacy_budget_metrics` and
`legacy_accepts_resources`. The former verifies the source AST and separately
computes physical and expression depth. The latter reproduces the old final
resource predicates: initialization checks node count; variation additionally
checks expression depth. This deliberately does not claim generation provenance,
grammar membership, or translated search-space equivalence. Native tests cover
exact limits, expression-depth rejection only in variation, statement/block
ancestry, and malformed source rejection. Physical-bound derivation and
source-equivalent generation/variation integration remain required.


The remote integrated 30-pair pilot completed. Its raw audit reports all 528
processes present, no missing blocks, and no replacements. Four CPU rows pass;
all four GPU rows fail the unchanged Q gate. Results are archived under
`snoopy-20260922/pilot-results-01`.

| Population / block | GPU before ms | GPU after ms | A_gpu | Q |
| --- | ---: | ---: | ---: | ---: |
| simple_exp_1024-p64-steady-b256 | 3.097696 | 3.216839 | 0.962963 | 0.854897 |
| simple_exp_1024-p64-steady-b1024 | 1.630896 | 1.731906 | 0.941677 | 0.839637 |
| simple_exp_1024-p1024-steady-b256 | 41.120540 | 39.448073 | 1.042397 | 0.944516 |
| simple_exp_1024-p1024-steady-b1024 | 25.227334 | 26.189572 | 0.963259 | 0.867265 |

The p1024/block256 GPU absolute-time improvement does not offset its reduced
CPU/GPU speedup. All confidence intervals remain in the complete comparison
artifact. This canonical pilot does not replace the complete required matrix.


The source-budget API was replayed across all 34 AST-bearing populations and
18,496 members with frozen input hashes checked. Every previous metric value
matches exactly. Under the audited 80-node/7-expression-depth limits, initialization
resource acceptance rejects no members, while variation resource acceptance rejects
88 for expression depth. These valid initial members must not be removed by
applying a variation-only depth predicate to initialization. The API explicitly
rejects intermediate lowering nodes outside the frozen source node domain.
Raw results, process records, and hashes are under `goal-11-budget-source-02`.
This is source resource evidence only; translated physical bounds and complete
search-space equivalence are still pending.


`predict_legacy_expansion` now independently computes each source tree's target
physical node count and depth from the current lowering rules. Fixed skeleton
nodes exclude recursively copied source children; child offsets count target edges
from the replacement root to each child root. Each child occurs exactly once in
these migration rewrites (this is not a claim about arbitrary authored templates).

| Source form | Fixed nodes | Fixed maximum depth | Child depth offsets |
| --- | ---: | ---: | --- |
| Map | 8 | 4 | 2, 2 |
| Filter | 9 | 4 | 2, 2 |
| LinearRec | 30 | 10 | 1, 3, 5, 9, 9 |
| DC | 6 | 3 | 1, 1, 1, 1 |
| DP1D | 6 | 3 | 2, 1, 1 |
| DP2D | 16 | 6 | 1, 2, 3, 3 |

Ordinary nodes retain one physical node with child offsets of one. Arithmetic is
checked for overflow; invalid and intermediate source forms are rejected. Unit
tests compare Map/Filter predictions with actual lowering. Across all 34 frozen
source populations and 18,496 members, both predicted physical metrics match the
actual normalized lowering output exactly, including nested and recursive forms.
Raw process evidence is under `goal-11-budget-expansion-03`.
This per-tree predictor does not yet derive a search-wide limit or prove
source-equivalent generation/variation; those remain required next steps.


`legacy_expansion_ceiling` now derives a conservative physical allowance from a
source node allowance by dynamic programming over all rewrite shapes. For each
source size, forest convolution maximizes expanded node counts over child-size
partitions; the depth recurrence reserves one source node per sibling and places
the remaining subtree on the longest target child path. Ordinary unary nodes make
the maxima monotone. Arithmetic is overflow checked. Complexity is quadratic time
and linear memory in the source node allowance.

For 80 source nodes, this shape-only ceiling is **531 physical nodes and depth
141**. It deliberately ignores source typing, lexical scope, grammar restrictions,
and stage-specific source depth, so it is conservative rather than a new search
contract. All 18,496 frozen source members remain below it, with exact per-member
node/depth predictions still matching actual lowering. Nested Filter tests and
small-budget/overflow boundaries pass. Evidence is under `goal-11-budget-ceiling-04`.
No production search limits have been raised. Source-equivalent grammar,
generation, and variation enforcement are required before applying a translated
physical allowance or accepting an evolution/reproduction mapping.


Integration review found a GPU reproduction capacity constraint:
`kGpuReproKernelMaxNodes` is 512, below the generic 531-node physical ceiling.
A native regression test constructs a valid balanced source with 15 LinearRec
nodes, exactly 80 source nodes, and expression depth within 7; lowering has exactly
515 physical nodes. The test passes source variation resource checks and checks
prediction against actual lowering. This proves that source node/depth resource
checks alone do not imply the current GPU capacity. It does **not** prove that
this tree is derivable under a particular frozen structured/nesting profile.

The mapping must include the actual structured/nesting restrictions before using
a tighter physical allowance. If genuinely admissible mapped shapes exceed the
kernel capacity, capacity must be repaired without pruning source programs or
moving required GPU reproduction to CPU. No kernel capacity or production search
limit has been changed by this audit.


`predict_legacy_expansion_layout` now supplies an independent source-indexed
structural witness: the complete target prefix span and physical root depth for
every source subtree. Rewrite rules record the cumulative administrative nodes
before each child and its emission order. LinearRec emits the last child before
the step child, so source prefix order cannot serve as target prefix order.
Administrative target nodes receive no source entry. This witness does not certify
variation eligibility, lexical compatibility, or grammar membership.

Native tests cover all six specialized rewrite forms, ordinary/control trees,
nested Filter and LinearRec, distinct leaf identities, and the LinearRec child
permutation. Across all 34 frozen AST populations, 18,496 programs and **224,261
source subtrees**, every predicted span ends at the actual verified target subtree
boundary and has the exact target depth; distinct source roots remain distinct and
source constant-table identities are retained. Raw source-indexed TSV witnesses,
input/source/library hashes and process records are sealed under
`goal-11-budget-layout-05`. Generation/variation integration remains pending.

Review of the frozen native implementation found that nominal structured nesting
fields cannot be assumed to be enforced search restrictions. Numeric initial
generation can emit LinearRec independently of the ASGP eligibility flag; its
initial phase bodies are fixed skeletons. Mutation donor construction has its own
limited shapes, while typed crossover requires matching scope, binder, scheme,
phase and visible-environment signatures. The resource-admissible 515-node
counterexample therefore remains a capacity warning, not a proven reachable
evolution member. Any tighter bound must account for the actual operator rules.


After the subtree-layout change, the full Debug build and all 112 native tests
passed, including 19 GPU tests with no skips. Operational tools passed 78 tests;
repository checks passed 23. Logs are sealed in
`goal-11-budget-layout-validation-05`. These are correctness results, not
performance acceptance.

Local verification of the downloaded remote integrated pilot now checks all
1,190 raw JSON file hashes, all 528 process commands, complete paired blocks and
raw timing reconstruction. Recomputing the 10,000-resample comparisons exactly
reproduces the remote report. The four GPU Q failures remain; no timing was
replaced. See `snoopy-20260922/pilot-results-01/local-verification.json`.


`legacy_replacement_resources` computes source-indexed resource allowances for
replacing one expression. A prefix/suffix maximum checks the untouched tree's
expression depth; subtracting the replaced subtree gives the remaining source
node allowance, and subtracting the incoming expression ancestry gives the donor
depth allowance. Structural nodes receive no expression allowance. This linear
analysis is separate from type/scope/binder/phase and grammar eligibility.

Native boundary tests compare the prediction with 4,896 actual verified splices
across node limits 0–16, depth limits 0–7, six sites and donor depths 1–6. They also
cover shallow wide donors and distinguish a repairable deep branch from an
unchanged over-depth branch. The frozen-population audit checks **148,226
expression replacements** with exact-typed one-node donors across all 18,496
members; predicted acceptance always matches full post-splice verification and
resource accounting. There are 530 resource-repairing positions across all 88
initial members above the variation depth limit. These positions are not yet
claimed eligible under the actual operators. Raw evidence is sealed under
`goal-11-budget-replacement-06`. This API is offline; production generation and
variation integration remains pending.


The replacement-resource implementation passed all seven migration tests and
23 repository checks after rebuilding; logs are sealed under
`goal-11-budget-replacement-validation-06`.

An isolated GPU experiment specializes root interpreter invocations that have no
phase presets, eliminating unused preset type tracking while retaining the full
phase interpreter. The experiment rejects accidental preset use of the root-only
entry. Four focused GPU/parity tests passed. The complete 72-row local GPU screen
ran five paired blocks with three warmups and 15 samples per process; all 360 pairs
had identical fitness. Thirteen rows have A below one, with the worst point
estimate 0.983530 for StringList empty Map at population 1024/block 1024. Canonical
p64/block256 improved to A=1.035332, which does not justify adopting a change with
slower other rows. The candidate is **not promoted**; production runtime is
unchanged. Five-pair screening is diagnostic, not the frozen final acceptance
protocol or CPU/Q evidence. Source hashes, build/test records and sealed trials
are under `goal-11-root-presets-01`.


Two further isolated interpreter experiments are not promoted. Scalar stack-top
caching passed four focused GPU/parity tests, including new operand-transition
and fuel-boundary cases, but 60 of 72 point estimates were below one in the
five-pair screen. The worst A was 0.950952. Static binary inspection reports
64 registers in both versions and kernel stack sizes changing from 6288/10512
to 6304/10576 bytes; this is not proof of the slowdown's cause. Opcode switch
dispatch likewise passed the four tests and exact fitness checks for all 360
pairs, but 47 of 72 point estimates were below one (worst A=0.986800). These
five-pair screens are diagnostic and do not replace formal CPU/Q measurements.
Source identities, build/test logs, complete paired trials and analyses are
archived under `goal-11-stack-top-01` and `goal-11-switch-dispatch-01`.

The operand-transition regression tests remain in the working tree: they cover
underlying operands across store/load, unary operations, multiargument builtins,
branches, empty-list checks and bounded calls, including every fuel boundary of
a mixed instruction sequence. They pass on the unchanged runtime as well as the
isolated interpreter variants.

The set-locals initialization experiment completed all 72 rows and 360 paired
fitness comparisons with exact agreement. Five-pair diagnostic bootstrap results
classify 42 rows as faster, 29 as uncertain and one as slower; 16 point estimates
are below one, with worst A=0.993009. The change is not promoted and has no formal
CPU/Q acceptance. Sealed trials and the hash-checked analysis are under
`goal-11-set-locals-01`.

The independent `goal-11-case-layout-01` experiment transposes case-local
transport tables and carries the case count in the private code view. All four
focused GPU tests passed. Its complete 72-row screen finished with exact fitness
agreement across all 360 pairs. Source
comparison against the integrated baseline confirms only the two intended
runtime files differ. New sparse case-local parity tests use slots 0, 7, 31 and
63, counts 1/33/257/2053, and block sizes 64/256/1024 to check missing values,
per-case identities and explicit local assignment. They passed on both the
unchanged primary runtime and the case-layout candidate. No production input
layout or initialization behavior has changed.

The case-layout screen has 35 of 72 point estimates below one, with worst
A=0.983391 for `dc_float-p1024-steady-b1024`. Diagnostic bootstrap
classifications are 12 slower, 41 uncertain, 19 faster. The experiment is not
promoted; this screen provides no formal CPU/Q acceptance. Both input-preparation
experiments are now terminal, with analyses checked against their raw-data seals.

Bounded sequence constant domains now describe String alphabets and exact typed
list element domains with inclusive length bounds. This removes the need to
restrict constants to observed frozen payloads. Generation, CPU constant and
subtree mutation, membership, concrete artifact encoding, and GPU reproduction
are exercised. GPU preparation samples fresh sequence proposals for each
preparation; device selection and replacement remain on GPU. Registered
immutable snapshots preserve both pending preparations and their nested payload
roots. Finite/range domains retain their cached tables and sampling behavior.

Validation passed all 113 native tests (19 GPU tests, no skips), 78 tool tests
and 23 repository checks. A subsequent test-only strengthening uses identical
parents to distinguish actual GPU constant mutation from selection/crossover;
the focused GPU test passed with changed children and no fallback or acceptance
rejections. Both direct and overlapped GPU evolution pass for String, IntList,
FloatList and StringList, with constant and subtree mutation separately tested.
The source snapshot, logs (including the corrected test-helper failures) and
hashes are under `goal-11-sequence-domains-01`. This is a production capability
needed by the mapping, not proof of complete frozen search equivalence or
performance acceptance. Logical budget integration and source-stage-specific
constant/perturbation policies remain outstanding.

The general `ResourceProjection` computes additive node charges and subtree
depth transforms, including zero-cost administrative expansion and explicit
depth resets. Atomic replacement allowances account for all copies and all
untouched branches, including repair of a parent initially above a variation
limit. Independent direct-tree splicing checks **299,200** resource decisions.
The offline adapter places source charges on independently verified lowered
subtree roots. The final frozen audit covers **34 populations, 18,496 members,
224,261 source subtrees and 148,226 expression sites**. Whole-tree projected
node/depth metrics match the source; all **96,050,448 abstract donor node/depth
combinations** agree with source-side resource acceptance. These combinations
are resource checks, not claims that every donor is type/scope/grammar eligible.
Final audit evidence is sealed under `goal-11-budget-projection-10`.

The production variation path shares the replacement-allowance arithmetic while
retaining its verified unit-charge scan. It does not infer source costs from
untrusted provenance or activate larger physical limits. An initial integration
allocated a full index for unit charges and regressed simple analysis workloads
by about 2 percent. That integration is not retained. The final optimized
variation-contract `.text` is **27,543 bytes**, identical to the original scan;
normalized instruction and relocation listings also match. Static evidence is
under `goal-11-resource-analysis-code-04`. Component screens retain all rows
under `goal-11-resource-analysis-timing-02` and `-03`; neither is formal CPU/Q
acceptance. Attempt `-01` was excluded before analysis because the new projection
object had Debug optimization while the replaced analysis used `-O3`.

Final integration validation passed **114 native tests**, including **19 GPU
tests with no skips**, **78 tool tests** and **23 repository checks**. Logs and a
source snapshot are sealed under `goal-11-budget-projection-integration-10`.
The bounded sequence extension is now also specified in
`spec/grammar_definition.md`, with only that current-spec digest mechanically
updated in `benchmarks/spec_freeze.json`; frozen benchmark inputs and performance
thresholds are unchanged. Certified source-cost grammar profiles, feasible
generation, source-stage-specific variation policies, physical GPU capacity
and the complete unchanged performance gates remain required.


The source generation-stage audit links an isolated probe against the frozen
reference libraries, with all four inspected source files verified byte-for-byte
against `b04918307eb69ec0f08c6bf5b03a0fae9399dbbf`. Across eight types and seeds
0–2047, it generates **16,384 typed initial programs and 163,840 empty-target
donors** at depths 0, 1, 2, 3 and 7 with structured forms enabled and disabled.
All **98,304** same-seed comparisons at depths 2/3/7 against depth 64 agree by the
reference full AST cache key. These are construction checks before operator
eligibility, splicing and resource acceptance, not a full equivalence proof.
Raw counts, probe source, build command, input/library hashes and a seal are in
`goal-11-generation-stages-01` under the artifact root.

The source branches establish distinct terminal domains: initial Char literals
use a-z, digits and space, while donor Char literals use a-z; initial typed-list
lengths reach 5, while donor lengths stop at 4; initial StringList elements reach
8 characters, while donor elements stop at 5. The samples witness these bounds;
initial counts include all constants in each whole program. Both CPU mutation
and GPU pool preparation call the helper with an empty target, so its
existing-variable branches are unreachable at those call sites. No sampled donor
has a free VAR node. With that empty target the helper constructs constants,
fixed Map/Filter forms, or eligible fixed DC/DP forms; increasing its depth above
2 does not recursively expand these donors. Initial generation has a different
recursive construction path, including LinearRec.

Consequently, stage filtering must cover terminal domains and fixed donor shapes,
not just a list of allowed root operators. Generation policy must remain separate
from union membership and crossover compatibility, and enter feasibility/cache
and replay identities. CPU statement-root structured eligibility and GPU pool
construction must retain their respective frozen rules. Numeric additive
perturbation remains a separate obligation. This audit changes no production
policy or budget and does not close search equivalence or any performance gate.


Production `generation_stages` now separates initial and mutation construction
from union membership. Stage-specific fixed-point costs, alias exits, template-hole
budgets and contextual caches prevent disabled alternatives from supplying a
feasible path. Shared CPU/GPU donor requests select mutation; replay and GPU run
resource identity retain stage. Membership-only alternatives remain available for
reconstruction, and crossover compatibility does not include generation stage.
Tests cover disjoint constants, nested sequence domains, alias cycles, exact shared
hole budgets, cache separation, default sampling stability and artifact replay.

The GPU stage test exposed a general table-capacity bug: replacing constant 1 by
constant 2 retained the removed value in the device table, causing eight budget
fallbacks with no acceptance rejection. Compiled device splicing now compacts live
node references in first-use order. Packing reserves the bounded union of two
source tables, retaining the existing 128-entry hard capacities and all search
limits. Metadata-only names still use the existing sidecar reconstruction. Tests
retain true live-table exhaustion checks, exact Float signed-zero/Bool identity,
shared repeated-copy slots and transactionality. The staged GPU mutation now
produces mutation-only children with zero fallback or acceptance rejection, and
direct/overlap evolution agrees.

The final checkpoint passed **115/115 native tests, including 19 GPU tests with
no skips**, plus 78 tool tests and 23 repository checks on local GPU 0. The source
snapshot, exact hashes, logs and corrected failures are sealed under
`goal-11-generation-stages-02`. This is implementation/correctness evidence only:
full source search mapping, numeric perturbation, logical-cost enforcement,
capacity closure and performance acceptance remain pending. No new Snoopy timing
was taken, and its previously failing Q rows are not resolved by these tests.


A new isolated local comparison is running under `goal-11-local-reference-01`.
The user confirmed local GPU availability; selected device 0 is RTX 4090 UUID
`GPU-079560d8-bac0-0829-4e73-820f61d1cb0c`. Device 1 is occupied and is not used.
The current 543-file source snapshot built successfully with Release `-O3`,
CUDA sm89 and register cap 64, matching the frozen reference runtime/evolution
build flags exactly. The frozen adapter binary hash matches its retained link
record. The measured candidate passed 115/115 Release native tests, including
19 GPU tests without skips.

The runner first checks all 72 fixed-runtime workloads for exact old/new CPU/GPU
fitness agreement, then runs the unchanged three-warmup/fifteen-measured paired
protocol with both CPU baselines on that same local device. It retains per-process
commands, results, timing and GPU telemetry. Preflight and timing completion are
pending; consult the live process and `status.json`, not this launch record, before
resuming. This subset does not establish evolution/search equivalence, does not
resolve the Snoopy Q failures, and is not full Goal 11 acceptance.

The trial auditor now independently checks every recorded steady-evaluation
block for the expected measurement format and engine, a finite numeric fitness
vector covering the complete population, exact per-member before/after agreement
within each engine, and the maximum CPU/GPU difference as a diagnostic. Bounded
GPU fallback is an existing runtime contract and need not equal the exact CPU
result. This supplements the preflight check; matching
timings and a self-consistent file hash inventory alone cannot certify semantic
equivalence. Regression cases include reordered or changed fitness, truncated
populations, Boolean/NaN entries and wrong-engine output, with recomputed file
inventories. This auditor change is outside the running candidate snapshot and
must be applied independently to its retained results when complete.

The first local preflight stopped at workload 65 because its new four-way exact
comparison rejected the frozen reference's CPU/GPU summation rounding (maximum
`1.4551915228366852e-11`). Before/after vectors were exact within each backend.
The follow-up under `goal-11-local-reference-01/resume-01` retains all original
files and uses the frozen parity test's absolute bound. It passed workload 66,
then stopped at `simple_exp_1024-p1024-steady-b256`: members 202 and 727 have
substantial CPU/GPU differences in both reference and candidate, with exact
before/after vectors for each engine. CPU fitness is respectively
`-580.4711255867442` and `-835.5134641669065`, while GPU fitness is `-1024`.
These differences are not rounding. Both stopped attempts are retained.

Direct execution probes under `diagnostic-01` reproduce both members with the
frozen and current evaluators. On case indices 0, 512 and 1023 all CPU values,
GPU tags/bits/errors and remaining fuel agree exactly before/after. Member 202
returns the same opaque fallback token with 19502 fuel left; member 727 returns
the same TypeError with 19450 fuel left. These are pre-existing bounded payload
behaviors, not migration regressions. Applying an ordinary numeric parity test's
absolute tolerance indiscriminately to fallback workloads was a harness error.
The corrected trial audit requires exact before/after fitness per backend and
reports cross-backend differences; it does not claim CPU/GPU result equality for
fallback rows. Performance gates, populations, fuel and binaries are unchanged.

The extended direct probe checks **all 1024 cases for each of the two members**:
all 2048 observations agree exactly before/after in CPU values and GPU values,
errors and remaining fuel. Probe sources, commands, binaries and raw observations
are sealed in `diagnostic-01`. `resume-02` continues preflight with the corrected
per-backend contract, reusing the completed process evidence.

Finite Float range domains are implemented for CPU generation/mutation,
membership, offline domain conversion and GPU scalar constant mutation. A shared
sampler specifies separate rounding and endpoint handling even for the full
finite interval and subnormals; GPU scalar sampling does not use a finite host
proposal table. Nested FloatList sequences use their complete declared range.
Tests cover exact host/device sampling bits, invalid domains, replay, membership,
overlap, full-width bounds and converter preservation. The checkpoint passed
116/116 native tests (19 GPU, no skips), 79 tool tests and 23 repository checks;
the subsequently added converter case passed its focused rerun. Source and
summary are sealed under `goal-11-numeric-domains-01`. This does not implement
source additive perturbation or numeric quantization, and is not part of the
earlier isolated local runtime candidate or a performance acceptance claim.

The corrected preflight has now passed all **72/72 workloads**, preserving exact
before/after fitness separately for CPU and GPU. The two discrepant canonical
members also passed all-case direct value/error/fuel checks. Frozen GPU runtime
and core sources used by the diagnostic were verified byte-for-byte against the
reference commit (21 files). Formal paired timing is running under
`goal-11-local-reference-01/resume-02/trials` with the unchanged isolated binaries,
all 72 workloads and the original 3 warmup/15 measured blocks. See its live process
and `timing-status.json` before resuming; timings and full Goal 11 remain pending.

The frozen numeric-policy source audit is sealed under
`goal-11-numeric-policy-audit-01`. Ordinary initial and empty-target subtree
Int literals/list elements use `[-8,8]`; Float literals/list elements use
`round(uniform_real(-8,8)*1000)/1000`. Initial index/slice parameters separately
use `[-6,6]`. These constructors must not be collapsed into one generic interval.

Source constant perturbation selects original CONST node occurrences, including
constants in structured skeletons. It adds an Int delta in `[-2,2]`, adds a Float
delta without re-quantizing or clamping to initial bounds, flips Bool, and leaves
other selected values unchanged. GPU Float deltas use 16-bit quantization;
exhaustive enumeration confirms 65,536 distinct values including both endpoints
but no zero delta. CPU Float deltas use the reference standard real distribution.
Accordingly, mapping needs separate membership, generation and perturbation
policies, mutable origins for original constants, fixed administrative constants,
and preservation of selected nonnumeric no-ops. Domain resampling, a uniform
finite grid, or fixing every template-skeleton constant would narrow or otherwise
change the source operator behavior. This audit is source evidence, not completed
policy integration or reachable-search equivalence.

`quantization_scale` is now implemented for Float range domains. The sampler
rounds the continuous draw, preserving the source three-decimal constructor's
endpoint cells and signed zero. Membership recognizes the quantized grid; CPU
and GPU apply separately rounded multiplication/division and the same tie rule.
Offline conversion and nested FloatList domains retain the scale. Endpoint
validation uses quantization round-trip rather than exact integer multiplication,
so valid values such as `1.001` are not rejected by binary rounding.

To preserve the local timing run, correctness validation used a separate source
snapshot on Snoopy (RTX 3090, CUDA 12.6, Release sm86, register cap 64). The final
snapshot passed **116 native tests, 19 GPU tests with no skips, 79 tool tests and
23 repository checks**. Coverage includes all 16,001 source grid points, 768
precision-boundary cases, GPU tie/signed-zero bits, scalar and sequence mutation,
overlap and conversion. Source hashes, compiler/build identity, complete logs and
seals are under `goal-11-quantized-domains-02`; the initial checkpoint and subsequent
endpoint correction are retained separately. No remote performance timings were
combined with the local run. Separate sampling/membership domains, source additive
perturbation and complete resource/search mapping remain required.

Numeric `sample_from` now separates construction/resampling from membership.
The parser proves a same-type numeric sampler is contained in its outer range,
rejects recursive samplers and handles finite choices or checked range/quantizer
inclusion. Membership retains the outer range; scalar GPU descriptors cache the
sampler without narrowing the owning grammar. Nested sequence sampling and
offline conversion preserve the distinction. Conversion also bounds domain
serialization depth before traversing programmatically constructed domains.

The independent Snoopy Release snapshot passed **116 native tests, including
19 GPU tests with no skips, 79 tool tests and 23 repository checks**. Tests start
with a legal Float value outside its sampling range, verify CPU/GPU resampling
returns to the narrow domain, check Int finite-choice descriptors, nested FloatList
proposals, quantization, overlap and conversion. Source, compiler identity, complete
logs and hashes are sealed under `goal-11-sampling-domains-01`. Local formal runtime
timing continues on its original isolated candidate. This supplies the membership
boundary needed for additive perturbation; it does not implement that policy or
close full source search equivalence or performance acceptance.

Constant domains now support `mutation: "keep"` and `"flip"`, retaining resampling
as the default. Keep preserves candidate eligibility and accepts an unchanged
child without fallback or unused GPU sequence proposals. Flip complements a Bool
and changes repeated logical copies atomically while preserving fixed aliases.
Construction and membership remain independent of these policies. The finite
membership encoding cache strips policy metadata from value identity; grammar
identity still includes it. Concrete constant artifacts reject policy fields.

The final isolated Snoopy Release snapshot passed **116 native tests, including
19 GPU tests without skips, 79 tool tests and 23 repository checks**. Coverage
includes all public device value tags, CPU no-op counters, repeated logical groups,
pinned roots, actual Bool/StringList GPU reproduction, direct/overlap replay,
conversion and invalid policy rejection. The initial failed checkpoint, repaired
checkpoint, and final source/build/log hashes are retained under
`goal-11-constant-policies-01` through `goal-11-constant-policies-03`; the final
directory is sealed. This implements the audited nonnumeric mutation actions,
not complete source candidate eligibility or numeric additive perturbation.
The local isolated runtime comparison continues on its earlier candidate; these
remote correctness results are not performance acceptance.

Numeric range domains now implement additive mutation independently of their
construction sampler. Int deltas use inclusive integer ranges; Float CPU deltas
use continuous intervals, and an optional explicit GPU grid retains the closed
discrete delta law. The source `[-1,1]`/65535-step configuration matches all
**65,536 source delta values bit-for-bit on CPU and GPU**, including both
endpoints and excluding zero. Scalar device mutation applies the delta directly
without host proposals or construction quantization. Overflow and sums outside
membership retain the old value without clamping, retry or fallback. These
boundary rules are explicit general-domain behavior, not a claim that every
source-reachable search state has already been mapped.

The final independent Snoopy Release snapshot passed **116 native tests,
including 19 GPU tests without skips, 79 tool tests and 23 repository checks**.
Tests exercise 128-bit reference checks for signed overflow, Float overflow,
signed zero and subnormals, growth beyond construction support, logical-copy
atomicity, fixed aliases, actual Int/Float GPU reproduction, overlap and offline
conversion. Source, build identity and complete logs are sealed under
`goal-11-additive-constants-02`; the earlier passing checkpoint remains under
`goal-11-additive-constants-01`. Local runtime timing continues on the unchanged
isolated candidate. Source-origin eligibility, source-cost grammar profiles,
feasible generation, physical capacity closure and the full final-candidate
performance gates remain incomplete.

The offline lowering boundary now exposes checked source constant origins.
It retains source prefix selection order despite target emission reordering,
keeps independent source occurrences sharing a value-table slot, and excludes
compiler-added constants. The maintained structural fixtures verify that a
source-node perturbation commutes with lowering after normalizing only constant
table indices; complete AST identities retain node order, metadata and fuel.

An independent probe checked **34 frozen populations, 18,496 members and 59,518
source constant sites**. It excluded **45,811 administrative constants**, covered
**1,268 shared-slot candidates** and **1,270 source-order/target-order inversions**,
and passed all 59,518 commutation checks. Each site uses one checked proposal:
Int +1 (or -1 at the maximum), Float +0.125, Bool flip, or unchanged nonnumeric
value. This establishes correspondence for the checked members/proposals, not
exhaustive reachable-search equivalence. All input hashes, per-member counts,
probe sources/build/library identities and raw output are retained under
`goal-11-constant-origins-01/audit`.

The same Snoopy Release source snapshot passed **116 native tests, including
19 GPU tests without skips, 79 tool tests and 23 repository checks**. Complete
source and validation evidence are sealed under `goal-11-constant-origins-01`.
The origin map remains offline evidence, not a trusted production derivation;
binding these origins into certified grammar definitions and source resource
limits remains required. Local runtime timing continues independently.

Compiled grammar expressions can now carry explicit construction resource
charges, separate from execution fuel. Shared source/compiler validation accepts
bounded nonnegative node/depth charges and explicit depth resets only on concrete
materialized owners. `project_derivation_resources` reconstructs canonical
membership, uses verified subtree boundaries, and obtains charges from the
immutable grammar rather than attached provenance. Implicit envelopes retain
unit charges; repeated physical hole occurrences are each charged. The result
supports the existing atomic replacement allowance arithmetic.

The independent Snoopy Release snapshot passed **117 native tests, including
19 GPU tests without skips, 79 tool tests and 23 repository checks**. New coverage
checks default physical accounting, zero-cost administrative nodes, structural
depth resets, repeated holes, forged attached metadata, invalid declarations,
grammar identity, and identical seeded AST/fuel before and after annotation.
Raw source/build/test evidence is sealed under `goal-11-authored-resources-01`.
This is the authored accounting layer, not completed budget conversion:
generation feasibility and CPU/GPU variation still enforce physical limits.
Joint projected-budget feasibility/enforcement and certified source grammar
profiles remain required before larger physical ceilings can be enabled.

The structural joint-resource query now retains nondominated combinations of
physical nodes and projected node/carried-depth/reset-depth costs at each physical
depth. It respects production stages, reference fixed points and a shared choice
for repeated template holes. Independent minima cannot be combined into a false
feasibility result. Retained-state and construction-work caps throw explicitly;
they never turn exhaustion into a silently pruned search space.

Validation compares the query with independent enumeration across **7,560 budget
combinations**, plus **256 generated recursive/reset trees**, stage separation,
incompatible independent minima, shared-hole tradeoffs and state exhaustion.
The final Snoopy Release snapshot passed **117 native tests, including 19 GPU
tests without skips, 79 tool tests and 23 repository checks**. Complete evidence
is sealed under `goal-11-joint-resources-03`; the two earlier passing checkpoints
are retained. Construction-work accounting covers traversal, combination,
aggregation, dominance and sorting, but work-limit exhaustion was not stress-tested.

This query does not yet enforce projected budgets in production. Remaining work
includes reusable/contextual cost tables, distinct carried/reset donor allowances,
budget-aware generation and membership witnesses, preserving weighted choices,
CPU/GPU enforcement, certified source profiles and final performance acceptance.
The local timing session remains on its unchanged isolated candidate.

### Nested crossover closure, revised targeted validation

`goal-11-nested-profile-04` repairs the membership closure of the frozen nested
Map population without changing donor sampling. Membership-only alternatives
admit captured `[10]` at the outer scope and complex Map bodies at nested scopes.
The profile generator lifts formal scopes through seven lexical contexts, bounded
by the source offspring depth limit. The production `crossover_scope: closed`
contract permits unused external formal scopes to differ only after checking
that the exchanged payload has no external lexical captures.

All seven compatible directed splices of the one unique initial member now pass
membership; the full frozen population retains its 64 copies. In two actual GPU
reproduction generations, generation and acceptance rejections are both zero.
The remaining 11 and 5 rejected children are source-budget rejections, with
source limits 80 nodes / depth 7 and physical limits 531 / 141 unchanged.
GPU kernels executed in both generations. These observations diagnose and repair
a mapping gap; they are not final performance evidence or exhaustive reachable
search-space equivalence. Raw results, the deterministic profile generator,
probe source and input/output hashes are retained in the artifact directory.

### Mixed typed-payload frozen population mapping

`goal-11-mixed-profile-01` admits the complete frozen 64-member mixed population
as one population with four exact root contracts: IntList, FloatList, StringList
and String. No per-type tournament partition is used. It maps the two captured
Map bodies, the StringList Filter and StringList-to-String LinearRec, preserving
source constant mutation and separating fixed administrative constants. The
LinearRec mapping preserves the frozen adapter's implicit default fuel profiles;
explicit default events in the general package are semantically equivalent but
do not satisfy exact structural membership of this imported AST.

An independently linked frozen-reference probe and current candidate probe agree
on all **208 candidate sites and 43,264 compatibility decisions**. Two GPU
generations completed with zero generation, acceptance or budget rejections.
Fallback counts were 30 and 48; these are retained rather than presented as zero.
This is initial mapping evidence only: String root DC/DP1D/DP2D donor alternatives
remain to be integrated, so these runs are not search-closure or final timing
acceptance. The generator, probes, raw results and hashes are retained with the
artifact.

`goal-11-mixed-profile-02` adds the missing String root donor families using
literal/DC/DP1D/DP2D weights .82/.06/.06/.06 and the frozen constructors. ASGP
phase subtree variation remains disabled while source constants retain their
mutation policies. DC reserves eight frames, matching the maximum reachable
String source length; the frozen source import uses the checked minimum-frame
option. Four independently generated frozen-reference String donors (seeds
0, 1, 2 and 23) all pass native grammar admission and actual GPU evaluation.
The full mixed population also completes two GPU generations. This closes the
previously missing constructor families; output/fuel comparison and final
representative acceptance remain pending. Raw generation/build commands,
snapshots and GPU results are retained with hashes in `verification.json`.

`goal-11-string-fuel-01` completes the focused CPU semantic comparison for the
four independently generated String donors. The frozen production archives and
current lowered implementation return identical byte strings. Their first
successful fuel limits are respectively 47 (DP2D), 28 (DC), 2 (literal), and
21 (DP1D); all return Timeout one unit below the threshold and succeed at the
threshold and one unit above. A binary search locates each boundary, avoiding
a repeated exhaustive fuel sweep. DC uses the checked eight-frame import.
This covers these four concrete source donors, not every generated String tree.

### Metadata pressure population initial mapping

`goal-11-metadata-profile-01` admits all 64 frozen members across the six original
shapes: nested Map, numeric LinearRec, Int and Float DC, captured DP1D, and DP2D.
The source DP1D's 0..2 coordinate domain remains distinct from the fresh donor's
0..5 domain. All members participate in one mixed population with exact IntList,
Int and Float root contracts. Frozen-reference and candidate probes agree on
**215 variation sites and all 46,225 compatibility decisions**. Two GPU evolution
generations complete with zero generation, acceptance and budget rejections;
fallback counts are 36 and 42. Float root and sequence donor closure remains
pending, so this checkpoint is not final search-space or timing acceptance.

`goal-11-metadata-profile-02` adds Float DC/DP1D/DP2D root donors with the
source .82/.06/.06/.06 literal/structured weights and FloatList literal/Map/Filter
donors with .65/.175/.175 weights. ASGP phase subtree exclusions and mutable
source constants remain intact. The full metadata population completes two GPU
generations. Four independent frozen-reference Float donors pass native profile
admission and GPU evaluation. In `goal-11-float-fuel-01`, output IEEE-754 bits
and fuel thresholds agree exactly with the frozen CPU implementation: DP2D 47,
DC 25, literal 2, DP1D 21. Each checked program times out one unit below its
threshold and succeeds at and above it. This focused validation does not claim
exhaustive reachable closure or final timing acceptance.

### Canonical typed-storage import integration

The final-candidate benchmark now exposes `--normalize-typed-storage on` at
frozen AST import and captured-parent import. It applies the previously verified
reaching-definition normalization before grammar admission and rebuilds genome
metadata; the result records the option. Source-less bytecode and current v2
artifacts reject this rewrite. Candidate-side manifest routing and workload
identity are checked by the 23 passing tool tests; the native benchmark contract
test also passes.

`goal-11-normalized-import-01` verifies the full 64-member canonical frozen
population against 1,024 cases on GPU, with normalization enabled and disabled.
All 64 aggregate fitness values match exactly. This verifies the integrated
import path, not completion of the canonical grammar profiles or performance
acceptance. Source snapshots and input/result hashes are retained.

`goal-11-canonical-profile-01` inventories all 1,024 canonical members. They have
seven exact return types; the previous per-program split left eight ordinary
spellings with conflicting types across members. The checked import now derives
non-input storage names from original name bytes and exact type, independent of
name-table order. The resulting 102 declarations have no population-wide type
conflicts. Focused native tests cover table reordering, cross-member type
separation, assignment/read identity, execution and source-name collisions;
the benchmark contract test passes. GPU evaluation of all 1,024 frozen members
on the canonical 1,024 cases gives identical fitness vectors with normalization
enabled and disabled. This completes the shared-storage prerequisite; canonical
grammar construction and final representative performance remain pending.

The canonical base grammar now covers primitive overloads, typed storage,
assignment, branches, loops and exact return contracts in one grammar. It admits
440 of the 1,024 frozen members; all 584 remaining members contain structured
forms whose templates still need integration. Three ordinary members exposed
a missing authoring capability: assignment to an input slot. Named controls now
accept exactly one local `name` or `input_name`, with exact target typing.
Focused compile/generation tests verify target exclusivity, type rejection,
membership, input write/read behavior and unchanged external case data. Both
native test targets and 23 repository checks pass. Source/operator eligibility
and donor distributions of this base grammar are not yet a source-search
certificate; no timing or full canonical acceptance is claimed.

The canonical shared grammar now integrates all observed Map/Filter and numeric
LinearRec forms plus the existing Int/Float/String DC and DP templates. It
admits **64/64 and 1,024/1,024 frozen members**, preserving multiplicity and
ordinary control-flow structure. The native final-candidate benchmark accepts
the complete 1,024-member population with source limits 80 nodes / offspring
depth 7, physical limits 531 / 141, checked canonical storage normalization and
eight-frame DC import. Actual GPU evaluation produces the same 1,024 fitness
values as the unnormalized lowered population.

This is full initial membership, not completed source-search equivalence:
scoped primitive candidate eligibility and donor sampling/closure still need
integration before evolutionary timing. The scoped grammar uses generic
primitive overloads and shared traversal templates, not one production per
frozen program. Generators, admission results and hashes are retained in
`goal-11-canonical-profile-01/initial-mapping-verification.json`.

### Canonical candidate eligibility

Alternatives now support `variation: false` and `variation: "unbound"`. The
latter excludes a site's subtree when it contains a non-fixed bound reference,
using reconstructed membership origins; fixed template implementation references
do not exclude it. Descendant choices remain independent and constant mutation
is unchanged. Focused native coverage checks exclusion of bound leaves and
ancestors, explicit disabling, and retention through fixed template references.
The variation-contract test and 23 repository checks pass.

`goal-11-canonical-contract-01` compares the complete 64-member canonical frozen
population against the frozen native reference. **All 1,648 candidate positions
agree**. Of 2,715,904 compatibility decisions, **186 still differ** (144 newly
allowed and 42 newly disallowed). The differences involve ordinary storage and
binder-context identity, not missing candidate positions. The raw matrices,
examples, probes and source snapshots are retained. The user subsequently accepted
explicit name/type and lexical-scope compatibility in place of incidental frozen
name-table indices. These 186 differences are therefore an accepted migration
change, not an outstanding equality gate. Candidate positions, type safety, full
populations, source-budget equivalence, fuel and execution checks remain required;
this acceptance alone does not establish performance or evolution completion.


### Local-independent donor costs

Expression donors with no reachable ordinary-local reads or captures reuse the
compiled stage feasibility tables instead of constructing frame-specific tables.
Reachability follows stage-enabled references and expanded template children.
Targeted contextual-donor and generation-stage tests pass, including stage-isolated
local-dependent caches and unchanged seeded generation with unrelated locals.

A single affected two-generation GPU diagnostic preserves best/mean fitness and
all reproduction counters. Preprocessing changes from 45,017.9 / 48,505.3 ms to
42,477.2 / 45,771.8 ms. This is a partial repair, not a final performance pass;
692 / 764 donor-generation rejections still require investigation. Raw results,
source snapshots and hashes are in
`goal-11-canonical-contract-01/local-free-verification.json`.

A direct donor diagnostic samples seed 7 at all 1,648 initial canonical sites:
all replacement children pass membership; 1,413 also meet the projected budget,
while 235 exceed it. Over-depth untouched branches in initially grandfathered
parents explain these observed budget rejections. This is one diagnostic seed,
not a universal donor-closure proof. The donor-enabled profile retains exactly
the previous candidate positions and compatibility matrix. User authorization of
the new compatibility policy is recorded in the artifact verification record.

Projected-budget donor retries now reconstruct canonical membership and resources
before building full child variation analysis. Rejected candidates need no future
crossover-site enumeration; accepted candidates still receive full analysis.
The same attempt limit, random seeds and budget criteria remain in force.
The derivation-resource and donor tests pass, as do 23 repository checks.
A single affected two-generation GPU diagnostic reduces preprocessing further to
31,243.3 / 35,523.5 ms, with identical best/mean fitness and reproduction counters.
This remains a material preprocessing cost, not final performance acceptance.
Source snapshots and result hashes are recorded in
`goal-11-canonical-contract-01/resource-only-verification.json`.


### Projected-budget retry diagnosis

Snoopy reports `perf_event_open` and CPU sampling unavailable (kernel paranoid
level 4); no system settings were changed. An independent diagnostic binary adds
host-side scoped timers around one generation. Of 49,559 donor attempts, generation
uses about 4.9 s, splicing 0.85 s and membership/resource reconstruction 19.9 s;
5,124 successful attempts spend another 3.25 s in full variation analysis.
These instrumented timings are diagnostic only, not acceptance measurements.
The retained source and log are `goal-11-canonical-contract-01/donor-timed.cpp`
and `donor-timed.log`.

Resource-only queries now reconstruct native membership and canonical charges
without compiling bytecode or capturing unused exact scope annotations. Full
witness generation and accepted-child analysis retain lowering validation.
A focused recursive-grammar test compares resource-only and fully lowered
canonical witnesses; membership and derivation-resource tests pass.
One affected generation takes 25,458.7 ms in preprocessing, compared with
31,243.3 ms before this change, with identical fitness and reproduction counters.
This run is diagnostic (a test target was rebuilt during it), not a controlled
paired performance result.

Projected donor retries also remember exact previously rejected decoded donor
identities within each bounded retry loop. The destination and frame remain fixed;
all random draws and retry limits are preserved. A focused test compares selected
donors and exhaustion against an independent uncached retry loop for 16 seeds.
Resource and donor tests pass; final representative acceptance remains pending.

With the bounded exact-rejection cache, the affected generation preprocesses in
19,008.2 ms, again with identical fitness and all reproduction counters. This is
one diagnostic run after a material change, not representative paired acceptance.
Results, source snapshots and hashes are retained in
`goal-11-canonical-contract-01/rejection-cache-verification.json`.


### Representative comparison setup

`representative-snoopy-01/workloads.json` is a validated paired manifest for the
six preselected logical workloads and all 1,344 frozen members. Twelve rows cover
warm evaluation and evolution; the 64-member canonical evolution row runs five
generations, and evolution rows include all four canonical CPU/GPU modes.
This prepared manifest is not a completed gate. Its readiness record keeps mapping
closure review, integrated checks, paired measurements and tolerance assessment
pending. Mixed population roots are now explicitly routed and hashed by the runner;
23 focused migration-tool tests pass.

The remote reference wrapper links the existing frozen reference libraries.
All 93 non-wrapper C++ source/header files match the frozen local source; the two
CLI wrapper files match the previously audited adapter. Reference and candidate
use Release `-O3 -DNDEBUG`, CUDA sm86 and `--maxrregcount=64`, with the same compiler
paths. The reference 64-member GPU reproduction smoke run completes in 227.7 ms
for one generation; candidate preprocessing around 19 seconds remains a persistent
material regression that prevents Goal 11 completion.

Direct reference/candidate GPU evaluation of the complete canonical 1,024-member
population gives identical fitness vectors. One warm-up and one diagnostic sample
per side gives warm call times 26.3633 / 28.3675 ms. These unpaired preflight samples
do not establish the 5% performance gate. Commands, results and hashes are retained
in `representative-snoopy-01/preflight-verification.json`.


### Prepare only donor pools the GPU can consume

Crossover preparation previously built mutation donors that its kernel never used.
It now omits them while preserving donor-seed draws and candidate ordering. A
second change anticipates each mutation child's possible candidate from the shared
seed/unit-sampling schedule, including the no-constant-groups fallback, and builds
only that pool. The GPU still performs mutation selection and splicing. Focused
preparation, compiled GPU backend and compiled GPU mutation tests pass; the CUDA
test checks seven candidate positions with only the anticipated pool populated.

On the same 64-member two-generation diagnostic, crossover-only preparation first
reduced preprocessing to 9,905.6 / 10,230.4 ms. Anticipated mutation preparation
reduces it further to 329.0 / 286.3 ms, with complete generation times
601.2 / 554.1 ms. Best/mean fitness and reproduction counters agree except for
fewer generation rejections from donor pools that are no longer constructed.
This remains roughly 56 times the 10.72 ms diagnostic target for generation zero;
no final performance pass is claimed. The earlier reference 227.7 ms total
includes initialization: its comparable generation-zero time is 10.2097 ms.
Source snapshots and result hashes are retained in
`goal-11-canonical-contract-01/anticipated-verification.json`.


A subsequent host-timing diagnostic records 2,129 runtime-identity calls (63.0 ms),
1,099 membership matches (125.2 ms), 1,099 witness reconstructions (33.9 ms), and
449 full variation analyses (85.9 ms). These scopes overlap and include startup;
they must not be added as disjoint generation phases. Raw instrumentation and
logs are retained under `goal-11-canonical-contract-01/analysis-timing`.

An experiment scaling the analysis cache above 128 entries gave two-generation
times 562.2 / 546.4 ms versus 601.2 / 554.1 ms. Fitness and counters matched, but
the small/inconsistent improvement did not justify increased retained memory or
further repetitions. The cache-capacity change was reverted; production retains
128 entries. `analysis-capacity-assessment.json` records that decision.


### Reuse immutable executable-root certificates

`CompiledGrammar::require_executable(root)` previously traversed the reachable
rule graph on every call, including calls reached through a cache-hit request
check. Successful root checks now share a mutex-protected certificate across
copies of the same immutable compiled grammar. Roots are checked independently;
invalid roots and unsupported execution still fail. Focused tests cover repeated
checks, copied grammars, and a supported root alongside unsupported recursion.
The grammar-compiled and compiled GPU backend tests pass.

The affected two-generation diagnostic drops total time from 601.2 / 554.1 ms
to 234.2 / 202.7 ms. Preprocessing is 154.2 / 126.1 ms and decode is 36.1 / 37.0 ms.
Best/mean fitness and all reproduction counters match. Generation zero is still
about 22 times its 10.72 ms diagnostic target; no final gate pass is claimed.
Sources and result hashes are in
`goal-11-canonical-contract-01/executable-cache-verification.json`.

### Remaining donor preparation costs

A one-generation scoped-timer diagnostic after the executable-root cache measured
150.10 ms of preprocessing, including 125.52 ms for donor generation/admission.
A separate donor diagnostic split this into generation 38.56 ms (380 attempts),
resource reconstruction 46.50 ms (270 children), and accepted full analysis
21.19 ms (124 children), plus destination lookup and splice work. These are
diagnostic timings, not paired acceptance measurements.

Caching the immutable resource-locality predicate did not materially improve the
two-generation run (235.93/202.00 ms versus 234.23/202.72 ms); that experiment
was reverted. Fitness and reproduction counters matched, and the resource and
GPU backend focused tests passed. Raw evidence and the next optimization target
are recorded in `goal-11-canonical-contract-01/donor-phase-assessment.json`
under the external grammar-migration artifact root.

Two donor-analysis consolidation experiments were also rejected: full cached
analysis for every attempt measured 269.81/181.97 ms; early budget rejection
inside witness reconstruction measured 265.54/206.51 ms. Both preserved the
observed fitness and reproduction counters, and the staged variant passed
resource, donor, and GPU backend tests, but neither established a consistent
improvement over 234.23/202.72 ms. Both implementations were reverted. The
staged diagnostic may overlap the end of the run with test compilation; it is
not acceptance evidence. See `single-analysis-assessment.json` in the external
canonical-contract artifact directory.

A fresh membership diagnostic (including initialization, 1,099 calls) measured
95.27 ms in the matcher: native verification/result transfer 48.10 ms, constant
encoding 16.27 ms, and grammar matching 27.31 ms. Witness construction separately
took 35.41 ms. These scopes are not additive to generation timing. Raw evidence
is in `membership-phase-timing/`. The matcher now moves the completed native
verification result instead of copying its type/scope vectors; validation itself
is unchanged. No performance gain is claimed from that ownership-only change.

Native verification now builds node-index diagnostic paths only on failure, and
its structural/type verifier success paths move their completed results. All
checks and error strings remain unchanged; structure tests now assert exact
constant/name/unused-index paths. Structure, type, exact-scope and GPU backend
tests passed. One two-generation diagnostic measured 230.37/198.30 ms against
234.23/202.72 ms before these ownership/allocation changes. Fitness and all
reproduction counters matched. This small observed reduction is not a paired
performance acceptance result. Source snapshots and hashes are in
`lazy-diagnostics-verification.json` under the canonical-contract artifact root.

### Omit unreachable donors for prepared constant branches

Mutation preparation now also omits the anticipated subtree donor pool when the
shared mutation schedule selects the constant branch and the prepared parent
stream contains a constant group. NoGroups is the only GPU constant outcome that
falls through to subtree mutation, so that pool cannot be read. The GPU still
performs its mutation decisions and operations, and host donor seeds are consumed
as before to preserve subsequent candidate ordering. Parents without groups
retain the existing fallback preparation.

Preparation and device tests cover both a nonempty constant stream without any
donors and the existing no-group subtree fallback; all three focused tests
(grammar_repro_prep, compiled_backend_gpu, compiled_mutation_gpu) passed. The
two-generation diagnostic fell from 230.37/198.30 ms to 213.06/186.77 ms, with
preprocessing 136.42/114.91 ms. Fitness and every reproduction counter matched.
This is not final paired acceptance; the material regression remains. See
`constant-branch-verification.json` and its source snapshot in the external
canonical-contract artifact directory.

The runtime identity path subsequently stopped copying the complete AST before
structural serialization. The new helper emits the same empty-constant-pool
structure bytes, and runtime identity still appends every decoded constant.
Cache tests compare the direct encoding with copy-and-clear serialization and
retain payload reload/token independence checks. Runtime cache, variation cache
and GPU backend tests passed. This allocation-only change was not separately
benchmarked; the 213.06/186.77 ms diagnostic above precedes it and is not final
revision evidence. See `structure-identity-verification.json` for source hashes.

Typed verification now maintains its local/binder type environments in index
order. Signature calculation iterates that order directly, removing temporary
vectors and sorting at each expression; exact scope annotations retain their
previous ordering. Type, exact-scope (assignment, branch intersection and loop
visibility), and GPU backend tests passed. Together with the preceding structural
identity copy removal, the next two-generation diagnostic measured 204.73/178.30
ms versus 213.06/186.77 ms. Fitness and reproduction counters matched. The timing
does not isolate these two edits or establish final performance acceptance.
Source hashes and the raw result are recorded in `ordered-scope-verification.json`
in the external canonical-contract artifact directory.

Direct scalar constant encoding now avoids construction of a singleton JSON
object/array before canonical serialization. Runtime identity, membership, and
variation comparison share this path. Exact-byte comparison against the original
JSON route covers all eight types, signed integer extrema, floating signed zero,
subnormal/maximal finite values, Unicode and escaping; invalid Char/NaN cases
remain rejected. Cache, membership and GPU backend tests passed. A two-generation
diagnostic measured 190.75/167.97 ms (preprocessing 123.54/103.36 ms), down from
204.73/178.30 ms. Fitness and reproduction counters matched. The first generation
remains about 17.8 times the 10.72 ms allowance; final paired acceptance is still
pending. Source snapshots/hashes are in `direct-constant-verification.json`.

Donor admission caching now spans all seeds in one prepared site pool. A cache
local to that pool retains at most 256 exact decoded identities and admission
results; it cannot cross destinations, sites or frames. Each seed still generates
its donor and follows the original retry sequence, but an identical donor does
not repeat splice/resource/full-admission work. Empty result slots retain bounded
generation failures, and preprocessing consumes the same host donor seeds.

Resource tests compare pooled results/exhaustion with independent calls, alongside
the existing uncached retry reference. Resource, preparation and GPU backend
tests passed. A two-generation diagnostic measured 182.70/153.59 ms, with
preprocessing 115.65/88.62 ms, versus 190.75/167.97 ms before this change. Fitness
and reproduction counters matched. Final paired acceptance remains pending;
`pool-admission-verification.json` records raw result and source hashes.

Contextual generation previously copied all grammar input/local names into every
donor, including 101 unused locals in a canonical constant donor. Framed generation
now assigns native name indices on first use, including named control nodes and
bounded captures. Initial non-framed generation preserves its original table
layout and seed behavior. Constant-only frame tests require an empty name table;
all eight typed-local tests require exactly the referenced name and remapped
index. Generation-stage, contextual-donor, preparation and GPU backend tests pass.

The two-generation diagnostic fell from 182.70/153.59 ms to 148.24/116.58 ms;
preprocessing fell to 80.77/52.09 ms. Fitness and reproduction counters matched.
The first generation remains about 13.8 times the 10.72 ms allowance. Final
acceptance is pending; `lazy-donor-names-verification.json` records source and
result hashes.

Certification subsequently stopped repeating the cache lookup and native metadata
construction after an unchanged table compaction. Validation before compaction
is retained, including rejection of invalid unused constants; changed tables
still receive fresh analysis. Reproduction tests cover both paths and remapped
constant indices, and reproduction/GPU backend tests passed. No separate timing
was run for this allocation/cache-lookup cleanup; the preceding 148.24/116.58 ms
diagnostic is not an exact-final-revision measurement. Source hashes are in
`unchanged-compaction-verification.json`.

Mixed-root analysis now selects the exact root inside membership reconstruction
using the same native verification that supplies type/scope annotations. It no
longer runs a separate native verification just to select a root. Each configured
request is still validated; the selected root must pass membership and lowering.
New eight-type tests compare explicit-root and population-root analyses, and
reject wrong-domain constants, missing result types and truncated ASTs. Variation
cache, membership, CPU reproduction and GPU backend tests passed.

Together with the preceding unchanged-compaction cleanup, a two-generation
diagnostic measured 139.07/106.88 ms (decode 22.06/23.04 ms) versus 148.24/116.58
ms. Fitness and reproduction counters matched. Preprocessing still accounts for
80.77/51.28 ms. This is not a final paired acceptance result; source and output
hashes are in `population-root-verification.json`.

A refreshed one-generation donor diagnostic after mixed-root reconstruction
measured generation 8.52 ms (348 attempts), destination analysis 3.40 ms
(96 calls), splice 3.07 ms (200 calls), resource reconstruction 33.42 ms
(200 calls), and accepted full analysis 14.23 ms (80 calls). These scopes
identify membership/resource reconstruction as the remaining donor hot path;
they are not a paired performance gate. The diagnostic source, build commands,
raw results and hashes are retained in `donor-timing-current2/assessment.json`
under the external canonical-contract artifact directory.

A scoped plan-encoding diagnostic found only 0.21 ms across six calls in the
canonical one-generation run, so no plan-encoding optimization was pursued
(`plan-encoding-timing/`). Membership matching instead consolidated its active,
accepted and decision indices into one table, avoiding duplicated key nodes.
Active sentinels preserve alias-cycle handling; failed entries are erased.
Membership (including self/mutual alias reconstruction), resource projection,
variation cache and GPU backend tests passed. Two-generation timing was
137.31/106.70 ms versus 139.07/106.88 ms, with identical fitness and reproduction
counters. This does not establish a material timing improvement or final
acceptance; `matcher-state-verification.json` retains exact source/output hashes.


A subsequent population-1024 full-generation diagnostic on Snoopy exposed costs
not represented by the earlier steady-evaluation timings. Old GPU generation
time was 74.275749 ms; the candidate with pool-owned destination certification
took 2746.889682 ms. Enlarging the GPU analysis cache from 128 to 4096 entries
reduced the candidate to 2115.631544 ms (23.0% reduction), with identical initial
fitness and all reproduction counters. The retained policy is four times the
population, clamped to 128–4096 entries; the cap was added after this measurement
and does not alter capacity for this workload. Retained entry count increases
32-fold here; byte/RSS cost has not yet been measured.

Single-generation CPU diagnostics took 3180.301884 ms before and 8657.924112 ms
after. Corresponding full-generation CPU/GPU speedups are 42.8175 and 4.09236;
retention is only 9.56%, far from the user's requested near-preservation of
speedup. These are diagnostic single samples, excluding initialization, not a
passed final gate. Raw commands/results, source snapshots and counter comparison
are under `representative-snoopy-01` (`canonical1024-*evolution*`,
`canonical1024-after-population-cache.json`, `population-cache-assessment.json`).
Pool certification retains a shared analysis across cache eviction; the resource
retry test now exercises a one-entry cache and matches independently generated
donors. Resource and compiled GPU backend tests passed; the bounded cache policy
also passed the compiled GPU backend test.

The next local cleanup reuses the first variation occurrence's already-computed
native scope; only subsequent linked occurrences require intersection. Contract
and compiled GPU backend tests passed. Population-1024 diagnostic times were
8565.590061 ms CPU and 2107.285545 ms GPU (4.06475x); this does not establish a
material improvement over 4.09236x. GPU fitness and every reproduction counter
were unchanged. Evidence is `representative-snoopy-01/scope-reuse-*` and
`canonical1024-scope-reuse-{cpu,gpu}.json`.

Next architectural investigation: batch only read-only membership/variation
analysis, keeping generation and payload registration ordered. Compiler and
membership inspection found no payload registration calls in these analysis
paths. Independent analysis results would still need compatibility IDs interned
in original order, bounded scheduling, propagated exceptions, and exact cache
identity validation before integration; parallel analysis is not implemented.

Bounded parallel parent analysis is now implemented in the GPU preparation path.
The new mixed-type 160-member cache test compares serial and parallel analyses
with capacities 3 and 256, including compatibility ID ordering, hit/miss/eviction
counts, verified types, lowered sizes, malformed AST rejection and zero-worker
rejection. It and the compiled GPU backend test passed on Snoopy.

The population-1024 diagnostic decreased from 2107.285545 to 1988.208002 ms;
prepare-input time decreased from 235.728072 to 120.681495 ms. Preprocessing
remains 1260.682350 ms and decoding 311.624404 ms. Initial fitness and all
reproduction counters are identical. This is useful but insufficient to restore
speedup. Evidence: `representative-snoopy-01/parallel-analysis-*` and
`canonical1024-parallel-analysis-gpu.json`. CPU code routing is unchanged; no
new paired CPU/GPU gate result is claimed.

The population-1024 donor diagnostic uses a separately linked timed donor object
and leaves production binaries unchanged. It records generation 153.516334 ms
(9082 attempts), destination analysis 11.098128 ms (431 pools), splice 47.317336
ms (4725 children), canonical resource reconstruction 551.486968 ms (4725),
accepted-child analysis 198.655241 ms (1421), and donor identity 36.302934 ms
(9082). This directs the next change toward reconstruction reuse while preserving
the resource-rejection fast path and retry order; parallel parent warm-up alone
cannot address these sequential donor costs. Sources, link recipe and timings are
in `representative-snoopy-01/donor-phases-1024/`.

A fused offspring-admission experiment passed the cache, resource-retry and GPU
backend tests, retaining identical fitness/reproduction counters, but regressed
the population-1024 GPU generation from 1988.208002 to 2278.608169 ms
(preprocessing 1522.880703 ms); CPU measured 8908.206790 ms versus the prior
8565.590061 ms. It captured full exact-scope data for every budget attempt and
checked canonical resources before lowering/sites, caching only accepted results.
The experiment was reverted, including the added API and test; parallel parent
analysis remains. Evidence is `representative-snoopy-01/fused-admission-*`.
This rules out this eager scope-capture fusion as the next retained optimization.

Two bounded follow-ups to fused admission were also reverted. Deferring exact
scope capture until after resource acceptance measured 2211.989208 ms GPU
(preprocess 1470.871687 ms). Deferring the runtime cache key until after acceptance
then measured 1993.329466 ms (preprocess 1256.770159 ms), essentially the retained
1988.208002 ms result. Both passed the cache/resource/GPU tests and preserved all
fitness/reproduction counters. Neither justifies the additional admission API
and deferred-verification machinery. The original files were restored exactly.
Raw results and the last variant source are in `representative-snoopy-01/` under
`lazy-admission-*`, `late-key-*`, and corresponding `canonical1024-*.json` files.

The report/runner protocol now matches the user's latest acceptance revision.
`representative-speedup-2026-09-23` gates Q >= 0.95 only; A_mode/A_cpu remain
reported with `reporting_only: true` and no minimum. The earlier representative
protocol and frozen-v1 retain their original judgments. Twenty-five focused
Python tests pass, including the new Q boundary, symmetric absolute slowdown,
bounded recheck, historical-protocol preservation, and runner/auditor round-trip.
`representative-snoopy-01/workloads-speedup.json` is a separate copy of the prepared
manifest with the new protocol; original workloads/trials are unchanged. This
corrects acceptance tooling, not the outstanding runtime regression.

Exact-scope capture now compares actual bindings with the last captured scope
before allocating a lookup key. Exact-scope tests (including restoration of the
original ID after a loop) and the compiled GPU backend passed. The population-1024
GPU diagnostic measured 1974.054281 ms versus 1988.208002 ms, with identical
fitness and reproduction counters. This small difference does not establish a
material speedup; the bounded reuse is retained for reduced allocations. Evidence
is `representative-snoopy-01/scope-cache-*` and
`canonical1024-scope-cache-gpu.json`.

A resource-shape probe observes canonical child structure, each constant's exact
type and membership in every compiled domain, and the equality partition of
canonical decoded constants (needed for repeated holes). Within fixed
parent/site pools, 3722 of 4725 successful resource queries repeat such a key;
no resource-count/depth mismatch was observed. Building naive keys costs
388.505500 ms; packed masks shared across this diagnostic grammar reduce that
to 291.699647 ms. This is still substantial versus the earlier 551 ms total
resource-reconstruction cost. No production shape cache was added. The probe's
static grammar-pointer cache is deliberately diagnostic-only and must not be
used as a production lifetime/concurrency design. Evidence and limitations are
in `representative-snoopy-01/resource-shape-probe/assessment.json`.

Instrumenting packed resource-shape keys further measured 303.281787 ms total:
88.264949 ms native structure serialization, 54.967147 ms constant encoding,
and 122.363208 ms domain-mask lookup/construction. The remaining time includes
equality partitioning and assembly. Repeated keys remained 3722/4725 with zero
observed resource mismatches. This argues against deploying the current whole-
child key. A smaller pool-scoped scalar-literal key is the next candidate, with
full reconstruction retained for unsupported shapes and payload-bearing parents.
The probe and its phase log are diagnostic only; production is unchanged.

The scalar-literal resource-cache experiment was also reverted: population-1024
GPU time was 2007.121555 ms versus 1974.054281 ms, with matching reproduction
counters. It used fixed parent/site ownership, scalar-only parent constants,
first-equal destination constant reuse (including signed-zero coalescing), and
all-domain membership bits; annotated and structured donors kept full resource
reconstruction and accepted children retained full analysis. Resource and GPU
backend tests passed. Independent Int-domain-cost and Float signed-zero retry
checks are retained as regression coverage; they compare pooled generation with
explicit full child analyses. Artifacts: `representative-snoopy-01/scalar-resource-*`.
Further key/cache layering is not supported by these measurements; broader
execution changes are needed to address the remaining serial work.

**Build-identity correction:** restoring source with preserved timestamps can
leave it older than experiment object files, so a successful incremental build
did not prove the intended revert. This was observed directly for donor.cpp:
source mtime 2026-09-23 23:22:49 versus object 23:59:41. Previous claims of a
rebuilt reverted candidate, and following incremental diagnostic comparisons,
must not establish exact source identity. Their timings remain historical
observations only. A clean-first rebuild of current restored sources and fresh
CPU/GPU diagnostics are required; see `representative-snoopy-01/
revert-build-caveat.json`. Future timestamp-preserving restores must force
recompilation. This correction supersedes unsupported binary/source claims above.

The clean-first rebuild completed and all four focused tests passed (exact
scopes, variation cache, derivation resources, compiled GPU backend). All 321
C++ source/header/test/build files match the local workspace by SHA-256, with
no missing files; the fresh binary hash is retained. New population-1024
diagnostics measured CPU 8434.456193 ms and GPU 1998.802866 ms
(speedup 4.219754x). Relative to the existing frozen-reference diagnostic
42.817500x, Q is 0.098552, still far below 0.95. These replace uncertain
post-revert candidate timings as the current-source observation, but remain
single samples rather than a final paired gate. Evidence is
`representative-snoopy-01/verified-clean-*` and
`canonical1024-verified-clean-{cpu,gpu}.json`.

Payload-free donor pools now use up to four independent seed workers, each with
its own variation context. The compiled graph reachability certificate checks
all construction stages; tests distinguish recursive scalar roots, unreachable
payload rules and scalar roots that consume generated strings, and copied
grammars/invalid roots. These checks plus the resource retry and compiled GPU
backend tests passed. The population-1024 GPU diagnostic decreased from the
clean-source 1998.802866 ms to 1817.468471 ms (9.07%); preprocessing was
1082.309939 ms. Initial fitness and all reproduction counters match. This is a
retained improvement, not final speedup acceptance. Evidence:
`representative-snoopy-01/parallel-donor-*` and
`canonical1024-parallel-donor-gpu.json`.

### Staged payload donor pools and shared destination (2026-09-24)

Freshly rebuilt Snoopy diagnostics use the canonical frozen population of 1024,
one complete generation, GPU reproduction with overlap off. One measurement per
version/engine gave reference CPU 3096.610861 ms and GPU 75.670673 ms (40.9222x),
and staged-pool candidate CPU 8455.936846 ms and GPU 1489.271043 ms (5.6779x).
Relative speedup retention was 0.13875, far below the 0.95 requirement. These are
single diagnostics, not the representative acceptance trials.

Sharing the pool-owned destination certificate across seed workers then reduced
the GPU diagnostic to 1427.619576 ms (4.14% below the preceding measurement).
Fitness and all recorded reproduction counters matched. CPU was not remeasured
for this GPU pool-only change, so this observation does not establish a new
paired CPU/GPU speedup. Parent membership/site verification remains intact;
workers retain independent donor generation and child admission.

Payload staging, registry and index tests, derivation resources, contextual donor,
compiled GPU backend, and CPU/GPU fitness/evolution parity passed across the
affected changes. A dedicated staged-read test also checks that an external
registry change cannot replace a worker's captured value or evade commit conflict
detection. Raw commands, binary hashes, measurements, source hashes and retained
source files are under `representative-snoopy-01/`, with names beginning
`canonical1024-speedup-current`, `shared-destination`, and
`canonical1024-shared-destination`. Goal 11 remains incomplete.

### Allocation-free fuel matching diagnostic (2026-09-24)

Membership no longer allocates temporary event maps for each candidate profile;
verified unique events are compared directly, with node-kind rejection before
leaf-profile checks. Existing reordered-event, changed-cost, missing-profile and
linked-hole checks pass, with explicit missing-event and duplicate-event cases
added. Source fuel and resource-contract checks pass as well.

One canonical 1024-member CPU/GPU pair measured 8404.527917 ms and
1390.786538 ms, respectively: 6.0430x speedup, or
0.14767 of the preceding reference's 40.9222x. GPU time was
2.58% below the shared-destination diagnostic; this small single-run
difference is not a statistically established gain. Fitness and reproduction
counters matched. Raw results and exact binary/source identities use the
`fuel-match` and `canonical1024-fuel-match` prefixes under
`representative-snoopy-01/`. The relative-speedup gate still fails.

`nsys status --environment` reports CPU sampling unavailable on Snoopy because
`perf_event_open` is denied (kernel paranoid level 4). No privilege or host-setting
changes were made; phase timings and source inspection remain available.

### Direct AST decimal serialization (2026-09-24)

Direct string output replaces decimal stream formatting in AST display, cache
keys and structural keys. A standalone differential oracle compiled the preceding
implementation alongside the candidate: 1,000 generated cases matched byte for
byte for all three encodings, including embedded NUL names, signed integer
boundaries, floating bit patterns and lexical/traversal/fuel/bounded metadata.
The fixtures exercise serialization, not native program validity. Cache, region
codec, compiled GPU backend and evolution CPU/GPU parity tests passed.

One canonical complete-generation diagnostic measured CPU 8131.173346 ms
and GPU 1265.726118 ms, a 6.4241x speedup (0.15698 of the
preceding frozen reference). GPU time is 8.99% below the preceding
fuel-match diagnostic. Both backends retain identical fitness and reproduction
counters. No repeated-trial confidence claim is made and Goal 11 still fails its
speedup gate. Sources/oracle are in `representative-snoopy-01/ast-writer/`;
commands, binary hashes, source hashes and assessments use the `ast-writer`
prefix, with run outputs named `canonical1024-ast-writer-{cpu,gpu}.json`.

### Worker admission without unused variation sites (2026-09-24)

An independent instrumented membership object measured the full diagnostic CLI
(import/init included): 20,873 matcher calls, with aggregate thread times of
540.85 ms for native verification/request setup, 132.82 ms constant decoding,
270.40 ms grammar matching, 150.90 ms witness/resources and 230.94 ms lowering
(15,315 lowering calls). These are not generation-only or additive wall times.
Probe source and results live in `representative-snoopy-01/membership-phases/`.

Pool seed workers previously constructed full future variation sites for admitted
children, despite discarding their contexts immediately. Execution admission now
reconstructs membership/resources once, lowers only budget-admitted children, and
omits unused variation sites. CPU and sequential fallback cache behavior remain
unchanged. Resource admission matches a full-analysis test oracle; contextual
donor and compiled GPU backend tests pass.

The intermediate diagnostic measured GPU 1235.713684 ms; the combined change
measured 1190.826564 ms, 5.92% below the preceding
1265.726118 ms observation. Fitness and all reproduction counters match. These
are single GPU diagnostics, with no CPU remeasurement or new paired speedup
claim. Exact commands/binary hashes and sources use `worker-admission-once`,
with run output `canonical1024-worker-admission-once-gpu.json`. Goal 11 remains
incomplete.

### Native fuel verification allocation reduction (2026-09-24)

Fuel side-table checks defer diagnostic path strings until an error, replace the
owner set with node-index flags, and compare earlier profile events directly.
Bounds checking precedes indexing; unsupported/duplicate events retain their
error precedence. No fuel checks or lowering checks are removed. Exact duplicate
event/profile paths and out-of-range owner behavior are covered by new assertions.
Source fuel, structural verification, grammar membership, compiled GPU backend,
and evolution CPU/GPU parity checks passed across the change.

One canonical CPU/GPU pair measured 7723.213609 / 1159.483302 ms,
respectively (6.6609x speedup; 0.16277 of the prior frozen reference).
GPU time was 2.63% below the preceding worker-admission observation;
this small single-run difference is not a stable-gain claim. Fitness and all
recorded reproduction counters match for each backend. Raw commands, binary and
source hashes, source snapshots and assessment use the `fuel-verifier` prefix
under `representative-snoopy-01/`; run files are
`canonical1024-fuel-verifier-{cpu,gpu}.json`. Goal 11 remains incomplete.

### Reusable donor-worker experiment, not retained (2026-09-24)

A thread-local four-worker executor replaced per-seed `std::async` thread starts.
The experimental canonical GPU generation measured 1151.084281 ms against the
retained 1159.483302 ms observation: only 0.72% lower in single runs. Fitness and
reproduction counters matched. This does not justify additional queue/shutdown
synchronization, so the executor was reverted with a freshly written source file
and the remote binary rebuilt. The retained implementation still uses joined
`std::async` workers; 1151 ms is not its measured performance.

Resource/donor/GPU backend and evolution parity tests passed during the experiment.
Exception-then-success and partial-final-batch donor tests remain as useful
regressions and were rerun after restoration. Experimental source, exact command,
binary hash, output and assessment use the `donor-workers` prefix under
`representative-snoopy-01/`. Next optimization should target validation work,
not assume thread startup dominates the remaining preprocessing time.

### Admission shape reuse cost probe (2026-09-24)

A separately linked diagnostic kept full production validation and additionally
compared native structure, constant type/domain masks and decoded-constant equality
partitions across budgeted worker admissions. Of 5,558 queries, 4,717 keys repeated;
3,789 were repeated rejections, with zero observed answer mismatches. Fitness and
reproduction counters matched the retained binary. This is a canonical workload
observation, not a general proof of cache equivalence.

Aggregate key construction took 432.652340 ms; full validation took 579.900824 ms,
of which repeated rejections took 428.490907 ms. These are summed worker times,
not additive generation wall times. Key cost already exceeds the optimistic
rejection-only savings, so no full-child shape cache was added. A useful next
prototype must avoid re-encoding unchanged parent constants, own its parent/pool
lifetime, and account for domain membership and constant equality/reuse exactly.
Raw source, build/command/binary hash and results are under
`representative-snoopy-01/admission-shape-probe/`. Production code and its latest
measured speedup remain unchanged.

### Pool-owned scalar rejection cache, not retained (2026-09-24)

A bounded shared pool cache reused only prior budget rejections for scalar literal
donors and scalar-only parents. Keys included donor structure/fuel metadata,
actual first-equal parent constant reuse (including signed-zero coalescing), and
all grammar constant-domain bits. Accepted candidates retained full membership
and lowering checks; nonliteral/payload cases stayed on the existing path.

Resource admission/domain/signed-zero oracle and compiled GPU backend tests
passed. The canonical GPU diagnostic measured 1168.787983 ms versus the retained
1159.483302 ms observation, with unchanged fitness and reproduction counters.
No gain was observed, so the prototype was reverted and rebuilt using fresh
source timestamps. Source, commands/binary hash and assessment use the
`pool-rejection` prefix under `representative-snoopy-01/`. This rules out retaining
this particular cache implementation; it does not establish that every form of
incremental validation is unhelpful. Goal 11 remains incomplete.

### Frozen backend donor routing audit (2026-09-24)

Source inspection confirms that frozen CPU subtree mutation passes
`is_statement_value_root(...)` as `allow_asgp`, whereas frozen GPU donor preparation
passes `true` for all fresh donors. Both start from an empty name table, so the
ordinary-name branches in the common generator are inactive. For eligible
Int/Float/String and sufficient donor depth, that generator proposes a structured
DC/DP form with probability 0.18, uniformly across enabled forms.

The measured canonical profile instead routes ordinary expression sites to
literal-only mutation entries, reserving the 0.82/0.06/0.06/0.06 mixture for
statement-value entries. Thus it follows the CPU location restriction and does
not yet establish frozen GPU donor-proposal equivalence. This is distinct from
the user-accepted name/type compatibility change. A directed valid non-statement
GPU splice and its capacity/fallback outcome remain to be checked; source routing
alone does not prove all proposals can be accepted.

Exact source/profile hashes and per-type route extraction are retained in
`representative-snoopy-01/donor-routing-audit/audit.json`. Formal mapping and timing
acceptance remain pending. Existing single-run timings are diagnostic observations,
not evidence that this search-space difference has been resolved.

A directed frozen-source witness now places each generated Int DC/DP1D/DP2D donor
as the left operand of ADD, below RETURN (not a statement-value root). Seeds
1/23/0 produce children of 15/12/13 nodes; all pass native verification with
80-node/depth-7 limits and compile successfully. Source, build commands and log
are retained in the routing audit directory. Device splice/copyback and mapped
candidate checks remain pending; this strengthens the source-language evidence
without claiming GPU execution acceptance.

### Directed GPU donor routing and candidate proposal draft

The frozen GPU pipeline was exercised with directed one-site/one-donor fixtures
and supported 128-name/128-constant capacities (80 source nodes, depth 7).
DP1D and DP2D each produced two valid non-statement children through device
splice/copyback and native lowering. The DC fixture fell back twice; its reason
remains pending. These directed fixtures establish reachable device behavior,
not natural sampling frequencies.

All three source witnesses map to current grammar members, with physical node
counts 28/20/17 and reconstructed source counts 13/15/12, all source depth 4.
Thus representation/membership already supports these nested programs. A separate
GPU proposal profile draft copies the structured mixture into ordinary canonical
Int/Float/String donor entries; the CPU profile stays unchanged. At one directed
non-statement Int site, seeds 0–31 yield zero structured donors under the original
profile and four under the draft. This is a functional routing check, not a
statistical distribution or universal closure proof.

The draft has not replaced the measured manifest. Exact profile/source/log hashes,
regeneration script, frozen GPU build and candidate probe commands are in
`representative-snoopy-01/donor-routing-audit/audit.json`. DC fallback, mode-specific
manifest integration, full-population correspondence and final timing remain
pending. Historical profile timings must not be presented as timings of this draft.

### Directed DC fallback: frozen name-remapping race

An independently linked copyback diagnostic shows that the original DC device
output is marked valid, but binder validation fails and native verification reports
`$.nodes[6]: index requires (Seq, Int)`. Distinct bound-variable references all
carry name index 0, while reconstructed DC metadata preserves distinct binder
indices 0–5. The frozen `remap_name_id` scans and updates shared name storage and
its count without synchronization, from parallel donor-copy threads.

A controlled diagnostic changes only the variation launch from 128 threads to
one, keeping the same frozen kernel source and fixtures. All three forms then
accept both children, including DC (originally 0/2). This supports the shared
name-remapping race as the directed failure mechanism. Its frequency across
historical populations is not measured. The actual frozen baseline libraries and
executable remain unchanged; no serial-diagnostic timings enter performance
comparison. Do not make the current correct mapping reproduce corrupted binder
indices or claim identical trajectories to this racy historical path.

Diagnostic source, compile/link commands, logs and hashes are recorded in
`representative-snoopy-01/donor-routing-audit/audit.json`. Mode-specific proposal
profile integration and complete mapping validation remain outstanding.

### Mode-specific proposal manifest integration (2026-09-24)

The runner now accepts hashed `after.grammar_by_mode` overrides with a required
default grammar. The auditor uses the same command construction and validates all
artifact hashes. Overrides participate in both frozen workload identity and the
cross-scope logical workload check. Targeted tests cover CPU/GPU command routing,
unknown/empty/malformed overrides, changed files and inconsistent scope profiles.

Artifact `representative-snoopy-01/workloads-mode-profiles-draft.json` applies the
previously diagnosed GPU proposal profile to canonical p64/p1024 GPU reproduction
modes, preserving the default CPU proposal profile and original manifests. Schema
and artifact validation pass; full-population search correspondence and performance
acceptance for these profiles remain pending. The other selected profiles have not
been converted by this step.

A fresh single sequential diagnostic per old/new CPU/GPU configuration on snoopy
is recorded in `speedup-check-20260924-011416-assessment.json` under the same
artifact directory. Generation times: old CPU 3068.850823 ms, old GPU 72.891385 ms,
new CPU 7732.877132 ms, new GPU 1154.025829 ms; speedups 42.1016945x and 6.7007834x,
Q=0.1591571. These runs retain the original common proposal profile and are not
acceptance evidence for the draft mode-specific profiles. They corroborate the
remaining material reproduction regression, rather than resolving it.

### Complete canonical population comparison for GPU proposal profile

`donor-routing-audit/population-profile-assessment.json` records an independently
linked probe against the current candidate libraries. Original candidate and draft
GPU proposal profiles both admit all 64 and 1024 canonical members. Respectively,
all 1,648 and 23,924 candidate sites retain their spans, binder mappings, replacement
limits and compatibility contracts (with the grammar identity intentionally
substituted). Whole-program source resource projections are equal; maximum source
nodes are 80 and initial depth is 8, retaining the previously documented initial
population exception. This comparison does not establish arbitrary offspring or
proposal-distribution closure and does not replace legacy-to-candidate evidence.

One corrected-profile p1024 GPU diagnostic gives generation 1221.169173 ms,
evaluation 79.493940 ms, reproduction 1136.446110 ms, preparation 641.019424 ms and
decode 253.736671 ms. See `gpu-profile-timing-assessment.json` for the command and
binary/profile hashes. Compared with the immediately preceding original-profile
1154.025829 ms observation, the profile change does not resolve the regression.
No additional CPU timing was needed because its profile and executable are
unchanged. This is one diagnostic observation, not the representative gate.

### Donor-pool worker context reuse experiment (reverted)

Reused one context per worker across strided pool seeds instead of allocating one
context per seed. Payload transactions remained per seed and committed in original
order; admission caches remained per seed. Four targeted tests passed (derivation
resources, payload staging, compiled GPU backend and evolution CPU/GPU parity).
The corrected GPU proposal profile p1024 diagnostic changed generation time from
1221.169173 to 1208.816054 ms, but preparation remained 641.019424 versus
639.355620 ms. Fitness and reproduction counters were unchanged. A roughly 1%
single-run difference does not establish a useful optimization; the experiment was
reverted rather than adding another retained change or repeating measurements.
Source and command/binary hashes are preserved in `pool-context-experiment.cpp`
and `pool-context-assessment.json` under the representative artifact root.

### Matcher-local region-plan encoding cache experiment (reverted)

Memoized successful validated region-plan encodings by immutable plan address for
one Matcher lifetime, retaining the same comparison bytes and validation on first
use. Four affected tests passed (membership, derivation resources, compiled GPU
backend, evolution CPU/GPU parity). Corrected-profile p1024 generation was
1223.547407 ms versus 1221.169173 ms before; preprocessing 642.617984 ms and decode
253.628088 ms showed no improvement. Fitness and reproduction counters matched.
Reverted with fresh source timestamps and rebuilt the benchmark. Raw evidence is
`plan-encoding-assessment.json` and `plan-encoding-experiment.cpp` under the
representative artifact root. Together with the worker-context experiment, this
rules out retaining either local cache adjustment on the available evidence;
further work must address whole-program admission work rather than repeating
these micro-optimizations.

### Budget lower-bound rejection coverage probe

An independently linked membership diagnostic retained all canonical matching,
resource reconstruction and lowering while evaluating minimum authored charges
by native kind and exact fuel profile. On the corrected-profile canonical p1024
GPU workload it observed 5,479 budgeted admissions, including 3,871 resource
rejections. The proposed lower-bound filter identified only 11 rejections (0.284%),
with zero observed false rejections. This is empirical coverage, not a general
soundness proof; implicit-envelope accounting would still need certification.
The filter was not added to production because it cannot remove a material share
of the repeated full-child analyses. Evidence, instrumented source, build and
command are in `budget-lower-bound-probe/` under the representative artifact root.
Production sources/binaries were unchanged by this diagnostic. Any next resource
shortcut needs more context than native kind/fuel alone; repeated timing of this
broad lower bound is not justified.

### Parent/donor resource-transplant agreement diagnostic

An independently linked donor probe compares the existing parent witness's
replacement allowance with the generated donor's projected resources, while still
performing every normal splice and canonical child admission. On the corrected
canonical p1024 GPU profile it observed 5,479 unique admissions: 1,608 accepted and
3,871 rejected. Transplanted resource predictions matched all 5,479 decisions;
there were no false acceptances, false rejections or membership exceptions in this
sample. Cache-hit retries are excluded. Raw source, command/build recipes and
hashes are in `transplant-budget-probe/assessment.json` under the artifact root.

This establishes useful coverage compared with the broad lower-bound probe, not
a proof that grammar choices remain invariant for arbitrary splices. Production
behavior is unchanged. A usable fast path must certify that earlier alternatives
cannot change the interpretation of untouched nodes and that donor charges agree
with the destination nonterminal; ambiguous cases must retain canonical matching.
No package-name checks, empirical allowlists or weakened budget checks are justified
by this observation.

### Certified donor budget rejection integration

The production donor path now rejects over-budget proposals before child splicing
when the destination analysis already carries a context-independent projected
allowance. It uses the certified destination site rather than caller-supplied
metadata; passing proposals still undergo full membership/execution admission.
Uncertified grammars retain the existing reconstruction path.

A new oracle test covers 32 seeds and up to four retries per seed, comparing
complete child reconstruction with both individual generation and parallel pool
results, including rejected proposals, retry exhaustion and forged caller
allowances. `gagp_test_derivation_resources`, `gagp_test_compiled_backend_gpu` and
`gagp_test_evolution_cpu_gpu_parity` all passed on snoopy. This integrates the
already-proved local certificate; it does not certify the canonical profile's
context-dependent charges or claim a canonical performance improvement. No
additional canonical timing was warranted for that unchanged admission path.

### Resource ambiguity product-grammar diagnostic

The compiled corrected canonical profile was exported into a relaxed tree grammar:
references/template wrappers become alternatives; materialized nodes retain native
kind, expression type, names where checked, fuel profiles and bounded-region plans.
Constant-domain restrictions, lexical mappings and repeated-hole equality are
ignored, enlarging the candidate language. Product states compare two derivations
of the same relaxed tree; finite productivity is computed as a least fixed point,
and differing charges propagate from productive children to their roots.

The diagnostic contains 11,702 compiled expressions and 51,158 product states,
17,454 productive. No productive differing-charge pair was found from any of the
eight canonical Program roots. Five synthetic solver tests cover actual charge
conflicts, disjoint roots, recursion/productivity and conflict propagation. The
initial exporter encountered unused, unexpanded template-hole definitions; these
are now explicitly marked unsupported and rejected if reachable, rather than
silently assumed valid. Evidence and regeneration sources are in
`resource-ambiguity-probe/` under the representative artifact root.

This is a promising static analysis result, not an enabled optimization. A final
certificate must be bounded and reviewed against Matcher semantics. Even a
resource-unambiguous root does not prove an arbitrary mutation-entry donor belongs
to the destination nonterminal. That membership/frame obligation must be checked
before transferring the parent witness; unsupported/uncertified cases must retain
full reconstruction. No new performance acceptance is claimed.

### Bounded C++ resource-invariance checker

`resource_invariance.cpp` implements the relaxed product analysis with explicit
work, product-state, edge, child-group and alias-closure storage limits. Incomplete
analysis certifies no roots; invalid root IDs are rejected. Productive recursion
uses a worklist least fixed point, followed by propagation of resource conflicts.
The native derivation-resource test now checks stronger-than-local certification,
equal-cost ambiguity, genuine overlapping charge conflicts, recursion, invalid
roots and conservative work-limit exhaustion; it passes on snoopy.

The C++ canonical corrected-profile probe matches the Python diagnostic exactly:
51,158 product states and all eight Program roots certified. Final probe time is
654.496 ms (single construction observation, not a performance gate). It remains
outside admission pending shared grammar-owned storage and destination donor
membership checks. Production reproduction timing is therefore not claimed to
improve yet. Evidence: `resource-ambiguity-probe/cpp-certificate-evidence.json`.

### Shared invariant-root donor rejection integration (performance unresolved)

Grammar copies now share per-root invariance certificates. Donor rejection uses
these only after reconstructing the donor's resources in the destination frame;
a mutation-entry witness is not trusted for that interpretation. Failure to match
the destination falls back to full child analysis. Passing proposals still receive
full membership and execution admission. A new regression uses a mutation-entry
charge of 20 and destination charge of 8 under a total budget of 12, proving the
valid donor is not rejected using the wrong resource interpretation. Shared-copy,
duplicate-root, framed-resource and retry-oracle tests pass, as do compiled GPU
backend and evolution CPU/GPU parity tests on snoopy.

Fresh single-process diagnostics: CPU generation 8122.667984 ms (reproduction
5271.827640 ms), corrected-profile GPU generation 1707.641235 ms (reproduction
1623.232420 ms, preprocessing 1143.948199 ms). Fitness and reproduction counters
are unchanged. The net result regresses because first certificate construction
costs about 0.65 s; this is not a retained-performance success or acceptance.
Integration is work in progress. Next work must reduce equivalent product-grammar
states before deciding whether to retain the admission optimization. Commands,
binary hashes and raw timings: `invariant-donor-assessment.json` and
`canonical1024-invariant-donor-{cpu,gpu}.json` in the representative artifact root.

### Weighted-grammar quotient removes certificate startup regression

The checker now partitions reachable materialized expressions by native label and
resource charge, then refines by ordered child-language class sets until stable.
Only weighted bisimilar states merge; unlike unweighted merging, this cannot hide
charge conflicts. A nested-definition regression confirms equivalent branches
certify while a differing descendant charge remains rejected. Existing recursion,
ambiguity, work-limit and destination-cost tests also pass.

Canonical product states decrease from 51,158 to 1,129; a single construction
observation decreases from 654.496 ms to 24.2503 ms, with all eight root results
unchanged. Three affected native/GPU tests passed on snoopy. Fresh single paired
candidate diagnostics give CPU 7402.221094 ms and corrected-profile GPU
1124.472398 ms (6.582839x). GPU reproduction is 1041.638898 ms, preprocessing
543.854058 ms, decode 253.726209 ms. Compared with the pre-integration corrected
profile's 1221.169173 ms, generation improves about 7.9%; this also removes the
intermediate 1707.641235 ms startup regression. Per-backend reproduction counters
and reported best/mean fitness summaries remain identical.

The quotient and certified donor rejection are retained. These are single-run
diagnostics, not the final representative gate, and the speedup remains far below
the old roughly 42x result. Evidence and exact source/binary hashes are in
`invariance-quotient-assessment.json`; construction evidence is in
`resource-ambiguity-probe/certificate-quotient-result.json`.

### Deferred parallel child analysis during GPU decode

Changed child ASTs are prepared in bounded batches and analyzed concurrently before
ordered acceptance. The retained variant defers compatibility-ID registration and
cache accounting until the original `analyze` call consumes a prepared result.
This preserves registry ordering even when preparation and consumption orders differ;
malformed proposals still receive normal ordered validation or fallback. Pending
storage is capped at min(cache capacity, 128). Small batches retain sequential
analysis. A new 160-member mixed-root test covers reverse consumption, malformed
input, small-cache eviction and batch boundaries, checking registry keys and cache
counters against sequential analysis after every operation.

Four tests pass on snoopy: grammar variation cache, compiled GPU transport,
compiled GPU backend and evolution CPU/GPU parity. The final p1024 corrected-profile
GPU diagnostic gives generation 1027.563915 ms, reproduction 947.163896 ms and
decode 179.012411 ms, versus 1124.472398/1041.638898/253.726209 ms before. This is
about 8.6% generation and 29.4% decode improvement. Reported best/mean fitness and
reproduction counters are identical. CPU timing was not repeated for this GPU
copyback change; no new paired speedup is claimed. The earlier eager-registration
prototype's 1009.723926 ms is historical, not the retained implementation.

Evidence: `decode-deferred-assessment.json`, including command, binary/source
hashes and raw timing. The improvement is retained, but this single diagnostic is
not the final representative gate and Goal 11 remains incomplete.

### Independent donor-pool batching API (integration pending)

Added `try_generate_donor_pools` as a bounded speculative interface. Destination
certificates are prepared by the owning context; workers reuse private contexts
and keep independent per-seed admission caches. Payload writes are staged per
pool and committed atomically in job order. Any payload conflict declines the
batch without changing registry contents, requiring the caller to replay its
original interleaved preparation. The single-seed path remains unchanged.

The derivation-resource test passes on snoopy. New cases compare nine pools with
individual seeded generation, cover exhaustion/order, reject invalid attempt
limits, retain single-seed fallback, prove colliding payload batches leave the
registry unchanged, and verify newly created string payloads commit successfully
with the expected proposal contents. The API is not yet wired into GPU
preprocessing, so no generation improvement is claimed or benchmark repeated.

### 2026-09-24 paired speedup diagnostic and prefetch rejection

One sequential run per version/backend on snoopy RTX 3090, population 1024, one generation, seed 42, the same frozen population/cases and equivalent resource budgets. Candidate GPU uses the corrected GPU proposal profile. Evidence: `speedup-measured-20260924-021653-assessment.json` in the representative-snoopy-01 artifact directory.

Old CPU/GPU: 3107.745850 / 76.045236 ms (40.8671x). Current experimental candidate CPU/GPU: 7305.503266 / 1054.047440 ms (6.9309x), retaining 16.96% of baseline speedup. This is diagnostic evidence, not a formal gate pass. Candidate GPU reproduction accounts for 972.423357 ms.

The donor-pool window-prefetch integration did not improve on the retained deferred-decode diagnostic (1027.563915 ms). Revert that integration; preserve the experiment source in `donor-prefetch-experiment.cpp`. The standalone batch API remains available but is not used by production preprocessing. Focused grammar reproduction preparation, compiled GPU backend, and evolution parity tests passed before measurement.

### Integrated correctness and checked preset repair

After reverting donor prefetch, the snoopy Release build completed all targets.
The complete CTest run executed 118 tests: 117 passed and the migration CLI
comparison failed because the six checked v2 presets predated the converter's
source resource charges and disabled Program variation. Regenerated `all`,
`num_list`, `scalar`, `sequence`, `string`, and `string_list` using the existing
constrained-intent conversion. A structural diff verified that the only changes
are control resource charges (one source node, zero expression depth, depth reset)
and Program `variation: false`, including embedded source content. The unchanged
byte-for-byte migration/determinism test passed on targeted rerun.

All 118 tests thus have passing outcomes across the integrated run and targeted
repair check; the 19 GPU-labelled tests executed without skips. The 23 repository
checks also passed. This is a dirty-worktree integration check, not Goal 12's final
clean-checkout proof. No final performance pass is claimed. Existing frozen
benchmark grammars were not regenerated by this preset repair. Evidence and
checksums: `integrated-20260924-assessment.json`, the build/test/recheck logs, and
`preset-refresh-review.json` under the representative artifact directory.

### DP2D coordinate donor routing and import closure repair

The selected DP2D profile also restricted coordinate donors to literals, although
frozen GPU preparation enables structured donors at these sites. Draft CPU/GPU
profiles now admit the supported structured expression alternatives recursively
at the three coordinate nonterminals. These are membership-only alternatives;
CPU donor entries stay unchanged. GPU coordinate entries use the existing
0.82 literal / 0.06 each DC, DP1D, DP2D proposal rule.

All 64 frozen members retain their 192 candidate sites, binder maps, compatibility
keys modulo grammar identity, replacement limits and resource projections (maximum
source nodes 9, depth 2). At one coordinate site, seeds 0–31 produce zero structured
donors in the original/CPU profiles and four in the GPU draft. All four spliced
children pass membership, lowering, and the source 80-node/depth-7 budget. A CPU
and GPU one-generation population-64 smoke run each succeeds without acceptance
rejections or fallback children. This is directed evidence, not full search-space
closure or performance acceptance; formal manifests still retain their earlier
profile paths pending closure review.

The representative artifact copy was missing `inputs/source_literals.json`, used
by three noncanonical profiles. Restored the exact shared historical dependency
(SHA-256 `4b4d46b60b75391370d7e09c77a2819fab7411adb85940fffbeee596bc0ac47e`),
not the different current repository package. Simply removing input `x` from the
canonical grammar fails membership for these noncanonical populations and is not
a valid profile replacement. Regeneration scripts, probe source/build commands,
raw results and checksums are in `dp2d-routing-audit/assessment.json`.

### Nested and metadata Int donor routing

Extended the directed profile repair to 10 Int nonterminals in nested_binders and
14 in metadata_stress. New structured alternatives are membership-only; original
CPU generation alternatives are retained in dedicated donor entries where needed.
GPU donor entries preserve each site's lexical scope and use the existing fresh
Int literal/DC/DP1D/DP2D proposal weights. A regeneration script is retained in
`nested-metadata-routing-audit/generate_profiles.py`.

Complete original-population comparisons pass for nested_binders (64 members,
320 sites, maximum source nodes/depth 13/5) and metadata_stress (64 members,
215 sites, maximum 18/5). The comparison checks resource projections, candidate
spans, binder maps, replacement limits and compatibility keys modulo grammar
identity. A nested lexical site generated four structured proposals in 32 fixed
GPU seeds; all four spliced children pass membership/lowering and the source
budget. The CPU route generated zero structured proposals as required.

The metadata comparison initially failed on original member 2 because the probe
used the wrong DC frame minimum. The profile's original scope record specifies
4; both 0 and 8 are incompatible with that initial derivation. Restoring 4 passes
all members. Added the omitted `--minimum-dc-frames 4` to the metadata rows in the
current draft manifest; historical manifests are unchanged. Draft schema/artifact
validation passes. This does not authorize changing the frozen execution limits.

One CPU and one GPU generation for each repaired profile succeeds with zero
acceptance rejections; fallback counts remain explicitly recorded (nested CPU/GPU
0/7, metadata CPU/GPU 28/45). These are functional smoke runs, not speedup gates.
Full search closure and the mixed payload routing repair remain pending. Evidence
and hashes: `nested-metadata-routing-audit/assessment.json`.

### Mixed payload scalar routing and six-workload draft integration

Added a reproducible mixed-profile generator that copies fresh Int/Float/String
structured dependencies under separate type namespaces. It expands scalar
membership at 16 reachable variation nonterminals, preserves CPU donor entries
and scopes, and gives GPU scalar entries the corresponding fresh donor proposal
distribution. Dependencies are embedded; no external import is needed. Regeneration
was checked byte-for-byte.

All 64 initial members retain 208 candidate sites, binder maps, compatibility keys
modulo grammar identity, replacement limits and resource projections (maximum
source nodes/depth 12/4). At Mixed.Factor.Int, Mixed.Factor.Float and Mixed.Empty,
32 seeds per mode/type produce zero structured CPU donors and four structured GPU
donors each. All 12 GPU children pass full membership/lowering and the 80-node /
depth-7 source budget. CPU and GPU one-generation smoke runs pass with zero
acceptance rejections; fallback counts are 40 and 36, recorded rather than hidden.
Evidence: `mixed-routing-audit/assessment.json`.

`workloads-all-mode-profiles-draft.json` now connects the repaired profiles to all
six selected logical workloads and twelve rows. CPU/gpu_eval use the CPU profile;
gpu_repro/gpu_repro_overlap use the GPU override. Noncanonical profiles use their
DC frame minimum of 4; the canonical profile retains 8. Schema/artifact validation
passes. This remains a draft: directed correctness checks are not proof of full
proposal/crossover closure, and no representative performance pass is claimed.

### Benchmark grammar dependency closure validation

The runner and trial auditor now validate the complete transitive import closure
for default and mode-specific grammar artifacts. Each imported file must have a
`dependencies` path/SHA-256 record. Missing, changed, duplicate, unused and cyclic
imports fail preflight. Dependency identities also participate in cross-scope
checks, preventing identical root files with different imported definitions from
being pooled as one logical workload. Historical manifests are not rewritten.

The current twelve-row all-mode draft includes dependency records for its 18
importing grammar entries and passes validation. All 84 tool tests passed; the
26 migration tests passed again after adding the same-root/different-import
cross-scope regression case. This changes evidence/preflight handling only, so no
C++ performance run was repeated. Evidence and source hashes are recorded in
`grammar-dependency-validation-assessment.json`.

### Local resource-certificate memoization experiment (reverted)

Tested sharing the native-kind/fuel resource-locality decision in the immutable
compiled grammar, avoiding repeated scans when donor worker contexts are created.
The resource, variation-cache and evolution CPU/GPU parity tests passed, including
a negative certificate copied across grammar instances. One canonical p1024 GPU
diagnostic measured 1041.349604 ms total, 532.533585 ms preprocessing and
185.221196 ms decode. Counters and reported fitness summaries matched. Compared
with retained preprocessing 532.410728 ms, no useful improvement was observed.

Reverted the experiment and rebuilt with fresh source timestamps. This rules out
that repeated certificate scan as a useful optimization target on this workload;
it does not establish statistical timing equivalence. Exact command, experimental
binary hash and measurements: `local-resource-cache-assessment.json`; source
backups and experimental variants are under `local-resource-cache/`. No new paired
CPU/GPU speedup or performance acceptance is claimed.

### Native verification structure cache experiment (reverted)

Tested a private thread-local cache of successful native verification results,
keyed by exact AST structure/metadata, ordered constant tags, inputs and all verify
options. The experiment kept payload decoding, grammar membership and lowering
uncached. Storage was bounded to 128 entries and an 8 MiB accounting budget per
thread, with oversized inputs bypassing the cache. Native-vs-cached diagnostic,
constant-type, scope, input, limit and invalid-structure checks passed, along with
resource and evolution CPU/GPU parity tests.

A single fresh CPU/GPU p1024 pair measured 7575.660861 / 1075.164107 ms (7.046051x).
GPU preprocessing was 554.143595 ms; no overall benefit was observed relative to
the retained diagnostics. The experiment was reverted, including its private API
and cache-specific test additions, and the production benchmark was rebuilt.
Do not interpret its ratio as a retained speedup improvement. Evidence and the
experimental source snapshot: `native-verification-cache-assessment.json` and
`native-verification-cache-experiment/`. Avoid repeating this approach without
new evidence that key/lookup costs or cache lifetime have materially changed.

### Five-generation canonical anchor across all four modes

One five-generation run per version/mode on snoopy RTX 3090, population 1024,
seed 42, complete frozen population/cases and existing equivalent budgets.
All eight runs exit successfully. Candidate binary SHA-256: `cb33f12eb80e709b6b1838d6b2e1f6cd814da31047780ed9a6c7aeacf0ad06cf`.
The candidate CPU/gpu_eval uses the CPU proposal grammar; GPU reproduction modes
use the corrected GPU proposal grammar. This is the short evolving anchor, not
the formal one-warmup/three-pair representative gate.

Times below sum five generation totals (evaluation plus reproduction), excluding
input loading and population initialization. Speedup uses the corresponding CPU sum.

| Mode | Old ms | New ms | Old speedup | New speedup | Q |
| --- | ---: | ---: | ---: | ---: | ---: |
| cpu | 11895.895 | 29599.531 | — | — | — |
| gpu_eval | 250.386 | 19558.498 | 47.510 | 1.513 | 0.0319 |
| gpu_repro | 337.060 | 4247.884 | 35.293 | 6.968 | 0.1974 |
| gpu_repro_overlap | 270.905 | 4266.628 | 43.912 | 6.937 | 0.1580 |

Candidate non-overlap GPU reproduction completes all five generations with zero
acceptance rejections; best fitness changes from -502.8972937632352 to
-488.6574919009581. Trajectory identity is not required or claimed. Both GPU
reproduction modes remain materially slower than the baseline, and GPU evaluation
with CPU reproduction has the largest relative-speedup loss. This points to CPU
crossover trial-child analysis as another mandatory repair target. Exact commands,
per-generation timings/counters, fitness summaries and binary hashes are retained
in `five-generation-anchor-summary.json`. No performance acceptance is claimed.

### CPU crossover trial admission without future-site analysis (retained)

CPU crossover now reconstructs each trial child's canonical derivation, native
verification, lowering and projected resources without enumerating future variation
sites or interning their compatibility keys. Full analysis remains at selected-child
acceptance. The root request comes from the certified destination parent; reciprocal
budget checks, short-circuit order and reservoir draws are unchanged. This also
avoids filling the compatibility registry with unselected hypothetical descendants.

A 32-seed recursive resource-tradeoff oracle retains the previous full-analysis
trial path and compares exact selected coordinates, resulting ASTs and rejection
classifications. Six distinct affected tests pass, including reproduction, bounded
variation, compiled GPU acceptance and evolution CPU/GPU parity.

Single generation-0 diagnostics on the complete p1024 workload give CPU
6340.786919 ms (crossover 3054.978351 ms) and GPU evaluation with CPU reproduction
3492.383424 ms (crossover 3004.114109 ms). The preceding five-generation anchor's
generation 0 measured 7678.259088 / 4661.470997 ms, respectively: about 17.4% and
25.1% lower total time. Reported fitness summaries and all reproduction counters
match per mode. This is retained as a material optimization, not formal performance
acceptance. GPU reproduction was not remeasured for this CPU-crossover-only change;
do not combine its historical timing with the new CPU time as a fresh paired ratio.

Exact commands, binary/source hashes and comparisons: `crossover-admission-assessment.json`.
The earlier five-generation results identify the pre-optimization binary and remain
historical diagnostics; the final candidate still needs its required anchor/gate.


### Current retained candidate: complete-generation speedup recheck (2026-09-24)

User requested a direct check of approximately preserved CPU/GPU acceleration.
On Snoopy RTX 3090 (UUID GPU-9c69fabe-a876-c803-9278-b54b8ec52daf), the GPU
reported 0% utilization before this sequential run. Reused the frozen canonical
1024-member population, cases, seed 42 and equivalent source 80-node/depth-7
budgets. One warm-up and three measured runs per version/backend, overlap off;
no concurrent build or tests. Generation timing includes evaluation and
reproduction, excluding load and initialization. These are diagnostic anchor
results, not the complete representative all-mode gate.

| Median milliseconds | Frozen baseline | Current candidate |
| --- | ---: | ---: |
| CPU generation | 3092.718006 | 6230.585179 |
| GPU generation (GPU eval + GPU reproduction) | 75.380306 | 1058.388184 |
| CPU/GPU speedup | 41.028196x | 5.886862x |

Relative speedup retention Q = 0.143483324: FAIL against 0.95. Candidate binary
SHA-256: `2befdb99393a8db46b34aadbdbe53c8753770c6c610a34492f425bdee08603d8`.
Baseline binary SHA-256:
`a10d20cfd2aafeac6e10994d34c4d70b3c5f6a467a8944483f1d82c998c1555e`.
Exact commands and all 16 raw results are embedded in
`representative-snoopy-01/speedup-current-20260924-031117-assessment.json`
under the documented artifact root.

The candidate GPU reproduction median is 974.426960 ms, versus 35.729810 ms
before; evaluation is 78.535525 versus 38.898348 ms. Candidate reproduction
phase medians include preparation 542.278096 ms, input preparation 92.154485 ms,
packing 55.449266 ms, decode 183.725430 ms, and kernels 1.701517 ms. Phase
medians are separate observations and must not be added as a generation total.
This supports targeting host-side admission/preparation work rather than GPU
kernel micro-optimization.

A bounded, per-matcher negative-membership cache was then tried, with active
alias-cycle detection to avoid reusing ancestry-dependent failures. Four focused
tests passed (membership, derivation resources, contextual donor, CPU/GPU
evolution parity). One diagnostic candidate run measured CPU 6054.660033 ms and
GPU 1033.713408 ms. The GPU result overlaps the pre-experiment three-run range
1034.444860–1074.646483 ms; the optimization was reverted rather than spending
more repetitions on a small uncertain gain.

Retained a semantic regression fixture: an alias fails while its caller is active,
then must succeed at the same AST coordinate after another root alternative is
tried. A deliberately unsafe cache that ignores alias ancestry fails this test;
the original matcher passes it. Experiment source, unsafe mutant, runner and
assessment are archived in `representative-snoopy-01/membership-negative-cache/`
and `membership-negative-cache-assessment.json`. No acceptance threshold or
search distribution changed. Goal 11 remains incomplete.


### Donor admission audit and rejected micro-optimizations (2026-09-24)

A single instrumented canonical 1024 GPU-reproduction run counted donor
admission paths (instrumented timings are not performance evidence):

| Path | Count |
| --- | ---: |
| Generated attempts | 9155 |
| Repeated donor rejected from the existing admission cache | 3676 |
| Destination-frame invariant budget rejection | 3871 |
| Complete contextual child accepted | 1608 |

These partition all attempts in this fixture. No complete-child membership
failure or complete-child budget rejection was observed. Reported reproduction
counters match the retained baseline exactly. Raw counts, source before/after
instrumentation, exact command and output are in
`representative-snoopy-01/donor-admission-audit/`.

Two bounded experiments were tested and **reverted**:

- Framed witness reuse before the existing admission cache: retained seed
  construction/payload writes and keyed witnesses by exact decoded AST,
  requested nonterminal/type/budgets/stage, locals and binder IDs. Four focused
  tests passed, including frame/payload invalidation and cached versus uncached
  shared-hole generation. Diagnostic CPU/GPU generation times were
  6077.954025/1037.151581 ms; reproduction counters were unchanged.
- Compiled conservative FIRST sets of physical root kinds: zero-node aliases,
  template forwarding and membership-only productions participate in a worklist
  closure; impossible alternatives are skipped without reordering survivors.
  Declaration-only uninstantiated holes initially caused a bounds error and
  were fixed with a conservative unknown head set. Five focused tests then
  passed, including an exhaustive-matcher differential oracle for canonical
  production/node provenance, cyclic aliases, fuel profiles and typed values.
  Diagnostic CPU/GPU times were 6011.604064/1035.397891 ms.

Both GPU times overlap the pre-experiment 1034.444860–1074.646483 ms range.
No additional repetitions were used to chase small uncertain gains. Source,
runner and raw results are archived under `framed-witness-cache/` and
`membership-head-dispatch/`, with their sibling `*-assessment.json` records.
Neither experimental API nor test-only oracle remains in production.

Environment audit found 20 allowed logical CPUs on an i9-10900K, load averages
1.37/1.32/1.18, and no cgroup CPU quota or throttling at the controlling user
slice (`cpu.max = max 100000`, `nr_throttled = 0`). Descendant slices do not have
the CPU controller enabled. See `donor-admission-audit/cpu-controls.json`.
There is no evidence here that CPU quota contention explains the regression.

Seven affected production sources were restored byte-for-byte to their
pre-experiment contents and checked equal on local and remote hosts; the
restored membership test passed. The rebuilt benchmark hash is
`f045f520c871b85d59b80c1482e1b3dbabbcd8d0ac66c4069ac4a40dfa0b287e`, which is
not byte-identical to the earlier benchmark after shared-header/CUDA-consumer
rebuilds. `donor-admission-audit/restoration.json` records source hashes. Earlier
timings remain attached to their original binaries; this rebuilt binary has no
new timing claim.

At the last measured CPU median, retaining 95% of the old 41.028196x speedup
would require a GPU generation at roughly 160 ms, versus the measured 1058 ms.
Preparation alone (542 ms) and decode alone (184 ms) already exceed that entire
target. Further repair must reduce whole admission/preparation work and preserve
its validation contract, rather than relying on these small cache changes.
Goal 11 and the prerequisite-dependent final report remain incomplete.


### Correctness repair: bounded captures in isolated donor frames (2026-09-24)

`project_frame` previously rewrote external `REGION_VAR` nodes into temporary
inputs but left lexical captures in `BoundedRegionSpec::parameters` unchanged.
A legal scoped recurrence donor therefore failed isolated native verification
with `bounded region captures an invisible lexical binder`. Existing variation
smokes could pass by varying only a recurrence's scalar phase holes.

The projection now maps those external metadata captures to the same synthetic
input names. Introduced lexical-region and bounded-phase bindings must remain
disjoint from frame IDs; internal captures remain lexical. The original donor
AST is not modified. Updated the contextual-frame specification and its current
spec hash manifest; the immutable release-1 reference artifacts are unchanged.

A new directed generation test failed before the repair and passes afterward.
It covers direct and permitted nested initial-operand recurrences, synthetic
name collision avoidance, original AST preservation, internal capture retention,
phase-binder collision rejection, CPU execution and lowered-instruction counts.
A shared repeated-capture fixture forces GPU mutation at the contextual recurrence
rather than a simpler phase hole. Its 8-member run has 8 mutation attempts,
zero generation/acceptance rejections or fallback, positive GPU kernel timing,
CPU outputs 9/29 and matching GPU fitness. Two-generation direct/overlap histories
also match with no execution-error penalty. Fitness comparisons use penalty 1000,
so the numeric error 20 is below the normative clamp and distinct from an error.

Five focused native tests passed: bounded generation, contextual donor, bounded
variation, compiled GPU backend, and CPU/GPU evolution parity. The compiled GPU
backend test was rerun after adding directed overlap coverage and passed.
Repository checks have 23 passing outcomes across the suite and the targeted
spec-hash recheck. Artifact:
`representative-snoopy-01/bounded-frame-capture/assessment.json`, including source
and binary hashes, the observed pre-fix failure, build and GPU test evidence.

Current benchmark binary SHA-256 is
`ac1b51e4a5c52888273358e17d9e85feeb4f15f6503e8223e00b6f47789e4cea`.
This is a correctness repair, not a measured performance improvement. Earlier
measurements retain their original binary identities. Goal 11 still requires
performance repair and the final representative gate.

### Payload snapshots during parallel analysis (2026-09-24)

The current `nsys` CUDA/OSRT trace (`current-host-trace.nsys-rep`) attributes repeated mutex waits to payload lookups under parallel variation analysis. Aggregate thread wait time is not wall-clock savings. Analysis workers now compute both identity and membership under one staged payload read view, validate dependencies before cache commits, and fall back to serial warming on conflict. Enclosing staged views use serial warming (optional deferred warming is skipped).

Five targeted tests passed: variation cache, payload staging, compiled GPU backend, fitness parity, and evolution parity. The cache test covers uncommitted payload visibility and invalidation after abort. The bounded capture variation-contract test also passed, confirming metadata-only external captures are not lexically closed.

A single diagnostic CPU/GPU pair measured 6202.047784 / 994.263643 ms per complete generation at population 1024, giving 6.237830x. Reproduction counters and fitness match the prior same-backend diagnostic. This is not a repeated final-gate measurement; its ratio relative to the old three-run median speedup (41.028196x) is 0.152038, still far below acceptance. The earlier three-run candidate median remains 6230.585179 / 1058.388184 ms (5.886862x). Evidence: `analysis-payload-snapshot-assessment.json`, its archived LastTest.log, and `current-host-trace-command.json` in the representative artifact directory. Goal 11 remains incomplete.

### Retained final-population evidence

The diagnostic candidate benchmark now exports `final_population` in the existing `final-candidate-children-v2` AST format when `--retain-final-population on --skip-final-eval off` are both selected. It checks the complete population size and reconstructs native verification, grammar membership, lowering and the initial source budget before writing the artifact. These checks execute after the recorded evolution-call timer. The initial budget intentionally permits unchanged grandfathered parents; this check alone does not prove the offspring depth-7 constraint or complete search-space closure. Final evaluation remains separately reported. With final evaluation skipped, the evolution API does not retain a scored final population, so no final-population evidence is emitted. Ordinary timing runs retain their existing output and behavior.

The benchmark integration test covers both the omitted-evidence path and a retained two-member mixed-return population. The representative diagnostic runner is `retained-closure-runner.py` under the representative artifact root; it selects the six draft evolution workloads in CPU, GPU evaluation, GPU reproduction and GPU reproduction-overlap modes, with one run each. These are validation diagnostics, not new acceptance timings.

All 24 retained-population diagnostics passed, checking 5376 members across runs. All 12 GPU reproduction/overlap runs recorded nonzero reproduction kernel time. Raw output hashes were verified after copying artifacts locally. Measured diagnostic binary: `944a7aa000f539c7d0257d7cf7c0132eeca24a5c16a5170a47a7e176abaca8d1`. This evidence covers the materialized final populations of these runs, not universal offspring/search closure or relative-speedup acceptance.

### Unequal-fitness overlap replay repair

Comparing the retained final populations exposed direct/overlap trajectory differences in four of six representative workloads. Direct GPU reproduction consumed fitness-sorted parents, whereas overlap prepared original-order parents before fitness was available. A new 16-member, three-generation unequal-fitness regression failed before the fix (`overlap reordered unequal-fitness parents before tournament selection`). Both evolution-loop GPU reproduction paths now consume the original population order with aligned fitness; ranking remains separate for best-member reporting. CPU reproduction and evaluation order are unchanged. This intentionally changes direct GPU seeded trajectories, so earlier direct-mode timings are historical, not measurements of this repaired candidate.

The compiled GPU backend, evolution CPU/GPU parity, and candidate-benchmark integration tests all passed after the repair. The resource-locality diagnostic found 1726 immutable-property scans totaling 26.860 ms of aggregate thread time, insufficient to explain the main regression; its instrumentation was removed, with the pre-repair benchmark hash restored to `944a7aa000f539c7d0257d7cf7c0132eeca24a5c16a5170a47a7e176abaca8d1` before this repair.

After the ordering fix, all six representative direct/overlap pairs match exactly in final AST populations, fitness histories and non-timing operator counters. Canonical p64 covers five generations; the other five workloads cover one generation each. All 12 runs pass retained-population validation. Evidence: `overlap-order-fixed-assessment.json` and `overlap-order-fixed-equivalence.json`; candidate SHA-256 `3e1f87295f838efea49a50afa6640e0a80bc8de45061502cd001dd76642c7066`. This is correctness evidence, not the final paired performance gate.

One post-repair CPU/GPU diagnostic pair (p1024, complete generation, final evaluation skipped) measured 6109.872276 / 1025.485679 ms, speedup 5.958028x and relative speedup 0.145218 against the historical old three-run median. This remains a material regression, not acceptance. Evidence: `overlap-order-speedup-assessment.json`. No repeated final gate was run.

### Prewarming compacted parent analyses

A diagnostic wall-time probe found 891 parent-analysis cache misses out of 1024 in crossover preprocessing (135.616 ms in analysis): the parallel warm-up had validated original tables, but preprocessing consumed compacted tables with different exact identities. The mutation pass had zero misses and took 20.069 ms in analysis; donor pools accounted for 313.921 ms there. These are instrumented diagnostic phases, not final acceptance times.

Preparation now retains original-table validation and additionally warms the compacted population in parallel when table counts change. This preserves validation of unused opaque payloads rather than silently dropping them before checking. The 64-parent regression compares compacted/uncompacted reproduction populations and counters and checks rejection of an unused opaque payload. Compiled GPU backend and evolution CPU/GPU parity tests passed.

One uninstrumented p1024 GPU-generation diagnostic decreased from 1025.485679 to 931.995020 ms (9.1%). Prepare-input time rose from 58.980992 to 85.307107 ms, while preprocessing fell from 564.758314 to 454.847721 ms. Fitness and all non-timing operator counters match. Candidate SHA-256 `5ce4aaed136bb5f4f067d560593553926bc26d6e818c9b2e9d4ef51f779e0d36`; evidence `compact-analysis-assessment.json` and `compact-analysis-gpu.json`. No new repeated CPU/GPU speedup claim or final-gate acceptance follows from this single GPU measurement. Diagnostic instrumentation was removed.

### Donor read snapshots and enclosing payload scopes

Historical `nsys` mutex callchains attribute 149.039321 ms of aggregate thread waits to donor work (not wall time). A targeted experiment used snapshots for scalar-only donor workers as well as payload-writing workers. One p1024 GPU run measured 916.620351 ms versus the preceding 931.995020 ms: the roughly 1.6% single-run change is insufficient to retain an additional optimization under the reduced-testing protocol. That experiment was reverted and is not a final-candidate speedup claim.

A separate correctness guard is retained: a donor pool called from an active payload transaction runs sequentially in that caller's view; optional batched donor preparation declines before launching readers. Otherwise worker threads cannot observe uncommitted destination payloads and cannot commit transactions inside the caller's scope. A regression stages the destination, checks every seeded donor, declines batched work and aborts the outer transaction, verifying no payload leaks. Derivation-resource, contextual-donor and compiled-GPU-backend tests pass. Evidence: `donor-read-snapshot-assessment.json` and `donor-read-snapshot/enclosing-scope-tests.log`. Retained benchmark hash `0c2c37c0e9c3a6ccc2461867ec7cb083fc28f274e5bdb913d44f07d8b56aeb2b` has not been timed; the experimental timing does not describe this binary. Goal 11 remains incomplete.

### Bounded donor prefetch after compact-analysis warming

Rechecking the previously rejected 32-parent window after fixing compacted-parent warming showed that all 32 batches committed successfully (428 total donor jobs), yet generation time remained 938.554212 ms. Thus conflict fallback did not explain the lack of benefit. A 128-parent window committed all eight batches and measured 817.917772 ms with diagnostic counters. The larger bounded window gives workers more independent donor pools to schedule.

The retained implementation previews the existing site shuffle and donor seeds using a copied RNG, prepares only anticipated subtree-mutation jobs, and verifies that consumed parent/site/seed coordinates match the ordinary loop. Failed speculative batches replay the original interleaved path without committed payload writes. Existing batch limits (128 jobs, seed/work caps, private contexts, atomic payload commit) remain in effect. Crossover and populations below 128 use the original path.

Without instrumentation, p1024 complete-generation diagnostics measured 830.683599 ms direct and 749.327714 ms overlap; direct preprocessing was 345.952574 ms. The preceding non-prefetch direct diagnostic was 931.995020 ms, so the observed direct reduction is about 10.9%, with only one sample per configuration. Final evaluation and retained-population checks are outside the generation timing. Both complete final AST populations, fitness histories and non-timing operator counters exactly match the pre-prefetch retained reference. The 128-parent GPU regression exercises actual subtree-donor preparation while comparing compacted/uncompacted sources and preserving opaque-payload rejection. Grammar preprocessing, derivation resource/batch oracle, and compiled GPU backend tests all pass.

Evidence: `prefetch128-retained-summary.json`, `prefetch128-retained-assessment.json`, `prefetch-after-compact-audit/retained-tests.log`. Candidate SHA-256 `a54610ebd3b97cc2b82484ea4f30813fbae5a3a5c3a71574ad0902725a3fd99a`. These diagnostics do not meet or replace the representative paired speedup gate; Goal 11 remains incomplete.

### Rejected flat type-environment experiment

A sorted-vector replacement for the native type verifier's ordered-map environments passed exact-scope, structure, and compiled GPU backend tests. One CPU/GPU pair measured 6052.098014 / 803.554204 ms per p1024 generation. Compared with the retained prefetch candidate's 830.683599 ms GPU diagnostic, this small single-sample movement does not establish a material improvement. The experiment was reverted rather than adding another data-structure optimization; no new acceptance claim is made. Archived sources and command: `flat-type-env/`; results: `flat-type-env-assessment.json`. Embedded runner `previous_result` values are historical and are not the immediate control.

Batch-preview follow-up: skip speculative preparation when mutation ratio is zero or hardware concurrency is below two. The 128-member zero-mutation regression verifies exactly one parent-analysis lookup per member, no donors, and unchanged candidate/source/compatibility identities against non-donor preparation. Grammar-preparation and compiled GPU backend tests pass; the single-core guard was reviewed, not executed on this host. No new timing run was necessary for this disabled-work path. Evidence: `prefetch-disabled/assessment.json`; current unmeasured benchmark SHA-256 `e69cfae4d5183b5ac6e426a403c097e69457ff12b558cc30038abbc71741242f`.

### Destination-proof coverage diagnostic

A non-behavior-changing probe in donor admission classified 1532 accepted unique donor attempts as having already passed destination-frame resource projection under the root-invariance certificate. Zero accepted attempts lacked this proof, and zero proof-fitting candidates subsequently failed full budget or validation checks in this canonical p1024 run. Non-timing reproduction counters match the retained candidate. This is one workload/seed, not a general proof that full validation is redundant.

No admission shortcut was introduced. Any compositional replacement must establish full-child membership and preserve repeated-hole coupling, lexical capture remapping, physical/search limits, canonical-witness constraints, native verification and exact lowering limits. Mutation-entry membership alone is insufficient. A safer implementation direction is reuse of verified unaffected subtrees while rechecking changed ancestors; skipping the full-child check based only on resource fit is not justified by these observations. Evidence: `composed-admission-audit/summary.json`. Instrumentation was removed and the benchmark hash restored exactly to `e69cfae4d5183b5ac6e426a403c097e69457ff12b558cc30038abbc71741242f`.

### Rejected hashed positive-match table

The membership specification limits positive match caching to a single grammar/AST invocation, so parent-to-child reuse is not an authorized drop-in cache change. A narrower experiment replaced the per-invocation ordered decision map with a hash table, retaining production order, active-alias sentinels, exact lexical keys, and all validation limits. Stable element references were used across recursive rehashing. Membership, bounded variation and compiled GPU backend tests passed. One CPU/GPU pair measured 6097.169535 / 806.637936 ms. This small single-run movement relative to the retained 830.683599 ms GPU diagnostic does not support retaining the change; sources were restored. Evidence: `hashed-positive-matches/` and `hashed-positive-matches-assessment.json`. Neither this result nor the flat-environment experiment supports further small lookup-container tuning as a route to the required speedup.


### Optional system SHA-256 and current speedup check

Generation-scoped instrumentation attributed 425.516 ms of aggregate thread time
across 34,686 calls to runtime identity construction, including 134.420 ms in
SHA-256 and 79.127 ms in structure serialization. These nested thread timings
are neither additive nor wall-clock savings. All instrumentation was removed
before timing. Evidence: `generation-membership-phases/hash-assessment.json`.

The retained implementation optionally uses OpenSSL Crypto for SHA-256, with
caller-owned digest storage and the existing portable implementation as fallback.
The identity format and bytes are unchanged. OpenSSL 3.0.2 was selected on Snoopy;
the build can explicitly disable it with `GAGP_ENABLE_OPENSSL_SHA256=OFF`.
Grammar-definition, grammar-cache and compiled-GPU-backend checks passed with
OpenSSL. The definition test, including a binary 0..255 vector, also passed in a
separate CPU-only build with OpenSSL disabled. Empty, short, multi-block and
million-byte known vectors remain covered. Provider failure itself was not
fault-injected. Archived test logs and source hashes: `openssl-identity/verification.json`
and its sibling logs.

The first single-pair diagnostic measured CPU 6107.608367 ms / GPU 780.274984 ms.
A subsequent user-requested same-session check measured all four paths once:

| Version | CPU generation ms | GPU generation ms | CPU/GPU speedup |
| --- | ---: | ---: | ---: |
| Frozen reference | 3085.141205 | 75.084189 | 41.089093x |
| Current candidate | 6059.038138 | 785.926552 | 7.709420x |

This is p1024, seed 42, fuel 20000, one complete generation (evaluation plus
reproduction), direct GPU reproduction, final evaluation skipped, on Snoopy's
RTX 3090 device 0. Load/init are outside this generation metric. GPU utilization
was zero before starting, with no benchmark/build process competing. No warm-up
or repeated matrix was added for this diagnostic. Q is 0.187627, so the relative
speedup requirement still fails materially. CPU and GPU absolute ratios are
1.963942 and 10.467271. GPU reproduction accounts for 706.627836 ms versus
36.055082 ms in the reference; GPU evaluation accounts for 74.883170 ms versus
38.062307 ms. The selected candidate SHA-256 is
`7030f77cb7937d51e5c6e2d73f000aef092405b6f8a9cb341483b46c115ebfc6`.

Fitness and all non-timing reproduction counters match the same-backend
source-order-repaired diagnostic, before the prefetch and SHA optimizations.
Evidence: `speedup-user-check-assessment.json`, its runner, and
`openssl-identity/verification.json`. This evidence does not replace the selected
representative gate or establish universal search closure. Goal 11 remains incomplete.


### Rejected cross-seed batch admission reuse

A bounded experiment reused exact decoded donor admission results across scalar
seeds within one batch job, keeping destination/site fixed and payload-writing
jobs on independent seed caches. It retained all generation/retry draws and the
existing 256-entry cap. The repeated-seed batch oracle and compiled GPU backend
checks passed; same-backend fitness and all non-timing counters matched the
preceding candidate. A single pair measured CPU 6280.944571 ms and GPU 788.336655 ms,
versus the preceding 6059.038138 / 785.926552 ms. This establishes no material gain;
both code and test edits were reverted. No acceptance measurement was repeated.
Evidence: `batch-admission-reuse/assessment.json`, `tests.log`, runner and archived
before/experimental sources. Further tuning of this cache is not supported by
these measurements.


### Current nsys trace and rejected executable-certificate fast path

A new CUDA/OSRT trace of candidate `7030f77cb7937d51e5c6e2d73f000aef092405b6f8a9cb341483b46c115ebfc6`
records 342.468 ms of aggregate mutex wait under staged payload reads and
91.700 ms under `CompiledGrammar::require_executable`. These are whole-process,
profiler-instrumented thread sums, not generation wall times or predicted savings.
The fitness kernel took 25.543 ms and all seven reproduction/packing kernel
instances together took about 1.884 ms in that trace. This supports investigating
host preparation, not kernel tuning. Command, run output, stacks and summary are
archived as `sha-candidate-host-trace-*`; the nsys report and SQLite export remain
on Snoopy under the representative artifact root.

An experiment replaced successful-root certificate lookups with monotonic atomic
flags, retaining the mutex and complete traversal for first verification and
leaving failures uncached. Concurrent first publication, copied grammar readers,
repeated unsupported-root rejection and compiled GPU backend tests passed.
However, one uninstrumented CPU/GPU pair measured 6186.054746 / 787.240690 ms,
with unchanged same-backend fitness and operator counters. Against the preceding
6059.038138 / 785.926552 ms pair this shows no material generation-time benefit.
The source and test experiment were reverted. Evidence:
`executable-certificate-atomic/assessment.json`, its runner, test log and archived
sources. Aggregate lock waits alone do not establish a wall-time bottleneck that
this change resolves; do not repeat this optimization without new evidence.

After restoration, all local/remote production source and header hashes matched,
and both affected tests passed again. Rebuilding after the header experiment
produced a different binary hash, so the earlier hash was not asserted restored.
The rebuilt binary is `453e8206019fdd0457c720c5a5892798c0e1afccb3d09e495505cdc07d8707a2`.
One follow-up pair measured 6051.537209 / 787.515121 ms (7.684344x), with the
same fitness and non-timing counters. Evidence: `certificate-restored-assessment.json`
and `executable-certificate-atomic/restoration.json`. This verifies the measured
identity of the restored candidate; it is not a representative acceptance run.


### Current native integration and diagnostic observation repair

After the overlap ordering, payload-scope, donor-prefetch and optional SHA changes,
one full Release native run executed all 118 tests. It passed 117 and exposed a
failure in `gagp_test_final_candidate_stats`: the diagnostic wrapper intercepted
both fitness ranking and the newly added source-order ranking for direct GPU
reproduction, counting the same evaluation twice. The wrapper now observes the
first ranking between reproductions and resets after reproduction, retaining the
optional final evaluation. Production evolution and the timed benchmark are
unchanged. The phase-order assertion now identifies the failing mode/options.

Only the affected target was rerun after repair. All 22 diagnostic/timed comparisons
passed, covering eleven mode combinations with final evaluation enabled/disabled,
phase order, NFE totals, population verification and unchanged non-timing results.
Together these are 118 passing native outcomes, including 19 GPU-labelled tests
and zero skips; this is not a second full-suite run or a clean-checkout Goal 12
claim. Test/config source hashes matched the local tree before the run (generated
Python bytecode excluded). The timed benchmark hash remains
`453e8206019fdd0457c720c5a5892798c0e1afccb3d09e495505cdc07d8707a2`.
No performance rerun was needed for this diagnostic-only fix. Evidence:
`current-integration/assessment.json`, full initial log, affected-target log and
`current-integration-test-source-audit.json`. Search mapping and performance
acceptance remain incomplete; Goal 12 has not begun.


### Sequence-proposal preparation exclusion

A scoped diagnostic emitted one record whenever sequence constant proposals were
materialized. The canonical p1024 GPU generation emitted zero records: its
sequence mutation policy keeps the relevant values, so this candidate does not
sample such tables. Calls with an already sampled table also return it unchanged
by design. Therefore eliminating cross-pass sequence proposal work cannot repair
this anchor's regression. No preparation/lifetime semantics were changed. Fitness
and operator counters matched the retained candidate; instrumentation was removed
and binary SHA-256 restored exactly to
`453e8206019fdd0457c720c5a5892798c0e1afccb3d09e495505cdc07d8707a2`.
Evidence: `sequence-preparation-audit/assessment.json`, archived source, command,
build log, stderr and run output. Instrumented timing is not acceptance evidence.


### Child-compaction reanalysis exclusion

A diagnostic around the second `VariationContext::analyze` in `certify` counted
zero calls and zero misses for the canonical p1024 GPU generation. Thus table
compaction during child certification does not explain remaining decode time in
this run, despite being a valid earlier concern for parent preparation. No extra
compacted-child warming was implemented. Fitness and non-timing counters match;
instrumentation was removed and the retained binary hash restored to
`453e8206019fdd0457c720c5a5892798c0e1afccb3d09e495505cdc07d8707a2`.
Evidence: `child-compaction-audit/assessment.json` and its source/command/log files.
This is a workload-specific exclusion, not a universal absence of compaction.


### Main-thread identity cost and direct typed-list encoding

Generation-scoped timers now distinguish the main thread from workers. The
canonical p1024 run performed all 24,044 native/membership checks and all 17,238
lowerings on workers; main-thread membership, witness and lowering counters were
zero. However, the main thread made 17,541 runtime-identity calls consuming
246.333 ms, including 52.362 ms of structure serialization and 55.006 ms of SHA-256.
Those subphases are nested and must not be added to the identity total. Main-thread
genome metadata refresh consumed 55.378 ms, including 40.006 ms of AST serialization.
Evidence: `main-thread-phases/assessment.json`; all instrumentation was removed.

The retained repair directly serializes IntList, FloatList and StringList canonical
constant identities instead of allocating a complete intermediate JSON tree.
Every payload is still looked up and every element type checked on every call;
there is no token-only payload cache. Float and string leaf formatting still uses
the existing canonical serializer. Grammar/artifact identity bytes remain unchanged.
The oracle compares against canonical serialization of `encode_constant`, including
empty lists, integer limits, signed zero, subnormal floats, control characters,
embedded NUL and supplementary Unicode. Grammar-cache and compiled-GPU-backend
tests pass. Same-backend fitness and all non-timing reproduction counters match.

One uninstrumented pair measured CPU 6045.131313 ms and GPU 746.870781 ms per
complete p1024 generation, speedup 8.093945x. The preceding retained pair was
6051.537209 / 787.515121 ms, an observed GPU reduction of about 5.2% with one sample
per configuration. Relative to the latest old diagnostic speedup 41.089093x,
Q is about 0.197: still a material failure, not representative acceptance.
Candidate SHA-256 `6c754f10ed0c72b81e208a89eb485a3920db8bde48645d42abc93d908a1aa1da`.
Evidence: `direct-list-encoding/assessment.json`, source hashes, runner and test log.
The earlier 118 native outcomes remain historical integration evidence; this
change reran only its affected checks.


### Rejected OpenSSL provider-description reuse

An OpenSSL 3-only experiment fetched SHA-256 once and used `EVP_Digest` with the
immutable description, retaining per-call digest state and the portable fallback.
This follows the ownership and return-value rules in the
[OpenSSL EVP documentation](https://docs.openssl.org/3.0/man3/EVP_DigestInit/).
Concurrent first-use known vectors and compiled GPU backend tests passed.
One uninstrumented CPU/GPU pair measured 6070.187491 / 733.332959 ms, with unchanged
fitness and non-timing counters. The roughly 1.8% GPU movement from 746.870781 ms
is insufficient to retain this extra path under the reduced-testing protocol.
Both implementation and test experiment were reverted. Evidence:
`cached-sha-provider/assessment.json`, runner, tests and archived sources.
The direct typed-list serializer remains retained; no new acceptance claim follows.


## Latest user-requested complete-generation check and identity memo experiment

On Snoopy RTX 3090, p1024, seed 42, one complete generation, direct GPU
reproduction, the retained candidate measured CPU 6129.773348 ms and GPU
737.874150 ms (8.307343x). The same-session frozen reference measured CPU
3077.824072 ms and GPU 76.535289 ms (40.214444x). Q = 0.206576; this remains
far below the accepted near-preservation allowance. GPU utilization was 0%
before measurement. These are one-sample diagnostics, not the final gate.
Commands, binary hashes, component times and outputs are in
`representative-snoopy-01/speedup-latest-user-check-assessment.json` under the
external grammar-migration artifact root.

A bounded per-thread runtime-identity memo used complete AST transport bytes,
input names and fuel, invalidated on committed payload overwrite/prune/clear,
and bypassed staged scopes so transaction dependency reads were preserved.
Focused grammar-cache and compiled GPU backend tests passed, including nested
string overwrite, list overwrite, prune/reload, aborted staged writes and
transaction read-conflict checks. Same-backend fitness and all non-timing
reproduction counters matched the retained candidate.

The experiment measured CPU 6190.841815 ms and GPU 715.721108 ms: only a 3.0%
single-sample GPU improvement. The cache and registry changes were rejected
because this gain did not justify their complexity. All four source/test files
were restored. Before/experiment source files, hashes, commands and results are
preserved under `representative-snoopy-01/identity-memo/`. This rules out this
particular memo implementation as a material repair; it does not establish
that identity work or other forms of reuse are negligible.


## Rejected parallel identity validation experiment

Bounded batches of 128 AST identities used at most eight workers, staged payload
snapshots with commit validation, serial replay on conflicts, and a serial path
inside enclosing staged scopes. Pack validation and prepared-population checking
used the helper; a const-AST overload removed temporary genome copies. Focused
identity tests (257 entries including a partial batch, staged override/abort,
null AST and missing payload) and compiled GPU backend tests passed. Same-backend
fitness and non-timing reproduction counters matched the retained candidate.

One p1024 pair measured CPU 6088.634097 ms and GPU 717.743509 ms, only 2.7% GPU
improvement against the immediately preceding retained-candidate check. All five
source/test changes were reverted; no batch identity API remains in production.
Evidence and source snapshots are in `representative-snoopy-01/identity-batch/`.
Together with the memo experiment, this does not justify further small identity
variants. Complete source/donor mapping remains necessary independently of the
still-failing speedup gate and is the next work item.


## Donor literal domain and declared-weight audit

The checker in external `representative-snoopy-01/donor-policy-audit/audit.py`
resolves every selected profile's imports and mutation-stage alias routes. Across
11 profile paths it checks 454 declared expression-variation routes, including
46 implicit self-donor routes (Bool filter bodies). All 454 literal proposal
domains match the frozen generator's rules: Int [-8,8], Float rounded to 1/1000
in [-8,8], Bool false/true, lowercase Char, String length 0..8, numeric lists
length 0..4, and StringList length 0..4 with element strings length 0..5.
Declared alternative weights also match the source branch masses: literal-only
routes; .82 literal plus three .06 scalar structured proposals; or .65 literal
plus .175 map and .175 filter. GPU scalar routes consistently use the structured
proposal family; CPU placement still needs separate proof.

This is a declaration audit against the frozen source, not a sampled frequency
test. Counts include equivalent p64/p1024 profile files and imported declarations;
they are not counts of unique reachable AST sites. Artifact/dependency hashes,
all route details, explicit/implicit distinctions and source hash are recorded in
`donor-policy-audit/assessment.json`. No production code or profile was changed.
The remaining mapping work is structured-template expansion, CPU statement-value
placement, reachable crossover closure, and finite-budget admission/rejection
behavior. This audit does not establish complete search mapping or pass the
performance gate.


## Frozen-population CPU donor placement audit

A current-library native probe reconstructs variation sites for each of the six
selected frozen populations using its CPU profile, root set, frame reservation
and typed-storage setting from the draft manifest. Across 1,344 members it
checks 15,164 Int/Float/String site occurrences. All 864 RETURN/ASSIGN value-root
occurrences route to structured scalar donors, and all remaining 14,300 scalar
occurrences route to literal-only CPU donors. There are zero placement mismatches.
The reference predicate is frozen `typed_expr_analysis.cpp:846`: an expression
immediately follows ASSIGN or RETURN. The probe applies that predicate to mapped
AST node positions; it does not independently establish arbitrary transformation
closure or finite-budget eligibility.

Evidence, raw per-site TSV files, commands, probe/build source hashes and the
probe executable hash are in external `representative-snoopy-01/donor-placement-audit/`.
The canonical p64 and p1024 rows were retried once after the orchestration script
encountered an omitted optional normalization flag in the next workload; the
script now uses the runner's off default. This was a read-only correctness audit,
not repeated timing. No production code, source grammar, or candidate binary was
changed. Structured-template expansion and reachable offspring/crossover mapping
remain pending; the performance gate remains failed.


## Structured donor hole-expression audit

The selected profile closures contain 571 structured donor alternatives spanning
15 type/family pairs: DC, DP1D and DP2D for Int/Float/String, and Map/Filter for
each typed list. All supplied template-hole expressions match the frozen
`subtree_utils.cpp:235..368` and list-construction rules. The independent expected
expressions check source literals, DP coordinates 4 or (2,2), scalar base values
and increments in the supplied expressions, dependency addition or string concatenation,
DC source/index/split/combine expressions, and fixed Map/Filter source/body values.
For Map, the checker verifies that the body references the newly introduced
element binding, after any outer formal scope, and that this is the traversal's
element binder. It does not equate arbitrary bound names.

External `representative-snoopy-01/donor-policy-audit/structured.py` and
`structured-assessment.json` preserve the checker, every proposal, template hashes
and reference source hash. There are zero mismatches. This checks hole arguments,
not the complete expanded template bodies: control-flow skeletons, fuel and
boundary semantics still require their own evidence. No profile or production
code changed, and this is not a performance result or a complete closure proof.


## Bounded donor template-body comparison

All 63 DC/DP template declarations used by structured donor proposals across the
selected profiles match the maintained compatibility-package control bodies.
The comparison retains fuel events, binder declarations, recurrence requests,
boundary values, coordinate domains, duplicate policy and evaluation order. It
replaces package input/solve/transition placeholders with corresponding template
holes and excludes only `resource_charge` plus frame/cell capacities from the
body equality. Capacities are checked separately: DP1D 7 frames/6 cells, DP2D
17 frames/16 cells, and DC 4 or 8 frames/0 cells. All 63 capacity checks pass.

The package behavior is covered by `test_legacy_package_equivalence.cpp`; this
structural audit links donor bodies to those package implementations without
claiming the existing test covers every possible substituted hole expression.
Package and test source hashes, every body comparison, capacity values and the
checker hash are recorded in external
`representative-snoopy-01/donor-policy-audit/body-assessment.json`. The checker is
`bodies.py`. No runtime test or timing was repeated. Map/Filter bodies, search
resource charges, budget filtering and reachable crossover closure remain
separate obligations. Goal 11 and the performance gate remain incomplete.


## Traversal template bodies and administrative resource charges

All 110 Map/Filter template declarations used by selected donor proposals match
an independently encoded recipe from `src/transition/linear_rec.cpp:131..188`.
The check includes exact traversal event costs, check-list/source/start order,
empty-list initialization, Filter's branch/append paths, Map's let/append path,
formal element bindings and every resource-charge annotation. It preserves
outer scopes and verifies the newly introduced element binding explicitly.

All 63 DC/DP template declarations also charge one source node/depth at their
outer source root and zero at every other administrative expression, with no
depth reset. For DP2D the source-root charge belongs to the outer let; charging
the inner bounded node instead was an incorrect initial checker assumption,
corrected after inspecting the source-root mapping. Production profiles were
not changed. These zero-cost wrappers keep the hole's source depth beneath a
single source-root charge regardless of physical expansion.

The checker and complete results are external
`representative-snoopy-01/donor-policy-audit/traversal.py` and
`traversal-assessment.json`; reference source and checker hashes are recorded.
There are zero mismatches. This closes the declared donor template-body and
administrative-charge checks, but not hole resource composition after insertion,
reachable crossover closure, or finite-budget admission/rejection equivalence.
No runtime suite or timing was repeated. The retained candidate and failing
performance gate are unchanged.


## Actual structured-splice resource regression

`test_legacy_budget.cpp` now verifies 90 actual nested Filter splices across
IntList, FloatList and StringList, parent nesting 1..3 and donor nesting 0..4,
at both the outer expression and innermost source leaf. It constructs the legacy
child directly, independently splices lowered native ASTs and charge vectors,
and compares source node/depth metrics with the native resource projection.
All native children verify. For each splice it checks the node and depth limits
one below, at, and one above the actual child metrics: 810 acceptance comparisons
agree with the legacy post-splice budget check.

The focused `gagp_test_legacy_budget` Release CTest passed on Snoopy (0.18 s).
Evidence and source hash are in external
`representative-snoopy-01/structured-splice-assessment.json`. This adds executable
composition/boundary evidence beyond synthetic donor size/depth pairs; it does
not prove membership under every profile or arbitrary crossover closure. Only
test/documentation files changed; production timing remains valid for the
retained candidate and no benchmark was repeated.


## Verification-scope correction

Review against the current user instructions and Goal 11 found that earlier
entries treated universal reachable-offspring/crossover closure as a mandatory
additional proof. The revised task requires correct semantics and search budgets,
verified mapped representative workloads, focused regression repair and final
integration; it does not require an exhaustive language-inclusion theorem.
Do not extend the previous audit sequence indefinitely or treat every unproved
universal statement as an independent implementation blocker.

The concrete evidence now covers full frozen populations, accepted compatibility
differences, literal proposal domains/weights, scalar donor placement, structured
hole arguments, template bodies/fuel/capacities, and actual resource-composition
boundaries. Existing retained-population and direct/overlap evidence remains
labelled by its tested binary. None of this is upgraded to a universal closure
claim or a final-current-candidate integration pass. Any concrete counterexample
still requires repair. Final verification must use the exact final candidate and
preserve full populations, runtime/type/fuel correctness and equivalent budgets.

The active performance failure remains material: retained p1024 complete-generation
speedup is 8.307343x versus 40.214444x (Q 0.206576, one diagnostic pair). The next
necessary work is a substantive performance repair followed by the prescribed
representative measurements and final affected integration. This scope correction
does not weaken the speedup gate, complete Goal 11, or authorize starting Goal 12.


## Main-thread identity callers after direct-list encoding

One instrumented p1024 GPU generation attributes 17,541 main-thread runtime
identity calls (208.565884 ms aggregate): variation-analysis keys 7,309 calls /
101.091229 ms; preprocessing parents/donors 3,580 / 35.647374 ms; pack validation
3,580 / 30.100317 ms; prepared-source/scored comparison 2,048 / 26.070401 ms;
and compile-population lookup 1,024 / 15.656563 ms. Instrumentation overhead is
included; these are diagnosis timings, not a speedup-gate observation.

This locates the largest identity cost in analysis-cache lookup, and explains
why parallelizing only pack/prepared validation had limited benefit. The next
repair should reuse validated immutable analysis ownership across phases while
preserving payload dependency validation and full child admission. The profile
adds no justification for bypassing correctness checks. Raw counters, commands,
source backups/instrumented versions and assessment are preserved under external
`representative-snoopy-01/identity-callers/`. Production sources were restored
following the diagnostic.


## Retained shared analysis-lookup identity

GPU preprocessing previously computed a parent runtime identity immediately before
`VariationContext::analyze` recomputed it for cache lookup. The analysis API now
optionally returns the exact freshly decoded identity used by that lookup.
Preprocessing consumes that output, eliminating 2,048 duplicate identity calls
per p1024 crossover-plus-mutation generation. No new memo state, payload epoch,
trusted attached provenance, or skipped validation is introduced. Cache-hit/miss
identity checks and the compiled GPU backend test pass.

One diagnostic CPU/GPU pair measured 6052.018564 / 721.147796 ms (approximately
8.39x). Same-backend fitness and every non-timing reproduction counter match the
preceding retained candidate. This modest change is retained because it directly
removes duplicate work without a new cache or invalidation mechanism; it does not
resolve the large relative-speedup failure. No repeated timing campaign or full
suite was run. Exact commands, binary/source hashes and results are external
`representative-snoopy-01/shared-analysis-identity/assessment.json`.


## Rejected decode parent-analysis handoff

A decode-local owned analysis handoff avoided looking up a just-certified parent
again during a child's root-contract check. Full child certification remained.
GPU backend tests and crossover tests including forged provenance and unrelated
root rejection passed. One diagnostic pair measured CPU 6185.381532 ms and GPU
723.003270 ms versus the retained candidate's 721.147796 ms GPU measurement.
No GPU gain was observed, so all four source/test changes were restored. The
preceding shared analysis-lookup identity optimization remains retained.
Evidence and source snapshots: external
`representative-snoopy-01/certified-analysis-handoff/`. This path does not justify
further small handoff variants without stronger evidence of material savings.


## Retained empty-donor-pool fast return

A focused preprocessing breakdown found 11,376 crossover and 10,673 mutation
serial donor-pool calls taking approximately 59.4 ms combined. These paths have
no donor seeds: crossover does not prepare donors, and selected mutation pools
are handled by the batch path. Nevertheless every empty call queried hardware
concurrency before falling through an empty loop. `generate_donor_pool` now
returns immediately for empty seeds. It preserves the existing empty-input
behavior, including no destination validation, RNG draws or analysis counters.

The derivation-resource test adds an empty-pool check with an invalid unused
site/parent/attempt limit to preserve that behavior. It and the compiled GPU
backend test pass. One p1024 diagnostic pair measured CPU 6260.191746 ms and GPU
665.523487 ms (approximately 9.41x). GPU total is 7.7% below the preceding
721.147796 ms sample; the single-sample CPU difference is also reported, not
used as proof of CPU improvement. Same-backend fitness and every non-timing
reproduction counter match the preceding retained version.

Preprocessing diagnostic sources/logs are external
`representative-snoopy-01/preprocess-breakdown/`; its instrumentation was removed
before measurement. Candidate commands, source/binary hashes and measurements
are `representative-snoopy-01/empty-donor-pool/assessment.json`. The material
speedup gap remains; this is not final representative acceptance.

### Current paired speedup confirmation and donor phase diagnosis

A single same-session p1024 full-generation comparison on Snoopy RTX 3090
(starting GPU utilization 0%) measured old CPU/GPU 3058.982391/73.661499 ms
and current CPU/GPU 6112.603987/665.176450 ms. Speedups are 41.5276x and
9.1894x, retaining 22.13%. This remains materially below approximate speedup
preservation; initialization and final extra evaluation are excluded. Evidence:
`representative-snoopy-01/speedup-confirmation-20260924-assessment.json`.

An independently linked donor diagnostic leaves production sources and binary
unchanged. Across workers, 13140 generated attempts consumed 352.197 ms in
donor generation and 49.426 ms in identity encoding; 6806 donor-frame resource
projections consumed 68.333 ms, and 1532 full-child admissions 120.615 ms.
Destination analysis consumed 7.041 ms over 428 calls. These are aggregate
thread times, not additive generation wall time or acceptance timings. Fitness
and all non-timing reproduction counters match the uninstrumented GPU run.
Evidence: `representative-snoopy-01/current-donor-phases/assessment.json`.
The next diagnostic should separate framed generation construction, frame-cost
preparation and canonical witness reconstruction. Previously rejected immutable
locality caching and worker-context reuse are not being repeated. Goal 11 remains
incomplete; this evidence does not authorize starting dependent Goal 12.

### Framed generation diagnosis and projection reuse experiment (reverted)

The current generator diagnostic records 13140 calls: frame-cost preparation
40.508 ms, AST derivation 28.605 ms, framed canonical witness reconstruction
233.274 ms, genome metadata 25.389 ms, and witness copying 2.705 ms. These are
aggregate worker times, not generation wall times. Fitness and non-timing counters
match the retained candidate. Evidence: `current-generation-phases/assessment.json`
under the representative artifact root.

An experiment retained the matcher's verified frame projection for witness
lowering, avoiding a second projection and redundant genome copy while preserving
native verification, canonical matching, and lowering limits. Contextual donor,
derivation resource, and compiled GPU backend tests passed. One CPU/GPU diagnostic
measured 6161.976764/736.632207 ms, with unchanged fitness and reproduction counts.
No useful improvement was established against 6112.603987/665.176450 ms, so the
experiment was reverted. Source snapshots and results are in
`reuse-frame-projection/`. The next architectural candidate is a rejection-only
resource precheck before canonical donor witness reconstruction, but it requires
an explicit charge-invariance certificate and must preserve bounded retry order;
it is not implemented or assumed valid by these measurements.

### Early repeated-donor rejection experiment (reverted)

A private post-construction probe checked exact decoded donor identities against
the existing bounded rejection cache before canonical framed witness rebuilding.
Only already validated negative entries for the same destination/site/frame and
budget were eligible; construction and payload writes retained their original
order, and successful candidates still received full validation. This avoided
assuming equivalence between mutation-entry and destination resource charges.

Derivation-resource tests (including the independent full-analysis retry oracle),
contextual donor tests and the compiled GPU backend passed. One CPU/GPU generation
measured 6026.641708/666.217706 ms. Fitness and all non-timing reproduction counters
matched the prior candidate by backend. GPU timing was effectively unchanged from
665.176450 ms, so the private probe, exception and header were removed rather than
retaining an unhelpful fast path. Evidence and source snapshots:
`representative-snoopy-01/early-rejected-donor/assessment.json`.
This rules out this specific early duplicate-rejection optimization on the current
fixture; aggregate witness time alone does not predict wall-time gains. Goal 11
remains incomplete and the representative acceptance gate has not passed.

### Remove temporary AST copies at reproduction identity boundaries

Retained a direct `AstProgram` overload of runtime identity and forwarded the
existing genome overload to the same implementation. Packing now reads parent
genomes and donor ASTs directly, and prepared-source comparison reads its owned
parent AST directly. All identity comparisons and payload reads remain intact;
this adds no cache, trust shortcut, or new mutation assumption. The recorded p1024
call counts imply 2048 parent-pack, 1532 donor-pack and 1024 prepared-parent AST
copies removed per generation. This is an allocation cleanup, not a claimed
measured speedup. The previously reported 9.19x measurement predates this change.
Evidence and original sources: `representative-snoopy-01/pack-without-ast-copies/`.

The AST-copy cleanup passed grammar-cache and compiled-GPU-backend tests. Current
benchmark SHA-256: `82c944873023caaef779886e89caada46dae8ad45e8889502bd6cb5cde5b025f`.

### AST-copy cleanup timing and owned compaction

One p1024 CPU/GPU pair for benchmark `82c944873023caaef779886e89caada46dae8ad45e8889502bd6cb5cde5b025f`
measured 5969.802678/655.979510 ms. Fitness and all non-timing reproduction counters
match the prior candidate per backend. The small timing difference does not
establish a material speedup improvement. See `pack-without-ast-copies/timing.json`.

A subsequent ownership cleanup changes table compaction to take its working
genome by value, allowing certification to transfer its already-owned genome
instead of copying the whole AST. Existing lvalue callers still retain their
source. Remapping, validation and metadata refresh are unchanged. This follow-up
is not covered by the preceding timing. Source evidence: `move-compaction/`.

Owned compaction passed AST codec/compaction, region splice, and compiled GPU
backend tests. Benchmark hash: `b08f3951406ee75f0bc17fa53f0ccd03a65bf99c14f18ede6ba302f65dfe6f9d`.

### Already-compact table fast path

Compaction now returns after validating all native/capture indices and refreshing
metadata when every name and constant entry is live. Stable compaction would use
identity remaps, so replacement tables and its second node-remapping walk are
unnecessary. New checks cover stale metadata and malformed constant indices;
AST compaction, region splice and compiled GPU backend tests passed.

One CPU/GPU p1024 diagnostic, including the preceding ownership cleanup, measured
6112.188161/655.537041 ms. Fitness and non-timing reproduction counters match the
prior candidate per backend. The GPU result is unchanged from 655.979510 ms;
these retained allocation cleanups do not resolve the regression. Further work
must target substantial analysis/dataflow costs rather than more small allocation
experiments. Evidence: `representative-snoopy-01/already-compact-tables/assessment.json`.

Measured binary SHA-256: `57da9d611599b8a924f58734c73fb7c6f0c7ddfc787aed629389400174cc0b71`.

### Generation-only analysis-cache audit

An independently linked diagnostic separated initialization/loading from the
actual p1024 generation. During the generation, 7309 main-thread member lookups
produced 6571 ordinary hits and 738 prepared-analysis hits, with **zero serial
full reconstructions**. No prepared analyses were discarded unused. Main-thread
member-key construction took 112.425 ms. Workers constructed 4005 keys (95.365 ms
aggregate), reused 1439 entries, and computed 2566 analyses (373.596 ms aggregate).
These instrumented costs are diagnostic, not acceptance timings. All 1930 serial
full analyses in the process occurred outside the measured generation, explaining
why the first unsegmented count would have misidentified the bottleneck.

Fitness and non-timing reproduction counters match the retained binary. Production
sources/binary were not modified by the probe. Evidence:
`representative-snoopy-01/current-analysis-cache/assessment.json`.
The working hypothesis of main-thread analysis misses is ruled out for this fixture.
Next work must address ownership and validated analysis/identity handoff between
internal stages while retaining payload-read validity and mutable public API
checks; increasing cache size or repeating warmup tuning is not supported.

### Payload-validated parent analysis handoff in mutation prefetch

The preview schedule and ordered parent loop now share the same analysis and
runtime identity within one bounded 128-parent window. Preview captures identity
and analysis payload dependencies in a single read transaction. Consuming the
saved result first validates those reads; conflicts use ordinary analysis. Existing
outer payload transactions retain the original path, avoiding nested scopes or
commits. Const population ownership lasts for the preprocessing call, so this
handoff introduces no AST-pointer cache or public trusted-provenance shortcut.

Compiled GPU backend and grammar preparation tests pass. A new 128-member oracle
compares prefetched output with the sequential donor path forced by an enclosing
transaction, checking parent/donor identities and order, compatibility keys,
candidate selection and donor slices. One p1024 CPU/GPU diagnostic measured
6053.827682/622.865473 ms, versus 6112.188161/655.537041 ms before. Fitness and
non-timing reproduction counters are unchanged per backend. The observed ~5% GPU
reduction is a single diagnostic, not representative acceptance; speedup remains
far below the frozen reference. Evidence: `prefetch-analysis-handoff/assessment.json`
under the representative artifact root.

Measured candidate SHA-256: `0ccf4bcd46a3f5a1d28c56da9a3f65d7b68a7e0189911b2bc20ca5aeb6ee9476`.

### Remove repeated compaction between compiled passes

The private mutation call now uses crossover decoder output as its packed
population directly. Decoder results consist only of certified/compacted children
or certified parent fallbacks. Population warming, payload revalidation, public
preparation, and packing checks remain unchanged. This removes one redundant
whole-population copy, table walk and metadata rebuild from the internal path.

Compiled GPU backend and evolution CPU/GPU parity tests passed. One GPU-only
p1024 diagnostic measured 635.743384 ms, versus the previous 622.865473 ms;
fitness and all non-timing reproduction counters match. This does not demonstrate
a timing improvement. The small ownership simplification is retained for removing
redundant work, without presenting it as a measured speedup. CPU timing was not
repeated for this GPU-only change. Evidence: `mutation-compact-handoff/assessment.json`
under the representative artifact root.

Current measured GPU candidate: `0f42d8aae26e13ec92c10100ba840ad9a82b78794498a0f15d8c30cf58b67e28`.

### Fixed-width runtime structure encoding experiment (reverted)

Tested a cache-only binary numeric writer using the existing complete AST field
walk, encoding integral fields explicitly as eight little-endian bytes. Public
AST/artifact text remained unchanged; only the ephemeral runtime cache namespace
and structure bytes changed. Grammar-cache and compiled GPU backend tests passed,
and fitness/non-timing reproduction counters were identical per backend.

The single CPU/GPU diagnostic measured 6127.441439/688.898690 ms, showing no
benefit over the retained candidate (latest GPU-only observation 635.743384 ms).
All three experimental source/header changes were reverted; no binary structure
API or runtime-cache-v2 format is retained. Evidence and before/experiment source
snapshots: `representative-snoopy-01/binary-runtime-structure/assessment.json`.
This excludes the tested eight-byte numeric encoding, not every possible encoding.

Restoration verified all three source files byte-for-byte against the saved
pre-experiment copies and matched remote SHA-256 values. The header-triggered
rebuild produced benchmark `55d19705020e5f2db48b1096da8621d5bd75294d7254d4867cf4ea8f61e7465c`,
which differs from the pre-experiment binary hash. Its source is restored, but the
previous timing must not be represented as a measurement of this rebuilt binary.

### Direct validated region-plan equality

Replaced canonical-JSON construction in matcher region comparisons and child
change classification with a shared field comparison. Both plans still undergo
`validate_region_plan`; every RegionPlan field participates, and validated inactive
fields retain their canonical zero requirements. No runtime cache format, public
AST representation or artifact encoding changes.

Membership and compiled GPU backend tests passed. The bounded-region compile test
now compares the direct predicate with canonical JSON on 66 mutations across
coordinate, captured-coordinate and sequence-window fixtures; it checks equal,
unequal, symmetric and rejected cases, including execution limits, types, request
shapes, bounds and inactive fields. Existing fuel/execution/splice checks also pass.

One p1024 CPU/GPU diagnostic measured 6038.068331/587.892916 ms (speedup 10.270694x).
The preceding GPU-only candidate observation was 635.743384 ms. Fitness and all
non-timing reproduction counters match per backend. This observed improvement
supports retaining the change but is not the representative acceptance gate;
the frozen reference remains much faster in relative CPU/GPU acceleration.
Evidence: `representative-snoopy-01/direct-plan-equality/assessment.json`.

Measured candidate SHA-256: `b799d63d99abfe510a79c45d9920c35b6fb2a5815fe135827ca766a552f0333f`.

### Already-validated plan comparator experiment (reverted)

Checked the native-verifier and compiled-grammar validation boundaries, then
experimented with bypassing repeat plan validation only inside matcher comparisons
and certified child classification. The normal validating comparator remained.
Membership, bounded-region JSON-oracle/compile, and compiled GPU backend tests
passed; fitness and all non-timing reproduction counters matched.

CPU/GPU diagnostics measured 6020.039238/583.493422 ms versus retained
6038.068331/587.892916 ms. The sub-1% GPU difference does not justify an additional
caller precondition, so all three experiment files were restored. The retained
comparator still validates both plans on every call. Evidence:
`representative-snoopy-01/validated-plan-comparison/assessment.json`.

### Direct native constant cache-key encoding

Removed per-constant `ostringstream` construction from the native AST cache-key
writer. Integer/token fields use direct decimal formatting; Float fields retain
exact zero-padded 16-digit lowercase hexadecimal bits. The key format and runtime
identity namespace are unchanged. A 46-case old-stream oracle covers all token
categories, signed integer extremes, Bool/Invalid, signed zeros, subnormal bits,
infinities and NaN payload bits. AST codec/compaction and compiled GPU backend
tests pass; fitness and non-timing reproduction counters remain identical.

One CPU/GPU diagnostic measured 6104.166184/583.166661 ms. The GPU difference
from 587.892916 ms is too small to establish a material gain. The simpler,
byte-identical encoder is retained without a speedup claim. Evidence:
`representative-snoopy-01/direct-native-constant-key/assessment.json`.

Measured candidate SHA-256: `c1ad71f7caff210fa1ac85e2df43772c9d40ad859fb4dd5f6b689406e8521950`.

### Reusable read-only payload validation for analysis handoff

Added a non-consuming check for sealed read-only payload snapshots. It validates
exact captured reads under the registry lock, including after an earlier commit,
and conservatively declines active/enclosing scopes, staged writes and conflicts.
The existing bounded mutation-preview handoff uses this check instead of a
single-use empty-write commit. Regression checks cover repeated validation,
post-commit validation, nested StringList string/list overwrites, restored exact
values, missing-to-present entries, active scopes and write transactions.

Payload staging, grammar reproduction preparation and compiled GPU backend tests
passed. This is the prerequisite for returning warm-up analyses and identities to
private preprocessing; that larger handoff is not yet implemented. No new timing
was run for this prerequisite. Benchmark hash:
`77309b407233b118313e712e992d20752ea2a3637a57ae7400f438a62946f1ce`.
Evidence: `representative-snoopy-01/reusable-read-snapshot/assessment.json`.

### Warm-analysis handoff candidate: requested full-generation comparison

A fresh single old/new CPU/GPU comparison on Snoopy RTX 3090 GPU 0 used the
same frozen population of 1024, 1024 cases, seed 42 and one complete generation,
including evaluation and reproduction, with overlap off. GPU utilization was 0%
before the sequential runs. These are diagnostic measurements, not a completed
acceptance matrix. Variation-cache, reproduction-preparation and compiled GPU
backend tests passed; focused tests of the new warm-analysis handoff are still
pending, so this remains a candidate.

| Version | CPU total ms | GPU total ms | CPU/GPU speedup |
| --- | ---: | ---: | ---: |
| Frozen old | 3087.800206 | 74.356321 | 41.5271x |
| Current candidate | 6027.309107 | 555.523613 | 10.8498x |

The candidate retains 26.127% of the old speedup, a 73.873% reduction, and does
not meet approximate speedup preservation. Absolute CPU/GPU times are 1.952x
and 7.471x the old times. GPU reproduction consumes 479.200409 ms versus
35.284038 ms in the old implementation; evaluation consumes 71.577072 ms versus
38.225015 ms. Fitness and non-timing candidate reproduction counters match the
previous measured candidate for each backend.

Candidate SHA-256: `af1895a8b5b2447d19c017ddffb7ed87f8d11b74f18043ea01a789f9b00db2c8`.
Evidence: `representative-snoopy-01/warm-analysis-handoff/assessment.json`.

The focused warm-analysis handoff oracle subsequently passed on Snoopy. It
compares public preprocessing with the private continuation, including donor
identities/order and every candidate field, tests changed unused String payload
invalidation, and checks that populations exceeding cache capacity retain no
handoff rows. The implementation is retained. No additional timing was needed;
the measured benchmark binary above is unchanged by these test/document edits.
The single-run GPU total is 4.74% below the preceding 583.166661 ms diagnostic,
which is a limited improvement and does not satisfy the speedup requirement.

### Current decode phase attribution (diagnostic only)

An independently linked instrumented copy of compiled_decode.cpp, using the
current production libraries, measures the two operator passes together:
parent certification 54.266293 ms / 1943 calls, child acceptance 40.198094 ms /
933 calls, speculative child analysis 22.443170 ms / 16 batches, metadata
reconstruction 7.057831 ms / 2048 calls, and physical child reads 1.877536 ms /
2048 calls. Total decode is 131.806363 ms. Fitness and all non-timing reproduction
counters match the current candidate. Production sources/binary are unchanged;
these instrumented figures are not acceptance timings.

This rules out vector allocation in physical child reads as a substantial target.
The next investigation is certification internals (identity lookup, compaction,
and witness materialization), before changing ownership or reusing certificates.
The earlier rejected parent-analysis handoff should not simply be repeated.
Evidence: `representative-snoopy-01/decode-phases-current/assessment.json`.

### Certification internals during decode (diagnostic only)

A second independently linked diagnostic scopes timers to decode only. Across
2876 certifications, initial analysis lookup takes 49.317812 ms, compaction plus
native metadata construction 23.662703 ms, and witness copying 2.661457 ms.
No certification changes table sizes, so there are zero post-compaction analysis
calls. The 933 parent-root lookups take 13.533201 ms. Fitness and non-timing
reproduction counters match the production candidate. Production code and binary
remain unchanged; this does not establish a new acceptance timing.

Witness sharing would save little while retaining complete variation analyses;
do not implement it on this evidence. Likewise, do not repeat the rejected
parent-root handoff. Analysis lookup serializes an exact runtime identity while
native metadata separately constructs an AST cache key; sharing this exact
serialization work is a more directly supported next investigation. Any such
handoff must retain payload validation and cannot trust mutable caller metadata.
Evidence: `representative-snoopy-01/certify-phases-current/assessment.json`.

### Paired AST key serialization prototype (not integrated)

An independent prototype serializes common AST structure once and constructs the
native-token key by replacing the known constant-field span with exact encoded
constants. Both resulting keys match existing production functions byte-for-byte
for all 1024 mapped canonical members. A small three-pass serialization-only
measurement takes 22.075 ms with separate writers and 13.0376 ms with the paired
writer. This is not a generation measurement; the production binary is unchanged.

At 2876 decode certifications the implied saving is approximately 8.46 ms, an
extrapolation rather than an observed end-to-end gain. Avoid adding multiple
cross-module handoff APIs for this limited result. The next investigation is
retaining prepared parent certification through private packing/decode ownership,
which targets the larger 54 ms parent certification phase. This differs from
the previously rejected optimization of only the post-child root lookup.
Evidence: `representative-snoopy-01/paired-key-prototype/assessment.json`.

### Retained prepared-parent certificates

The private backend now carries compact-parent metadata and warm analyses through
packing to decode, tied to the exact immutable packed-source owner and variation
context. Decode validates payload snapshots before reuse and otherwise certifies
the parent normally. Retention follows the warm-up cache-capacity bound. This
removes repeated parent identity/metadata construction; child acceptance and
source/provenance validation are unchanged.

Compiled GPU backend tests pass, including a new 128-member enabled/disabled
continuation oracle, stale imported metadata refresh, and unusable-snapshot
fallback with deliberately poisoned saved metadata. The test fixture initially
requested an impossible value 7 from a grammar producing 3/13; that fixture was
corrected before the passing run. Fitness and all non-timing reproduction
counters match the preceding candidate for each backend.

One CPU/GPU diagnostic measured 6009.348830/496.985420 ms (12.0916x). GPU decode
fell from 130.992332 to 86.975577 ms; complete GPU generation time fell 10.54%
from 555.523613 ms. Compared with the recent old 41.5271x baseline this still
fails approximate speedup preservation. No additional repeated timing campaign
was run. Candidate SHA-256:
`bdce6095aea31244f19fe5e452631dc3092caced8fa51a7db0c129072103085c`.
Evidence: `representative-snoopy-01/prepared-parent-certificates/assessment.json`.

### Retained warm parent identities in private packing

The private backend now passes its unchanged-population warm rows into packing.
After checking current payload reads, packing reuses the exact runtime identity
and compares it with preprocessing output; public packing and invalid snapshots
retain full identity reconstruction. Donor and shape/contract checks are unchanged.
Grammar-preparation and compiled GPU backend tests pass. The extended warm-up
oracle compares physical packing and rejects forged/expired payload identities.
Fitness and non-timing counters match the preceding candidate for each backend.

One CPU/GPU diagnostic measures 6001.053748/472.650110 ms (12.6966x), versus
6009.348830/496.985420 ms before this change. Complete GPU generation time is
4.90% lower in this single comparison. This remains far below old 41.5271x;
Goal 11 is not accepted. No repeated timing campaign was run.

Candidate SHA-256: `6783fbda563369b8220b17fdf965ea3f3295159370d17a17d572800d7fc0f255`.
Evidence: `representative-snoopy-01/warmed-parent-pack/assessment.json`.

### Parent certificate ownership and parity follow-up

Extended the 128-member certificate oracle with independent packed-source owner
and variation-context owner mismatches. Each case poisons saved metadata and
requires ordinary certification to reconstruct correct child metadata, populations
and counters. Compiled GPU backend and evolution CPU/GPU parity tests pass on the
current libraries. No production changes or new timing runs were needed. The
machine-readable current acceptance summary and this document's opening metrics
now reference the retained warm-parent-packing candidate rather than older builds.
Evidence: `representative-snoopy-01/warmed-parent-pack/ownership-parity-tests.log`.

### Retained immediate execution after owned preparation

The direct backend no longer round-trips its just-created prepared state through
public replay validation. It owns the unchanged input population and all prepared
state in the same call. Initial import validation, resource/request/fuel checks,
payload validation and child certification remain; externally supplied prepared
state still takes the public validation path.

Compiled GPU backend tests pass, including the new 128-member direct-versus-replay
oracle for population, counters and host RNG consumption, plus invalid imported
AST and mismatched-fuel rejection. Benchmark fitness and all non-timing counters
match the preceding candidate for each backend.

One CPU/GPU diagnostic measures 6172.874035/441.599888 ms (13.9784x), versus
6001.053748/472.650110 ms previously. GPU generation time is 6.57% lower; CPU
variation also contributes to the speedup-ratio increase. This is a single pair,
not representative acceptance; the old speedup remains approximately 41.53x.

Candidate SHA-256: `039e58a978124eb1678048c965893f4285b07df30d495d9524b70d99de6ca884`.
Evidence: `representative-snoopy-01/immediate-prepared-run/assessment.json`.
