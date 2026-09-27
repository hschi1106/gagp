# Getting started

This tutorial evolves small integer programs, changes their search grammar, and
saves/replays the results. Run all commands from the repository root in a Bash
shell. Inputs are included; no dataset download is needed.

## Install and build on CPU

You need CMake 3.16+, a C++17 compiler and a build tool (Make or Ninja). Python
3.10+ with `venv` and pip is needed for this tutorial's authoring helpers, but not
for the native runtime. Linux is the verified platform; Windows/macOS are not
covered by the release validation. CMake presets additionally require CMake 3.21+.
OpenSSL Crypto is optional; the built-in hash implementation is used without it.

```bash
git clone https://github.com/hschi1106/gagp.git
cd gagp
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=OFF
cmake --build cpp/build -j 4 --target gagp_evolve_cli gagp_generate_cli gagp_grammar_cli gagp_migrate_artifact
python3 -m venv .venv-tools
.venv-tools/bin/python -m pip install -e tools
.venv-tools/bin/gagp-tools --help
```

Use fewer build jobs if memory is limited. The tools have no runtime third-party
Python dependencies; pip installs setuptools 64+ in its isolated build environment
for package metadata and editable installation. Offline installation needs those
build dependencies available from a local package cache/index. No administrator
installation is required. Full test builds are in [Development](development.md).

## Create and customize a grammar

Keep one output directory for this shell session. `mktemp` creates a fresh one so
rerunning the tutorial cannot overwrite earlier results. Copy it elsewhere if you
want to keep results beyond temporary-directory cleanup.

```bash
export GAGP_RUN_DIR="$(mktemp -d "${TMPDIR:-/tmp}/gagp-tutorial.XXXXXX")"
export GAGP_BIN=cpp/build
.venv-tools/bin/gagp-tools grammar init --example scalar --out "$GAGP_RUN_DIR/grammar.json"
.venv-tools/bin/gagp-tools grammar validate --grammar-definition "$GAGP_RUN_DIR/grammar.json"
```

The definition admits input `n`, zero and addition. Change the zero domain to
include one, and increase addition's relative sampling weight:

```bash
python3 - <<'PY'
import json, os
from pathlib import Path
path = Path(os.environ['GAGP_RUN_DIR']) / 'grammar.json'
grammar = json.loads(path.read_text())
for alternative in grammar['nonterminals'][0]['alternatives']:
    if alternative['id'] == 'zero':
        alternative['expression']['constant']['values'] = ['0', '1']
    elif alternative['id'] == 'sum':
        alternative['weight'] = 3
path.write_text(json.dumps(grammar, indent=2) + '\n')
PY
.venv-tools/bin/gagp-tools grammar validate --grammar-definition "$GAGP_RUN_DIR/grammar.json"
.venv-tools/bin/gagp-tools grammar inspect --grammar-definition "$GAGP_RUN_DIR/grammar.json"
.venv-tools/bin/gagp-tools grammar resolve --grammar-definition "$GAGP_RUN_DIR/grammar.json" --out "$GAGP_RUN_DIR/resolved.json"
```

Validation must exit successfully; inspection reports the compiled definition and
its identity. The content change changes the grammar hash. JSON changes take effect
on the next run without rebuilding C++. The matching cases declare input `n` and
expected integer outputs. Learn the case schema in [Fitness cases](../../spec/fitness_cases.md)
and further grammar options in [Grammar authoring](grammar-authoring.md).

## Generate, replay and evolve

```bash
"$GAGP_BIN/gagp_generate_cli" --grammar-definition "$GAGP_RUN_DIR/grammar.json" --cases configs/grammar/examples/authoring/scalar.cases.json --population-size 16 --seed 7 --out-json "$GAGP_RUN_DIR/population.json"
"$GAGP_BIN/gagp_generate_cli" --replay-json "$GAGP_RUN_DIR/population.json" --grammar-definition "$GAGP_RUN_DIR/grammar.json" --cases configs/grammar/examples/authoring/scalar.cases.json --out-json "$GAGP_RUN_DIR/replayed.json"
cmp "$GAGP_RUN_DIR/population.json" "$GAGP_RUN_DIR/replayed.json"
"$GAGP_BIN/gagp_evolve_cli" --grammar-definition "$GAGP_RUN_DIR/grammar.json" --cases configs/grammar/examples/authoring/scalar.cases.json --population-json "$GAGP_RUN_DIR/population.json" --population-size 16 --seed 7 --generations 2 --engine cpu --repro-backend cpu --repro-overlap off --show-program ast --out-json "$GAGP_RUN_DIR/cpu.json"
```

`cmp` should print nothing and exit zero: same-version generation replay is exact.
Do not edit the grammar between generation and replay. Evolution prints generation
statistics and writes a JSON run report. A run report is not a population replay
artifact. Seed equality does not guarantee identical old/new-version evolution.

The grammar owns node/depth search limits and execution fuel. Its small limits
keep this example inexpensive; conflicting CLI overrides are rejected. A larger
population or more generations increases search work, not the allowed grammar.

## Inspect results and export the selected program

```bash
python3 - <<'PY'
import json, os
from pathlib import Path
root = Path(os.environ['GAGP_RUN_DIR'])
run = json.loads((root / 'cpu.json').read_text())
assert not run['final']['skipped']
print('best fitness:', run['final']['best_fitness'])
print('program:', run['final']['ast_repr'])
(root / 'best.ast.json').write_text(json.dumps(run['final']['ast']) + '\n')
PY
"$GAGP_BIN/gagp_evolve_cli" --cases configs/grammar/examples/authoring/scalar.cases.json --eval-ast-json "$GAGP_RUN_DIR/best.ast.json" --engine cpu --fuel 32 --out-json "$GAGP_RUN_DIR/best.eval.json"
```

The export contains the materialized program and typed constants; it does not
require the grammar to execute. Supply the same cases and fuel when comparing its
fitness. `AST_EVAL fitness=...` confirms evaluation. Fitness is maximized; exact
interpretation and error penalties are in [Fitness](../../spec/fitness.md).
This short search is a workflow check, not a guarantee of finding a solution.
The one-AST evaluation command currently supports CPU only.

## Run on CUDA (optional)

Keep the CPU build and create a separate CUDA build. Install an NVIDIA driver and
CUDA toolkit compatible with your GPU and host compiler. The verified environment
includes RTX 3090 / CUDA 12.6 / GCC 11.4. This example uses architecture **86** for
RTX 3090; use **89** for RTX 4090, or your device's compute capability. CMake's
current default is 89, so pass this explicitly for other devices.

```bash
cmake -S cpp -B cpp/build-cuda -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=ON -DCMAKE_CUDA_ARCHITECTURES=86
cmake --build cpp/build-cuda -j 4 --target gagp_evolve_cli gagp_generate_cli
export GAGP_BIN=cpp/build-cuda
"$GAGP_BIN/gagp_evolve_cli" --grammar-definition "$GAGP_RUN_DIR/grammar.json" --cases configs/grammar/examples/authoring/scalar.cases.json --population-json "$GAGP_RUN_DIR/population.json" --population-size 16 --seed 7 --generations 2 --engine gpu --repro-backend gpu --repro-overlap on --blocksize 256 --out-json "$GAGP_RUN_DIR/gpu.json"
```

CMake rejects a CUDA-enabled build if it cannot find the compiler. If `nvcc` is
outside PATH, add `-DCMAKE_CUDA_COMPILER=/path/to/cuda/bin/nvcc` when configuring. GPU-capable commands select the least-used visible GPU;
`GAGP_CUDA_DEVICE=0` forces visible device 0. Inspect the run's config and timing
output to confirm the requested route. These are the four canonical modes:

| Evaluation | Reproduction | Options |
| --- | --- | --- |
| CPU | CPU | `--engine cpu --repro-backend cpu --repro-overlap off` |
| GPU | CPU | `--engine gpu --repro-backend cpu --repro-overlap off` |
| GPU | GPU | `--engine gpu --repro-backend gpu --repro-overlap off` |
| GPU | GPU with preparation overlap | `--engine gpu --repro-backend gpu --repro-overlap on` |

Reuse the same frozen population when comparing modes and use a distinct output
filename each time. Tiny populations need not benefit from GPU execution. End-to-end
generation time includes preparation and reproduction; kernel time alone is not
its speedup. See [Benchmarking](benchmarking.md) and [Timing fields](../reference/timing.md).
The optional tcmalloc setting in historical measurements is not required to use
GAGP; comparison runs must use the same allocator on both CPU and GPU.

Next: [advanced authoring](grammar-authoring.md), [migration](grammar-config.md),
[troubleshooting](troubleshooting.md), or [contributing](development.md#contributor-workflow).
