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
