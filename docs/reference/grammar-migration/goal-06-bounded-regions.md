# Bounded-region implementation evidence

Status: Goal 06 CPU implementation is complete. Tests ran on the implementation
working tree based on parent `d61fcf9154c840bee98c1e440bcc1c50cec3acfa` before its
implementation commit. This is an intermediate evidence record,
not final migration acceptance. The artifact root is
`/home/hschi1106/gagp-artifacts/grammar-migration`; frozen earlier builds remain
unchanged. Current builds use separate `goal-06-*` roots.

## Implemented behavior

One typed descriptor supports proper sequence windows and checked monotone
integer-coordinate requests, optional sparse memoization, ordered phases, explicit
captures, and serialized execution limits and fuel charges. CPU execution uses
iterative frames. Compiler, serialization, structural grammar generation,
membership, variation, and analysis retain descriptor ownership and phase scopes.
The general evaluator does not dispatch to legacy ASGP implementations.

The standalone `bounded_sequence.json` and `bounded_memo.json` grammar definitions
exercise three-way sequence decomposition and custom two-coordinate dependencies.
Generation and materialized replay artifacts are retained under the artifact root.
Generic GPU execution remains explicitly rejected pending Goal 07.

`CpuExecutionSession` owns an immutable program snapshot, validates segments lazily,
and reuses frame, memo, and phase-binding storage across cases. CPU fitness uses one
session per bounded program. Tests cover capture changes, errors, snapshot ownership,
unreachable invalid segments, multiple segments, fuel, and scratch reset.

## Current correctness evidence

| Check | Result | Artifact |
| --- | --- | --- |
| CPU Debug build and native suite | 98/98, 5.98 s | `goal-06-current-cpu-02.log` |
| Targeted sanitizer suite | 27/27, 5.11 s | `goal-06-current-sanitizer-tests-01.log` |
| Full CUDA-enabled native suite | 116/116, 46.35 s, no skips | `goal-06-final-gpu-tests-01.log` |
| Operational Python tests | 53/53 | `goal-06-current-tools-01.log` |
| Repository checks | 17/17 | `goal-06-current-repository-04.log` |
| Frozen bytecode CPU comparison | 195 translated rows, 30,053 fuel comparisons | `goal-06-bytecode-capacity-02.json` |
| Frozen GPU limits evaluated on bounded CPU regions | 33 rows, 104 checkpoints, 11,885 fuel comparisons | `goal-06-bytecode-capacity-02.json` |

The frozen checker consumes all 186 r64 oracle rows and 15 named error-order probes.
It checks 1,584 archived checkpoints including source observations. Six legacy
mixed/wrong-output probes are explicitly rejected by exact phase typing and are
**not** counted as translated execution parity. Outside-domain base probes retain
boundary-before-base semantics; only a verification copy normalizes that legacy
metadata defect before checking remaining source structure.

The exact-type verifier recognizes error-only paths, including an adjacent literal
zero divisor with no jump entering the arithmetic instruction. It preserves code
and fuel charges. Tests reject unsound proofs when any jump form bypasses the zero.
The capacity comparison selects frozen device ordinals 55–87 with 64 DC frames,
128 DP frames, and 128 memo cells. It reproduces 19 scalar Int successes and 14
capacity timeouts at the frozen 20,000-fuel cap. The checker rejects altered fuel
settings. Internal GPU typed-list slice fallback is not claimed as exact payload
parity, and this comparison does not execute the current GPU backend.

The manifest records inputs, report, and separately archived candidate binary,
cache, and source snapshot: `goal-06-bytecode-capacity-02.manifest.json`. Reproduce
from the archived source root using `goal-06-bytecode-capacity-checker-02` with
`boundary-oracle-r64/capture-0.jsonl`, an output path, and
`gpu-boundary-results-01/capture-0.jsonl` as its three arguments. Paths in this
command are relative to the artifact root; the process working directory must
remain the source root for the tracked named oracle.

## Exact-type compatibility boundary

The six raw-bytecode probes below pass the legacy structural bytecode verifier
but violate the existing normative ASGP phase signatures in
[`spec/grammar.md`](../../../spec/grammar.md). DC requires an Int divide result
and one common solve/combine type. DP requires one common boundary, solve,
dependency and transition type. The native typed verifier and general descriptor
enforce those signatures; the legacy raw interpreter checks some tags later.

| Frozen ordinal | Legacy runtime TypeError | First non-Timeout fuel | General descriptor rejection |
| --- | --- | --- | --- |
| 3 | DC divide returns Float | 5 | Preparation must return Int |
| 4 | DC children return Int and Bool | 19 | Base body has incompatible output paths |
| 11 | DP1 transition returns Bool for Int dependencies | 8 | Combine must return Int |
| 12 | DP1 base returns Bool, boundary returns Int | 7 | Base body must return Int |
| 19 | DP2 transition returns Bool for Int dependencies | 12 | Combine must return Int |
| 20 | DP2 base returns Bool, boundary returns Int | 10 | Base body must return Int |

All six remain in the checker: it replays their legacy observations at the frozen
fuel/cap, threshold, and one below threshold, then requires an explicit lowered
descriptor rejection. This is a raw-bytecode compatibility limitation, not runtime
parity. These phase bodies cannot be authored under the normative typed grammar.
In particular, ordinals 12 and 20 let a wrong-tag base result survive while another
sibling runs. Reproducing that ordering would require deferring exact child-type
checks, contradicting the descriptor's exact result contract. The implementation
keeps that contract and reports the discrepancy instead of accepting heterogeneous
recursive results or rewriting individual fixtures into constant failures.

Valid typed error paths remain execution comparisons, including lazy unset captures,
division by zero, source/argument type guards, ordered child failures, and exhaustion.
The typed transition matrix covers all four sequence source types, all eight result
types, and supported DP dependency patterns. Future package/cutover coverage must
retain this explicit limitation rather than claiming all legacy raw programs match.

## Timing and remaining acceptance

Final seven-sample Release diagnostics in `goal-06-final-timing-01.json` measured
the two verified frozen native DC fixtures, with 10,000 iterations per sample.
Warm general sessions took 808/843 ns versus 807/820 ns for legacy sessions.
General cold construction and first execution took 7,750/7,685 ns versus
1,155/1,184 ns; one-shot calls took 6,543/6,410 ns versus 807/817 ns. The general session
retained 1,056 bytes of frame/memo backing per fixture, excluding its program,
verifier, bindings, and payload registry. These figures are diagnostic and do not
satisfy the frozen statistical performance gate. Both cold overhead and possible
warm regressions require the prescribed paired measurements and repair in Goal 11.

The final Debug deep-chain probe visited 100,001 frames, retained 26,400,264 bytes
of frame storage, and reached 36,992 KiB process RSS in 0.06 s
(`goal-06-final-engine-memory-01.log`). RSS includes the test process and temporary
vector growth; retained frame storage is a separate measurement.

Goal 06's substrate and CPU checks are complete with the compatibility boundary
above recorded. General GPU execution belongs to Goal 07. Cold-path performance
regressions and formal statistical acceptance remain mandatory Goal 11 work;
integration/cutover and clean-checkout gates remain later goals. No complete
performance or GPU generic-runtime parity claim follows from these results.
