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
