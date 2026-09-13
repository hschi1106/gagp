# Grammar migration execution evidence

Goal 01 baseline capture is complete.
Goals 02–12 have not started. The immutable source
revision and binary/log hashes are recorded in [baseline-capture.json](baseline-capture.json).
No generation or runtime implementation has changed.

## Expanded matrix and launch capability

The materializer `tools/gagp_tools/experiments/freeze_migration_workloads.py`
now freezes 208 workload configurations when `--asgp-block1024` is enabled.
The manifest at the artifact root's `workload-matrix-r64/workloads.json` includes
all eleven effective evaluation/reproduction modes for evolution and CPU/GPU
evaluation for steady sessions. Each steady session reuses production runtime
state for three warmups and fifteen measured calls; the outer comparison treats
the session median as one observation. Opaque fallback fixtures use lossless raw
bytecode and case bindings because the public case codec cannot express them.
Baseline-only runner output is explicitly distinct from paired candidate evidence.

The original default reference build failed an ASGP 1024-thread launch because
its kernel uses 142 registers. A non-ASGP starting population also encounters
this limit after evolving ASGP children. The failed preflight is retained under
`matrix-preflight-01/`. A separate unchanged-source Release build, `b049183/build-r64/`,
adds `-DCMAKE_CUDA_FLAGS=--maxrregcount=64`. Static `cuobjdump` output confirms
64 registers; all 33 reference tests passed without skips. The failing nested
20-generation workload and DC, DP1D, DP2D, and metadata steady workloads now run
at block size 1024. The 186-execution boundary capture in `boundary-oracle-r64/`
is byte-identical to `boundary-oracle-02/`, including known CPU/GPU disagreements.
Build hashes and capability evidence are recorded in `baseline-capture.json`.
Any candidate measured against this reference must use the same compiler flags.
The original build and its failures remain preserved.

The expanded matrix passed its one-block preflight: 1,640 processes completed,
covering all 208 workload configurations and 4,632 timing rows. The final audit
at `matrix-preflight-r64-audit-final.json` verifies these raw records but correctly
reports pending because only one warmup block exists. The full baseline run under
`baseline-matrix-r64/` completed three warmup and fifteen measured blocks.
`baseline-matrix-r64-audit-final.json` verifies all 29,520 processes and 4,632
required rows, with no missing blocks and a complete raw hash inventory.
It stopped in block 5 when device selection failed with CUDA out-of-memory.
After confirming the runner had stopped, `--resume` audited existing evidence,
retained completed workload blocks, archived the incomplete block and its failure,
and successfully reran it. Recovery receipts and `failed-attempts/` preserve the
original process files and hashes. A kernel-owned lock prevents concurrent new
runners; legacy invocations without this lock must be confirmed terminal first.
Only incomplete, uncommitted workload blocks may be replaced this way. Slow or
noisy completed observations cannot be selectively discarded. Completed runs are
idempotent under resume; changed fixtures, executables, settings, or evidence fail
validation. The audit verifies archived failure reasons and hashes as well as
accepted trials.
Full baseline measurements are complete. Candidate statistical acceptance belongs
to the later implementation comparison and is not claimed here.
The 30-block steady reference calibration under
`steady-calibration-30/` passed its CPU row but left GPU results inconclusive
under contention. Neither calibration nor successful capability probes establish
candidate acceptance. Earlier capture counts below describe their respective runs.

The comparison CLI now requires `--workloads` to audit against the independent
frozen matrix. `reports.audit_migration_trials` checks binary and fixture hashes,
all required rows and block classifications, raw command lines and timings, and
the complete raw-file hash inventory. Missing trials remain pending; removed rows,
fabricated timings, and modified evidence are errors. Audit/comparison outputs
must be outside the raw directory. The older steady calibration's derived
`comparison.json` was moved to `steady-calibration-30-comparison-original.json`
at the artifact root; all raw evidence stayed unchanged. The audited comparison
is `steady-calibration-30-comparison-audited.json`. Recovery now audits archived incomplete-block failures and replacement trials as
described above; completed noisy observations cannot be selectively excluded.

## Reference capture

Artifact root: `/home/hschi1106/gagp-artifacts/grammar-migration/b049183`.
The detached `source/` worktree preserves the baseline independently of subsequent
changes. `build/` is an optimized CUDA build; raw logs, `hardware.json`, and
`sha256.json` remain outside tracked source. Do not rebuild this directory with
candidate sources. The tracked manifest hashes the original build products and logs.

Commands executed from the repository root (shell commands use `rtk proxy`):

```bash
git worktree add --detach /home/hschi1106/gagp-artifacts/grammar-migration/b049183/source b049183
cmake -S /home/hschi1106/gagp-artifacts/grammar-migration/b049183/source/cpp -B /home/hschi1106/gagp-artifacts/grammar-migration/b049183/build -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=ON -DGAGP_BUILD_BENCHMARKS=ON
cmake --build /home/hschi1106/gagp-artifacts/grammar-migration/b049183/build -j 4
GAGP_CUDA_DEVICE=0 ctest --test-dir /home/hschi1106/gagp-artifacts/grammar-migration/b049183/build --output-on-failure
python3 -m unittest discover -s tools/tests -p test_grammar_migration.py -v
```

Reference result: 33/33 native tests passed, including GPU smoke and both parity
tests, with no skips. Comparator result: 7/7 focused tests passed. The current
worktree also passed all 37 operational tool tests and 17 repository checks
(`python3 -m unittest discover -s tools/tests -p 'test_*.py' -v` and the same
command with `-s tests/repository`). The initial documentation check caught a
missing index entry; it passed after adding the entry to `docs/README.md`. Compiler:
GNU C++ 11.4.0, CUDA 12.2.140, architecture 89. Hardware: two RTX 4090 GPUs;
driver 595.84. Both devices had competing compute processes at capture time.
Correctness results are not uncontended performance evidence.

## Implementation inventory

All paths below are relative to the repository root at the frozen revision.

| Responsibility | Current owner |
| --- | --- |
| Grammar config parsing and search restrictions | `cpp/src/evolution/grammar_config.cpp` |
| Prefix AST and specialized metadata tables | `cpp/include/gagp/evolution/ast_program.hpp`, `cpp/src/evolution/ast_program.cpp` |
| Node signatures, scopes, structural/type checks | `cpp/src/evolution/node_descriptor.cpp`, `ast_verify.cpp`, `ast_type_verify.cpp`, `typed_expr_analysis.cpp` |
| Generation and initialization | `cpp/src/evolution/genome_generation.cpp`, `population_init.cpp` |
| Typed variation and subtree metadata | `cpp/src/evolution/mutation.cpp`, `crossover.cpp`, `subtree_utils.cpp` |
| AST JSON, seed replay and CLI dispatch | `cpp/src/cli/commands.cpp` |
| Bytecode codec/verifier | `cpp/src/cli/codec.cpp`, `cpp/src/runtime/bytecode_verify.cpp` |
| Specialized lowering | `cpp/src/evolution/compiler.cpp` |
| CPU structured evaluators | `cpp/src/runtime/cpu/execute_bytecode_cpu.cpp` |
| GPU structured evaluators | `cpp/src/runtime/gpu/device/execute_bytecode_device.cuh` |
| Payload registry and transport | `cpp/src/runtime/payload/payload.cpp`, `cpp/src/runtime/gpu/host_pack_gpu.cu`, `fitness_gpu.cu` |
| GPU reproduction candidate preparation/packing | `cpp/src/evolution/repro/prep.cpp`, `pack.cpp`, `gpu.cpp` |
| Device variation | `cpp/src/evolution/repro/gpu/device/variation_kernels.cuh` |
| Overlap and lifetime | `cpp/src/evolution/lifecycle.cpp`, `evolve.cpp` |

LinearRec has an AST binder table and specialized lowering in `compiler.cpp`;
it lowers to ordinary bytecode, including private list/integer checks, rather
than a dedicated LinearRec runtime opcode or segment. MapList and FilterList
also lower through the ordinary instruction machinery. ASGP DC/DP1D/DP2D have
dedicated AST tables, bytecode segment metadata, and CPU/GPU evaluators.
Existing fixtures
under `cpp/tests/fixtures/ast_eval_asgp_*.json` and structured/ASGP tests are
starting evidence, not a complete migration oracle.

The CLI accepts CPU or GPU evaluation independently from CPU or GPU reproduction.
Consequently CPU evaluation plus GPU reproduction must be inventoried alongside
the four mandatory modes. Overlap only activates when both evaluation and
reproduction are GPU (`gpu_reproduction_overlap_enabled`); enabling the flag for
other combinations does not create another effective overlap path. CPU reproduction
ablation flags expose three experimental GPU-assisted paths and need explicit
classification in the final workload manifest.

Product population loading currently regenerates from seeds. `--eval-ast-json`
consumes materialized ASTs but explicitly rejects GPU evaluation. The new benchmark
adapter covers population capture and replay. Independent CPU donor tapes and GPU
parent/donor snapshots are complete, with the reproduction evidence described below.

## Materialized adapter and initial oracle

Named first-error and phase visibility observations are preserved in
`boundary-oracle-order-01/` and `gpu-boundary-order-01/`. The 11 added programs
all pass bytecode verification. Competing errors expose DC source-before-divide,
divide-before-solve, left-before-right, and solve-before-combine order. Additional
probes load an unbound local in each DC and DP phase. CPU and GPU match in returned
error and exact fuel thresholds for all 11; `order-visibility-audit-01.json` maps
names to raw ordinals. These phase-local probes do not alone establish every
lexical-shadowing or enclosing-scope rule.

`boundary-oracle-order-02/` adds 15 payload probes around the frozen Mixed scratch
limits: 511/512/513 string bytes, 127/128/129 list values for each typed-list kind,
and 31/32/33 string entries. Both CPU and direct GPU captures are complete.
The subsequent order-03 captures add four DP dependency-order observations and
nine typed-list entry-capacity observations; all 39 named cases are frozen.
Five payload observations differ beyond the device scratch capacities; both
outcomes are frozen in `reference-boundary-oracle.jsonl`, and the two focused
CPU/GPU replay tests pass. The [semantic coverage ledger](semantic-coverage.md)
maps concrete cases to artifacts and identifies remaining gaps. The oracle checker now rejects
missing required result/boundary phases instead of silently skipping comparisons.
The semantic ledger records the supported forms, rejected nested ASGP capability,
error-order and resource-limit evidence, and known reference discrepancies.

Population statistics are captured separately from timing trials by
`capture_evolution_stats.cpp` and `experiments.capture_evolution_statistics`.
Observer-only call-site wrappers around the immutable evolution loop record
every evaluated and reproduced population: node/depth histograms, unique program
counts, requested individual fitness evaluations, requested case scores, and
post-backend verifier outcomes. These counts are not executed instruction counts
or evidence of operator acceptance/fallback decisions.

The pilot under `evolution-statistics-pilot-01/` compares original and observed
executions for scalar and metadata-heavy populations, 1 and 20 generations, and
all 11 modes. All 44 combinations preserve the exact best-program snapshots and
fitness histories. Native CPU/GPU integration tests check complete histogram and
evaluation-count accounting. The full capture under `evolution-statistics-full-01/`
completed on device 1; its manifest records commands, hashes, and completed rows.
Host operator decisions are now instrumented through
`experiments.instrument_migration_operators`. It generates separate diagnostic
copies of the immutable crossover, mutation, reproduction backend, and GPU host
decode sources. Exact anchors must match once; generated sources, counter event
names, source hashes and diffs are archived. Removing the inserted counter lines
reconstructs the original sources exactly. Production sources remain unchanged.
The observer records decisions per reproduced generation, including accepted
children that happen to equal their parents; equality is not used to infer a
fallback. Mutation eligibility and selection are separate counters, so skipped
mutation is their difference. Constant mutation attempts include both the direct
constant branch and unsuccessful subtree attempts.

`operator-statistics-adapter-01/` preserves the build commands and binaries.
`operator-statistics-pilot-01/` contains 88 plain/instrumented process results for
Bool/Char and canonical scalar populations, 1 and 20 generations, and all 11 modes.
All 44 combinations preserve exact best-program snapshots and fitness histories.
`operator-statistics-pilot-01-audit.json` verifies decision accounting for every
child: 21,504 CPU mutation eligibility decisions and 8,064 GPU host decode
decisions. The latter include 7,241 accepted children, 480 device-invalid fallbacks,
341 binder/AST verification fallbacks, and two empty-decoding fallbacks.
These are diagnostic observations, not performance measurements.

The subsequent `operator-statistics-adapter-02/` also compiles a diagnostic copy
of the production GPU launch and variation sources. `migration_gpu_probes.py`
inserts atomic counters without changing the original source lines. It records
assembly versus parent fallback, each fallback condition (conditions may overlap),
mutation choice, metadata validity before fallback handling, and final device
validity. Counters reset before each variation launch and are read after its
existing synchronization. Host and GPU decisions occupy separate output fields.
These extra transfers and counters are excluded from performance trials.

All 44 comparisons in `operator-statistics-pilot-02/` preserve exact best programs
and fitness histories. Its audit checks 126 GPU reproduction generations / 8,064
physical children: 103 assembly fallbacks, 385 metadata-invalid results, and 480
final invalid outputs. The final count matches host device-invalid decode decisions
exactly. These stages overlap and must not be summed as independent failed children.
Tests reject missing decision counts or host/device disagreement and verify that
removing probe blocks reconstructs the original CUDA sources. The full combined
distribution/decision capture in `operator-statistics-full-02/` completed on
device 1 across all 136 evolution configurations and 11 modes. Independent audits
of both full captures verify all 1,496 comparisons / 2,992 processes per capture,
unchanged fitness histories and best programs, raw hashes and commands, and 15,708
evaluation plus 15,708 reproduction observations. The decision audit accounts for
6,214,656 CPU children and 2,330,496 GPU children; all 12,390 final device-invalid
outputs match host device-invalid fallback counts. Reports are
`evolution-statistics-full-01-audit.json` and `operator-statistics-full-02-audit.json`.

`gagp_grammar_migration_bench` is opt-in under `GAGP_BUILD_BENCHMARKS`.
Its source is `cpp/src/bench/grammar_migration_bench.cpp`, with lossless artifact
support in `migration_snapshot.{hpp,cpp}`. It accepts common evolution CLI options
plus `--action freeze|freeze-repro|repro-check|repro-steady|run|oracle|steady`, `--snapshot PATH`, and optional
`--source-ast PATH` for capture. The product CLI has no added flags. The only
CLI library addition exposes its existing fitness-case parser for reuse.

`freeze` invokes production population initialization or materializes a supplied
AST. `run` invokes production `evolve_population` with the restored population.
`oracle` compiles restored ASTs, archives complete bytecode/phase metadata,
records CPU values/errors and the first non-Timeout fuel boundary, and computes
fitness in original population order. GPU mode explicitly calls `FitnessSessionGpu`;
failure is an error, not a CPU fallback. CPU values/fuel boundaries are separate
from GPU fitness. The direct device oracle described below now captures per-case
values, errors, payloads, and fuel; the complete native capture is in progress.

`capture_gpu_results.cu` includes the immutable production fitness source to reuse
its packing helpers and device execution core. Each diagnostic probe runs the
ordinary `d_execute_bytecode_impl` and the same `d_run_code_core` with retained
scratch state, and rejects any difference between their returned value/error.
The scratch state provides materialized result payloads and remaining fuel.
Host payload registries are restored after every probe. This is diagnostic code,
not an alternate evaluator or a timing path. `experiments.capture_gpu_oracle`
archives reference/source/library hashes, compilation and execution commands,
direct GPU observations, and comparisons with the CPU oracle.

All 18 compact oracle cases match CPU values, errors, and fuel boundaries exactly;
the native `gagp_test_gpu_result_oracle` checks this. The 186-case capture under
`gpu-boundary-results-01/` confirms the 14 known frame/memo fitness disagreements
and reveals one additional fuel-boundary difference: ordinal 20 returns a DP2D
dependency `TypeError` at fuel 9 on GPU versus fuel 10 on CPU. Both observations
are preserved. No runtime charging rule has been changed. The native capture under
`gpu-native-results-01/` is complete: 3,078 executions, with the same DP2D fuel
difference and four repeated integer-overflow observations. For `INT64_MAX + 1`,
CPU returned bits `8000000000000001` while GPU returned `8000000000000000`.
Both arithmetic paths convert operands through `double` before converting to
integer, exposing a host/device boundary-conversion difference. The recorded
numeric fitness is zero despite this value difference, so fitness parity alone
is insufficient. `reference-gpu-differences.jsonl` preserves CPU and GPU observations
with provenance. `gagp_test_gpu_known_differences` checks the recorded GPU outcomes;
it does not claim CPU/GPU parity for these cases. The later named-boundary and verifier captures complete the coverage ledger.

`migration-population-v1` stores AST structure separately from constants. Values
use numeric tags, 16 hexadecimal scalar-bit digits, a Bool field, and decoded string
bytes (hex) or typed list elements. This avoids double-based integer conversion
and non-finite float normalization in existing public JSON codecs. Tests cover
signed 64-bit extremes, negative zero, infinity/NaN bits, Bool/Char, binary strings,
and every typed list, clearing the payload registry before restoring. This is a
benchmark format; it does not redefine or repair the public wire format.

`freeze-repro` captures production reproduction preparation from an existing
population snapshot. It ranks parents using production CPU fitness, compacts their
tables, and invokes the production candidate/donor preprocessor. The
`migration-reproduction-v1` output contains lossless parents, ranked fitness,
all preparation settings, subtree boundaries, candidate ranges and scope metadata,
and materialized typed donor expression fragments. Seeds and 64-bit signatures
are decimal strings to avoid double conversion. Donor fragments use the population
serializer's structure/constants layout but are not executable programs and cannot
be read through the whole-program population decoder.

The artifact root's `reproduction-workloads-01/manifest.json` records 34 successful
captures across the 17 evolution families at populations 64 and 1024. The adapter
under `reproduction-adapter-r64/` links the immutable register-limited reference
archives; its exact link command, source copies, and hashes are preserved.
Fresh-process integration checks verify deterministic parent order, donor payload
serialization, and scope metadata. `repro-check` restores captured parents, donors,
and preparation metadata without invoking generation. It checks parent subtree
boundaries and donor expression structure using a temporary program wrapper.
All 34 files round-trip byte-for-byte through the immutable-reference-linked
adapter in `reproduction-restore-r64/`; `results.json` preserves commands and outcomes.
Integration tests reject damaged boundaries, overflowing 64-bit signatures,
invalid donor ASTs, and missing donors.

`repro-steady` consumes this restored data through production `pack_population`
and `run_gpu_repro_backend_prepared`. Packing occurs once outside repeated calls;
each call includes upload, selection/variation kernels, copyback, and decoding.
After timing, the adapter verifies children and requires identical lossless child
populations across all three warmups and fifteen measured calls. Changed capture
settings are rejected before execution. This isolates GPU reproduction with fixed
parents and donors. This initial GPU-only pilot was followed by the CPU donor-replay counterpart,
complete reproduction baseline, and full decision capture described below.
Candidate performance acceptance remains a later goal.
The 34 sessions under `reproduction-steady-r64/` all completed with identical valid
children across their 18 calls. `results.json` records commands, hashes, and device 1;
the reference-linked adapter and source copies are preserved alongside it. These
capability measurements ran while the main device-0 baseline was in its first
warmup block and do not establish uncontended performance acceptance.

CPU mutation creates donors inside each mutation rather than consuming the GPU
donor pool. `experiments.capture_cpu_reproduction` compiles the immutable mutation
source with interception at its production donor call, while a separate plain
executable links the original mutation archive unchanged. The captures under
`cpu-donor-capture-03/` preserve 7,565 calls across all 34 parent workloads, including
exact pre/post RNG states, input state, donor ASTs and payloads, and final children.
Every captured child population and verifier result matches the plain executable.
The subsequent `cpu-donor-capture-04/` run adds fresh-process replay through
`--donor-tape`. All 34 workloads consume exactly 7,565 captured calls, generate
zero replacement donors, and reproduce the plain executable's children and
verifier outcomes. Each intercepted call checks its request and pre-call RNG
state, restores the materialized donor, and advances to the recorded post-call
RNG state.

`--steady --donor-tape PATH` adds CPU reproduction timing. Tape parsing and a full
state-validation call occur before timing. Each repeated call restores recorded
donors and RNG state, checks request shape and tape consumption, and then checks
exact child equality outside the timer. The measured interval contains production
selection, crossover, mutation with donor restoration, and their allocations;
it excludes donor generation and diagnostic JSON/RNG serialization. The output
separately records selection, crossover, and mutation phases.

All 34 sessions in `cpu-donor-capture-05/` passed with three warmups and fifteen
measured calls, zero generated donors, and unchanged verifier outcomes. Its
manifest records the original, intercepted, replayed, and steady process commands
and hashes. These session measurements still need outer paired blocks and the
frozen comparison protocol before they can support performance acceptance.

The shared runner supports `measurement: steady_repro`, with separate frozen
CPU/GPU donor inputs and `--before-cpu-repro` / `--after-cpu-repro` executable
identities. Each outer block contains CPU and GPU reproduction session medians;
inner calls are not treated as independent blocks. The independent audit verifies
both executable hashes, donor-tape hashes, exact commands and sample counts,
raw timing reconstruction, and zero regenerated CPU donors. The 34-workload
manifest is `reproduction-matrix-01/workloads.json`. Its preflight completed all
68 process runs; `reproduction-preflight-01-audit.json` remains pending solely
because it contains one warmup block. The full device-1 baseline under
`reproduction-baseline-01/` is complete. Its independent audit verifies 1,224 process
runs and all 68 required rows, each with three warmup and fifteen measured blocks.
This is baseline capture, not candidate acceptance.

Optional benchmark builds provide `gagp_migration_cpu_reproduction_plain` and
`gagp_migration_cpu_reproduction_capture`. The native `gagp_test_cpu_donor_tape`
integration test compares original, intercepted, and replayed children and rejects
missing/extra tape entries, changed requests, and malformed RNG states. The
reference capture tool archives its sources, compile commands, library hashes,
and individual process records independently of these current-worktree targets.

This direct-backend probe found 37 `type_mismatch` child rejections in
`simple_exp_1024-p1024` with RNG initialized to 42. Both executables produce the
same rejected children. Their ASTs and verifier locations remain in the capture;
they were not repaired or removed. This observation concerns the frozen direct
backend invocation, not every evolving run. The failed first probe is retained
under `cpu-donor-capture-02/`, and the earlier compile failure under `-01/`.

Example (common evolution limits must match the frozen workload):

```bash
REFERENCE_ADAPTER --action freeze-repro --snapshot POPULATION.json --cases CASES.json --population-size 64 --seed 42 --out-json REPRODUCTION.json
```

Fresh-process integration tests cover capture, exact replay, population-order
fitness, fuel boundaries, CPU evolution, and GPU reproduction with overlap. The
test initially assumed uncapped numeric error. `spec/fitness.md` and
`value_semantics.hpp` cap error at `penalty`; the test now explicitly supplies
`--penalty 10` for its error-of-two example. Runtime and normative semantics agree.
The repository skill's uncapped formula is stale and is not the migration oracle.

Additional artifacts below are relative to
`/home/hschi1106/gagp-artifacts/grammar-migration`. `adapter-build/` is the current
Release/CUDA adapter build. Its CPU/core/evolution static libraries are byte-identical
to the original baseline. The CUDA archive differs across builds, so a second
adapter was linked directly against the immutable original CPU/core/evolution/CUDA
archives: `oracle-pilot/baseline-adapter`. The exact link command and executable
hash are in `oracle-pilot/baseline-adapter-link.json`.

The DC fixture is frozen in `oracle-pilot/dc.population.json`. The original-library
adapter records integer result 2, CPU/GPU fitness 0, first successful fuel 12, and
Timeout at fuel 11 (`dc.baseline-256.json`). At block size 1024 the same adapter
fails with CUDA `too many resources requested for launch` (`dc.baseline-1024.log`).
The untouched reference CLI succeeds on the scalar canonical fixture at 1024
(`reference-1024.log`/`.json`). Source confirms separate Mixed kernel instantiations
for ASGP-present/absent populations. This initial default-build ASGP/1024 failure was subsequently reconciled by the
separate unchanged-source register-limited build described above.
The failed row must not be silently replaced by 256 or counted as passing.
Static inspection with `cuobjdump --dump-resource-usage` of the untouched reference
CLI confirms 64 registers for the ASGP-disabled kernel and 142 for the ASGP-enabled
kernel (`b049183/cuda-resource-usage.txt`). This explains why the latter cannot
launch 1024 threads within the device register budget. This was binary inspection,
not a profiled timing run. The 256-thread supported equivalent and any new 1024-thread
capability must remain distinct in the final frozen workload inventory.

Historical adapter verification: 36/36 native tests passed, including four GPU-labelled
tests with no skips (`adapter-final-ctest.log`); 40/40 operational tool tests and
17/17 repository checks pass. The ten migration tool tests cover statistics,
missing modes, frozen-file changes, option overrides and overlap-safe extraction.
Commands were `GAGP_CUDA_DEVICE=0 ctest --test-dir
/home/hschi1106/gagp-artifacts/grammar-migration/adapter-build --output-on-failure`
and the two `unittest discover` commands in the reference capture section.
`oracle-pilot/adapter-source.tar.gz` preserves the measurement adapter and tooling;
`oracle-pilot/evidence-sha256.json` hashes that archive, the two adapter binaries,
oracle results, fixture population and link recipe. This evidence is partial Goal
01 work, not an implementation or performance completion claim.

## Independent native oracle expansion

The capture tool `tools/gagp_tools/experiments/capture_migration_oracle.py` builds
`cpp/src/bench/capture_baseline_tests.cpp` around unchanged tests from the immutable
reference checkout. It links the original core, CPU, evolution and GPU archives.
Wrappers observe production calls and retain the original assertions and exit codes;
fuel probing saves and restores the payload registry to avoid changing later tests.
Skipped reference tests fail capture. Each artifact records source/library hashes,
compile commands, executables, logs and JSONL observations. The final captures also
retain the exact capture-tool and adapter sources for reproduction after cutover.

Current raw captures, relative to `/home/hschi1106/gagp-artifacts/grammar-migration`:

| Reference source | Artifact | Captured observations |
| --- | --- | --- |
| Structured semantics | `native-oracle-04/capture-0.jsonl` | 15 compilations, 15 executions, 2 AST rejections |
| ASGP runtime semantics | `native-oracle-04/capture-1.jsonl` | 23 executions |
| CPU/GPU fitness parity | `native-oracle-04/capture-2.jsonl` | 80 compilations, 3,040 executions |
| Additional ASGP boundaries and type/dependency combinations | `boundary-oracle-02/capture-0.jsonl` | 186 executions, with independent CPU and GPU fitness observations |

[oracle-summary.json](oracle-summary.json) records manifests, hashes, replay
counts, the 14 baseline disagreements, and remaining limitations. Executions cover
all eight public result types, the internal fallback token, and Name/Type/ZeroDiv/
Value/Timeout errors. Captured bytecode includes phase bodies, mappings, constants,
dependency descriptors and bounds. CPU observations preserve result bits/decoded
payloads, error messages, fitness and exact first-non-Timeout fuel with checks at
and immediately below the boundary. The bounded probe cap is 20,000; an unresolved
Timeout at that cap remains explicitly unresolved.

`gagp_migration_oracle_replay` reconstructs bytecode/payloads and checks these
observations in fresh processes. The corpus contains an intentional invalid
list-tag runtime test rejected by the bytecode verifier. Such tests require an
explicit captured verifier result plus `runtime_negative_test: true`; only this
test adapter executes those marked cases. Production ingestion is unchanged.
Opaque payload tokens in low-level tests are recorded as `materialized: false`;
the ordinary population snapshot encoder still requires decoded payloads.

All 3,078 native execution rows passed fresh-process CPU/GPU fitness replay.
The 186 boundary observations also replayed exactly against their respective
backend references, including 14 disagreements. This is reference preservation,
not a claim of parity in those 14 cases. The primary examples are DC lengths 65–66,
DP1D states at/beyond 128 and a shallow dependency graph exceeding the 128-entry
memo capacity, and DP2D grids exceeding the memo limit. Source inspection finds
64 DC frames and 128 DP frames/memo entries on GPU; CPU uses recursive calls and
dynamic memo maps without corresponding explicit limits. GPU fitness alone does
not identify the exact error code or distinguish all payload/frame failure paths.
These observations must be reconciled against the normative resource semantics
during migration rather than overwritten as golden outputs.

`cpp/src/bench/migration_boundary_cases.hpp` also probes all four DC source types
against all eight result types; all six DP1D direction/arity combinations and all
six DP2D dependency patterns against all result types; empty/singleton DC inputs;
both split-clamping directions; repeated DP1D dependencies; and boundary-before-base
ordering. This still needs a requirement-by-requirement lexical-scope, first-error
order and payload-overflow coverage audit. GPU returned tags, decoded payloads,
error codes and fuel boundaries need direct observation beyond fitness.

A compact, unchanged 18-row selection from the independent reference is checked
in under `cpp/tests/fixtures/migration/`, with source revision, original ordinals
and hashes in `provenance.json`. Native CPU/GPU tests replay it and prove that a
corrupted expected result fails. Current validation: 38/38 native tests pass,
including five GPU-labelled tests without skips (`oracle-final-ctest.log`);
40 operational tool tests and 17 repository checks pass. The full fitness and
boundary captures remained byte-identical when regenerated with archived sources.

Reproduction (prefix with `rtk proxy`, set `PYTHONPATH=tools`):

```bash
python3 -m gagp_tools.experiments.capture_migration_oracle --reference /home/hschi1106/gagp-artifacts/grammar-migration/b049183/source --reference-build /home/hschi1106/gagp-artifacts/grammar-migration/b049183/build --adapter-build /home/hschi1106/gagp-artifacts/grammar-migration/adapter-build --output NEW_CAPTURE_DIRECTORY --device 0
```

Add `--boundaries --gpu-observations` for the supplemental ASGP probes. Replay
with `gagp_migration_oracle_replay INPUT.jsonl SUMMARY.json --gpu`. These are
diagnostic commands, not performance timing runs.

## Statistical comparator

`tools/gagp_tools/reports/grammar_migration.py` compares paired trial blocks.
It retains excluded block reasons, rejects duplicate blocks/nonpositive or nonfinite
timings, requires three warmups and fifteen measured blocks, and resamples entire
CPU/mode before/after blocks 10,000 times. It uses median ratios and the existing
shared interpolated-percentile helper for 95% intervals. It gates Q, absolute mode
time and absolute CPU time separately. Failing initial rows require 30 trials;
unresolved rows remain pending. Synthetic reference-equality tests are unit tests,
not measured reference calibration.

Manifest format `grammar-migration-trials-v1` contains `analysis_seed`, an explicit
`required_rows` list, and a `rows` mapping. Each row contains matching
`workload_before_sha256`/`workload_after_sha256`, `timing_source: direct`, and `blocks`.
For the canonical cold scope, `timing_source: canonical_cold_disjoint` permits only
the documented generation-0 total plus disjoint GPU session initialization.
Each block contains a unique `block_id`, `warmup`, and positive `cpu_before_ms`,
`mode_before_ms`, `cpu_after_ms`, `mode_after_ms`. Excluded blocks instead carry
`excluded: true` and a nonempty `reason`. The runner binds this input to manifest
workload hashes and raw logs; the comparator alone does
not prove workload coverage or provenance.

`tools/gagp_tools/experiments/grammar_migration.py` provides a fresh-process runner.
Its `migration-workloads-v1` manifest fixes case/snapshot/optional grammar hashes,
execution/search options, all five base modes, and trial counts. It randomizes
workload and before/after/mode order within paired blocks, measures actual process
wall time, retains phase timings, commands, timestamps, device state and process
output, and checksums evidence. Existing output directories cannot be overwritten.
The runner now also supports warm persistent sessions, frozen donors and audited
incomplete-block recovery. Complete evolving-population statistics are captured
separately from timing trials.

Initial calibration used identical adapter copies on both sides: `simple_exp_1024`,
population 64, one generation, block size 256, seed 42, fuel 20000, three warmup and
fifteen measured blocks, analysis seed 20260911. All 180 fresh-process invocations
completed across five modes. Raw artifacts are in `calibration-256/` (`identity.json`,
`trials.json`, `comparison.json`, per-process logs, `raw-sha256.json`). This pilot is
not the full frozen matrix or final baseline.
[calibration-summary.json](calibration-summary.json) contains compact results and
evidence hashes. All fifteen mode/scope rows require reruns: shared-machine noise
produces broad intervals even with identical binaries. No trial was excluded and
no threshold changed. A controlled 30-measured-block rerun remains necessary.

Reproduction commands (prefix shell commands with `rtk proxy`, and set
`PYTHONPATH=tools` for these Python modules):

```bash
python3 -m gagp_tools.experiments.grammar_migration /home/hschi1106/gagp-artifacts/grammar-migration/oracle-pilot/calibration-workloads.json --before /home/hschi1106/gagp-artifacts/grammar-migration/oracle-pilot/reference-adapter --after /home/hschi1106/gagp-artifacts/grammar-migration/oracle-pilot/reference-adapter --output NEW_RUN_DIRECTORY --device 0
python3 -m gagp_tools.reports.grammar_migration NEW_RUN_DIRECTORY/trials.json --workloads WORKLOAD_MANIFEST.json --output NEW_RUN_DIRECTORY.comparison.json
```

## Goal 01 acceptance evidence

- The immutable optimized references pass all 33 original native tests each.
- Full evolution/evaluation and reproduction baselines are independently audited:
  29,520 and 1,224 processes respectively, covering every frozen required row.
- Both full distribution and decision captures cover all 1,496 workload/mode
  comparisons, with exact output comparisons and complete child accounting.
- Native, direct GPU, boundary and verifier artifacts preserve values, payloads,
  errors, fuel, rejected inputs, and known CPU/GPU discrepancies. Replay checks
  runtime outcomes, verifier diagnostics, and compilation snapshot reconstruction.
- Comparator tests cover missing modes, changed workloads, ratio and absolute
  regressions, overlap attribution, noisy trials, and recovery evidence.
- Reference calibration measured variance and exposed shared-machine uncertainty.
  It is not candidate acceptance; future comparison gates retain their thresholds.

Final validation and the single commit are recorded in `goals/goal-01.md` and
`baseline-capture.json`. Binary and raw trial evidence remain outside tracked source.

## Goal 02 compiler acceptance

The internal `grammar-definition-v1` compiler lives in
`cpp/include/gagp/evolution/grammar/` and `cpp/src/evolution/grammar/`.
The [normative schema](../../../spec/grammar_definition.md) and
[custom integer example](../../../configs/grammar_definitions/custom_integer.json)
cover exact value types, structural categories, local imports/overrides, typed
constant domains, scopes, fixed templates, shared/forwarded holes, separate limits,
canonical SHA-256 identity and membership/provenance/cache contracts.

Compilation builds const numeric expression, production, template, region and
context tables. Joint depth/node feasibility uses a fixed point, including
productive recursive rules. General recursion/memoization declarations preserve
exact result tags and phase visibility but explicitly reject execution until the
later runtime stages. This commit does not introduce package-name runtime dispatch.
The production grammar-config/generation/runtime paths remain available.

Validation artifacts are under the existing grammar-migration artifact root:

- `goal-02-gpu-build/`: separate Debug CUDA build with benchmarks enabled; the
  Goal 01 `adapter-build` and original-reference builds are preserved.
- `goal-02-gpu-native-tests.log`: 50/50 native tests passed, including GPU/parity
  and the frozen migration oracle suites; no CUDA skips.
- `goal-02-final-tools.log`: 52/52 operational tool tests passed.
- `goal-02-final-repository.log`: 17/17 repository checks passed.
- `goal-02-final-focused-tests.log`: 5/5 grammar/repository native tests passed
  after the final context-index capacity accounting correction; the full 50-test
  run preceded that compiler-only correction.
- Compiler tests cover exact overloads and all 21 control signatures, invalid scopes,
  category mismatch, productive/unproductive cycles, exact feasibility boundaries,
  template cycles/forwarding/duplication, all typed domains, import cycles and
  conflicts, source validation before replacement, transitive hash changes,
  relocation independence, resolved round-trip and standard SHA-256 test vectors.

Goal 03 implements grammar-driven materialization and artifacts; Goal 04 implements
membership-aware variation. General structured execution remains assigned to
Goals 05–07. Goal 02's declarations are not a claim that these later stages are done.
