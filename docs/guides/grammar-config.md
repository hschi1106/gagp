# Compiled Grammar and Migration Guide

Release 2 uses `grammar-definition-v2` as the only production search-space
format. Definitions control initial generation, membership, CPU/GPU donor
generation, crossover, mutation, and replay identity. They never change runtime
semantics for an already materialized program.

## Choose a checked definition

Checked roots live under `configs/grammar/`:

- `basic/`: concrete reusable building blocks;
- `packages/`: reusable templates and bounded-region productions;
- `compat/`: runnable v2 roots preserving supported release-1 search intent;
- `examples/authoring/`: scalar, typed-sequence, template, and memo starters;
- `examples/types/`: checked roots and cases for every public value type;
- `benchmarks/`: definitions tied to maintained benchmark fixtures;
- other `examples/`: custom definitions showing changed structure.

Paths and filenames do not enable constructs or participate in the grammar
hash. The resolved content, catalog/schema versions, exact types, domains,
templates, limits, and weights define behavior.

Run evolution with a definition:

```bash
cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --population-size 64 --generations 5
```

The production option is `--grammar-definition`; there is no
`--grammar-config` alias. The definition owns `search_limits.max_depth`,
`search_limits.max_nodes`, and `execution_limits.fuel`. Explicit matching
CLI values are accepted where documented; conflicting overrides fail.

## Generate and replay

```bash
cpp/build/gagp_generate_cli \
  --grammar-definition configs/grammar/examples/authoring/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --population-size 64 --seed 0 \
  --out-json /tmp/scalar.population-v2.json

cpp/build/gagp_generate_cli \
  --replay-json /tmp/scalar.population-v2.json \
  --grammar-definition configs/grammar/examples/authoring/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --out-json /tmp/scalar.replayed.population-v2.json
cmp /tmp/scalar.population-v2.json /tmp/scalar.replayed.population-v2.json
```

Generation emits `grammar-population-v2` containing complete
`grammar-generated-v2` members. Replay is same-version and checks the embedded
grammar/generator/RNG identity. Supplying `--grammar-definition` during replay
also requires that resolved grammar identity.

A generated or migrated single-program artifact may be evaluated with
`--eval-ast-json`. Evaluation checks the case schema and uses the artifact's
recorded fuel. Materialized execution establishes native validity; it does not
claim original seed provenance.

## Offline release-1 migration

The production CLI rejects `format_version=grammar-config`. Convert its enabled
ordinary-operation intent with the migration executable using the exact
fitness-case schema and the explicit constrained profile. The converter emits
`grammar-definition-v2`.

```bash
.venv-tools/bin/gagp-tools grammar migrate \
  --input configs/grammar/migration/v1/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --conversion-profile constrained-intent-v1 \
  --out /tmp/scalar.migrated-v2.json
```

The profile is required because release-1 configs do not contain exact
constant domains, execution fuel, or a typed assignment environment. It uses
the case schema, canonical finite v2 domains, and fuel 1,000,000. This is a
deterministic but explicitly lossy search-space conversion: it preserves the
ordinary enable/disable intent that can be represented by the fixed v2 schema,
but it does not preserve the complete release-1 program set, dynamic assignment
environment, literal domains, or seed replay. Use materialized migration when
exact old values and behavior are required.

Conversion reads enablement from document contents and never infers “full”
behavior from `all.json` or any other filename. An enabled legacy structured
switch is rejected because its boolean does not encode the typed holes, phase
scopes, or dependency choices needed for an exact v2 definition; select the
corresponding general package or migrate materialized programs instead.
Unsupported or ambiguous inputs fail.

Release-1 materialized `ast-prefix` programs can be converted to
`grammar-materialized-v2` with exact cases and explicit fuel/node/depth limits.
A complete `grammar-generated-v1` member already embeds its contract and is
converted without `--cases` or overrides. Both routes emit a target semantic
version of `gagp-native-2.0.0`, an empty `ast-prefix-v2` constant table, and
exactly one separate lossless constant pool. The generated-member route validates
the release-1 semantic/generator/RNG identities and uses the embedded schema,
limits, fuel, AST shape, and detached constants; it does not rerun the old
generator. The result executes without the authoring package tree. A
`grammar-population-v1` container is rejected and must be split into complete
members, each migrated independently.
Release-1 bytecode cannot be converted: migrate the source AST and recompile it.

A `population-seeds` artifact is also insufficient for exact migration.
Materialize each member with the frozen release-1 build, then migrate those
ASTs. Do not compare new seed regeneration as exact replay.

## Compatibility packages

Compatibility packages may retain names such as linear recurrence, DC, DP1D,
or DP2D to state the search intent they reproduce. They contain ordinary
release-2 definitions and materialize only general lexical/traversal/bounded
AST forms. Copying or renaming a package without changing resolved content does
not change grammar identity.

The exact schema, artifact contracts, and structural forms are normative in
[grammar_definition.md](../../spec/grammar_definition.md). The legacy
conversion boundary is normative in
[grammar_config.md](../../spec/grammar_config.md).

The [grammar authoring guide](grammar-authoring.md) provides complete scalar,
typed-sequence, template/package, and memo workflows; exact type conversion;
authoring versus execution bounds; and validate/inspect/resolve commands.
