# Timing

This document defines the canonical timing metric names used by the native benchmark and evolution CLIs.

Internally, the evolution engine stores `EvolutionTiming` with nested
`EvaluationTiming`, `ReproductionTiming`, and one `GenerationTiming` per
generation. The CLI output layer is the compatibility adapter that emits the
flat names below. A metric for an inactive backend remains present as zero in
JSON/per-generation records; it is not omitted from the internal model.

The goal is to keep timing output:

- explicit about scope,
- consistent across CLI, JSON, and internal structs,
- safe to aggregate without residual guessing,
- easy to extend when a new phase needs its own timer.

## Naming Rules

- Every duration is reported in milliseconds and ends with `_ms`.
- A metric without `_total` is one measurement for one call or one generation row.
- A metric with `_total` is the sum of the corresponding per-generation or per-call values over the full run.
- `generation_*` names are aligned by generation index in `evolve_cli` JSON.
- When a metric family is inactive for a run, its per-generation arrays are zero-filled rather than omitted.
- `gpu_eval_*` is reserved for GPU fitness evaluation.
- `repro_*` is reserved for reproduction.
- `compile_ms`, `eval_ms`, `repro_ms`, and `total_ms` are coarse wall-clock rollups.

## Measurement Layers

There are three timing layers in the repo.

### 1. Runtime-internal phase timing

These are the direct timers produced by runtime code.

#### `FitnessSessionInitTiming`

Produced by `FitnessSessionGpu::init(...)`.

- `device_select_ms`: device selection and first-touch CUDA setup
- `shared_case_pack_ms`: host packing of shared cases and expected values
- `payload_cache_warm_ms`: payload cache preparation for shared inputs
- `upload_ms`: upload of shared-case buffers to device memory
- `total_ms`: full `init(...)` wall-clock

#### `FitnessEvalTiming`

Produced by `FitnessSessionGpu::eval_programs(...)`.

- `pack_ms`: host packing of bytecode programs, bounded bytecode verification, and payload token lookup
- `launch_prep_ms`: launch-shape preparation and host-side eval setup after packing
- `upload_ms`: per-eval device allocation, upload, and fitness buffer initialization
- `kernel_ms`: eval kernel launch plus synchronization; with a host overlap
  callback this uses CUDA events enclosing the kernel, excluding CPU callback
  work. The full callback remains inside `total_ms` and generation wall time.
  Overlapping reproduction preparation ranges must not be added to eval time.
- `copyback_ms`: device-to-host fitness copyback
- `teardown_ms`: teardown of temporary eval allocations after copyback
- `total_ms`: full `eval_programs(...)` wall-clock, including teardown

#### `ReproductionStats`

Produced by the reproduction backend.

- `selection_ms`: top-level selection phase
- `crossover_ms`: top-level crossover phase
- `mutation_ms`: top-level mutation phase
- `prepare_inputs_ms`: host extraction of genomes, fitness, and backend config
- `setup_ms`: GPU reproduction setup, device selection, and reusable arena preparation
- `preprocess_ms`: subtree/candidate/donor preprocessing
- `pack_ms`: host flattening into the packed GPU upload schema
- `upload_ms`: H2D upload of reproduction inputs
- `kernel_ms`: host wall time around GPU reproduction kernel launches and synchronization
- `copyback_ms`: child copyback to host staging
- `decode_ms`: reconstruction of `ProgramGenome` children from packed copyback buffers, including child analysis/admission
- `teardown_ms`: reproduction arena teardown
- `selection_kernel_ms`: selection-kernel subset of `kernel_ms`
- `variation_kernel_ms`: variation-kernel subset of `kernel_ms`

## Canonical Metric Families

### Coarse wall-clock metrics

These are the first metrics to use for end-to-end benchmark comparison.

| Metric | Meaning |
| --- | --- |
| `compile_ms` | Recorded genome-to-bytecode compilation spans; not the full compile-population wall time |
| `eval_ms` | One benchmark eval stage wall-clock |
| `repro_ms` | One benchmark reproduction stage wall-clock |
| `total_ms` | Full benchmark or full evolution run wall-clock |
| `generation_eval_ms` | One generation scoring-stage wall-clock in `evolve_cli` |
| `generation_repro_ms` | One generation reproduction-stage wall-clock in `evolve_cli` |
| `generation_total_ms` | One generation total wall-clock in `evolve_cli` |
| `final_eval_ms` | Final post-generation scoring wall-clock in `evolve_cli` |

Important notes:

- In `evolve_cli`, `generation_eval_ms` includes compile, scoring, canonicalization, and scored-population rebuild. It is intentionally broader than the device kernel path.
- The fixed-pop benchmark workflow derives `eval_ms` and `total_ms` from `evolve_cli --generations 1 --skip-final-eval on` rather than from a benchmark-only CLI.
- `repro_ms` can be lower than the sum of some `repro_*` prep phases when overlap is enabled, because part of reproduction is hidden behind evaluation.

### GPU eval metrics

These are the canonical metrics for GPU fitness attribution.

| Metric | Meaning | Direct or derived |
| --- | --- | --- |
| `gpu_eval_init_ms` | Full `FitnessSessionGpu::init(...)` wall-clock | Direct |
| `gpu_eval_call_ms` | Full `FitnessSessionGpu::eval_programs(...)` wall-clock | Direct |
| `gpu_eval_pack_ms` | Host program packing | Direct |
| `gpu_eval_launch_prep_ms` | Launch preparation and host-side pre-launch setup | Direct |
| `gpu_eval_upload_ms` | Per-eval upload/allocation phase | Direct |
| `gpu_eval_pack_upload_ms` | `gpu_eval_pack_ms + gpu_eval_upload_ms` | Derived convenience aggregate |
| `gpu_eval_kernel_ms` | Host wall time around eval kernel launch and synchronization | Direct |
| `gpu_eval_copyback_ms` | Fitness copyback | Direct |
| `gpu_eval_teardown_ms` | Temporary eval teardown after copyback | Direct |

Each evaluation launches one kernel family over the full population: generic
`Mixed`, or an opt-in type-proven integer/list profile (see
[payload design](../design/payload.md#proven-integerlist-execution-profile)).
`gpu_eval_kernel_ms` includes host launch and synchronization; CUDA activity time
must come from the trace, and must not be mixed with unprofiled generation totals.

`FitnessSessionGpu::eval_programs(programs, true)` optionally collects five counts
per program: evaluated cases, errors, timeouts (a subset of errors), fallback-token
results and unscored results. `GAGP_GPU_DIAGNOSTICS=1` enables this in evolution,
accumulating `program_cases`, `eval_errors`, `eval_timeouts`, `eval_fallbacks` and
`eval_unscored`. Counters are empty/zero when not requested; zero then does not
assert error-free evaluation. Device atomics and their transfer are diagnostic
overhead and must be disabled for headline timing runs.

### Reproduction metrics

These are the canonical metrics for reproduction attribution.

| Metric | Meaning |
| --- | --- |
| `selection_ms` | Host top-level selection phase |
| `crossover_ms` | Host top-level crossover phase |
| `mutation_ms` | Host top-level mutation phase |
| `repro_prepare_inputs_ms` | Reproduction input extraction and config setup |
| `repro_setup_ms` | GPU reproduction setup and reusable arena prep |
| `repro_preprocess_ms` | Preprocessing for subtree/donor metadata |
| `repro_pack_ms` | Host flattening into packed reproduction buffers |
| `repro_upload_ms` | Reproduction H2D upload |
| `repro_kernel_ms` | Host wall time around reproduction kernel launches and synchronization |
| `repro_copyback_ms` | Reproduction D2H copyback |
| `repro_decode_ms` | Host decode and analysis/admission of copied-back children |
| `repro_teardown_ms` | Reproduction teardown |
| `repro_selection_kernel_ms` | Selection-kernel subset of `repro_kernel_ms` |
| `repro_variation_kernel_ms` | Variation-kernel subset of `repro_kernel_ms` |

## Fixed-Pop Benchmark Workflow

The fixed-pop benchmark workflow now uses:

```bash
cpp/build/gagp_evolve_cli \
  --cases ... \
  --population-json ... \
  --generations 1 \
  --skip-final-eval on \
  --timing all \
  --out-json ...
```

The canonical derived benchmark fields are:

| Field | Meaning |
| --- | --- |
| `compile_ms` | `generation_cpu_compile_ms[0]` for CPU, or `generation_gpu_compile_ms[0]` for GPU |
| `eval_ms` | CPU: `generation_eval_ms[0] - generation_cpu_compile_ms[0]`; GPU: `gpu_eval_init_ms + generation_gpu_eval_call_ms[0]` |
| `steady_eval_ms` | CPU: `eval_ms`; GPU: `generation_gpu_eval_call_ms[0]` |
| `repro_ms` | `generation_repro_ms[0]` |
| `total_ms` | CPU: `generation_total_ms[0]`; GPU: `generation_total_ms[0] + gpu_eval_init_ms` |
| `warm_total_proxy_ms` | Optional derived comparison field: `total_ms` with cold `gpu_eval_init_ms` removed, and also `repro_setup_ms` removed for GPU reproduction modes |

The canonical GPU eval detail fields remain:

- `gpu_eval_init_ms`
- `gpu_eval_call_ms`
- `gpu_eval_pack_ms`
- `gpu_eval_launch_prep_ms`
- `gpu_eval_upload_ms`
- `gpu_eval_pack_upload_ms`
- `gpu_eval_kernel_ms`
- `gpu_eval_copyback_ms`
- `gpu_eval_teardown_ms`

Any external analysis script should derive fixed-pop benchmark reports directly from these generation-0 values rather than inventing alternate names.

## `gagp_evolve_cli`

The evolution CLI exposes the same timing families in three forms:

- summary `TIMING phase=...`
- per-generation `TIMING gen=...` and `TIMING gpu_gen=...`
- JSON in `meta.timing` and top-level `timing`

Relevant fixed-pop benchmark flags:

- `--population-json PATH`
- `--skip-final-eval {on|off}`

### Summary `TIMING phase=...`

Common summary phases:

- `init_population`
- `generations_eval_total`
- `generations_repro_total`
- `generations_selection_total`
- `generations_crossover_total`
- `generations_mutation_total`
- `generations_repro_prepare_inputs_total`
- `generations_repro_setup_total`
- `generations_repro_preprocess_total`
- `generations_repro_pack_total`
- `generations_repro_upload_total`
- `generations_repro_kernel_total`
- `generations_repro_copyback_total`
- `generations_repro_decode_total`
- `generations_repro_teardown_total`
- `generations_repro_selection_kernel_total`
- `generations_repro_variation_kernel_total`
- `cpu_compile_total`
- `final_eval`
- `total`

GPU summary phases:

- `gpu_eval_init`
- `gpu_compile_total`
- `gpu_eval_call_total`
- `gpu_eval_pack_total`
- `gpu_eval_launch_prep_total`
- `gpu_eval_upload_total`
- `gpu_eval_pack_upload_total`
- `gpu_eval_kernel_total`
- `gpu_eval_copyback_total`
- `gpu_eval_teardown_total`

### Per-generation console lines

`TIMING gen=...` reports the coarse per-generation wall clocks and reproduction breakdown:

- `eval_ms`
- `repro_ms`
- `total_ms`
- `selection_ms`
- `crossover_ms`
- `mutation_ms`
- `repro_prepare_inputs_ms`
- `repro_setup_ms`
- `repro_preprocess_ms`
- `repro_pack_ms`
- `repro_upload_ms`
- `repro_kernel_ms`
- `repro_copyback_ms`
- `repro_decode_ms`
- `repro_teardown_ms`
- `repro_selection_kernel_ms`
- `repro_variation_kernel_ms`
- `cpu_compile_ms`

`TIMING gpu_gen=...` reports GPU scoring detail for the same generation:

- `gpu_compile_ms`
- `gpu_eval_call_ms`
- `gpu_eval_pack_ms`
- `gpu_eval_launch_prep_ms`
- `gpu_eval_upload_ms`
- `gpu_eval_pack_upload_ms`
- `gpu_eval_kernel_ms`
- `gpu_eval_copyback_ms`
- `gpu_eval_teardown_ms`

For GPU evaluation populations of at least 32 members, compilation may use the
[bounded worker batches](../design/dataflow.md#bounded-gpu-evaluation-compilation). `gpu_compile_ms` sums the wall-clock spans from the
first to last published cache-miss compilation in each batch; it does not sum
concurrent worker durations. Such spans may include interleaved identity work.
The full generation wall time remains the acceleration comparison metric.
CPU evaluation retains sequential compilation. Small populations and an enclosing
payload transaction also retain sequential preparation.

### JSON timing keys

`meta.timing` stores run-level totals:

- `init_population_ms`
- `gpu_eval_init_ms`
- `final_eval_ms`
- `cpu_compile_ms_total`
- `gpu_compile_ms_total`
- `gpu_eval_call_ms_total`
- `gpu_eval_pack_ms_total`
- `gpu_eval_launch_prep_ms_total`
- `gpu_eval_upload_ms_total`
- `gpu_eval_pack_upload_ms_total`
- `gpu_eval_kernel_ms_total`
- `gpu_eval_copyback_ms_total`
- `gpu_eval_teardown_ms_total`
- `generations_selection_ms_total`
- `generations_crossover_ms_total`
- `generations_mutation_ms_total`
- `generations_repro_prepare_inputs_ms_total`
- `generations_repro_setup_ms_total`
- `generations_repro_preprocess_ms_total`
- `generations_repro_pack_ms_total`
- `generations_repro_upload_ms_total`
- `generations_repro_kernel_ms_total`
- `generations_repro_copyback_ms_total`
- `generations_repro_decode_ms_total`
- `generations_repro_teardown_ms_total`
- `generations_repro_selection_kernel_ms_total`
- `generations_repro_variation_kernel_ms_total`
- `total_ms`

Additional metadata fields relevant to fixed-pop benchmarking:

- `meta.population_source`
- `meta.population_json`
- `meta.skip_final_eval`

The top-level `timing` object stores per-generation arrays:

- `generation_eval_ms`
- `generation_repro_ms`
- `generation_cpu_compile_ms`
- `generation_gpu_compile_ms`
- `generation_gpu_eval_call_ms`
- `generation_gpu_eval_pack_ms`
- `generation_gpu_eval_launch_prep_ms`
- `generation_gpu_eval_upload_ms`
- `generation_gpu_eval_pack_upload_ms`
- `generation_gpu_eval_kernel_ms`
- `generation_gpu_eval_copyback_ms`
- `generation_gpu_eval_teardown_ms`
- `generation_selection_ms`
- `generation_crossover_ms`
- `generation_mutation_ms`
- `generation_repro_prepare_inputs_ms`
- `generation_repro_setup_ms`
- `generation_repro_preprocess_ms`
- `generation_repro_pack_ms`
- `generation_repro_upload_ms`
- `generation_repro_kernel_ms`
- `generation_repro_copyback_ms`
- `generation_repro_decode_ms`
- `generation_repro_teardown_ms`
- `generation_repro_selection_kernel_ms`
- `generation_repro_variation_kernel_ms`
- `generation_total_ms`

## Accounting Notes

- Native `*_kernel_ms` metrics above are host wall scopes, not CUDA device-activity timestamps. Do not add separately measured synchronization waits to these scopes: they overlap kernel execution. Nsight Systems device activities are needed to distinguish actual kernels, copies, and exposed API overhead.
- `compile_ms` omits some identity/cache preparation and worker setup outside the recorded compilation spans. For the entire compile stage, measure `compile_population` wall time; concurrent worker durations must not be added as generation wall time.
- The [1024 × 1024 fixed-ASGP profiler baseline](../../benchmarks/fixed_asgp/20261001-profiler/README.md) shows a concrete, additive Nsight/NVTX partition, including verification inside packing and admission inside reproduction. It also records overlap separately. This evidence does not rename the native metrics or change their timing boundaries.
- `gpu_eval_init_ms` depends on shared cases, payload setup, CUDA context state, and shared-buffer upload. It does not scale with program depth in the same way as `gpu_eval_call_ms`.
- `gpu_eval_call_ms` includes `gpu_eval_teardown_ms`. This is intentional so temporary eval allocation cleanup is no longer hidden in an unlabelled residual.
- `gpu_eval_pack_upload_ms` is a convenience aggregate. The direct timers remain `gpu_eval_pack_ms` and `gpu_eval_upload_ms`.
- `repro_kernel_ms` should equal the sum of `repro_selection_kernel_ms` and `repro_variation_kernel_ms` up to normal timer rounding.
- With `repro_overlap=on`, the coarse `repro_ms` value is a visibility metric, not a full-accounting sum of all reproduction work.
- With `skip_final_eval=on`, `final_eval_ms` is zero and the JSON final object is marked as skipped.
- New timing work should extend the direct phase structs first, then thread the names through CLI and JSON unchanged.

## Removed Legacy Names

The repo no longer emits the old GPU eval aliases:

- `pack_upload_ms`
- `kernel_ms`
- `copyback_ms`
- residual-derived fake GPU init metrics
- `gpu_session_init_ms`
- `gpu_scoring_*`
- `gpu_generations_*`

Use the canonical `gpu_eval_*` family instead.


Generic bounded-region GPU verification and descriptor/phase/binding flattening
are included once in the existing GPU evaluation packing interval. Their device
array allocations and copies are included in the existing upload interval. Region
workspace sizing and block-count planning belong to `gpu_eval_launch_prep_ms`;
workspace allocations belong to `gpu_eval_upload_ms` and their frees belong to
`gpu_eval_teardown_ms`. Phase execution and workspace reuse across programs remain
part of the Mixed kernel timing. There is no separate generic-phase
timer to add to these totals. Numeric reproduction contract and formal binder
preparation remain inside reproduction preprocessing, and copying the resulting
host tables remains inside reproduction packing.

Goal07 low-level reproduction launch timing accumulates the current pass's selection
and variation durations into `kernel_ms`. It does not add the already accumulated
selection/variation totals again, so callers sharing one stats object across device
passes retain `kernel_ms = selection_kernel_ms + variation_kernel_ms`.

Compiled GPU constant-domain materialization is charged once per run resource to
`repro_preprocess_ms`, where lazy initialization occurs. Per-pass stream/group
preparation remains in that interval. Static domain upload and host validation are
charged to `repro_upload_ms` only when the domain owner changes or the arena is
recreated. Subsequent passes upload their dynamic tables. No extra timer is added
to these totals. Synchronous and overlap modes use the same accounting scopes.
