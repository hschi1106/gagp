# Development

How to build, test and change GAGP. Read the
[architecture](../design/architecture.md) and the
[repository layout](../reference/repository-layout.md) first.

## Build

```bash
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Debug
cmake --build cpp/build -j
```

This builds `gagp` and every test. Use a separate Release tree for timing.

| CMake option | Default | Meaning |
| --- | --- | --- |
| `GAGP_ENABLE_CUDA` | `ON` | Build the GPU runtime and GPU tests; configuration fails if no CUDA compiler is found |
| `GAGP_ENABLE_OPENSSL_SHA256` | `ON` | Use OpenSSL for grammar hashes when found; the built-in SHA-256 gives identical hashes |
| `GAGP_ENABLE_SANITIZERS` | `OFF` | AddressSanitizer and UndefinedBehaviorSanitizer (CPU only) |
| `GAGP_BUILD_FUZZERS` | `OFF` | libFuzzer targets (Clang, CPU only) |

`cpp/CMakePresets.json` defines the presets `debug`, `release`,
`cuda-parity`, `sanitizer` and `fuzz`, each building into
`cpp/build/<preset>`:

```bash
cmake --preset release -S cpp
cmake --build cpp/build/release -j
```

## Test

```bash
ctest --test-dir cpp/build --output-on-failure                 # everything
ctest --test-dir cpp/build -L gpu --output-on-failure          # GPU tests
ctest --test-dir cpp/build -L cpu_gpu_parity --output-on-failure
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

| Label | Covers |
| --- | --- |
| `unit` | Single components |
| `contract` | Specified behavior: formats, errors, fuel, types |
| `property` | Randomized and corpus checks |
| `integration` | Whole runs through several components |
| `gpu` | Needs a CUDA device |
| `cpu_gpu_parity` | CPU and GPU produce identical results |
| `tooling` | Repository checks (`tests/repository`) and the benchmark runner |

The most important checks for engine changes:

- `gagp_test_phase_population_cpu_reference`: the GPU resident engine and the
  CPU reference engine produce identical populations, fitness values and
  champions;
- `gagp_test_fitness_cpu_gpu_parity` and `gagp_test_evolution_cpu_gpu_parity`:
  the CPU and GPU evaluators agree;
- `gagp_test_cli`: the command-line contract.

`tests/repository` checks the documentation: relative links resolve, the
indexes list every document, the [repository layout](../reference/repository-layout.md)
matches the tree, and the [command reference](../reference/cli.md) lists exactly
the options and environment variables the code accepts.

### Sanitizers and fuzzing

```bash
cmake --preset sanitizer -S cpp
cmake --build cpp/build/sanitizer -j
ctest --test-dir cpp/build/sanitizer -L 'unit|contract|property' --output-on-failure

cmake --preset fuzz -S cpp
cmake --build cpp/build/fuzz -j --target gagp_fuzz_bytecode_json gagp_fuzz_ast_verify
cp -a cpp/tests/fuzz/corpus/json /tmp/gagp-corpus
cpp/build/fuzz/gagp_fuzz_bytecode_json -runs=1000 /tmp/gagp-corpus
cpp/build/fuzz/gagp_fuzz_ast_verify -runs=1000 /tmp/gagp-corpus
```

Copy the corpus first so a campaign does not modify the source tree. Keep a
minimized reproducer as a corpus file or a property test.

## Benchmarks and profiling

`python3 benchmarks/run.py run --quick` runs a smoke subset of the benchmark
suite; see [benchmarks/README.md](../../benchmarks/README.md). Report speedups
from Release builds without a profiler attached. Profile the GPU with Nsight
Systems (`nsys profile cpp/build/gagp run ...`); `GAGP_GPU_DIAGNOSTICS=1` adds
evaluation counters, and `GAGP_SEARCH_TRACE=1` prints per-generation timings.

## Making changes

**A grammar or example.** Grammars are JSON read at run time; no rebuild is
needed. Put reusable templates in `configs/grammar/packages/` and runnable
examples with their cases in `configs/grammar/examples/` or `examples/`. The
native code must never branch on a package or grammar name.

**A primitive or runtime behavior.**

1. Find the owning specification in [spec/](../../spec/README.md) and the
   compiler, verifier and CPU and CUDA runtime code that implement it.
2. Change types, scopes, errors, fuel and resource bounds together. AST kinds
   and opcodes are numbered; never reuse a retired number.
3. Add contract tests and CPU/GPU parity coverage, registered in
   `cpp/CMakeLists.txt`.
4. Update the specification in the same change.

**The resident engine.** Every operator step is a `__host__ __device__`
function in `phase_operators.cuh` shared by both engines. An optimization is
accepted only if `gagp_test_phase_population_cpu_reference` and the parity
tests still pass and exported champions and histories are unchanged. Anything
that changes which programs are explored belongs in the
[search configuration](../../spec/search_config.md), implemented by both
engines; see [gpu-reproduction.md](../design/gpu-reproduction.md).

Commits use `<type>: <summary>` with `feat`, `fix`, `refactor`, `docs`,
`test` or `chore`. Do not commit builds, run outputs (`logs/`, `runs/`) or
datasets.
