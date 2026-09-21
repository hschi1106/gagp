# Documentation

This index is the entry point for maintained GAGP project documentation. Each fact
has one owner: specifications define behavior, design documents explain the
implementation, guides describe workflows, and references record checked
interfaces. Documents may link to another layer but must not restate its
contract as a second source of truth.

## Ownership

| Document | Owns |
| --- | --- |
| [`../spec/README.md`](../spec/README.md) | Normative language, bytecode, builtin, fitness, fixture, compiled-definition, and migration behavior |
| [`design/architecture.md`](design/architecture.md) | Native component boundaries and invariants |
| [`design/dataflow.md`](design/dataflow.md) | End-to-end execution and evolution dataflow |
| [`design/payload.md`](design/payload.md) | Host/device payload transport implementation |
| [`design/gpu-reproduction.md`](design/gpu-reproduction.md) | GPU reproduction pipeline and overlap design |
| [`guides/development.md`](guides/development.md) | Build, test, CLI, and local development workflows |
| [`guides/benchmarking.md`](guides/benchmarking.md) | Fixed-population timing and canonical native run workflows |
| [`guides/psb-workflow.md`](guides/psb-workflow.md) | PSB dataset, regression, comparison, and manifest workflows |
| [`guides/grammar-config.md`](guides/grammar-config.md) | Compiled grammar use, artifacts, and offline release-1 migration |
| [`guides/experiment-protocol.md`](guides/experiment-protocol.md) | Canonical experiment protocol and reporting constraints |
| [`reference/timing.md`](reference/timing.md) | Timing field names, scopes, and output mapping |
| [`reference/grammar-migration/README.md`](reference/grammar-migration/README.md) | In-progress grammar migration baseline capture and execution evidence |
| [`reference/grammar-migration/goal-03-initialization.md`](reference/grammar-migration/goal-03-initialization.md) | Typed grammar initialization timing, setup costs and comparison limitations |
| [`reference/grammar-migration/goal-04-variation.md`](reference/grammar-migration/goal-04-variation.md) | Compiled variation contracts, shared preparation, rejection accounting and validation |
| [`reference/grammar-migration/goal-06-bounded-regions.md`](reference/grammar-migration/goal-06-bounded-regions.md) | Bounded CPU regions, frozen-oracle comparisons, timing evidence and remaining acceptance |
| [`reference/grammar-migration/goal-08-compatibility-packages.md`](reference/grammar-migration/goal-08-compatibility-packages.md) | User-authored compatibility packages, typed variant validation, transition size evidence and current acceptance limits |
| [`reference/grammar-migration/goal-09-cutover.md`](reference/grammar-migration/goal-09-cutover.md) | Release-2 production cutover, version boundary, and documentation validation record |
| [`reference/grammar-migration/semantic-coverage.md`](reference/grammar-migration/semantic-coverage.md) | Semantic baseline evidence mapped to requirements and remaining gaps |
| [`reference/cli.md`](reference/cli.md) | Mechanically checked native CLI flags and defaults |
| [`reference/tooling.md`](reference/tooling.md) | Maintained tool and auxiliary executable ownership |
| [`reference/repository-layout.md`](reference/repository-layout.md) | Mechanically checked stable repository paths and roles |
| [`refactor/README.md`](refactor/README.md) | Evidence retained for the Python-retirement/native-verifier refactor |
| [`../tools/README.md`](../tools/README.md) | Operational tool commands and artifact lifecycle |
| [`../benchmarks/README.md`](../benchmarks/README.md) | Committed benchmark-manifest provenance and interpretation |

`README.md` at the repository root remains the product entry point;
`AGENTS.md` contains contributor constraints; `VERSION.md` contains release and
compatibility history. Those files link here rather than duplicating this map.

## Editing rules

- Put normative semantics only in `spec/` and update the spec-freeze manifest
  when a frozen specification changes.
- Put implementation rationale and module boundaries in `design/`.
- Put commands and reproducible procedures in `guides/` or the owning
  subsystem README.
- Put generated or mechanically checked names and layouts in `reference/`.
- Keep refactor evidence historical; do not turn it into active architecture
  documentation.
- Run the repository documentation checks after moving or linking files:

  ```bash
  python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
  ```
