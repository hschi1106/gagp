# Stage A optimization resumed — 2026-09-27

Current status: active, incomplete. The user requested preservation and rollback
of Stage B, then continued optimization from Stage A. Q >= 0.95 is the restored
reference target; it is aspirational, but a result below it must not be declared
complete. There is no two-round optimization limit. CPU must not be deliberately
slowed and GPU workload must not be reduced. Any new grammar/semantic restriction
requires user agreement; regressions must be reverted.

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
