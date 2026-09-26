# Goal 11.5 restricted evolution results

Measurement date: 2026-09-27. Clean-snapshot validation is complete; final commit
identity is recorded in the handoff below.

The restricted implementation substantially reduces CPU reproduction cost but
remains **materially below** the historical reference acceleration target. The
revised Goal 11.5 has no mandatory Q threshold; the original unrestricted Goal 11
performance acceptance remains unpassed. CPU was measured again, not slowed to
inflate the CPU/GPU ratio.

## Versions and reproducibility

- Immutable Goal 01: `b04918307eb69ec0f08c6bf5b03a0fae9399dbbf`, unchanged reference
  libraries with the frozen snapshot benchmark adapter.
- Stage A recovery: `2d11d4a`, documented in [the checkpoint report](goal-11.5-stage-a.md).
- Stage B: reviewed working source based on `7c11429`; final commit identity pending.
  `reviewed-source-hashes.json` identifies every file of the clean build snapshot.

All timed binaries use Release/O3, GCC 11, CUDA 12.6, sm_86 and maxrregcount=64
on Snoopy's RTX 3090 (`GPU-9c69fabe-a876-c803-9278-b54b8ec52daf`). Raw build logs,
commands, return codes, cold wall time and output/log SHA-256 receipts live outside
the repository at `/home/hschi1106/gagp-stage-b-20260927` on the authorized SSH host;
the local copy is `/home/hschi1106/gagp-artifacts/grammar-migration/goal-11.5-stage-b`.
The compact [measurement record](goal-11.5-measurements.json) includes every receipt
hash, medians, individual observations, phase timings and population diagnostics.

| Timed binary | SHA-256 |
| --- | --- |
| reference | `a10d20cfd2aafeac6e10994d34c4d70b3c5f6a467a8944483f1d82c998c1555e` |
| stage_a | `59f17fcf98d2f44d75d771bafced6f0c8ec8ca34b4e8f97e5a7f0693d12a7089` |
| restricted | `1e3049c219b31ca63d08b954b324f43b94946bf09dcbd1274da5fe983d694223` |

The reproducible campaign is `python3 runner.py --scope evolution`, followed by
`--scope five`, `--scope steady`, and `--scope stats`, from the remote artifact
root. Its exact source hash is in the measurement record. Each receipt retains
the fully expanded CLI command. The input root is the preserved
`/home/hschi1106/gagp-goal11-representative-01`; reference artifacts are unchanged.
Use the corresponding audited CPU/GPU donor grammar in `donor-routing-audit`,
`mixed-routing-audit`, `nested-metadata-routing-audit`, and `dp2d-routing-audit`.
These per-backend routes were selected before this implementation.

The six workloads were preselected, not replaced after timing. Each evolution
row has one warmup and three measured fresh CLI processes in paired blocks,
rotating version order. Five-generation runs use the p64 canonical row. Steady
fixed-work evaluation uses three processes, each with one warmup and one measured
call. The 24 instrumented population runs are diagnostic only. Total: 288 one-gen,
48 five-gen, 108 steady-evaluation and 24 diagnostic processes, all successful.
Three observations establish these local medians, not a confidence interval.

An initial runner incorrectly used DC frames=8 for the metadata fixture, whose
frozen mapping requires 4, causing Stage A membership rejection. The failed run
and all affected nested/metadata observations are preserved under
`frame-mapping-diagnostic/`; they are excluded. Corrected rows use 4 for both
nested and metadata, preserving the existing mapping and the identical inputs.
The initial CPU-only build is also retained as a diagnostic, never a GPU result.

## Implemented restrictions

The [pre-implementation compatibility inventory](goal-11.5-compatibility.md)
records retained families and boundaries. The production architecture remains
one data-defined grammar implementation, without package-name dispatch.

1. The compiler computes conservative recursive replacement-language classes.
   Differently named equivalent rules can share a class; equal type or a supplied
   crossover-group label alone cannot establish interchangeability. Exact category,
   scope and ordinary-name availability remain checked. Closed exchanges require
   both the certificate and a runtime closure proof. Imported programs and public
   prepared states retain complete membership, ownership and payload checks.
2. Mutation uses grammar inputs, declared lexical binders and explicit
   `mutation_locals` (empty by default), excluding incidental destination locals.
   Bounded GPU preparation groups the same entry, environment, binder mapping,
   node/depth budgets and seed count. At most eight equivalent jobs share a pool
   before refill; 128 jobs, 64 slots/job, a 1,048,576-node reservation bound and at
   most eight workers bound a preparation window. Windows do not persist across
   generations. Rejected reuse gets the job's fresh bounded retries. CPU selected-
   site donors remain fresh. Pool correlations and changed RNG trajectories are
   intentional; identical support does not mean identical independent sampling.
3. Template control/binding skeletons stay fixed. Typed holes and explicit
   `mutable: true` constants evolve; undeclared fixed constants must be singleton.
   Shared holes remain one atomic logical choice, with at most 64 authored uses;
   unsupported relationships fail validation. Whole-template replacement and
   reference-reachable structured donors remain available.

All eight runtime types, prefix AST, typed_subtree, tournament selection,
crossover-before-mutation, execution fuel/error semantics and typed payload
ownership remain. Construction limits are distinct: 80 nodes/depth 7 in source
resources map to 531 nodes/depth 141 in the physical representation. As in the
frozen mapping, the initial materialized population may exceed source depth 7;
the depth-7 source constraint governs offspring, while physical bounds and initial
node limits remain checked. This is recorded as `initial_depth_unbounded` in
every candidate receipt output. The semantic
identity is `gagp-native-2.0.0-restricted-1`; old replay identity is not silently
accepted. [The authoring example](../../../configs/grammar/examples/restricted_variation.json)
combines equivalent named rules, a fixed input interface and a mutable template
constant. The normative contract is [grammar_definition.md](../../../spec/grammar_definition.md).

## Generation wall time and speedups

Milliseconds are median sums of generation wall timers, excluding cold loading,
initialization and final evaluation. `E` means GPU evaluation with CPU reproduction;
`D` means GPU direct reproduction; `O` means GPU overlap. Evolving rows measure the
whole supported evolution workflow, **not identical changed search work**. Initial
materialized inputs are equal; crossover eligibility, donor correlation and later
program populations can differ.

| Workload | Version | CPU ms | E ms | D ms | O ms | S(E) | S(D) | S(O) |
| --- | --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| simple_exp_1024-p64 | reference | 97.748 | 6.241 | 9.980 | 8.698 | 15.663 | 9.795 | 11.238 |
| simple_exp_1024-p64 | stage_a | 438.853 | 362.787 | 66.749 | 65.126 | 1.210 | 6.575 | 6.739 |
| simple_exp_1024-p64 | restricted | 152.442 | 75.281 | 62.627 | 61.611 | 2.025 | 2.434 | 2.474 |
| simple_exp_1024-p1024 | reference | 3049.941 | 55.525 | 73.881 | 63.484 | 54.929 | 41.282 | 48.043 |
| simple_exp_1024-p1024 | stage_a | 5987.150 | 3180.448 | 443.402 | 410.879 | 1.882 | 13.503 | 14.572 |
| simple_exp_1024-p1024 | restricted | 3563.729 | 830.986 | 423.912 | 402.996 | 4.289 | 8.407 | 8.843 |
| mixed_exact_payloads-p64 | reference | 57.783 | 3.868 | 7.031 | 6.580 | 14.937 | 8.218 | 8.782 |
| mixed_exact_payloads-p64 | stage_a | 63.096 | 17.165 | 21.280 | 20.697 | 3.676 | 2.965 | 3.049 |
| mixed_exact_payloads-p64 | restricted | 59.721 | 13.561 | 19.504 | 18.375 | 4.404 | 3.062 | 3.250 |
| nested_binders-p64 | reference | 103.796 | 4.139 | 7.137 | 6.817 | 25.079 | 14.543 | 15.226 |
| nested_binders-p64 | stage_a | 118.041 | 35.496 | 24.618 | 23.280 | 3.325 | 4.795 | 5.070 |
| nested_binders-p64 | restricted | 101.462 | 19.133 | 25.432 | 24.422 | 5.303 | 3.990 | 4.154 |
| metadata_stress-p64 | reference | 66.930 | 4.798 | 8.351 | 7.086 | 13.950 | 8.014 | 9.446 |
| metadata_stress-p64 | stage_a | 82.652 | 28.996 | 27.163 | 25.512 | 2.851 | 3.043 | 3.240 |
| metadata_stress-p64 | restricted | 72.931 | 19.438 | 22.067 | 20.396 | 3.752 | 3.305 | 3.576 |
| dp2d-p64 | reference | 23.509 | 4.645 | 8.043 | 6.735 | 5.061 | 2.923 | 3.491 |
| dp2d-p64 | stage_a | 54.135 | 33.283 | 24.514 | 22.827 | 1.627 | 2.208 | 2.372 |
| dp2d-p64 | restricted | 37.665 | 15.049 | 23.784 | 22.495 | 2.503 | 1.584 | 1.674 |

`S=T_cpu/T_gpu`; `Q=S_restricted/S_reference`. Absolute ratios above 1 mean
the restricted version takes less time. A faster CPU can reduce Q even when
GPU time improves, as the p1024 comparison with Stage A demonstrates.

| Workload | Mode | Q | Reference/new time | Stage A/new time |
| --- | --- | ---: | ---: | ---: |
| simple_exp_1024-p64 | gpu_eval | 0.129 | 0.083 | 4.819 |
| simple_exp_1024-p64 | gpu_repro | 0.249 | 0.159 | 1.066 |
| simple_exp_1024-p64 | gpu_repro_overlap | 0.220 | 0.141 | 1.057 |
| simple_exp_1024-p1024 | gpu_eval | 0.078 | 0.067 | 3.827 |
| simple_exp_1024-p1024 | gpu_repro | 0.204 | 0.174 | 1.046 |
| simple_exp_1024-p1024 | gpu_repro_overlap | 0.184 | 0.158 | 1.020 |
| mixed_exact_payloads-p64 | gpu_eval | 0.295 | 0.285 | 1.266 |
| mixed_exact_payloads-p64 | gpu_repro | 0.373 | 0.361 | 1.091 |
| mixed_exact_payloads-p64 | gpu_repro_overlap | 0.370 | 0.358 | 1.126 |
| nested_binders-p64 | gpu_eval | 0.211 | 0.216 | 1.855 |
| nested_binders-p64 | gpu_repro | 0.274 | 0.281 | 0.968 |
| nested_binders-p64 | gpu_repro_overlap | 0.273 | 0.279 | 0.953 |
| metadata_stress-p64 | gpu_eval | 0.269 | 0.247 | 1.492 |
| metadata_stress-p64 | gpu_repro | 0.412 | 0.378 | 1.231 |
| metadata_stress-p64 | gpu_repro_overlap | 0.379 | 0.347 | 1.251 |
| dp2d-p64 | gpu_eval | 0.495 | 0.309 | 2.212 |
| dp2d-p64 | gpu_repro | 0.542 | 0.338 | 1.031 |
| dp2d-p64 | gpu_repro_overlap | 0.480 | 0.299 | 1.015 |

Nested GPU reproduction is slightly slower than Stage A; it is retained in the
comparison. No row was removed to improve the apparent result.


| Workload | Reference/new CPU time | Stage A/new CPU time |
| --- | ---: | ---: |
| dp2d-p64 | 0.624 | 1.437 |
| metadata_stress-p64 | 0.918 | 1.133 |
| mixed_exact_payloads-p64 | 0.968 | 1.057 |
| nested_binders-p64 | 1.023 | 1.163 |
| simple_exp_1024-p1024 | 0.856 | 1.680 |
| simple_exp_1024-p64 | 0.641 | 2.879 |

## Five-generation canonical evolution

| Version | CPU ms | E ms | D ms | O ms | S(E) | S(D) | S(O) |
| --- | ---: | ---: | ---: | ---: | ---: | ---: | ---: |
| reference | 532.374 | 25.656 | 49.908 | 42.312 | 20.751 | 10.667 | 12.582 |
| stage_a | 2230.292 | 1880.793 | 242.810 | 241.131 | 1.186 | 9.185 | 9.249 |
| restricted | 597.496 | 186.147 | 227.341 | 226.543 | 3.210 | 2.628 | 2.637 |

Fitness histories, node sizes and counters are in the JSON. Restricted direct and
overlap histories agree; the reference's direct/overlap trajectories differ in
this frozen implementation. Exact old/new evolutionary trajectories are not an
acceptance requirement. Five generations cannot establish equal solution quality.

## Fixed-work evaluation correctness

All six workloads, both CPU and GPU evaluation, all three observations: Stage A
and restricted per-member fitness arrays equal the reference **exactly within
the same backend**. This does not erase inherited CPU/GPU discrepancies: the
p1024 reference discrepancy at members 202 and 727 remains the Stage A payload
fallback reproducer described in its checkpoint report. No expected output was
changed. The fixed-work results isolate evaluation from changed evolution work.


| Fixed workload | Version | CPU call ms | GPU call ms | S |
| --- | --- | ---: | ---: | ---: |
| dp2d-p64 | reference | 22.455 | 1.251 | 17.950 |
| dp2d-p64 | stage_a | 27.999 | 2.296 | 12.193 |
| dp2d-p64 | restricted | 27.254 | 2.417 | 11.278 |
| metadata_stress-p64 | reference | 68.187 | 1.409 | 48.397 |
| metadata_stress-p64 | stage_a | 59.972 | 2.325 | 25.798 |
| metadata_stress-p64 | restricted | 60.473 | 2.524 | 23.956 |
| mixed_exact_payloads-p64 | reference | 59.826 | 0.847 | 70.593 |
| mixed_exact_payloads-p64 | stage_a | 50.590 | 0.855 | 59.137 |
| mixed_exact_payloads-p64 | restricted | 49.766 | 0.860 | 57.875 |
| nested_binders-p64 | reference | 105.979 | 0.934 | 113.473 |
| nested_binders-p64 | stage_a | 88.104 | 1.060 | 83.108 |
| nested_binders-p64 | restricted | 87.911 | 1.056 | 83.266 |
| simple_exp_1024-p1024 | reference | 3185.073 | 25.334 | 125.723 |
| simple_exp_1024-p1024 | stage_a | 2902.036 | 28.474 | 101.919 |
| simple_exp_1024-p1024 | restricted | 2865.996 | 28.057 | 102.149 |
| simple_exp_1024-p64 | reference | 94.904 | 2.141 | 44.327 |
| simple_exp_1024-p64 | stage_a | 84.886 | 2.120 | 40.038 |
| simple_exp_1024-p64 | restricted | 84.125 | 2.112 | 39.826 |

## Optimization and remaining costs

Only two optimization rounds followed the restrictions. Round 1 reused certified
per-root charge invariance for crossover feasibility instead of constructing a
full trial child for every pair. Round 2 used proved replacement closure and
projected allowances for generated donors. Full final-child validation remains.
Single-observation p1024 diagnostics moved CPU from 6258.9 to 3733.3 to 3531.7 ms;
GPU moved 407.5 to 421.9 to 397.1 ms. These diagnostics used the CPU proposal
profile even in GPU mode and are not substitutes for the audited final matrix.
Existing unused-donor skipping, parallel generation and prefetch predate this work.

The final p1024 GPU pass breakdown follows; donor time is nested in preprocessing,
and waits/overlap cannot be added to other timers as independent work.

| Phase, ms | Direct | Overlap |
| --- | ---: | ---: |
| total_ms | 423.912 | 402.996 |
| eval_ms | 71.886 | 80.936 |
| evaluation.gpu_compile_ms | 22.081 | 23.583 |
| evaluation.gpu_eval_kernel_ms | 28.117 | 27.977 |
| evaluation.gpu_eval_upload_ms | 0.688 | 0.776 |
| evaluation.gpu_eval_copyback_ms | 0.015 | 0.028 |
| reproduction.crossover_prepare_ms | 64.576 | 68.003 |
| reproduction.crossover_preprocess_ms | 6.228 | 7.564 |
| reproduction.crossover_donor_ms | 0.223 | 0.263 |
| reproduction.crossover_pack_ms | 3.375 | 7.363 |
| reproduction.crossover_decode_ms | 35.060 | 35.879 |
| reproduction.mutation_prepare_ms | 4.648 | 4.648 |
| reproduction.mutation_preprocess_ms | 136.181 | 139.085 |
| reproduction.mutation_donor_ms | 118.038 | 119.894 |
| reproduction.mutation_pack_ms | 11.005 | 7.845 |
| reproduction.mutation_decode_ms | 48.605 | 47.868 |
| reproduction.kernel_ms | 1.867 | 1.855 |
| reproduction.upload_ms | 3.471 | 3.412 |
| reproduction.copyback_ms | 0.572 | 0.565 |
| reproduction.replay_validation_ms | 0.000 | 38.873 |
| reproduction.overlap_wait_ms | 0.000 | 6.763 |

Host donor generation, parent certification and child decode remain dominant;
GPU reproduction kernels are a small fraction. Mutation preparation depends on
crossover children, so its entire preprocessing cannot be hidden behind the
preceding evaluation. Replay-validation time is zero on the internal immediate
path; public prepared replay remains checked and is exercised by GPU tests.
Cold CLI, load, initialization, evaluation compilation/transfers and all measured
observations are recorded separately in JSON; phase medians need not sum exactly.


Cold p1024 process and initialization timings (ms):

| Version/mode | Cold CLI | Load | Population init | GPU eval init | Generation |
| --- | ---: | ---: | ---: | ---: | ---: |
| reference/cpu | 3126.751 | 70.501 | 0.260 | 0.000 | 3049.941 |
| reference/gpu_eval | 303.353 | 68.668 | 0.256 | 122.410 | 55.525 |
| reference/gpu_repro | 323.517 | 69.702 | 0.301 | 122.045 | 73.881 |
| reference/gpu_repro_overlap | 313.281 | 70.531 | 0.238 | 122.350 | 63.484 |
| stage_a/cpu | 6809.023 | 662.550 | 151.786 | 0.000 | 5987.150 |
| stage_a/gpu_eval | 4187.102 | 634.647 | 150.733 | 122.929 | 3180.448 |
| stage_a/gpu_repro | 1488.445 | 695.744 | 151.219 | 1.440 | 443.402 |
| stage_a/gpu_repro_overlap | 1437.153 | 677.540 | 152.362 | 1.350 | 410.879 |
| restricted/cpu | 4550.924 | 809.586 | 152.593 | 0.000 | 3563.729 |
| restricted/gpu_eval | 2025.836 | 812.483 | 150.344 | 137.545 | 830.986 |
| restricted/gpu_repro | 1660.304 | 875.599 | 152.453 | 1.419 | 423.912 |
| restricted/gpu_repro_overlap | 1618.306 | 857.792 | 151.611 | 1.417 | 402.996 |

## Validation and handoff

Integrated candidate checks passed 118/118 native tests (including actual CUDA),
84/84 operational tool tests and 23/23 repository checks. An additional GPU regression
verifies explicit mutable template constants, fixed skeleton preservation and
seeded direct/overlap equality. The clean-source Release/CUDA build independently passed all 118 native tests.
Both scalar initialization and the new restricted example passed validation,
inspection, resolution, population generation, byte-identical export/replay and
all four two-generation execution modes (19 commands, all exit 0).
Source comparison found no C++ differences between timed and clean snapshots.
The separate clean build has different binary identities, recorded explicitly;
only the campaign identities above are used for timing. No unrelated untracked
file was included in the 570-file build snapshot.


Clean reproduction commands (from `clean-source/`):

```sh
cmake -S cpp -B cpp/build -DCMAKE_BUILD_TYPE=Release -DGAGP_ENABLE_CUDA=ON -DGAGP_BUILD_BENCHMARKS=ON -DCMAKE_CUDA_COMPILER=/usr/local/cuda-12.6/bin/nvcc -DCMAKE_CUDA_ARCHITECTURES=86 -DCMAKE_CUDA_FLAGS=--maxrregcount=64
cmake --build cpp/build -j 8
ctest --test-dir cpp/build --output-on-failure
python3 -m unittest discover -s tools/tests -p 'test_*.py' -v
python3 -m unittest discover -s tests/repository -p 'test_*.py' -v
```

`python3 workflows.py` from the artifact root executes the exact authoring and
all-mode CLI commands recorded in the measurement JSON. Build logs, the 19
workflow outputs and replay files remain under `clean-workflows/` and
`clean-{build,native}.log`. The tested snapshot/archive and C++ source hashes
are in JSON. The final implementation commit contains those sources.

The final donor probes sample 32 seeds at each of `Mixed.Factor.Int`,
`Mixed.Factor.Float`, `Mixed.Empty`, nested-index, metadata-index and DP2D coordinate
sites. Their frozen CPU routes produce 0 structured donors; GPU routes produce
4/32 at each site, all admitted within the source budget. This preserves actual
backend routing differences rather than claiming identical donor policies.
These sampled probes supplement the full structural/operator tests; they do not
prove every possible generated program has unchanged search probability.

## Search work, diversity and scope of conclusions

| Workload | GPU direct initial/final unique | Initial/final logical sites | NFE requested |
| --- | ---: | ---: | ---: |
| dp2d-p64 | 1/28 | 192/189 | 64 |
| metadata_stress-p64 | 6/34 | 215/160 | 64 |
| mixed_exact_payloads-p64 | 4/29 | 208/206 | 64 |
| nested_binders-p64 | 1/26 | 320/303 | 64 |
| simple_exp_1024-p1024 | 911/884 | 23924/22915 | 1024 |
| simple_exp_1024-p64 | 64/54 | 1648/2553 | 320 |

The p64 canonical diagnostic spans five generations (320 NFE); the other rows
span one. Each NFE request evaluates all corresponding fitness cases; duplicate
programs remain counted. All captured populations passed native verification.
Node/depth ranges and means, physical occurrences, closed sites, node-kind
histograms and contract-compatible ordered site pairs are included in JSON.
The pair count includes same-parent pairs and omits resource feasibility; it is
not the number of executable crossover attempts.

For canonical p1024 direct preparation, 372 pool classes supply 1488 fresh and
224 reused batch slots, with 0 rejected reused slots. These exclude sequential
preparation and per-slot retry attempts. Each new preparation refreshes its pools;
reuse does not pin evolution to a permanent tiny donor set. GPU direct p64
canonical uniqueness moves 64 → 61 → 60 → 57 → 51 → 54 over five generations.
This supports bounded refresh in these observations, not universal diversity or
solution-quality preservation. `changed_children`, `unchanged_children` and
fallback counters accumulate across crossover and mutation passes; they are not
counts of distinct final members.

Unsupported authoring combinations fail validation (non-singleton fixed constants,
mutable annotations outside fixed template constants, invalid/missing local
interfaces, incompatible shared-hole scope or more than 64 uses). Differently
shaped but semantically equivalent languages can remain in separate conservative
compiler classes. Imported materialized execution is independent of packages;
the three restrictions limit construction/variation, not the runtime type set.
The revised completion must not be described as passing original Goal 11 or as
being close to reference GPU acceleration.
