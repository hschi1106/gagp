# Repository cleanup and release usability audit

Goal 12 expands the final report into a repository cleanup and user-workflow
verification. Goals 11/11.5 were explicitly accepted by the user; the historical
Q threshold is not a new blocker and its measured shortfall remains documented.
This audit starts from `226ad21`; implementation/documentation checkpoint is
`25e566e`, followed by configure-contract fix `f864070`. Native runtime source
is unchanged from the measured `36c8cea`; CMake now rejects unavailable CUDA.

## Inventory and disposition

The initial inventory covers every tracked top-level area, plus ignored local
state. File-level paths/hashes and changed-path records are in the evidence root's
`source-files.json` and `inventory.json`. Counts below are the pre-cleanup snapshot,
not a claim that every file is executed by a test.

| Area | Tracked files before cleanup | Ownership / consumers | Disposition |
| --- | ---: | --- | --- |
| `.agents` | 1 | Agent navigation to actual native/tool owners | Keep routes; document explicit CUDA configure requirement |
| `.gitignore` | 1 | Native/Python generated artifacts and personal workspaces | Consolidate 237-line generic template to project rules; remove unrelated framework settings |
| `AGENTS.md` | 1 | Contributor contracts and GPU policy | Keep |
| `README.md` | 1 | User entry point | Replace Debug/full-suite-first path with minimal CPU Release run and explicit CUDA route |
| `VERSION.md` | 1 | Release/compatibility history | Keep; no runtime release boundary changed |
| `benchmarks` | 12 | Compact baseline/protocol/spec-freeze records used by tools/tests | Keep; not disposable run logs |
| `configs` | 65 | Public roots, reusable packages, authoring cases, migration inputs and PSB policies | Keep; tested or documented use |
| `cpp` | 363 | Native product, migration, benchmark, test and fuzz targets | Keep module boundaries; all implementations have CMake consumers; reject silently downgraded CUDA builds |
| `data` | 19 | Committed runnable fixtures and PSB regression cases | Keep; tutorial/benchmark/regression consumers |
| `docs` | 40 | Guides, design, reference and historical evidence | Separate historical index; add onboarding/troubleshooting; extend contributor instructions |
| `spec` | 10 | Normative contracts, covered by spec-freeze checks | Keep unchanged |
| `tests` | 5 | Repository contract checks | Update packaging ownership; add actual CMake CUDA-failure/CPU-recovery regression |
| `tools` | 57 | Operational package, maintained wrappers, tests and packaging | Remove duplicate `setup.cfg`; keep metadata in `pyproject.toml`, require setuptools 64+ for editable installation |

The only tracked file removed is `tools/setup.cfg`: its name/version, package
selection, Python requirement and entry point duplicated `pyproject.toml`. The
higher build-backend floor supports the single PEP 621/660 configuration. No
runtime Python dependency was added. Installation is verified from a fresh venv;
older offline setuptools installations must provide the declared build dependency.
The prior file remains recoverable from Git at `226ad21:tools/setup.cfg`.

Ignored local `tools/UNKNOWN.egg-info` and `tools/g3pvm_tools.egg-info` were stale
packaging outputs for retired identities, not source or public interfaces. They
were archived before removal (`retired-local-packaging.tar.gz` and its SHA receipt).
Current `gagp_tools.egg-info`, build directories and caches remain regenerable local
state. No blanket cleanup was performed. `.vscode`, `.codex`, `goals`, `experiment`,
`paper`, `ref`, `logs`, dataset mirrors and other user work were inventoried and
left alone. The previously mentioned untracked `gagp_progress_report.html` was
already absent at the start of this goal; this cleanup did not remove it.

## Architecture review and retained files

CMake owns native dependencies: core verification → CPU runtime/payload → evolution;
grammar compilation, GPU execution and CLI adapters have distinct targets. Python
already separates datasets, experiments, reports, grammar wrappers and shared
formats. Public headers stay in `cpp/include/gagp`; implementation helpers remain
with their owners. No source-file move or runtime refactor was needed to repair an
ownership violation found in this audit.

The largest native module is CLI command/codec orchestration (1,467 lines), then
compiler (963), bytecode verifier (940), migration artifact conversion (873), and
grammar compilation (844). These are candidates for future owner-specific splits,
but moving them now would not improve the user workflow and would invalidate more
verification. The six migration and four transition implementation files serve
offline conversion/oracle targets, not production legacy dispatch; repository
core guards verify that boundary. Native implementation files all have build
consumers, so none is deleted merely because its feature looks historical.

Byte-identical file groups were examined rather than automatically deduplicated:

- `configs/grammar/all.json` and `sequence.json` preserve independently named public
  presets and their legacy conversion contract. Replacing them with a new schema
  wrapper would change their documented representation.
- DC compatibility and restricted-combine example cases intentionally share inputs
  so different package structures can be compared and copied independently.
- The valid AST fixture and fuzz seed are separate consumer-owned corpora; retaining
  a small duplicate keeps fuzz workflows independent of test fixture paths.

Thin top-level Python wrappers are documented compatibility entry points with
callers in experiment workflows. Their implementations already live only in the
package. Removing them would break public commands without eliminating duplicate
logic. The tooling inventory now explicitly names the grammar/generation native
CLIs and package metadata owner. No product CLI flag, artifact, type, fuel, search rule,
GPU workload or allocator default changed.

## Build-contract issue found during verification

The first remote configure requested CUDA but `nvcc` was outside noninteractive
SSH PATH. Old CMake silently configured CPU-only and its 98 tests passed. Those
results are explicitly CPU-only evidence, not CUDA verification. `f864070` makes
`GAGP_ENABLE_CUDA=ON` fail with an actionable compiler/CPU-only configuration hint.
A new regression actually configures with a forced missing compiler, checks the
failure, then verifies explicit CPU configuration recovers in the same directory.

The remote verification then selects `/usr/local/cuda-12.6/bin/nvcc` explicitly.
The failed-detection logs are preserved separately from the real CUDA logs.
Default CUDA intent now requires an installed/discoverable compiler; CPU users
must explicitly select `-DGAGP_ENABLE_CUDA=OFF`, as the new quickstart does.

## User-facing changes

[Getting started](../guides/getting-started.md) provides explicit prerequisites,
a small CPU build, optional CUDA architecture configuration, a real grammar edit,
validation/resolution, deterministic population replay, evolution, fitness/AST
inspection and selected-program export/evaluation. Commands use committed inputs
and a fresh temporary directory. They do not require private experiment files.

[Troubleshooting](../guides/troubleshooting.md) covers build/venv issues, native
binary discovery, grammar and artifact failures, GPU architecture/device problems,
unsupported CLI help and CPU-only one-AST evaluation. The
[contributor route](../guides/development.md#contributor-workflow) explains package
versus primitive changes and matching contracts/tests. README and the documentation
index now route users by task before presenting historical evidence.

The project has no automated installer for system CUDA/compiler dependencies;
Linux is the verified platform. The native evolve command still lacks `--help` and
one-AST GPU evaluation; these are explicit supported-workflow limits, not hidden
success claims. The short tutorial verifies usability, not search quality or GPU
speedup for tiny populations.

## Evidence and acceptance

Raw evidence root: `/home/hschi1106/gagp-artifacts/goal12-20260927`.
Remote clean CUDA source/build: `/home/hschi1106/gagp-goal12-final-20260927`.
Source snapshots, configure/build/install logs, command transcripts, input/output
hashes and verification summaries are retained there. Validated checkpoints and evidence:

| Requirement | Evidence / scope |
| --- | --- |
| Clean CPU setup and tutorial | Fresh local clone at `25e566e`; Release build of four product tools, new venv editable install, actual tutorial shell blocks; `tutorial-cpu.log` |
| Packaging after duplicate removal | Editable and wheel builds/installations succeed; `tools-install.log`, `wheel-build.log`, `wheel-install.log`, `wheel-help.log` |
| Grammar customization and artifacts | Modified domain/weight validates, inspect/resolve succeed; generated/replayed population bytes match; exported best AST has the same fitness under the same cases/fuel |
| Offline migration | Fresh CPU build converts committed v1 scalar input and validates the v2 output; `migration.log` |
| Repository contracts | 24/24 tests, no skips, including real CUDA-missing configuration failure and CPU recovery; `repository-final-tests.log` |
| Operational tools | 84/84 tests, no skips; `tools-tests.log` |
| Current native integration | Explicit CUDA compiler, Debug build: **116/116** default CTest targets pass, no skips; 18 GPU-labeled targets actually execute; `cuda-explicit-tests.log` |
| Auxiliary benchmark contracts | Two opt-in targets are absent from the default 116; their prior passing evidence is retained rather than counted as newly executed. Source comparison confirms their code is unchanged |
| Backend routing | Both Debug and Release tutorial runs cover all four canonical modes and cross-machine replay; requested metadata agrees and GPU paths report positive evaluation/reproduction kernel time; `workflow-validation.log`, `release-workflow-validation.log` |
| Performance correspondence | All **362 native paths other than CMake** match measured code `36c8cea`; no runtime retiming needed. `native-source-correspondence.json` |

The source checkout was updated to `f864070` before the explicit CUDA integration
and final CPU configure. The only post-tutorial behavior change is configure-time
failure on an unavailable requested CUDA compiler; its regression verifies CPU
recovery. Prior 59 supported authoring/migration workflow commands remain valid
supplemental evidence for other examples at their documented older revision, not
relabeled as new executions. Their outputs are retained in the prior final archive.


Existing performance evidence remains at its exact measured native revision:
[second optimization round](grammar-migration/goal-11.5-target35.md) and
[first-round full representative matrix](grammar-migration/goal-11.5-final-measurements.md).
No timing campaign is repeated for packaging/documentation cleanup. The native
source hash comparison establishes unchanged runtime implementation; older
full-matrix results retain their older label.
Required baseline/oracle evidence was not deleted or moved, so existing hashes
and retrieval paths remain valid.

## Requirement-by-requirement completion audit

| Goal 12 requirement | Proof and outcome |
| --- | --- |
| Numbered goal / commit / primitive mapping | [Implementation report](custom-grammar-implementation-report.md) maps Goals 01–10 and source-to-general packages; Git commits exist; Goals 11/11.5 explicitly accepted at `ce75eb2` |
| Six original user requirements | General grammar and migrated behavior are covered by native contract/migration/parity tests; supported GPU routes and representative performance are recorded; optimization is accepted with limitations; usable repository and final report are delivered here |
| Complete repository inventory / unnecessary files | Every tracked top-level area and ignored local category is covered above; duplicate packaging configuration removed, retired local metadata archived, actual changed/deleted paths recorded |
| Architecture / dependencies | Native implementation registration and module boundaries inspected; targeted configure failure fixed; retained wrappers, fixtures and modules have documented consumers |
| User instructions / examples / contributor route | README and task index point to the executable tutorial, recovery guide and contributor steps; CPU and CUDA Release flows run from clean sources |
| Artifact / migration / replay compatibility | Exact population replay across local CPU and remote builds; exported selected AST fitness matches; committed v1 conversion validates; full migration CTests and prior supported workflow evidence retained |
| Final correctness | 116 default Debug/CUDA targets, 84 tool checks and 24 repository checks pass; two unchanged opt-in benchmark contracts retain their previous evidence |
| Exact implementation / performance | 362 unchanged native files, only CMake configuration differs; first/second-round performance remain labeled by their measured revision; no false numerical Q pass |
| Reports / reproducibility / evidence | Public report links, source/command/output/binary hashes, build environments and archived logs; documented limits distinguish supported behavior from research algorithm or old RNG replication |

No required cleanup, documentation or validation item remains pending. Known
product limitations (Linux validation scope, no native evolve help command, CPU-only
one-AST evaluation, lower speedup than the old reference) are reported explicitly;
they are not undisclosed failed checks. No new grammar rule or performance target
was introduced by this cleanup.

To reproduce the new-user checks, follow the shell blocks in Getting started from
a fresh clone; select your actual CUDA compiler/architecture. To reproduce release
correctness, build all targets in a Debug/CUDA directory, run `ctest --test-dir
cpp/build-cuda --output-on-failure`, then the two Python unittest discovery commands
in Development. For CPU-only integration select `GAGP_ENABLE_CUDA=OFF`. Use fresh
output directories; do not overwrite retained receipts. Source commits plus the
archive command transcripts are the exact audit recipe, not a requirement for users
to possess the original machine paths.

Final local archive: `goal12-evidence.tar.gz`, 44 files, all archived
bytes verified against their SHA-256 manifest. Archive SHA-256:
`0e11f001d62c5dfcc370e72645f797ebf21c67dfcf714cbcf0d998aec0fa7918`.
Remote archive (60 verified files) SHA-256:
`053facd488527eca29e49bcf38d9d0237a3d9c77ad1e477039677f0491258405`.
[Machine-readable completion evidence](repository-cleanup.json) records counts,
source correspondence and archive receipts. The archive bundles local transcripts
and the remote archive; extract it, then its nested remote archive for CUDA logs.
All are available under the raw root above; no external download service is implied.
