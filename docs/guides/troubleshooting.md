# Troubleshooting

Run commands from the repository root. Preserve a failing command, input files and
stderr before changing settings; the [CLI reference](../reference/cli.md) lists
supported options and defaults.

| Symptom | Check and recovery |
| --- | --- |
| CMake/compiler missing | Install CMake, a C++17 compiler and Make/Ninja through your system's development tools; use the explicit CPU build in [Getting started](getting-started.md). |
| Build process is killed | Reduce `cmake --build ... -j 4` to `-j 1`; use a separate Release build for runs. |
| `venv` or pip unavailable | Install your distribution's Python venv/pip support; native CLI use does not require the Python wrapper. |
| pip cannot fetch build requirements | Provide setuptools 64+ in an accessible package index/cache; runtime tools have no third-party dependencies. Do not run pip as root. |
| `gagp-tools` not found | Use `.venv-tools/bin/gagp-tools` after `.venv-tools/bin/python -m pip install -e tools`. |
| Wrapper cannot find native binary | Build `gagp_grammar_cli` in `cpp/build`, or pass `--native path/to/gagp_grammar_cli` to validate/inspect/resolve. Migration uses `gagp_migrate_artifact`. |
| Cannot open cases/import/output | Start from repo root, create output parent directories and keep package imports relative to the importing JSON. Use a fresh output directory. |
| `grammar init` refuses overwrite | This protects your edits. Choose a new filename or explicitly manage your existing copy. |
| Grammar type/scope/productivity error | Run `grammar validate` and `grammar inspect`, follow the reported field/production, and compare with the matching checked authoring example. |
| Replay identity or schema mismatch | Restore the exact resolved grammar, cases and version tuple used to generate the artifact. Changed weights/domains change identity. |
| Conflicting fuel/node/depth overrides | Edit the grammar's search/execution limits and regenerate; do not silently override a saved population contract. |
| GPU unavailable after a successful build | Check `nvidia-smi`, `nvcc --version` and CMake's CUDA compiler detection; reconfigure the separate CUDA directory after fixing toolkit discovery. |
| No kernel image / invalid device function | Rebuild with the device's `CMAKE_CUDA_ARCHITECTURES` value (86 for RTX 3090, 89 for RTX 4090). |
| GPU is busy or timings vary | Inspect `nvidia-smi`, choose an available visible index with `GAGP_CUDA_DEVICE`, and avoid concurrent experiments; do not stop someone else's processes. |
| GPU descriptor/workspace capacity rejected | Inspect reported limits, program structure and documented [payload](../design/payload.md) / [grammar](../../spec/grammar.md) constraints; use a supported configuration or explicitly choose CPU execution. |
| `--help` rejected by `gagp_evolve_cli` | This native command currently treats it as an unknown argument. Use the checked CLI reference; the Python `gagp-tools ... --help` commands do provide help. |
| `--eval-ast-json --engine gpu` rejected | One-AST evaluation supports CPU only. Use population evolution/evaluation workflows for GPU. |

CPU-only configurations cannot validate CUDA behavior. A skipped GPU test does
not establish parity. Build/test routes and expected labels are in
[Development](development.md); data and artifact formats are linked from
[Grammar and migration](grammar-config.md).

For a reproducible issue include: commit (`git rev-parse HEAD`), platform,
compiler/CMake versions, CUDA/driver/GPU when used, configure flags, exact command,
small non-sensitive grammar/cases, seed, stdout/stderr, and whether the CPU route
reproduces it. Keep generated logs outside versioned fixtures, under `logs/` or a
separate temporary directory.
