# Grammar Config Guide

`grammar-config` restricts what evolution may generate or synthesize. It does
not change execution semantics and does not reject an already-materialized AST
or bytecode program solely because a construct is disabled. The complete schema
and validation rules are normative in
[`../../spec/grammar_config.md`](../../spec/grammar_config.md).

## Choose a checked preset

Presets live under `configs/grammar/`:

| Preset | Intended search space |
| --- | --- |
| `all.json` | All enabled source constructs |
| `scalar.json` | Numeric/boolean scalar programs |
| `string.json` | Scalar plus string-compatible constructs |
| `num_list.json` | Legacy numeric-list comparison profile, translated on load |
| `string_list.json` | Scalar plus string/string-list constructs |
| `sequence.json` | Broad sequence profile |

Pass one to the native CLI:

```bash
cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-config configs/grammar/scalar.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --population-size 64 --generations 5
```

The selection affects initial generation, CPU mutation donors, GPU
reproduction candidate/donor preparation, and seed replay that regenerates
genomes. CPU/GPU runtime semantics and fitness remain unchanged.

## Compatibility profiles

Checked presets from the legacy search-space shape are accepted as comparison
inputs. The loader translates legacy numeric-list enablement into the explicit
typed-list search space and rejects unknown keys. This preserves benchmark
search-space comparability; it does not reintroduce legacy runtime values.

The tool package can derive deterministic profiles:

```bash
gagp-tools grammar profile \
  --profile compact \
  --base-grammar-config configs/grammar/num_list.json \
  --fixture-cases data/fixtures/psb1/count-odds.train.json \
  --out logs/count-odds.compact.json
```

- `compat` derives the native typed-list choices from the fixture schema.
- `compact` preserves the base profile's numeric-list search shape for direct
  comparisons and records its compatibility metadata.
- `full` emits the full native profile for experiments.

`gagp-tools psb run --profile compat|compact --base-grammar-config PATH`
creates per-problem configs under the run output directory and records base,
generated-config, and fixture-schema hashes.

## Seed replay

Population-seed artifacts should record the grammar-config path and hash. A
replay with recorded config identity requires matching content; an artifact
without config metadata uses the all-enabled default unless the caller supplies
`--grammar-config`.

Use the same cases, grammar config, limits, and population-seed file across
backend comparisons. The native CLI records the selected config identity in
its output JSON.

## Implementation and tests

- `cpp/include/gagp/evolution/grammar_config.hpp` and
  `cpp/src/evolution/grammar_config.cpp`: native configuration model/validation
- `cpp/src/evolution/genome_generation.cpp` and reproduction modules: search
  gating consumers
- `tools/gagp_tools/experiments/grammar_profiles.py`: profile generation
- `cpp/tests/evolution/test_genome_properties.cpp`: deterministic generation
  conformance
- `cpp/tests/evolution/test_repro_prep.cpp`: reproduction preprocessing
  conformance



## Staged typed-definition artifacts

The internal compiled-grammar C++ APIs generate typed programs and initial populations
with immutable derivation metadata. `encode_generated_artifact` records one program;
`encode_generated_population_artifact` records an initial population with exact
same-version replay checks. Both retain decoded payload contents rather than relying
on process-local registry entries. Their schema and limits are defined in
[`../../spec/grammar_definition.md`](../../spec/grammar_definition.md).

A generated single-program artifact can be evaluated with the existing CLI:

```bash
cpp/build/gagp_evolve_cli --cases cases.json --eval-ast-json generated.json --out-json result.json
```

The fixture must match the artifact's exact input schema and return type. The recorded
fuel is used automatically; if supplied, `--fuel` must agree. Evaluation can use the
materialized program even when its generator version is unavailable. Use the C++ replay
API when validating generation identity and provenance. The typed-definition generation
and population replay APIs are still staged; legacy `--grammar-config` evolution remains
the production workflow until grammar-aware reproduction is integrated.


Generate or replay a staged initial population with the native generation CLI:

```bash
cpp/build/gagp_generate_cli --grammar-definition grammar.json --cases cases.json --population-size 64 --seed 0 --out-json population.json
cpp/build/gagp_generate_cli --replay-json population.json --cases cases.json --out-json replayed.json
```

Supply `--grammar-definition grammar.json` during replay to require that exact resolved
grammar and import identity. A changed grammar fails before the output is replaced.
Population artifacts bundle complete single-program members; the one-AST evaluation
command above consumes one such member, not the population wrapper.


`grammar/config_adapter.hpp` provides the explicit C++ migration adapter
`convert_grammar_config`. Supply exact inputs, typed locals, owned constant domains,
new search/fuel limits, statement capacity and loop bounds in `GrammarConfigConversion`.
Then pass the returned resolved definition to `compile_grammar`. This maps supported
enabled operations without changing the legacy generator. Local initialization and
limit differences are specified in
[`../../spec/grammar_config.md`](../../spec/grammar_config.md#staged-explicit-typed-conversion).
Unsupported structured switches fail with an actionable diagnostic.


With `GAGP_BUILD_BENCHMARKS=ON`, the scalar initialization benchmark also emits the
resolved conversion used for its measurements:

```bash
cpp/build/gagp_grammar_initialization_bench --grammar-config configs/grammar/scalar.json --cases data/fixtures/simple_exp_1024.json --population-size 64 --seed 0 --warmups 3 --trials 15 --out-json logs/initialization.json --out-definition logs/converted-scalar.json
```

The benchmark explicitly chooses one initialized Int local, 80 full-prefix nodes,
depth 32, six statements per block, loop bounds 0..16, and fuel 20000. Scalar domains
include Int -8..8, both Bool values, and every Float thousandth from -8 through 8 plus
negative zero. The emitted definition records the actual search space. Setup timing
includes input loading, domain construction, conversion and compilation. Each raw
initialization sample includes generation, membership verification and lowering; output
serialization and summaries occur afterward. This is an early migration overhead
measurement, not a claim of identical old/new program distributions or a performance gate.
