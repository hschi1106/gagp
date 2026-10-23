# Priority from actual profiling — 2026-10-22 18:4x UTC

- Candidate13 exact unchanged-fitness reuse PASS6 native176.49s, immutable12f31bbf4e4a47f84ab7b508398132455661196cb1e3de3503d8435b364655fd. Source/binary/external/link audits passed. New reuse2 waits vm0; old shape1/register1/vm0/training0 continue on their own immutable8/9 binaries. Do not duplicate jobs or edit their frozen harnesses.
- NEW IMPORTANT evidence: profile-baseline0 completed nsight on oldTRAINING MedianP1024 and GradeP16384. Grade CUDA API cudaMalloc380calls=3.818513194s (64.1% of CUDA API time), cudaFree0.3400s, memcpy0.3130s. preserve_elites8kernel calls=0.537136270s (37.3% GPU-kernel time, NOT search wall). Median cudaMalloc only0.00427s; preserve_elites0.00245s. Do not mix profiled timings with unprofiled ratios.
- Unprofiled old baseline GradeP16384: search10.4245s, reproduction4.4606s but reproduction GPU-kernel0.6146s; initial admission2.5500s, GPU initialization0.1976s. This points to allocation/transport/lifetime overhead rather than solely the O(P^2) elite kernel. Next priority is a measured allocator optimization, not more small arithmetic shortcuts.

## Proposed NEXT implementation (NOT implemented yet)

Try a dedicated CUDA stream-ordered memory pool covering only the synchronous default-stream native-GPU phase path. It should remove repeated large cudaMalloc/cudaFree overhead while preserving GPU selection/variation/evaluation and physical capacities. Keep16GiB workspace and24GiB capacity guard; no smaller workspace or CPU reproduction.

- Consider cudaMemPoolCreate + cudaMallocFromPoolAsync/cudaFreeAsync, scope-local release threshold (e.g. fixed16GiB; not a reduced evaluation budget). Inspect installed CUDA12.6 headers / driver support. Avoid changing the device's global/default pool or relying on environment-wide daemons. No profiling tools exceptnsys.
- Scope starts in evolve_gpu_phase_population after the strict native-path gate and before owner import; owner must destruct before scope. Generic/overlap reproduction may use other streams and should keep its current allocation path. Default baseline flagOFF must call original cudaMalloc/cudaFree. Use one global opt-in setting selected on oldTRAINING/runtime/VRAM only.
- Important lifetime: outer FitnessSessionGpu outlives evolve_gpu_phase_population and may retain region/private workspace allocated inside the pool. Track pooled pointers with shared pool ownership so live allocations survive scope end, then free/destroy after the session actually releases them. Pool destruction should synchronize its default stream and release all retained backing; this must remain inside the overall timed evolve call. Do not cudaFree suballocated pool addresses or leave stale maps. Move/switch-device cases need correct recorded device and ownership.
- Native Buffer alloc/free sites are in repro/gpu/phase_population.cu; fitness_gpu.cu owns DeviceArena allocation, region/private/global replay workspace; host_pack_gpu.hpp cuda_alloc_and_copy_in template calls cudaMalloc/cudaFree; DeviceArena::~DeviceArena is already out-of-line in host_pack_gpu.cu. Cover matching allocate/free paths, including failure paths, without globally macro-replacing CUDA APIs. Add a small private wrapper/scope implementation and matching CMake source. Evolution C++ scope header should not force CUDA device headers into CPU-only builds.
- Preserve stream ordering: native path has default-stream kernels/copies; async free must be ordered after all uses. Do not apply a default-stream-only assumption to generic custom streams. Existing nonpooled pointers must still use real cudaFree. Requested-but-unsupported pool behavior must be explicit. Preserve failed allocations/attempts and CUDA errors; do not silently rerun scientific samples.
- Measure NVML24GiB peak: pool retains free backing, so allocator reservation can grow even though logical workspace stays16GiB. Unified pool should recycle between staged reproduction buffers and evaluation scratch, rather than keeping both physical sets separately. Driver release/trim/lifetime must be charged. If it cannot fit, report/reject it.
- Tests: native Evolve cacheOFF/ON same seed/full final AST/champion/fitness/wrong with GPU reproduction; ordinary and staged private-list/tuple cases, dirty recycled backing, session workspace that outlives pool scope, context/device cleanup. Existing raw NativePhasePopulation tests run OUTSIDE an evolve scope, so merely toggling a flag during them would NOT test the pool. Add dedicated scoped/integrated tests and run actual targeted regressions before any pilot. Full Release/allnative regression on exact winner still required later.
- For a new build: inspect all current controllers/realPIDs, then mutate under common GPU lock only once source stable; snapshot/pin as a NEW attempt (next available14). Never edit CPP during its build. Any earlier successful binary remains a preserved standalone candidate.
- After admission, freeze global pool flag/config, run fresh old-baseline / poolOFF / poolON controls on the development set and then all30 at BOTH populations; full AST/score/champion comparisons and GPU reproduction guards mandatory. Use only unprofiled search wall for geometric means.

## Secondary candidates

linear_elite.cuh + linear_elite_probe.cu passed98 standalone GPU ordering cases up toP16384; not integrated. O(P) single-elite preservation is lossless under existing fitness/index tie order, but the trace suggests it is secondary to allocation. Repro block-width retuning is also possible (one global setting, preserve per-child RNG/AST); no per-task final tuning.

For quality budget, common-loss quality-source already deduplicates identical champion ASTs. champion_audit.py currently scores all65 selected entries individually with native CPU. If cost would prevent80 fresh seed clusters, a NEW helper/harness could deduplicate exact ASTs within one fixed split, audit each unique program, and expand comparisons back to EVERY generation/final fitness/wrong. Report actual unique CPU calls/cases separately from expanded observations; never treat duplicates as independent evidence. Do not edit live training0/quality helpers: they are launch-hash frozen. Candidate/G/N/seedset must be frozen before any fresh heldout creation.

---

# Current next decisions — candidate9 admitted

- Build9 six targeted native checks,19 focused Python guards and4-task native champion smoke PASS. No2x claim, no new heldout/formal freeze. Current complete full30 evidence remains candidate6 case reuse1.0724/1.1233 atP1024/P16384.
- shape1 → register1 → vm0 form the running serialized development chain; training0 then measures all30 old-trainingG64 with full final AST and native champion scores. These scripts are frozen by launch hashes. Build9 binary29849304... is immutable.
- Read RECOVERY.md latest section and real controller PIDs before action. Wait for actual unprofiled all30 ratios before selecting or extending candidates. All successful/failing/stopped attempts remain preserved.
- Static bound512 stack increased from7968 to9136 bytes in build9 despite intended small-register VM; measure its runtime. Possible followups should eliminate verified-unused generic phase dispatch/call frames, or reuse exact unchanged-offspring results with explicit session/generation invalidation; don't infer speedups from register counts. Private IntList frame compaction is another candidate for list tasks.
- New quality heldout stays unopened until candidate, globalG/repetitions/seeds and runtime forecast are frozen. Remaining time, reporting reserve and exact coverage take precedence over aspirational power.

---

## Historical planning (superseded by state above)

# Next development decisions (not a frozen formal selection)

- Current full30 P1024 simple pilot: case_only1.072394747, simple1.070244510 geometric means. Exact full final AST/descriptors/champions match. The earlier13-task1.32/1.45 numbers are a development subset; never report them as all30 success.
- Finish current full30 P16384 controls. Build7 is waiting for simple1 exit and includes shape-result caching plus a512-thread register specialization. It must build/check/pin all current CPP files once the source is stable. Shape0 then compares baseline/case_only/shape/simple+shape; register0 waits shape0 and compares baseline/reg512/reg512+shape. Every arm uses fresh controls, source/code hashes and exact AST comparisons.
- Inspect actual build7 test failures and preserve them. The dedicated cache test covers distinct list contents and labels, discounted scores, low fuel, a reachable element reader, an unreachable combine reader under a literal-true base predicate, and saturated65536 backing extents. Native32generation audits now also select reg512. Do not assume these untested changes pass.
- Read static resources from the pinned binary under common lock (`cuobjdump --dump-resource-usage`), compare the old bound1024 direct-root symbol with the extra bound512 symbol. This is resource metadata; unprofiled full search wall remains the speed metric. No ncu.

## If shape cache is too restrictive

Its current grouping key includes ALL effective scalar bindings. A random target/index capture that a program never LOADs can make every input shape unique and decline the whole session. A stronger exact candidate can project the input key using fresh direct-root/phase dependencies:

1. Always include root LOAD locals (state/bound operands) and their assignment bits. Direct-root proof excludes STORE, so captures refer to original immutable case locals.
2. Scan each reachable phase LOAD; for bindings in Parameter bank, include `parameter_caller_locals[binding.slot]`. State/Prepared/Result/Measure dependencies are covered by root inputs and all reachable phase parameters. Include boundary. Under literal-true base predicate, cut/request/combine remain unreachable and their parameters need not be included.
3. Keep the current no-element-read/no-list-key-memo/scalar-result proof, exact backing-length proof and original per-case scoring/order. No data/test-guided output approximation.
4. Cache case partitions by64-bit dependency mask at session scope, built once per observed mask. Bound to64partitions and bounded GPU metadata; memoize declined masks too. Program metadata needs group-map offset, representative offset and group count. GPU group IDs/representatives can be flattened arrays; refresh upload only when a new admitted mask appears, synchronized between evaluations.
5. Shared cache storage uses max admitted group count; each program executes its own group count. Report total cached-program groups and actual root invocations. Do not reuse results across programs/generations. Existing complete-case reuse must still leave>=4fold extra savings.
6. New tests must cover ignored scalar/missing/Float captures, a USED scalar varying within one list length, used-parameter name/type errors, and changing masks after GPU reproduction; compare full fitness/wrong/counters/ASTs and independent CPU.

Do not edit CPP while build7 is compiling/testing. Use the common GPUlock for source mutation if a queued build could start, and assert its manifest is still waiting after acquiring the lock. Alternatively wait until build7 finishes, then implement a new candidate and a new build/pilot name.

## Other execution candidates

Private IntList SequenceWindows still uses tagged80/96-byte frames; a private normalized IntList result proxy could reuse an unboxed AoS/SoA frame (~48/56bytes) while preserving `b=true`, exact allocation, fuel, frame limits and replay sizing. It requires changes to region_types/window accessor, workspace allocation including replay, kernel slot addressing and private dispatch. Test lowfuel and changing populations, staged/reusable workspace and overflow replay. This mainly affects list/tuple tasks and cannot alone justify all30×2.

No formal/global scale freeze and no fresh heldout currently exist. Validate formal.py / quality.py guards, then select one binary/global settings using training fidelity/runtime only. Estimate full30 timing repetitions plus all-program CPU audits and new-seed quality. Reserve3h reporting. Preserve failures/native-unadmitted/budget-not-run. Current inference guard8/8 passed; final full native regression on exact selected binary remains required.
