# GAGP

<p align="center">
  <strong>GPU-Accelerated Genetic Programming for Program Synthesis</strong>
</p>

<p align="center">
  <a href="VERSION.md"><img alt="Release 2.0.0" src="https://img.shields.io/badge/release-2.0.0-2563eb"></a>
  <img alt="C++ 17" src="https://img.shields.io/badge/C%2B%2B-17-00599C?logo=cplusplus&amp;logoColor=white">
  <img alt="CUDA accelerated" src="https://img.shields.io/badge/CUDA-accelerated-76B900?logo=nvidia&amp;logoColor=white">
  <img alt="CPU and GPU parity" src="https://img.shields.io/badge/parity-CPU%20%2B%20GPU-7c3aed">
</p>

GAGP evolves typed prefix-AST programs, compiles them to a compact bytecode,
and evaluates entire populations on either a native CPU runtime or CUDA. It
combines GPU-accelerated fitness and reproduction with a verified, contract-led
program-synthesis pipeline.

> One semantic contract, two execution backends: the CPU path anchors correctness
> while CUDA accelerates population-scale search.

## Why GAGP?

- **GPU-first search** — CUDA backends accelerate both population fitness and
  reproduction, with optional preparation/evaluation overlap.
- **Correctness by construction** — typed generation, structural verification,
  bytecode verification, and dedicated CPU/GPU parity tests guard every trust
  boundary.
- **Reproducible experiments** — fixed populations, deterministic seeds,
  compiled grammar definitions, timing output, and compact benchmark manifests support fair
  comparisons.
- **A lean native core** — language semantics, compilation, evolution, and
  execution live in C++/CUDA. Python is limited to independent dataset and
  experiment tooling.
- **Contract-led development** — grammar, bytecode, builtins, fitness, fixtures,
  and search-space configuration are defined in release-governed specifications.

## Capabilities

| Capability | CPU | CUDA GPU |
| --- | :---: | :---: |
| Bytecode execution and fitness | ✓ | ✓ |
| Population evaluation | ✓ | ✓ |
| Tournament selection and reproduction | ✓ | ✓ |
| Typed-subtree crossover and mutation | ✓ | ✓ |
| Reproduction preparation/evaluation overlap | — | ✓ |
| One-AST evaluation command | ✓ | — |

CUDA execution supports the documented device payload limits and deterministic
fallback behavior. The exact public contracts live in the
[specification index](spec/README.md).

## Quick start

### Requirements

- CMake 3.16 or newer
- A C++17 compiler
- An NVIDIA CUDA toolkit and compatible GPU for CUDA backends
- Python 3.10 or newer only for operational tools and repository checks

### First CPU run

```bash
git clone https://github.com/hschi1106/gagp.git
cd gagp
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=OFF
cmake --build cpp/build -j 4 --target gagp_evolve_cli
cpp/build/gagp_evolve_cli \
  --cases configs/grammar/examples/authoring/scalar.cases.json \
  --grammar-definition configs/grammar/examples/authoring/scalar.json \
  --engine cpu --repro-backend cpu --repro-overlap off \
  --population-size 16 --generations 2 --seed 7 --show-program ast
```

Successful execution prints generation fitness statistics and the selected AST.
This small example checks the workflow; it does not guarantee a solved problem.
For a complete **install → customize grammar → run → inspect → export/replay**
walkthrough, follow [Getting started](docs/guides/getting-started.md).

### CUDA route

Use a separate build with `-DGAGP_ENABLE_CUDA=ON` and the correct
`-DCMAKE_CUDA_ARCHITECTURES` for your GPU (86 for RTX 3090; 89 for RTX 4090).
CMake must detect the CUDA compiler. Then select
`--engine gpu --repro-backend gpu --repro-overlap on`.
The [CUDA tutorial](docs/guides/getting-started.md#run-on-cuda-optional) includes
complete build/run commands and all four backend combinations.
GPU paths choose the least-used visible device; `GAGP_CUDA_DEVICE=0` pins visible
device 0. Linux CPU/CUDA workflows are verified; other platforms are not part of
the release validation. See [Troubleshooting](docs/guides/troubleshooting.md) for
setup errors and [Development](docs/guides/development.md) for full test builds.

## How it works

```mermaid
flowchart TB
    A[Cases · compiled grammar definition · seed]
    A --> B[Prepare typed AST population]
    B --> C[Verify + compile to bytecode]
    C --> D[CPU or CUDA fitness]
    D --> E[Canonical ranking]
    E --> F[CPU or CUDA reproduction]
    F --> G[Verify next generation]
    G --> C
```

CPU and GPU evaluation converge on the same fitness-vector boundary before
ranking. Selecting a GPU backend changes execution and scheduling, not the
language or operator contract. See the [architecture](docs/design/architecture.md)
and [native dataflow](docs/design/dataflow.md) for the full model.

## Operational tools

The dependency-free Python package handles PSB datasets, fixture conversion,
v2 population materialization, regression runs, comparisons, and manifests. It does not
implement product runtime semantics.

```bash
python3 -m venv .venv-tools
.venv-tools/bin/pip install -e tools
.venv-tools/bin/gagp-tools --help
.venv-tools/bin/gagp-tools grammar init --example scalar --out /tmp/my-grammar.json
.venv-tools/bin/gagp-tools grammar validate --grammar-definition /tmp/my-grammar.json
```

Edit the copied JSON and rerun `grammar validate`; grammar files are loaded at
process startup, so JSON-only changes do not require a C++ rebuild. The checked
scalar, typed-sequence, template, and memo examples are documented in the
[grammar authoring guide](docs/guides/grammar-authoring.md).

Follow the complete artifact pipeline in the
[operational tools guide](tools/README.md).

## Documentation

| Start here | What it covers |
| --- | --- |
| [Getting started](docs/guides/getting-started.md) | CPU/CUDA setup, complete custom-grammar tutorial, results and replay |
| [Troubleshooting](docs/guides/troubleshooting.md) | Common setup, grammar, artifact and GPU errors |
| [Specifications](spec/README.md) | Normative grammar, bytecode, builtin, fitness, fixture, compiled-definition, and migration contracts |
| [Architecture](docs/design/architecture.md) | Native components, dependency direction, and stable invariants |
| [Dataflow](docs/design/dataflow.md) | End-to-end execution, evolution, and artifact flow |
| [Development](docs/guides/development.md) | Builds, presets, tests, sanitizers, fuzzing, and GPU policy |
| [CLI reference](docs/reference/cli.md) | Mechanically checked flags and defaults |
| [Benchmarking](docs/guides/benchmarking.md) | Reproducible fixed-population CPU/GPU comparisons |
| [PSB workflow](docs/guides/psb-workflow.md) | Dataset acquisition, fixtures, regression, and reports |
| [Grammar definitions and migration](docs/guides/grammar-config.md) | Compiled search spaces, artifacts, and offline v1 migration |
| [Grammar authoring](docs/guides/grammar-authoring.md) | Start, validate, inspect, resolve, customize, and run a definition |
| [Implementation and performance report](docs/reference/custom-grammar-implementation-report.md) | Verified grammar migration, retained optimizations, p1024 speedup, accepted results and measured limitations |
| [Documentation index](docs/README.md) | Ownership of every maintained document |

## Repository layout

```text
gagp/
├── cpp/
│   ├── include/gagp/     Public C++ interfaces
│   ├── src/runtime/      CPU VM, CUDA runtime, and payload transport
│   ├── src/evolution/    Compiler, generation, operators, and evolution loop
│   ├── src/cli/          Native command-line entry points
│   └── tests/            Unit, contract, property, fuzz, and parity tests
├── spec/                 Normative behavioral contracts
├── docs/                 Design, guides, references, and refactor evidence
├── tools/                Independent operational Python package
├── tests/repository/     Runtime-independent repository checks
├── configs/grammar/      Compiled definitions, packages, and examples
└── benchmarks/           Compact validation manifests and provenance
```

The mechanically checked directory map is maintained in the
[repository-layout reference](docs/reference/repository-layout.md).

## Testing

Run all three verification layers from the repository root:

```bash
ctest --test-dir cpp/build --output-on-failure
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

Focused CTest labels include `unit`, `contract`, `property`, `gpu`,
`cpu_gpu_parity`, and `tooling`. Sanitizer and libFuzzer workflows are described
in the [development guide](docs/guides/development.md).

## Contributing

Before changing behavior, identify the owning document in
[spec/](spec/README.md) and update its conformance tests in the same change.
Parser or output changes must also update the checked
[CLI reference](docs/reference/cli.md). Repository conventions and GPU profiling
rules are collected in [AGENTS.md](AGENTS.md).

GAGP is currently at release **2.0.0**. See [VERSION.md](VERSION.md) for the
release contract.

Mixed exact-type populations can use `gagp_evolve_cli --population-roots RootInt,RootFloat`
with roots from the supplied grammar. Selection remains global across all members;
see the [CLI contract](docs/reference/cli.md) for root and replay requirements.
