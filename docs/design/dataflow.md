# Native Dataflow

## Command boundary

Evolution requires cases and a compiled definition:

```text
fitness-cases + grammar-definition-v2 + options
                    |
                    v
strict resolve / compile / exact case request
                    |
                    v
generate or replay grammar-population-v2
```

The CLI rejects release-1 `grammar-config`, `population-seeds`, AST, and
bytecode formats. Migration is an offline command before production execution.

## Evolution loop

```text
verified ast-prefix-v2 population
          |
          v
compile + bytecode verification
          |
          +----------------------+
          |                      |
          v                      v
 CPU fitness              GPU FitnessSession
          |                      |
          +----------+-----------+
                     v
          canonical fitness vector
                     |
                     v
             shared ranking
                     |
                     v
      CPU or GPU compiled reproduction
                     |
                     v
   verify + reconstruct accepted children
```

Initialization materializes from immutable compiled tables. Membership and
lowering run before a generated member is accepted. CPU and GPU evaluation
return the same population-shaped fitness vector. Ranking and statistics are
shared.

Reproduction reconstructs compiled witnesses, derives exact typed replacement
contracts, prepares compatible donors, performs typed crossover then optional
mutation, and certifies complete children. GPU reproduction moves these
contracts and candidate payloads to the device; host copyback still decodes and
certifies the result.

With GPU overlap enabled, immutable reproduction preparation may run while
fitness is in flight. Selection waits for the completed vector. Payload
lifetimes cover cases, active programs, donors, history, best, and optional
final results.

The evolution loop's private overlap continuation owns the same immutable
population read by evaluation. It consumes completed fitness once and keeps
prepared state private. A changed source payload snapshot triggers fresh
preparation; caller-supplied prepared replay still performs full replay checks.
Preparation, joining, and destruction of the owned population remain inside
the complete-generation timing boundary.

## Artifact flow

```text
grammar-definition-v2 + fitness-cases
  -> grammar-generated-v2 / grammar-population-v2
  -> ast-prefix-v2
  -> bytecode-json-v2
  -> CPU or GPU execution
```

Same-version population replay checks grammar and generation identity. A
materialized member can execute without the definition file, but reproduction
requires the active compiled definition and successful membership
reconstruction.

Release-1 conversion is separate:

```text
grammar-config + exact cases + constrained-intent-v1
  -> lossy search-space migration -> grammar-definition-v2

release-1 ast-prefix + exact cases + explicit limits
  -> exact program migration -> grammar-materialized-v2

complete grammar-generated-v1 member
  -> embedded-contract migration -> grammar-materialized-v2

grammar-population-v1
  -> split complete members -> migrate each member independently

population-seeds
  -> frozen release-1 materialization -> AST migration
```

Both exact program routes record `gagp-native-2.0.0`, keep the v2 AST constant
table empty, and carry one detached lossless constant/payload pool. There is no
release-1 bytecode route. Recompile the migrated AST.

Operational Python tools orchestrate datasets and reports; they do not
implement grammar, AST, runtime, or reproduction semantics. Generated raw runs
stay under artifact directories, and only reviewed compact manifests belong in
`benchmarks/`.

### Bounded GPU evaluation compilation

GPU evaluation prepares bytecode in source-ordered batches of at most 128 members,
using up to 20 workers, capped by hardware concurrency, when the population has at least 32 members. Each worker
captures the runtime identity and compilation under one payload read snapshot.
The operation reuses one worker team across batches. After each batch barrier,
all snapshots must validate atomically before cache/results
are published in population order. Conflicting reads fall back to sequential
preparation; enclosing payload scopes and small populations do not spawn workers.
Duplicate cache keys retain the first published bytecode without dropping any
population member or fitness work. CPU evaluation keeps sequential compilation.
Timing semantics are defined in [the timing reference](../reference/timing.md).

GPU reproduction also batches parent compaction and complete child admission.
Stable table compaction can transport already validated analyses; payload changes
or unavailable ownership proofs fall back to full analysis. See the
[GPU reproduction dataflow](gpu-reproduction.md) for ownership and publication
invariants. These schedules retain every population member and operator proposal.


### Reusable membership proofs and GPU packing

Successful membership reconstruction can retain an opaque immutable
`DerivationCertificate` (`grammar/membership.cpp`). Reuse compares the full
runtime identity, including decoded constant contents, compiled grammar hash,
and complete generation request. It returns the private verified witness and
exact scopes, never caller-editable provenance. Changes trigger full validation.
`GAGP_NO_DERIVATION_CERTIFICATES=1` provides a control path. The certificate does
not retain executable bytecode; ordinary generation compilation remains timed.

GPU host packing independently verifies every bounded program. For populations
of at least 32 it performs these checks on up to 20 workers and publishes errors
in original program order. `GAGP_SERIAL_PACK_VERIFY=1` selects serial checks.
The verifier supplies stack bounds for the execution storage choices described
in [payload execution profiles](payload.md#proven-integerlist-execution-profile).

The optional benchmark-only phase-bank prototype replaces AST evolution with
immutable compatible phase references. Its restricted search space, initialization
cost and export audit are described in the
[fixed benchmark guide](../guides/fixed-asgp-benchmark.md#optimization-and-diagnostic-probes).
It is not part of `evolve_population` or the default reproduction backend.
