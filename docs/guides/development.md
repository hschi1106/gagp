# Development

## Build configurations

The default native build includes the product CLI and maintained test targets.
From the repository root:

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build -j
```

CUDA is enabled by default and configuration fails if its compiler cannot be
found. Select `-DGAGP_ENABLE_CUDA=OFF` for CPU-only use, or pass
`-DCMAKE_CUDA_COMPILER=/path/to/cuda/bin/nvcc` for a toolkit outside PATH. This
prevents a requested CUDA test build from silently losing its GPU targets.

Grammar identities use system OpenSSL Crypto for SHA-256 when CMake finds it.
OpenSSL is optional: without it, the built-in implementation is used. Configure
with `-DGAGP_ENABLE_OPENSSL_SHA256=OFF` to explicitly select the built-in path.
Both implementations produce identical grammar and cache identities; artifacts
do not depend on which implementation built them.

Install the dependency-free operational tools in a separate virtual
environment. This editable install exposes `gagp-tools`, including the thin
wrappers around the native grammar commands:

```bash
python3 -m venv .venv-tools
.venv-tools/bin/pip install -e tools
.venv-tools/bin/gagp-tools --help
```

Rebuild after changing C++ or CUDA. A grammar definition is JSON loaded at
process startup, so editing one under `configs/grammar/` or `/tmp` needs only a
fresh validate/generate/evolve command, not a rebuild or reinstall.

`cpp/CMakePresets.json` also defines `debug`, `release`, `cuda-parity`,
`sanitizer`, and `fuzz` configure presets. For example:

```bash
cmake --preset debug -S cpp
cmake --build cpp/build/debug -j
ctest --test-dir cpp/build/debug --output-on-failure
```

Benchmarks and experiment probes are excluded from the default graph. Enable
them intentionally:

```bash
cmake -S cpp -B cpp/build/aux \
  -DGAGP_BUILD_BENCHMARKS=ON \
  -DGAGP_BUILD_EXPERIMENTS=ON
cmake --build cpp/build/aux -j \
  --target gagp_runtime_multi_bench gagp_simple_exp_population_probe
```

The experiment probe requires CUDA. See the
[tooling reference](../reference/tooling.md) for ownership and support status.

## Test layers

Run the complete configured suite:

```bash
ctest --test-dir cpp/build --output-on-failure
```

Focused labels are available for `unit`, `contract`, `property`,
`cpu_gpu_parity`, `gpu`, and `tooling`:

```bash
ctest --test-dir cpp/build -L contract --output-on-failure
ctest --test-dir cpp/build -L property --output-on-failure
ctest --test-dir cpp/build -L cpu_gpu_parity --output-on-failure
```

The runtime semantic corpus is split into independently named scalar,
control-flow, builtin, typed-value, lexical/traversal, and bounded-region contracts.
Fixture data lives under `cpp/tests/fixtures/runtime/`; tests do not compile C++
sources ad hoc.

When Python 3 is available during CMake configuration, CTest also registers the
runtime-independent repository contract suite with the `tooling` label. It can
always be run directly, together with operational tool tests:

```bash
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
```

Neither suite imports a product/runtime Python package or needs `PYTHONPATH`.

### Sanitizers and fuzzing

The `sanitizer` preset is a CPU-only ASan/UBSan configuration:

```bash
cmake --preset sanitizer -S cpp
cmake --build cpp/build/sanitizer -j
ctest --test-dir cpp/build/sanitizer -L 'unit|contract|property' --output-on-failure
```

The Clang-only `fuzz` preset builds bytecode-JSON and AST-JSON/prefix-stream
libFuzzer targets. Copy the seed corpus before a campaign so minimized inputs do
not modify the source tree:

```bash
cmake --preset fuzz -S cpp
cmake --build cpp/build/fuzz -j \
  --target gagp_fuzz_bytecode_json gagp_fuzz_ast_verify
cp -a cpp/tests/fuzz/corpus/json /tmp/gagp-json-corpus
cpp/build/fuzz/gagp_fuzz_bytecode_json -runs=1000 /tmp/gagp-json-corpus
cpp/build/fuzz/gagp_fuzz_ast_verify -runs=1000 /tmp/gagp-json-corpus
```

Keep any smallest reproducer as a named corpus or deterministic property-test
regression.

## Spec freeze

`benchmarks/spec_freeze.json` records hashes of the normative spec set. A spec
change must update its conformance tests and this manifest in the same change:

```bash
python3 -m unittest tests.repository.test_spec_freeze -v
```

## GPU run policy

GPU-capable native paths select the least-used visible CUDA device. To force a
visible-device index:

```bash
GAGP_CUDA_DEVICE=0 cpp/build/gagp_evolve_cli \
  --cases data/fixtures/simple_exp_1024.json \
  --grammar-definition configs/grammar/benchmarks/simple_exp.json \
  --engine gpu --repro-backend gpu --repro-overlap on \
  --population-size 64 --generations 2
```

Use `nsys` only when profiling is needed; do not use `ncu` in this environment.

## Native CLI

[`../reference/cli.md`](../reference/cli.md) is the checked flag/default
reference for `gagp_evolve_cli`. The parser requires `--cases`; `--help` is not
a supported flag and exits through the unknown-argument error contract. Command
stdout and top-level JSON keys are locked by the native CLI contract test.

Compiled grammar definitions control generation and reproduction search space,
not execution of a materialized AST/bytecode program. Operational use and
offline release-1 conversion are in [grammar-config.md](grammar-config.md);
the authoring loop is in [grammar-authoring.md](grammar-authoring.md);
the production schema is normative in
[`../../spec/grammar_definition.md`](../../spec/grammar_definition.md).

## Related workflows

- [Benchmarking](benchmarking.md): fixed-population timing and canonical runs
- [PSB workflow](psb-workflow.md): datasets, regression, comparison, manifests
- [Experiment protocol](experiment-protocol.md): controlled formal studies
- [Operational tools](../../tools/README.md): unified commands and artifacts

## Contributor workflow

Start with the [repository map](../reference/repository-layout.md) and the
[architecture boundaries](../design/architecture.md). Public C++ headers belong
under `cpp/include/gagp`; implementation details stay with their owning module.
Python `gagp_tools` handles orchestration and reports, never runtime semantics.
Compatibility command wrappers are maintained entry points, not duplicate logic.

For a grammar-only change, copy a checked root, adjust its JSON, validate it and
run the authoring tutorial against matching cases. Add a new grammar package under
`configs/grammar/packages` only when its definitions are reusable; keep runnable
roots and cases under examples. Check generation, membership, variation and replay
for the new package without adding package-name branches to the native core.

For a primitive or behavior change:

1. Identify the owning specification and catalog entry; read the matching compiler,
   verifier and CPU/CUDA runtime callers before editing. The navigation routes in
   [the repository skill](../../.agents/skills/gagp-navigation/SKILL.md) map these owners.
2. Update types, scopes, error/fuel behavior and resource bounds together. Preserve
   reserved numeric holes and versioned artifact contracts; document any deliberate
   compatibility boundary in `VERSION.md`.
3. Add meaningful native contract tests and CPU/GPU parity coverage. Register new
   test sources in `cpp/CMakeLists.txt`; update operational tests only if tool flow
   changes. Keep test fixtures small and independent of downloaded datasets.
4. Run the affected checks, then the integration suite for cross-module changes.
   Update the owning spec (and spec-freeze hash when changed), design, user guide
   and navigation links only where their contract or ownership changed.

Use `feat:`, `fix:`, `refactor:`, `docs:`, `test:` or `chore:` commits. Explain the
observable change, validation and limitations in review. Do not commit generated
builds, dataset mirrors, virtual environments or raw run logs. Retain compact,
reviewed manifests and put unique raw evidence in a documented archive.
