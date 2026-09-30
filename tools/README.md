# GAGP Operational Toolchain

The tools are a standard-library Python package organized by responsibility:

```text
PSB upstream JSONL
  -> psb fetch
  -> psb convert / psb materialize
  -> fitness-cases fixtures and support manifests
  -> authored grammar-definition-v2 search space
  -> benchmark population (for fixed-population runs)
  -> psb run -> gagp_evolve_cli
  -> psb compare
  -> report psb-manifest / report simple-manifest
  -> reviewed compact evidence under benchmarks/
```

Install an editable command in an isolated environment:

```bash
python3 -m venv .venv-tools
.venv-tools/bin/pip install -e tools
.venv-tools/bin/gagp-tools --help
```

Retained `tools/*.py` paths are thin wrappers around the same package functions
used by the unified command. The explicitly named legacy grammar-config wrapper
is migration-only.

## Grammar commands

The grammar group copies checked starters and delegates semantic work to the
native binaries; Python does not parse or compile definitions:

```bash
.venv-tools/bin/gagp-tools grammar init \
  --example scalar --out /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar validate \
  --grammar-definition /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar inspect \
  --grammar-definition /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar resolve \
  --grammar-definition /tmp/my-grammar.json \
  --out /tmp/my-grammar.resolved.json
.venv-tools/bin/gagp-tools grammar migrate \
  --input configs/grammar/migration/v1/scalar.json \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --conversion-profile constrained-intent-v1 \
  --out /tmp/scalar.migrated-v2.json
```

`init` accepts `scalar`, `sequence`, `template`, and `memo` and never
overwrites an existing path. Pass `--native PATH` to the other commands when
using a build directory other than `cpp/build`. Editing JSON needs no rebuild;
rerun validate and generate/evolve in a new process. See the
[authoring guide](../docs/guides/grammar-authoring.md) for complete workflows
and the migration boundary.

## Dataset commands

```bash
.venv-tools/bin/gagp-tools psb fetch --suite psb1 --problems count-odds --dry-run
.venv-tools/bin/gagp-tools psb convert --suite psb1 --problem count-odds --out /tmp/count-odds.train.json
.venv-tools/bin/gagp-tools psb materialize --suite psb1 --datasets-root data/psb1_datasets --out-dir /tmp/psb1
```

Dataset commands own acquisition and conversion only. They emit typed
`fitness-cases` fixtures; they do not implement runtime semantics.

## Experiment commands

The `benchmark fixed-asgp prepare` and `benchmark fixed-asgp run` commands implement
the [fixed ASGP/GAGP daily and scaling workflow](../docs/guides/fixed-asgp-benchmark.md).
They require the optional native benchmark target and an external ASGP checkout.

```bash
mkdir -p logs
.venv-tools/bin/gagp-tools benchmark population \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --cases data/fixtures/simple_exp_1024.json \
  --population-size 1024 --seed 0 \
  --out logs/fixed_population.json
.venv-tools/bin/gagp-tools grammar migrate \
  --input configs/grammar/migration/v1/sequence.json \
  --cases data/fixtures/psb1/count-odds.train.json \
  --conversion-profile constrained-intent-v1 \
  --out /tmp/count-odds-v2.json
.venv-tools/bin/gagp-tools psb run \
  --suite psb1 --cases-root data/fixtures/psb1 \
  --problems count-odds --seeds 0 \
  --grammar-definition /tmp/count-odds-v2.json \
  --out-dir /tmp/gagp-psb-smoke --dry-run
```

Experiment execution records cases/config hashes, seeds, native binary path,
backend choices, and run status needed for replay.

## Report commands

```bash
.venv-tools/bin/gagp-tools psb compare --baseline baseline.json --candidate candidate.json
.venv-tools/bin/gagp-tools report psb-manifest --help
.venv-tools/bin/gagp-tools report simple-manifest --help
```

Comparisons reject incompatible run metadata before computing ratios. Report
commands compact reviewed raw runs into versioned evidence manifests.

## Artifact policy

- `data/psb*_datasets/`: mirrored upstream source data
- `data/fixtures/`: materialized, runtime-consumable fixtures
- `logs/`: ignored raw runs, generated configs, and local reports
- `benchmarks/`: intentionally committed compact evidence and freeze manifests
- `configs/`: versioned input policies, not generated run output

Run the independent suite without the retired runtime package:

```bash
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
```
