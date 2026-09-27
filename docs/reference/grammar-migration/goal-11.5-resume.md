# Stage A optimization resumed — 2026-09-27

Current status: active; final acceptance remains pending. The latest validated
p1024 checkpoint reaches **30.4896x with overlap on**, versus **24.9515x in the
original direct setting**, using tcmalloc equally for CPU and GPU. The same-allocator
reference is 37.8023x (Q=0.8066). Overlap meets the numerical target; direct does
not, and the two settings must not be conflated. Clarification of whether the
overlap option satisfies the user's >30x acceptance is pending. Details and
evidence are in the final section below.

The user requested preservation and rollback of Stage B, then continued
optimization from Stage A. Q >= 0.95 remains aspirational; the original Goal 11
gate is not passed. There is no two-round optimization limit. CPU must not be
deliberately slowed and GPU workload must not be reduced. The latest request
authorizes the proposed rule experiments; regressions are reverted.

## Preserved state and rollback

- Original HEAD: `6ec6ecbb16c36aecd0177a0829feaa9907e99036`.
- Stage B: `462d9582c048220137bbd011b7abc48b48678d6b`.
- Stage A: `2d11d4a1971b4cbcd61ebd43e0f0b1745392296d`.
- Revert commit: `6538358`; reverses the final report commit and Stage B while
  retaining the independent navigation commit `7c11429`.
- Production sources, specs, configs, benchmarks and tools match Stage A exactly
  before subsequent optimization. The unrelated HTML report is unchanged.
- Recovery ref: `archive/goal11-stage-b-20260927`; a verified complete Git bundle,
  copied Stage B raw evidence and reports are stored at
  `/home/hschi1106/gagp-artifacts/grammar-migration/resume-stage-a-20260927`.
  Before rollback, all 468 receipt hashes and 361 implementation source hashes
  matched the final Stage B measurement record. Remote evidence remains intact
  at `/home/hschi1106/gagp-stage-b-20260927` on Snoopy.

## Reused measurements

The completed immediate-prepared-run evidence was checked, not rerun: CPU
6172.874035 ms, GPU 441.599888 ms; binary SHA-256
`039e58a978124eb1678048c965893f4285b07df30d495d9524b70d99de6ca884`.
The newer Stage B campaign's p1024 CPU/direct/overlap medians are:

| Version | CPU ms | GPU direct ms | GPU overlap ms |
| --- | ---: | ---: | ---: |
| Reference | 3049.941 | 73.881 | 63.484 |
| Stage A | 5987.150 | 443.402 | 410.879 |
| Reverted Stage B | 3563.729 | 423.912 | 402.996 |

Stage B reduced absolute time but reduced relative CPU/GPU acceleration. It is
historical evidence, not the current implementation or an accepted performance
result. Stage A's remote benchmark hash is
`59f17fcf98d2f44d75d771bafced6f0c8ec8ca34b4e8f97e5a7f0693d12a7089`;
this matches the campaign and remains untouched for comparisons.

## Experiment 1: omit redundant constant preview table (reverted)

An isolated Stage A candidate used the already validated warm witness to check
for mutable constants without constructing the temporary preview stream. Native
preparation and GPU backend tests passed. One CPU/direct diagnostic measured
6233.597064/440.761862 ms. Against the existing Stage A 443.402 ms direct median,
the sub-1% difference does not justify a new branch; the experiment was reverted.
No old experiment was rerun. Source patch, build log and measurement receipt are
preserved under `resume-stage-a-20260927` in the external artifact directory.
All 936 Stage B output/log hashes were also checked against preserved receipts.

## Experiment 2: coalesce identical speculative analyses (reverted)

Within each deferred child batch, one producer computed analysis for each exact
cache key while followers shared its result. Per-member identity/read snapshots,
atomic validation and ordered cache accounting remained. Focused cache tests
covered shared successes/failures, eviction and reverse consumption; GPU backend
checks passed. CPU/direct measured 6049.758065/441.397283 ms. The negligible GPU
difference does not justify futures/mutex complexity, so this experiment was
also reverted. Its patch, test log and result are preserved externally.

## Experiment 3: internal verified compilation (reverted)

A private continuation skipped repeated structure verification only after full
native validation inside generation/membership. Public compiler validation was
unchanged. Compiler, membership, bounded membership and GPU backend tests passed.
CPU/direct measured 5930.792055/443.545560 ms; GPU performance did not improve,
so the extra trusted interface was removed. Patch and logs remain external.

## Retained: bounded compilation before GPU evaluation

GPU evaluation now prepares identities and bytecode in bounded 128-member
batches with at most eight workers. Exact payload snapshots are validated before
ordered publication; conflicts or enclosing transactions use the sequential path.
All population members, cases, grammar rules, fuel, operator work and RNG choices
are unchanged. CPU evaluation retains sequential compilation. Compilation timing
uses batch wall spans, not a sum of overlapping worker durations.

An initial both-backend experiment improved GPU time but showed a small CPU
regression on p64. It was superseded by the GPU-only schedule to avoid benefiting
from CPU slowdown. Initial experiment receipts remain separate. The final fresh
p1024 comparison uses one warm-up and three paired observations per version/mode:

| Version | CPU ms | GPU direct ms | S | Q |
| --- | ---: | ---: | ---: | ---: |
| reference | 3105.474 | 75.557 | 41.1013 | 1.0000 |
| stage_a | 6045.842 | 436.670 | 13.8453 | 0.3369 |
| candidate | 6110.105 | 408.751 | 14.9482 | 0.3637 |

The GPU improvement is retained; Q remains materially below 0.95 and the goal
is incomplete. CPU variation must not be interpreted as intentional acceleration
ratio improvement. All candidate fitness and non-timing reproduction counters
match Stage A within each backend. Existing GPU backend/parity checks passed;
expanded payload checks exercise 64 members, duplicate programs, all eight types,
GPU evaluation and overlapped GPU reproduction, with independent final CPU/GPU
fitness comparison. This is targeted validation, not final all-goal acceptance.

Raw commands, outputs, receipt hashes and test logs are in the external
`resume-stage-a-20260927/gpu-compile-paired` directory and `gpu-compile-summary.json`.
Remote builds and logs remain at `/home/hschi1106/gagp-resume-a-20260927`.

## Target-30 continuation (2026-09-27, in progress)

The user now authorizes all five proposed directions, including experimental
rule changes, and asks for >30x CPU/GPU speedup on the original p1024 complete-
generation comparison. Keep improvements only; preserve raw evidence and report
rule/semantic differences. No local GPU runs or subagents. The old Q>=0.95 remains
a reference aspiration; this continuation has not reached either target.

Experiments are isolated under external `grammar-migration/target30-20260927`
and remote `/home/hschi1106/gagp-resume-a-20260927/target30-*`. No final acceptance
claim follows from exploratory single pairs. The retained candidate and final validation are recorded below.

- Complete child admission in bounded parallel batches: first GPU 383.291 ms,
  CPU 5904.840 ms; fitness and reproduction counters match the starting candidate.
- Conservative production root-node dispatch: CPU/GPU 5918.256/378.052 ms;
  insufficient standalone improvement, investigating name-sensitive dispatch.
- GPU parent compaction in bounded parallel batches: CPU/GPU 5884.105/365.839 ms.
- Rule experiment: merge 44 pairs of identical productions whose initial/mutation
  stage masks are disjoint (same weight and all other metadata). Profile identity
  and production IDs change; no production language, stage support, fuel or
  resource limit is removed. CPU/GPU 5819.653/370.214 ms; not retained.
- Rule experiment: canonicalize newly accepted child lexical binder IDs by native
  declaration position, updating lexical references and bounded captures. Ordinary
  names and external inputs remain exact. Full native/membership checks remain;
  alpha-equivalent AST identity and evolutionary equality accounting can change.
  CPU/GPU 5918.388/377.141 ms; reverted.
- Incremental membership prototype: on private constant mutation only, reuse
  parent decisions in unchanged subtrees with empty formal scopes and no root
  alias ambiguity; retain full native, resource, witness and lowering validation.
  CPU/GPU 5884.640/374.839 ms; reverted. This was a restricted prototype, not
  general incremental structural/type verification.
- Four-seed donor chunks: CPU/GPU 5755.755/368.362 ms; reverted.
- Parallel donor fragment construction/identity: CPU/GPU 5787.279/366.568 ms;
  reverted (no material GPU improvement over simpler compaction candidate).
- Omit unused donor fragment population metadata: CPU/GPU 5807.820/365.081 ms;
  reverted (no material GPU improvement).
- Retain worker child analyses for subsequent mutation preparation: CPU/GPU
  5852.096/367.993 ms; publication overhead offsets saved analysis, reverted.

Each removed implementation patch is archived externally. Focused tests passed
for the experimental paths; final candidate tests, paired measurements, source
identity audit and this section's final assessment remain pending.

### Retained candidate and paired comparison

The candidate retains complete child admission batches, parent compaction
batches, private stable-compaction proof transport and a GPU host-worker cap of
20 (bounded by hardware concurrency). Donor preparation retains its previous
eight-worker cap. CPU evaluation/CPU reproduction algorithms are unchanged.
The cache-key refactoring needed by private compaction publication preserves the
same decoded identity/request encoding and validation boundaries.

One warmup plus three measured paired blocks rotated the reference, Stage A,
proof-transport candidate and otherwise identical no-transport candidate. The
metric remains the complete p1024 generation, direct GPU reproduction, seed 42,
1024 cases, fuel 20000, and equivalent source 80-node/depth-7 limits. No candidate,
case, operator proposal or donor seed was removed.

| Version | CPU median ms | GPU median ms | CPU/GPU S | Q versus reference |
| --- | ---: | ---: | ---: | ---: |
| reference | 3113.729 | 74.210 | 41.9585 | 1.0000 |
| Stage A | 6044.358 | 438.732 | 13.7769 | 0.3283 |
| retained candidate | 6083.856 | 356.214 | 17.0792 | 0.4071 |
| no compaction proof transport | 6022.472 | 361.991 | 16.6371 | 0.3965 |

The retained candidate reduces GPU time 18.8% relative to paired Stage A and
increases S about 24.0%. Relative to the earlier 9cdc72e campaign (14.9482x), S is
higher, but that is a historical comparison rather than a paired observation.
Proof transport's 1.6% GPU median advantage is modest; individual timings overlap,
and CPU timing variation contributes to its speedup-ratio difference. Three
observations do not establish a confidence interval. A long-running Python
process was present on Snoopy; it was not modified or terminated.

**The >30x target is unmet.** Holding current CPU time fixed would require GPU
wall time below approximately 202.8 ms, another 43.1% reduction. Original Q>=0.95
is also unmet. This checkpoint must not be represented as Goal 11 completion.

A separate same-backend validation retained all 1024 final children and performed
final evaluation. The complete final-population identity records match Stage A
for both candidate variants. All three reports confirm native membership and
lowering validation of the complete final population. Paired timings exclude this
extra final evaluation, as in the original comparison.

### Additional rejected refinements and rule effects

- Name-sensitive production dispatch also failed to improve the complete result
  (CPU/GPU 5913.823/366.020 ms) and was removed with root-node dispatch.
- Reusing parent root certificates in fresh admission workers did not improve
  GPU time (6096.363/376.731 ms); removed.
- Stable-compaction analysis transport was refined to reuse unaffected registry
  IDs within the same context. Its retained semantics are described in
  [GPU reproduction](../../design/gpu-reproduction.md).
- Sixteen readers measured 6139.474/359.327 ms; retaining eight donor workers
  measured 6381.803/359.155 ms. Twenty readers with eight donor workers measured
  6070.523/342.682 ms before the more representative paired campaign above.
  Exploratory minima are not substituted for the paired median.
- A host-only release diagnostic found about 12.9 ms destroying private prepared
  state and 1.1 ms releasing the input copy. No deferred-cleanup optimization was
  introduced and the instrumentation was removed.
- A further naming-profile experiment bijectively renamed declared ordinary
  locals to short sorted aliases (`v0000`, ...), transformed matching native name
  tables and grammar references, and preserved external inputs. It measured
  6106.252/343.693 ms, versus the preceding 6070.523/342.682 ms candidate. No gain:
  the benchmark adapter option and transformed profile were not retained.

No grammar or naming restriction survives these experiments. The two naming
experiments would change AST/artifact/cache identity and can change later tie
ordering/trajectories despite preserving individual program execution. Merging
stage-disjoint duplicate productions preserves stage support and weights but
changes production identity/order and can change seeded trajectories. None is a
new production semantic contract. The incremental constant-mutation experiment
was deliberately limited; general incremental structural/type certification was
not implemented or claimed validated.

The external `paired-summary.json` records all observations and the final
population comparison. All 72 output/log receipt hashes in the captured campaign
were verified after transfer. Raw binaries remain under their distinct remote
experiment names; source patches, rule profiles, runners and copied logs remain
under `grammar-migration/target30-20260927`. Final source/test audit follows.

### Retained validation and source identity

Seven focused native checks passed: compiled GPU backend, all-eight-type compiled
payload evolution, CPU/GPU evolution parity, variation cache, exact native scopes,
crossover and derivation resources. The backend oracle now exercises both zero
and full mutation with/without prepared parent certificates, including invalid
certificate boundaries. A new independent full-analysis oracle verifies removal
of unused declared input names, remapped scope IDs/signatures and compatibility
keys after compaction. All 23 repository checks passed; `git diff --check` passed.

All 360 tracked C++/CUDA/header/test/build/fixture files match the remote source
snapshot by SHA-256. Rebuilding the retained source reproduces the measured
candidate exactly: `8a726b1ec7b1ccc12035b40c3e045e062ea06d43bb8fcdf90b704e5f836079ab`.
Unused headers from rejected experiments were removed from the isolated remote
source. The unrelated untracked `gagp_progress_report.html` remains unchanged.

The final population comparison covers the complete ordered native AST JSON
arrays for all 1024 children, not just aggregate fitness. This is a focused
checkpoint validation, not completion of Goal 11 or all later goals.


### Continuing toward 30x: worker reuse and wider donor scheduling

The next retained checkpoint reuses operation-owned workers across the existing
128-member compilation, parent-analysis, compaction and admission batches.
Every batch still completes all callbacks before payload validation and ordered
publication; exceptions also wait for all readers. CPU compilation is unchanged.
The private helper is `cpp/src/evolution/batch_workers.hpp`.

Mutation prefetch now spans up to 1024 parents with up to 20 donor workers. It
stops before another selected parent would exceed the existing 1,048,576 declared
donor-node storage bound, then resumes at that parent. Job count is bounded at
1024; the 64-seed per-job bound is unchanged. Actual seeds, retries, proposed
programs, rejection outcomes and final population are unchanged. Resource-proof
labels are interned for exact integer comparisons without changing equivalence,
work-limit accounting or the proof algorithm.

Exploratory CPU/GPU observations (ms; not acceptance medians):

| Experiment | CPU | GPU | Disposition |
| --- | ---: | ---: | --- |
| Reorder donor-site equality checks | 5990.558 | 352.008 | Reverted; no clear gain |
| Reuse workers across batches | 6004.753 | 336.143 | Retained |
| Intern resource-proof labels | 6265.388 | 338.498 | Included in the candidate below |
| Wider donor window, 8 workers | 5959.987 | 302.038 | Superseded by 20 workers |
| Wider donor window, 20 workers | 6006.695 | 274.293 | Retained, then paired |
| Expand analysis/admission batches to 1024 | 6057.341 | 276.305 | Reverted; no clear overall gain |
| Parallel child-metadata reconstruction | 6187.152 | 277.015 | Reverted; no clear overall gain |

One warm-up followed by three paired observations compared the immutable
7495153 candidate with the new scheduling checkpoint on Snoopy:

| Version | CPU observations ms | GPU observations ms | CPU/GPU median ratio |
| --- | --- | --- | ---: |
| 7495153 control | 6056.088, 6075.219, 6143.329 | 346.455, 364.569, 372.092 | 16.6641x |
| Worker/window candidate | 6146.424, 5879.989, 6034.888 | 284.071, 279.175, 274.954 | 21.6168x |

Candidate medians are CPU 6034.888 ms and GPU 279.175 ms: GPU time is 23.4% lower
and S is 29.7% higher than its paired control. Against the earlier measured
reference S=41.9585x, Q is 0.5152; that reference comparison is historical, not a
new simultaneous reference measurement. **The 30x target remains unmet:** at this
CPU time the GPU generation must fall below 201.163 ms, another 27.9% reduction.
No grammar, naming, budget or execution rule is changed by this checkpoint.

Six focused tests passed: grammar preparation, derivation resources, compiled
GPU backend, all-eight-type compiled payload evolution, CPU/GPU evolution parity,
and variation cache. New checks exercise worker exception/barrier/payload-scope
lifetimes, 137 independent donor jobs, a 257-parent prefetch, and capacity-driven
128+128+1-job windows against sequential generation. A separate retained final
population run validates all 1024 members and matches every ordered native AST
JSON record from the prior candidate. The additional final evaluation is excluded
from paired timings. This remains checkpoint evidence, not Goal 11 completion.

Raw receipts, binaries, complete population comparison, diagnostic Nsight reports,
source snapshots and rejected patches are preserved under the existing external
`target30-20260927` artifact directory and isolated Snoopy build directory. The
paired runner refuses to overwrite completed receipts. CPU sampling with Nsight
is unavailable on Snoopy (`perf_event_open` denied); no host configuration was
changed to enable it.

The source audit matches all 361 production/test/build/header fixture paths to the
isolated remote source. Its rebuilt benchmark is byte-identical to the paired
candidate: `faca74fcc17c0886d172dc77a6d00cf579d279407fd8e019b682c78811eaaa7b`.
All 23 repository checks passed. The transferred follow-up archive has 35 verified
output/log receipts; `window-summary.json` records the medians, source audit and
full-population comparison. Diagnostic NVTX instrumentation was removed.

### Owned overlap and admission proofs: 30.4896x with overlap enabled

The retained continuation adds no grammar, naming, sampling, fuel or budget
restriction. It preserves the original p1024 snapshot, 1024 cases, seed 42,
blocksize 1024, complete operator schedule and full-generation timing. Overlap
is an explicit option: the original direct result is reported separately.

Retained implementation:

- Private overlap owns the same immutable population used by evaluation and
  keeps prepared state private. Completed fitness is consumed once. A changed
  payload snapshot causes fresh preparation with the same seed, including checks
  on unused constants. Public mutable prepared replay retains full validation.
  Preparation, joins and population destruction remain inside generation timing.
- Resource-invariance proofs run concurrently with parent preparation, with
  separate cache locking and unchanged roots, proof limits and semantics.
- Donor prefetch reuses its already validated destination analyses after checking
  their payload snapshots. All original seeds, retries and complete admissions
  remain present. Public donor APIs do not accept these private handoffs.
- Parallel child admission reuses validated parent root contracts. A previous
  experiment had checked snapshots inside an active payload scope, where reuse
  is intentionally refused. The corrected path checks before that scope and
  again before publication. Candidate membership/lowering/budget checks remain.
- Short scalar-only generated donor keys reuse freshly rebuilt native identities
  within their bounded destination/site/frame cache. Keys over 256 bytes and
  payload-backed values retain decoded runtime hashing. This changes only private
  cache work; it does not trust imported metadata or alter public identity bytes.
- Device reproduction arrays share one aligned allocation with unchanged array
  capacities and transport checks. Pinned host staging remains unchanged.
- The tested runtime option preloads Ubuntu's isolated `libtcmalloc-minimal4`
  library, equally for reference/candidate and CPU/GPU. No system package was
  installed, no global environment was changed, and no allocator dependency was
  added to the default build. Default allocator settings were used.

Exploratory observations below are individual runs, not final acceptance medians.
All CPU/GPU values are milliseconds; `GPU` means overlap only where stated.

| Experiment | CPU | GPU | Disposition |
| --- | ---: | ---: | --- |
| Atomic executable-root flags | 6042.853 | 278.848 | Reverted |
| Split payload reader locks | 6306.067 | 280.239 | Reverted |
| Existing public overlap | — | 286.026 | No benefit by itself |
| LTO build | 6134.242 | 278.403 | Not selected |
| Move metadata ownership / identity path | 6146.482 | 278.848 | Reverted |
| Low-level SHA-256 context | 6146.795 | 280.215 | Reverted |
| jemalloc, direct | 5508.079 | 249.364 | Superseded by tcmalloc |
| tcmalloc, direct | 5200.122 | 235.708 | Retained runtime option |
| Owned overlap, default allocator | 5892.798 | 246.717 | Retained implementation |
| Owned overlap, tcmalloc | 5148.842 | 200.790 | Retained combination |
| Owned overlap, mimalloc | 9436.232 | 413.335 | Not selected |
| Concurrent resource proof, tcmalloc overlap | 5344.162 | 192.234 | Retained |
| 16 / 10 donor workers, tcmalloc overlap | — | 201.631 / 206.864 | Restored 20 workers |
| One pinned host allocation, tcmalloc overlap | 5380.444 | 192.298 | Reverted |
| One device allocation, tcmalloc overlap | 5346.900 | 188.850 | Included in final candidate |
| Blocksize 128 / 256 / 512 | — | 238.828 / 203.612 / 194.803 | Restored 1024 |
| 128 MiB tcmalloc thread cache / plus transfer 64 | — | 187.560 / 193.121 | Default settings retained |
| Donor analysis handoff | 5170.392 | 183.420 | Retained |
| Active parent root proof | 5116.762 | 172.049 | Retained |
| Scalar admission identity prototype | 5237.356 | 169.041 | Bounded before final measurement |

The paired candidate (before the final guard noted below) used one warm-up and
three rotated CPU/direct-GPU/
overlap-GPU blocks. The reference observations are from the immediately preceding
same-host/same-allocator campaign and were reused, not rerun. Reference and final
candidate were therefore not interleaved in the last campaign. No confidence
interval is claimed.

| Mode | Observations ms | Median ms | CPU/GPU S |
| --- | --- | ---: | ---: |
| Reference CPU | 2660.746, 2666.578, 2631.304 | 2660.746 | — |
| Reference direct GPU | 71.443, 70.386, 70.307 | 70.386 | 37.8023x |
| Candidate CPU | 5283.606, 5424.718, 5239.292 | 5283.606 | — |
| Candidate direct GPU | 209.723, 211.755, 215.164 | 211.755 | 24.9515x |
| Candidate overlap GPU | 176.268, 173.292, 172.564 | 173.292 | **30.4896x** |

S is the ratio of CPU and GPU medians. The >30x median is not a guarantee that
all individual observations exceed 30x. Same-allocator overlap Q=0.8066; against
historical default-allocator reference S=41.9585x, Q=0.7267. These comparisons must
remain labeled. The original direct >30x and original Q>=0.95 gates remain unmet.

A separate final-evaluation run validates all 1024 retained members and compares
every ordered native AST JSON record against the prior window checkpoint; all
match. The extra final evaluation is excluded from timing observations. GPU
operator/rejection counters and inherited CPU/GPU fitness summaries remain
unchanged. No new rule difference survives; earlier canonical-binder, short-name
and stage-duplicate-production experiments remain reverted. The already accepted
migration difference (actual names/types rather than legacy numeric name-table
indices for compatibility) is not changed by this optimization.

Seven focused CUDA-build checks passed: donor generation, grammar preparation,
derivation resources, GPU transport, compiled GPU backend, all-eight-type payload
evolution, and CPU/GPU evolution parity. New checks cover private/public donor
agreement, unsealed and changed-payload certificate rejection, shared resource
proof concurrency, single-use overlap, exact offspring/counters, and revalidation
of a missing unused payload. A fresh CPU-only Release build and its evolution
pipeline/donor tests also passed. This is not yet the complete Goal 12 audit.

Source audit matched all 363 C++/CUDA/header/test/build/fixture paths to Snoopy.
The measured immutable candidate and rebuilt binary both have SHA-256
`10efeeff3f0628f858519db6c72c7c7725d0b31342414759cc97f6c5381c77dd`.
The allocator SHA-256 is
`572af05b75e2366a3e8c06c29d3a04c0a538c9635c9957f2808846730ab81da5`.
The archive audit verified 181 output/log receipts. Raw logs, preserved binaries,
runner scripts, source manifests/patches, allocator package and population
comparison are under local external `grammar-migration/target30-20260927` and
Snoopy's `/home/hschi1106/gagp-resume-a-20260927`. Final summary:
`target30-admission-summary.json`; evidence: `target30-admission-evidence.tar.gz`.

A final guard avoids speculative resource-proof work when subtree mutation is
fully disabled. The p1024 workload still takes the same proof path, but the
binary is different, so it has a separate bridge receipt rather than inheriting
the paired binary identity. The focused backend test passed. CPU/overlap GPU
was 5284.263/178.932 ms (29.5323x); this single bridge is not a new median and does
not establish >30x on the final revision. Final acceptance remains pending.
Final code SHA-256 is
`449ff9969cbf65406d094b9cb47d52539219e0843d705fbea34dc7c958c863c2`;
`guard-cpp-hashes.json` verifies all 363 source paths. Receipts are preserved in
`target30-proof-guard-tcmalloc/`. No noisy completed observation was excluded.

### Final guard measurements and clean-checkout diagnostic repair

The final guard binary's one-warmup/three-block follow-up is preserved in
`target30-guard-final-paired/`. CPU observations are 5216.690, 5200.009,
5317.230 ms; direct GPU 287.518, 204.885, 204.925 ms; overlap GPU 175.576,
176.147, 180.874 ms. The medians give 25.457x direct and 29.616x overlap.
The slower direct observation remains included. This final source has not yet
established >30x; the earlier 30.4896x remains an earlier-binary result.

A fresh Debug/CUDA checkout of `d21b062` on Snoopy passed 117/118 native tests.
The diagnostic-only `gagp_final_candidate_stats` still intercepted the old
overlap completion function and missed reproduction observations on the new
owned continuation. Its wrapper now captures the owned completion without
changing production evolution or benchmark timing. The existing failed test
then passed (all evaluation/reproduction modes, final evaluation on/off).
This is 118 distinct native checks covered across the original suite and focused
repair rerun, not a claim of a second complete suite. The tools suite passed
80 checks locally and all four initially skipped native integration checks
on the fresh remote build (84 total); repository checks passed 23/23.
Logs are in external `grammar-migration/goal12-audit-20260927` and remote
`/home/hschi1106/gagp-goal12-audit-20260927`.

### Retained parallel donor identity validation

The private owned packing continuation now computes every donor's full runtime
identity using up to 20 workers (minimum pool 128, chunks of 16), joins them,
and consumes matches/errors in original order. Public packing and enclosing
payload transactions stay sequential. No donor, constant, hash input, proposal,
validation rule, or GPU workload is removed. Additional scratch is one byte plus
one exception pointer per donor and worker-local identity strings.

The exact final production benchmark SHA-256 is
`75f53eaf85a69c24e959fa5b5de5f2d7f56e908330e1abcf32f9f06471e4281f`.
`target30-parallel-donor-paired/` records one warm-up and three rotated blocks:

| Mode | Three observations (ms) | Median (ms) | Speedup |
| --- | --- | ---: | ---: |
| CPU | 5385.446, 5245.560, 5213.354 | 5245.560 | — |
| GPU direct | 206.070, 205.081, 202.150 | 205.081 | 25.5780x |
| GPU overlap | 177.498, 169.709, 173.250 | 173.250 | **30.2773x** |

Each of the three same-block CPU/overlap pairs exceeds 30x. Samples remain small;
no confidence interval or guarantee for other workloads/hosts is implied. Versus
the preceding guard campaign, overlap median decreases 1.64%; CPU variation is
0.55%, and its implementation is unchanged by this patch. Original direct >30x
and original Q>=0.95 remain unmet. Same-allocator reference S=37.8023x gives
Q=0.8009; the historical default-allocator S=41.9585x gives Q=0.7216.

The four affected Release/CUDA checks pass (grammar reproduction preparation,
compiled transport, compiled backend, evolution CPU/GPU parity). Added cases
exercise >=128 donors, forged identities, exceptions from invalid donor nodes,
and enclosing payload scopes. Independent final-evaluation validation retains
all 1024 members, checks native membership/lowering and budget handling, and
compares complete ordered native AST records against the window checkpoint: equal.
Output SHA-256: `f401ece00609aca0c46c45ea4577ac66dcce3de37cd67fc3d5820dfa2a1c0525`.

`target30-parallel-donor-summary.json` records medians, hashes and receipts;
`parallel-donor-paired.py`, `parallel-donor-population.py`, and `audit-final.py`
reproduce the campaign/validation/audit using preserved inputs and binaries.
The previous guard campaign, exploratory trial, failed workflow probe and all
completed observations remain preserved. No new grammar restriction is retained.
