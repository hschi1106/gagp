# Goal 04: compiled grammar variation

Goal 04 adds constrained CPU reproduction and shared host preparation. The public
operator names remain `crossover` and `mutate`; compiled mode is selected internally
through an owned immutable grammar and optional generation request in `EvolutionConfig`.
The legacy path remains available until the production cutover. General lexical and
structured execution is implemented in subsequent goals; unsupported bound/structured
productions still fail executable preflight rather than entering variation unsafely.

## Contracts and acceptance

| Requirement | Implementation and evidence |
| --- | --- |
| Imported AST membership | Independent deterministic witness reconstruction ignores supplied provenance; witness, reproduction and fallback tests reject nonmembers before population admission. |
| Exact replacement contracts | Nonterminal/type/category/context, template slot and exact visible/native environments enter a length-framed key. Site tests and 64-seed crossover tests reject same-type/different-rule substitutions. |
| Fixed skeleton and shared holes | Witness choices identify admitted boundaries and group physical copies. Atomic insertion replaces every copy. Tests cover nested/forwarded holes, fixed constants and whole admitted templates. |
| Scope and budgets | Opt-in native scope annotations and normalized available/free local names; exact compatibility plus independent node/depth/template allowances. Tests cover branch/loop availability, tight budgets and asymmetric physical template nesting. |
| Contextual mutation | Isolated frames supply declared available locals as explicit donor inputs; contextual minimum costs exclude unavailable alternatives. Every complete child is then certified under original grammar inputs. |
| Constant domains | Mutation resamples the reconstructed selected domain, including full `int64` ranges and all eight value types. Shared copies change together; fixed constants remain excluded. |
| Metadata and compaction | Owned witness metadata is not original seed replay provenance. Compaction preserves semantics; analysis identities cover grammar, AST/value contents, request environment and limits. |
| Selection and order | CPU backend shares one context per generation, validates every parent, preserves tournament/shuffle/seed flow and applies crossover before child mutation. A separate manual replay test verifies this sequence. |
| Shared preparation | Logical-site sampling, dense compatibility IDs, flattened occurrence groups, per-site donor ranges and explicit materialized measures. Host tests verify IDs, payload/table round trips, ownership and deterministic offsets. |
| Safe packing | Population/donor identities and prepared limits reject stale data; payload capacities grow after prescan, without truncation. Actual candidate counts and invalid padding avoid duplicating logical candidates. |
| Device staging | Explicit compiled mode is rejected before legacy GPU allocation, upload, launch, copyback, decode or overlap. CPU and CUDA-conditional guard tests exercise empty/null entry paths. |
| Rejection/fallback accounting | Nine integer counters propagate into reproduction timing and CLI JSON/stdout. Dedicated tests force generation and acceptance failures and verify certified fallback parents. |

Full-environment equality deliberately rejects some substitutions that could be legal
with a more permissive free-variable analysis. Complete Program and Expression boundaries
are supported; independent Block/Statement donors are not introduced. Current native
locals have unique declared names. General immutable lexical binder IDs and dependency
arity changes remain behind the later general-runtime executable gate; this checkpoint
does not claim those runtimes are complete.

Counters classify operator outputs, not final population members or diversity. A pair
produces two classified crossover outputs even if an odd population discards one, and
mutation classifies its output separately. Pair contract/budget rejection counts reflect
candidate enumeration. Fallback children are a subset of unchanged outputs. Actual-change
comparison resolves names and constant values, ignoring table sharing and unused entries.

## Validation

Artifact root: `/home/hschi1106/gagp-artifacts/grammar-migration`.

- Final Debug CUDA build with benchmarks enabled: **73/73**, no skips, 50.44 seconds.
  Logs: `goal-04-final-build-02.log`, `goal-04-final-tests-02.log`; configuration in
  `goal-04-final-configure.log`. Tests ran with `GAGP_CUDA_DEVICE=1`.
- CPU-only checkpoint: **55/55**, followed by the added fallback test **1/1**.
  Logs: `goal-04-shared-tests.log`, `goal-04-fallback-tests.log`.
  The final CUDA suite also includes the fallback and final packing checks.
- Operational tools: **53/53**, `goal-04-shared-tools-02.log`.
- Repository checks: **17/17**, `goal-04-final-repository-02.log`.
- Diagnostic probe compatibility with all six frozen original source units:
  `goal-04-frozen-probe-compat/manifest.json`. Frozen sources/builds were not modified.

The first benchmark-enabled CUDA run passed 69/72; three direct oracle tests failed
with out-of-memory errors on device 0. At inspection, device 0 had about 4.9 GiB free
and device 1 about 11.1 GiB. The same oracle binaries passed 3/3 on device 1
(`goal-04-gpu-oracles-device1.log`), followed by the successful complete final run.
The original failure is retained in `goal-04-gpu-bench-tests.log`.

A diagnostic tool test also caught a changed legacy branch anchor after compiled backend
integration. The probe now accepts the two explicit historical/current forms and still
rejects missing or ambiguous matches. It does not rewrite the frozen baseline or broaden
the probe into an unbounded textual match.

These checks establish correctness for this staged contract. They are not the final
performance acceptance gate. Goal 11 must measure preprocessing and total reproduction
costs, absolute CPU/GPU times, speedup and evolutionary progress on the frozen workloads.
