# ASGP 30-task support and aligned metrics — 2026-10-15 (validation complete)

- Integration `feat/asgp-support-contract-20261015`, worktree `gagp-support-20261015`; protected checkpoint `dc99120`. Runtime checkpoint `e2da6ad`, diagnostic/migration/test followups through `8163e47`. Main fast-forward delivery; preserve the dirty original main checkout rather than resetting it. Original chat worktree and historical inputs/binaries remain unchanged.
- Three Astra-medium agents; native five modes, CPU1T and GPU reproduction retained. Added configurable task fitness, elite1 alignment, wrong-count ranking, exact int64 execution, generic protected truncating primitives, source-derived grammar adapters, bounded CPU payload lifetime, separate GPU extended arena and safe memo admission. Named search-rule/resource differences remain; `fully_aligned=false`.
- 30/30 P1024 admission; 150/150 P8/full1024/G2 five-mode searches complete. Combined strict gate23/30: six source/native resource-status gaps (including CumulativeSum source-language limitation), plus NegativeToZero GPU arena mismatch. Known witnesses29/30. Do not call all30 fair or fully supported.
- P1024/full1024 quality: three tasks x3seeds x2gens x5modes,45searches+45heldout audits passed in197.6s. No clear short-run degradation; Sum already solved atgen0 and Median unchanged. No convergence equivalence claim; final native audit16programs only.
- One-shot two-generation search wall (seconds), ASGP/CPU/GPU-e/GPU-er/GPU-er-o: Sum6.068/20.500/1.089/1.152/1.074; House3.053/8.111/.943/.521/.518; Median.171/1.453/.764/.769/.779. New grammar/metric/input contract, own controls; no old25x/30x denominator reuse. Median GPU slower. Native AST fallback causes Sum/Median host decode cost; House retains native phase path.
- Build complete. Native128 covered (126firstpass+2corrected retests),3focused external regressions; tools100(4skips),repository24. Preserved old migration fixtures; changed wrong int64 rounding expectations to exact low-bit checks. No full precision repeats/scaling/long-run benchmark.
- [Chinese report](results/support-20261015/REPORT.zh-TW.md), raw compact summaries/manifests/settings/tests under same directory. Full logs/binary/exports: `logs/support-20261015/{native-support,quality,benchmark}`. Failed macro-lowering prototype removed; builtin lowering preserves physical source-op count. Payload metadata reuse has no isolated speedup claim.
- Reproduce `PYTHONPATH=tools python3 -m gagp_tools.experiments.fixed_asgp.low_budget support --out logs/support-NEW --budget 600 --timeout 25`; then `quality`/`benchmark --artifacts logs/support-NEW --out <NEW>`. DefaultsP1024/C1024/G2 for latter stages. GPU lock `/tmp/gagp-support-20261015-gpu.lock`; never prepare old inputs.
- Next: admit repeated-hole guards/captures into existing compact GPU phase pipeline without changing source semantics; align list lifetime/resource budgets; broaden quality only after support gates. No active experiment or automatic continuation required.

---

# Expanded native benchmark — 2026-10-14 (complete)

- Runtime unchanged from db1dd55/2938d69; source checkpoint41f2566, binary SHA b9952b911df5f22f1ed361472663a114994882278bcededfbe3f8efd3d3d7980. Original worktree/frozen inputs preserved; no push.
- Count Odds / Last Index: P1024/C1024, CPU1T,16generations×3seeds×5native modes=30completed searches. Full-search medians ASGP31.352/24.826s, GPU-er-o1.005/1.017s →31.21x/24.41x. Complete CLI ratios13.57x/11.82x. New common-fitness denominators; original Last discounted control separately27.440s, not substituted.
- Final independent exact accuracy ASGP19.43%/22.66%, GPU-er-o7.42%/15.33%. Additional archived-training-best audits12.60%/22.66%; no convergence equivalence. Native GAGP can lose best; ASGP elite1. Neither task solved in these16gens. Native pipeline/CPU1T retained; no solver or known-solution injection.
- All30 formal +30 final/history quality +30 archived-best quality +3 native-policy +3 native-quality cells completed. Initial fullpopulation snapshots retain66/21ASGP-CPU mismatches and1/0CPU-GPU mismatch. Known-solutions and finaltop16GPU/CPU audits pass; scope not full exact parity. Last repeated-square numeric precision discrepancy independently reproduced; others not fully diagnosed.
- Stock exact clamped memo transition capability probes reject both candidate encodings. Negative knownsolutionGPU fails; CPU randomP8 preflight stopped at95.36GiB RSS. No qualified ratios for either. No rerun of unsafe randomlist evolution. Explicit expected failures/skips preserved, not called support.
- Release benchmark build; focused native CTest passes (2successes,2expected failures,1skip); tools7/repository24/report6 pass. Original3tasks/five modes smoke preserved. Formal processes16.16min; elapsedstage18.44min including shared-lock waits. No scaling or long-run claim this round.
- [Report](results/expanded-20261014/REPORT.zh-TW.md), [settings](results/expanded-20261014/settings.json), [contract](results/expanded-20261014/contract.json), [all seed timings](results/expanded-20261014/final/per-seed.csv), [all480 generation rows](results/expanded-20261014/final/generations.csv), [quality](results/expanded-20261014/final/best-ever.csv). Rawlogs/artifacts `logs/optimization/expanded-20261014/`. Serial `run.py <pilot|formal|quality|best-quality|native-controls>`; `summarize.py` + `summarize_best.py` regenerate figures into a NEW directory. Exact commands in report; do not prepare old inputs.
- Next work: loss of historical best / equal-wall-clock quality, explicit integer semantics, bounded list payload/GPU evaluation, clamped memo contracts. These are identified followups, not changes implemented here. All jobs complete.

---

# Sum25 / House25 / Median5 — 2026-10-14 (complete)

- Best runtime `2938d69`; final test-only correction `c0d24a9`; branch `opt/gpu-generation-20261001`, worktree `gagp-opt-20261001`. Candidate binary SHA`e4c8e8ebebab92a6cc81be1e8df31d0a43d9dc9fb6f9dacc12e0f302c36e519b`. Start checkpoint `checkpoint/pre-sum25-20261014`=`320d8bf`; original worktree clean at`75d7f83`, archived baseline/frozen inputs preserved. No push.
- Native pipeline, CPU1T and GPU reproduction unchanged; no RNG/operator/numeric/fuel/grammar changes. All64 original flags plus `GAGP_MEASURE_PHASE_CACHE=1`, `GAGP_PER_PHASE_STACK_BOUND=1`, `GAGP_HYBRID_PHASE_EXECUTOR=1`, `GAGP_GPU_WINDOW_LEAF_DIRECT=1`, `GAGP_WINDOW_SOA=1`, `GAGP_DYNAMIC_PROGRAM_QUEUE=1`. [Complete flags/identity/denominators](results/sum25-20261014/contract.json).
- Nine fresh unprofiled cold searches per task: Sum32/seed0 **1762.552080ms /27.832x**, House32/seed0 **1009.139088ms /27.418x**, Median64/seed2 **807.146462ms /7.218x**. All nine individually meet25x/25x/5x. Existing `evolve_wall_ms` includes cold evolution/all generations/final evaluation; parsing/independent audit/JSON output remain outside. CLI process medians3.507/2.237/1.980s, reported separately.
- Same-round old binary3 controls:4303.359/1011.774/806.421ms. Strong improvement is Sum2.44x; House−0.26%/Median+0.09% time differences are noise, not new speedup claims. MedianASGPdrift+2.68% does not replace frozen denominator. GPU-e/ER each3new repeats; CPU historical full-search controls explicitly reused (unchanged code), all5modes smoke/scaling rerun.
- Three Astra-medium worktrees explored phase execution/window continuations/scheduling. Kept exact pure-Measure reuse, per-phase certificates, hybrid VM fallback, leaf-direct, queue/SoA. Removed rejected top-cache and entry-direct; Index inspected only. No CPU fragment route. [Contracts](../../../docs/design/gpu-reproduction.md).
- Final evidence:90search samples,54exact semantic/final/history AST comparisons; three seeds×128gens per task before/after identical. Full initial snapshots exact;4inheritedHouseCPU/GPUdifferences unchanged. Sum gen0 already solved; top16long CPU audit only; no improved convergence claim. Sumseed0last16medians202.04→46.91ms. RSS+roughly12–18MiB,128gens not an unbounded-history proof.
- Release build; native126/127 initial plus corrected fixture1/1 (all127 covered); repository24/tool5; main CUDA12.6memcheck0errors/racecheck0hazards. Stale agent test object concealed invalid4096-frame fixture; coverage retracted and replaced with fresh97program/65case/128frame fixture. Capacity safety remains. All60fixed-parent scaling cells P1024/2048/4096/8192×3tasks×5modes pass, own controls rerun, noOOM.
- [Chinese report](results/sum25-20261014/REPORT.zh-TW.md), [final JSON/manifest](results/sum25-20261014/final/summary.json), [per-generation CSV](results/sum25-20261014/final/generations.csv), [scaling](results/sum25-20261014/final/scaling.csv), [tests](results/sum25-20261014/validation.json), [nsys diagnostic](results/sum25-20261014/profile.json). No NCU; profiled time never used in ratios. Sum eval89%ofGPUkernel time; House eval and Median reproduction/lowering remain bottlenecks.
- Reproduce: `python3 benchmarks/fixed_asgp/optimization/results/sum25-20261014/run_validation.py <timing|quality|other-modes|snapshot|scaling> --out <NEW stage directory>` then adjacent `summarize.py --runs <stage parent> --out <NEW report directory>`. Raw `logs/optimization/sum25-20261014/`; shared GPU lock `/tmp/gagp-parallel-20261013-benchmark.lock`. No prepare. All agents/jobs complete; no pending experiment. Next higher-target work must remeasure remaining eval/phase cost or Median lowering, preserving native pipeline.

---

# Three-task rerun — 2026-10-14 (complete)

- No runtime change: validated `be455a4` binary SHA764ab8df9dbea4b2cb9e439a8e2608f7b1bd7fd9aa9f7ecd8bf4c4a6e09b53a0 and full64flags. CPU1T, P1024/C1024; no profiler/input preparation/early-success stop.
- Sum32/seed0, House32/seed0, Median64/seed2; all5native modes×3fresh complete cold searches=45runs. Allcontrols rerun. GPU-er-o medians4339.178/1045.019/857.690ms; same-round ASGP ratios11.305x/26.477x/6.793x. Frozen historical House/Median ratios26.495x/6.838x kept separate. Fastest GPU happens to beER by a tiny margin; not evidence that overlap hurts.
- Fresh fixed-parent all3tasks×5modes=15cells, each warmup+3samples:GPU-er-o45.844/30.940/6.604ms, same-round ratios41.38x/41.96x/8.63x; these do not establish complete-search targets.
- Quality limitation:Suminitialgeneration0 alreadyfitness0 for allmodes. HousefinalfitnessASGP−4234/CPU,e−6544/ER,o−7146;Median−578/−642. Three timing repeats use one seed/task, not three independent quality seeds. Fullcases/unscored/audit checks passed in runner. Five fixed-tool tests and24repository checks passed; no runtime rebuild needed.
- [Report](results/three-task-20261014/REPORT.zh-TW.md), [raw summary](results/three-task-20261014/summary.json), [per-generation CSV](results/three-task-20261014/generations.csv), [English chart](results/three-task-20261014/search-comparison.png). Raw root`logs/optimization/three-task-20261014/`; alljobs complete.
- Reproduce with `python3 benchmarks/fixed_asgp/optimization/results/three-task-20261014/run.py --out <NEW>`; summarize via sibling `summarize.py --raw <runs> --out <NEW summary>`. Existing short_search now accepts explicitSum32/seed0; defaultHouse/Medianunchanged. No push.

---

# Bloat cost optimization — 2026-10-14 (complete)

- Best runtime `be455a4`, branch `opt/gpu-generation-20261001`, worktree `gagp-opt-20261001`. Protected prior checkpoint `checkpoint/pre-bloat-cost-20261014` = `71dc916`. CPU1T, native GPU reproduction, full P1024/C1024; no RNG/operator/fuel changes, no push this round.
- Keep all previous manifest flags; add `GAGP_GPU_DEFERRED_CROSSOVER_COPY=1`, `GAGP_GPU_FUSED_SPLICE_DEPTH=1`, `GAGP_GPU_COMPACT_COMPILE_FRAMES=1`. Runtime eliminates overwritten parent copies and duplicate depth scan, reduces lowering stack2816→768bytes. Full phase lowering still runs; no persistent DAG/subtree executable cache claim.
- Three Astra-medium agents in separate worktrees explored four routes: executable-only CSE, incremental metadata/lowering, physical sharing/deferred copies, live workspace. Root adopted only measured native changes. Builds/GPU experiments serialized with shared lock.
- Nine interleaved complete cold searches: House32/seed0 **1047.911452ms /26.421x**, Median64/seed2 **858.233274ms /6.834x**. Same-round prior best1067.850610/914.303899ms:1.87%/6.13% less time. Frozen ASGP denominators27687.368225/5864.915193ms unchanged. House30x unmet (requires922.912274ms);Median5x met;CPU still far fromASGP.
- 18 short paired and6 task/seed128-generation comparisons: semantic counters, fitness, final and history AST exact (history elapsed timestamps excluded). All P×C evaluations retained. Long CPU audit covers16programs, not fullpopulation. Tests127/127 + expanded variation; repository24/24 passed.
- Rejected/removed executable CSE: packing exceeds eval savings, Median regresses badly. Rejected/removed live memo backing removal including bounded allocation reuse: House still regresses. Patches/raw evidence archived, no active runtime flags. [Incremental/sharing](results/bloat-cost-20261014/incremental-sharing.json), [CSE](results/bloat-cost-20261014/rejected-executable-cse.json), [workspace](results/bloat-liveness-20261014.json).
- Raw root `logs/optimization/bloat-cost-20261014/validation/runs/`; [report](results/bloat-cost-20261014/REPORT.zh-TW.md). Reproduce: `python3 benchmarks/fixed_asgp/optimization/results/bloat-cost-20261014/run_validation.py --out logs/optimization/bloat-rerun-NEW`; summarize with sibling `summarize_final.py --runs <fresh runs>`. No input preparation/overwrite.
- Remaining:House eval/bloat;Median reproduction/lowering;CPU evaluation/reproduction. House128 seed0 early/late16medians20.926/171.181ms. No measured case for extending rejected prototypes or building persistent DAG this round.
- Final validation complete:40/40 fixed-parent scaling cells (P1024/2048/4096/8192, all5modes, controls rerun), Sum all5 regression,10five-mode smoke cells,3focused CUDA memchecks0errors. [final.json](results/bloat-cost-20261014/final.json) records raw repeats/flags/input/binary identity; [per-generation CSV](results/bloat-cost-20261014/generation-times.csv). All agents and benchmark jobs complete; no pending continuation.

---

# Astra parallel optimization — 2026-10-14 (complete)

- Best runtime `5a3be05`, branch `opt/gpu-generation-20261001`, worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`. Protected start `ce0d811`; original workspace, frozen inputs and historical binaries untouched. No push.
- Authoritative binary/full settings/input hashes and raw repeats: [final.json](results/astra-20261014/final.json), raw `logs/optimization/astra-20261014/final/gpu-nine/manifest.json`. All previous-best flags remain required. Add `GAGP_GPU_FUSED_DONOR_SITES=1`, `GAGP_CPU_FUSED_ADMISSION=1`, `GAGP_CPU_COMPACT_ADMISSION=1`, `GAGP_GPU_CONSTRUCTIVE_EXPORT=1`, `GAGP_REUSE_IMPORT_CERTIFICATE=1`, `GAGP_PREPARED_CASE_VIEWS=1`; CPU1T only.
- P1024/full1024, changing House32/seed0 and Median64/seed2: GPU-er-o9 medians **1064.776693ms /26.002981x**, **909.163542ms /6.450891x** against frozen ASGP search denominators27687.368225/5864.915193ms. Prior1317.052/1136.319ms:19.15%/19.99% less time. House30x remains unmet; another13.3% time reduction needed.
- All5 modes: CPU3 medians82103.390/42153.159ms, GPU-e3 8398.843/24131.383ms, GPU-er3 1063.300/915.630ms. ASGP3 same-round27576.167/5899.860ms. CPU remains far from ASGP. CPU/ASGP controls reuse their unchanged archived binary and CPU implementation; GPU-e rerun after GPU-only view change. CLI process ratios are lower than search ratios; report includes both.
- Kept native architecture/RNG/operators/numerics/fuel. Removed duplicated donor annotation, fused admissibility+witness traversal, continued scalar proofs across compaction, reused private cold-import admission/executable, omitted optional reverse-parsed export cache, prepared private list-parameter views. Fused admission's speculative-work guard accounting differs explicitly; no new search-rule profile retained.
- Rejected/removed runtime: GPU superinstructions, power2 division, exact guarded32 division, CPU cached fusion (including misleading regressed-OFF artifact), size-near crossover. Evidence/patches archived. Equal-wallclock heldout rejects bloat change; don't revive CPU fragment route.
- Final validation:127 native tests,24 repository checks,89 tools (4 skipped); focused CUDA12.6 memchecks0errors.36 complete short/128-generation trace+AST comparisons exact; fixedP1024/C1024 snapshot exact before/after, inherited4House CPU/GPU mismatches unchanged. Three seeds×128gens per task remain identical; not a convergence-equivalence proof.
- Scaling40/40: all5 modes×2tasks×P1024/2048/4096/8192, controls rerun, noOOM. Separate frozen-parent9 medians32.228752/6.956174ms, not the search target. SumP1024 all5 regression complete. Long-run bloat and history/JSON memory remain; peak RSS1.16–2.28GiB is not proof of bounded host history.
- Remaining:House eval;Median GPU reproduction/lowering;CPU evaluation plus Median CPU reproduction. Final list-view step is modest (pairedHouse−1.67%,Median+1.16%); no guaranteed all-workload speedup. No evidence to extend rejected static/bloat prototypes this round.
- [Chinese report](results/astra-20261014/REPORT.zh-TW.md), [per-generation CSV](results/astra-20261014/generation-times.csv), [validation](results/astra-20261014/validation.json). Reproduce via `python3 benchmarks/fixed_asgp/optimization/results/astra-20261014/run_final.py --out logs/optimization/astra-rerun-NEW`; summary via sibling `summarize.py`. Never prepare original inputs. All agents/builds/experiments completed; no pending jobs or context-dependent continuation.

---

# Parallel worktree optimization — 2026-10-13 (complete)

- Protected original workspace checkpoint `75d7f83`; optimization input checkpoint `9ddab1d`. Original workspace remains unchanged after its checkpoint; no push.
- Best runtime `2d7dac3`, branch `opt/gpu-generation-20261001`, worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`. GPU runtime unchanged from `b60a336`; final addition is exact CPU all-fit compatible-pair bounds.
- Authoritative binary/settings/input identities: `logs/optimization/parallel-20261013/followup-main/manifest.json`. All best flags including CPU1T and `GAGP_CPU_ALL_FIT_PAIRS=1` are also in [latest/final.json](results/parallel-20261013/latest/final.json).
- Primary contract: House32/seed0, Median64/seed2, P1024/full1024 cases. Cold admission/import, every complete generation, final evaluation/export included in search wall; full process adds input parsing and audit/JSON output.
- Final GPU-o9 medians: House1317.052ms /21.022x, Median1136.319ms /5.161x against same-round ASGP3 medians27687.368/5864.915ms. CPU3 medians84247.491/44762.427ms, still3.043x/7.632x slower than ASGP. House30x and CPU-near-ASGP are NOT achieved.
- Separate frozen-parent formal9:32.926999/7.277462ms, historical frozen ratios38.935x/8.489x. Do not substitute these for changing-search ratios.
- Final scaling:40/40 cells (two tasks x four populations x five modes), all controls rerun. Earlier Median8192 OOM retained; other user's GPU workload later released memory naturally and final rerun passed. SumP1024 five-mode regression passed.
- Validation:127 native tests before compactTask,6 affected after compactTask,2 affected after all-fit;24 repository and89 tools (4 skipped); focused CPU sanitizers/GPU memcheck. Final30 main traces and final fitness/sizes/ASTs exactly match pre-all-fit checkpoint, frozen input hashes unchanged.
- Quality:counted CPU pair sampling changes seeded trajectories (Median chosen seed best-462→-642; House improves). All-fit changes no RNG. GPU128gens x3seeds and Median512 memory diagnostic pass, but bloat/history/output memory remain; short quality is not convergence equivalence.
- Retained:CPU typed/view/memo/compact driver and selected-site preparation; GPU batch import/export, static grammar/executable reuse, packed carrier, capability buckets/workspace, comparison/ring memo, mutation scratch and compact Tasks. Native GPU selection/reproduction stays GPU, one application CPU thread.
- Rejected and removed:sampled sites,size ties,rootfilter,copy/compaction variants,shared/cached/register stacks,fuel/atomic variants,lazy CPU LOAD bindings,GPU per-slot executable reuse. Raw evidence retained, no experiment entry points.
- Remaining bottlenecks:House eval;Median CPU admission/reproduction and GPU mutation/lowering;long-run bloat. No measured basis for another small stack/cache/binding variation. A future static typed phase execution plan needs a separate costed prototype; do not resume rejected CPU fragment architecture.
- [Full report, raw repeats, scope, limits and reproduction command](results/parallel-20261013/REPORT.zh-TW.md). Summary: `python3 benchmarks/fixed_asgp/optimization/results/parallel-20261013/summarize.py --followup`. Archived raw root `logs/optimization/parallel-20261013/`; prior b60a336 evidence remains immutable under final-main/final-fixed.
- All agents, builds and benchmark processes completed. No pending experiment or automatic continuation is required to recover this checkpoint.

---

# Native 1T optimization checkpoint — 2026-10-02

Active objective: preserve CPU / GPU-e / GPU-er / GPU-er-o architecture, use
CPU 1T + GPU, target fastest native GPU >30× frozen ASGP-1T on Sum/House;
Median remains fixed-overhead workload. Do not resume retired CPU fragment route.

## Long-search follow-up — 2026-10-13 (complete)

House64 / Median128, all5 modes, seeds0/1/2, P1024/C1024; then timing-budgeted
Median256 reruns. Unchanged archived binary and flags, CPU1T. No runtime edits.
39 runs complete in54.16min, 5184 generations; all30 primary plus9 Median256
extensions. 150 checkpoint records /39 unique programs independently rechecked,
native scores reproduce and GAGP CPU/GPU match. Historical16-gen and extension
128-gen prefixes match; full CPU/e population parity is not claimed.
[Results and figures](results/long-search-20261013/REPORT.zh-TW.md): House64
fastest GPU13.27x, Median128 1.49x search-wall. Quality is seed-dependent: House
ASGP seed0 solves atgen37; Median CPU/e seed2 held-out61.13% vs ER/o33.40%.
Raw checkpoint: logs/optimization/long-search-20261013/manifest.json.
[Contract](results/long-search-20261013/CONTRACT.zh-TW.md). Runner long_search.py;
post-run checkpoint audits long_search_audit.py; plots long_search_report.py.
Runner refuses to overwrite its output; reproduce into a fresh directory.
No runtime modifications; source/binary/input identities and validation archived.

## Multi-generation validation — 2026-10-13

[Six all-mode comparison figures](results/multigen-20261013/all-mode-comparison/README.md):
English labels, common-generation fitness/time and four ASGP speedup bar charts;
long-run timing uses seed 0 only. Reproducer: multigen_comparison_plots.py --results
benchmarks/fixed_asgp/optimization/results/multigen-20261013.

Runtime Z/settings unchanged; benchmark adds five search entries and opt-in trace.
[ASGP vs GAGP overview](results/multigen-20261013/OVERVIEW.zh-TW.md) fixes GPU-er-o
as the primary comparator; regenerate with multigen_overview.py --results
benchmarks/fixed_asgp/optimization/results/multigen-20261013. Archived CSV unchanged.
[Report, plots and CSV](results/multigen-20261013/REPORT.zh-TW.md): P1024/C1024,
3 seeds,16 generations+final,45 aligned primary runs,15 original Median controls,
30 timing-selected extensions (up to1024 generations);90 processes ~40.6min total.
Median scoring aligned to ASGP exact using cap1 for admitted Int output only.
Search-wall fastest GPU: Sum3.142s/9.97x,House1.279s/10.78x,Median1.022s/0.76x.
These are changing-population search results, not the frozen30x headline.
House improves in all3seeds at10s; Median is variable. Long House seed0 generation
cost grows11.51x. ASGP translated Median has76 distinct semantic mismatches;
negative-index behavior confirmed. Native ASGP quality/heldout used, not ported
scores. GAGP CPU/e mean differs in10 generation records; no full parity claim.
4109 unique recorded champions pass GAGP CPU/GPU full-case audit; native ASGP
snapshots reproduce search scores. Focused native integration,5tool/24repo checks pass.
Raw/binaries/commands: logs/optimization/multigen-20261013-v2/,
multigen-20261013-exact/,multigen-20261013-analysis/. Runner multigen.py; independent
ported/native audits and report scripts alongside it. No runtime optimization
or operator change in this validation. Custom metric/ranking API remains proposed.

## Current best and active experiment

Validated best opt-in runtime Z: `d1712a5`, X plus `GAGP_COUNTED_SITE_DRAW=1`.
GPU count/rank site sampling retains uniform candidates but changes RNG trajectory.
Interleaved3 processes x1 warm+3 measured repeats per setting:
- Sum: uncounted50.603 -> counted48.884881ms (range48.580490–50.016281),37.66x.
- House:42.306 ->40.384029ms (39.790332–42.125251),31.75x; all9 below42.734ms.
- Median:15.565 ->12.213825ms (11.871306–12.702482),5.06x.
Frozen denominators1841.160175/1282.011816/61.777141ms unchanged. These are full
generations on frozen parents, not identical evolving populations or search-wall
speedups. Cold owner admission363.6/333.0/369.8ms remains included in search wall.
Base R flags below plus `GAGP_GPU_PHASE_POPULATION=1 GAGP_OWNED_PHASE_PACK=1
GAGP_OWNED_PHASE_CAPS=1 GAGP_COORDINATE_EXECUTOR=1 GAGP_COUNTED_SITE_DRAW=1`.
No CPU workers. Generic native fallback/all original modes remain.
Archive/manifest/raw9 repeats: `logs/optimization/native-counted-validation/`.
Runner from repo root: `python3 logs/optimization/native-counted-validation/run.py
STAGE`, stages paired,screen,quality,snapshot,cpu-views,scaling. Registered cells
resume; original inputs and historical results never overwritten. Archived bench
SHA and frozen input SHA live in manifest; runtime is d1712a5 (later docs-only
spec-freeze manifest correction does not change executable).

Validation done:3 seeds x4 generations; long3 seeds x64 Sum/96 House/512 Median.
Common5-second completed prefixes including cold initialization:
- Sum base/new best=[0,0,0]/[0,0,0].
- House base[-7570,-7465,-7570],new[-5492,-4337,-5843].
- Median base[-26620,-28478,-25038],new[-26445,-16159,-18062].
All top16 final CPU audits exact; complete pairs,zero timeout/fallback/unscored.
Runtime error rate at5s did not increase; they remain real penalized errors, not
skipped pairs. Unique final genotypes generally>1000/1024. New long-run populations
bloat: final means Sum~300+,House~550+,Median~800 nodes; late generation times
are larger, so do not transfer frozen-parent headline to whole searches.
Independent1024 heldout cases per task (same ASGP ranges,new seed,no exact
training inputs):18 selected best programs all CPU/GPU exact,zero runtime errors;
House/Median fitness advantages remain. Raw heldout/summary.json,quality-summary.json.
A separate512-generation Median RSS diagnostic shows~1011MiB at5–15s increasing
to~1075MiB at25–30s,peak1313MiB including final audit/report; GPU genotype857MiB.
Owner buffers retain only live generations, but requested history-best AST/proofs
still grow with generation count. This is a disclosed retained-output cost, not
an assertion of constant total-process memory. Raw memory/rss.csv and manifest.

All3072 frozen rows exactly match historical allocator reference. House retains
4 inherited CPU/GPU differences (347,359,622,766), not new; don't claim all3072
CPU/GPU equal. X reporting checkpoint ce0e430 had House43.634ms/29.4x; its earlier
30.54x result was not robust. Z's final-source snapshots now also match reference.
Full127-target suite with CPU view opt-in:126 passed; spec-freeze hash test caught
changed grammar-definition contract doc. Corrected only its manifest hash; failed
repository target rerun passes,24 repository and5 fixed-tool checks pass. No test
removed. Weighted/reset fixed skeletons explicitly decline profile. Negative tests
confirm existing physical admission bounds, including zero-charge resources;
no redundant runtime budget scan was added. Y construction-site code is removed.
CPU window view adopted opt-in after interleaved9 repeats: Sum16960.628->9465.923ms
(1.792x); House7952.051->7957.459 (+0.068%),Median687.083->686.402ms. All non-time
outputs match. Add `GAGP_CPU_REGION_VIEWS=1` to best flags; generic fallback remains.
3072 evolved seed0 final programs (64/96/512 generations) independently admitted,
full1024-case CPU(view enabled)/GPU reevaluation exact versus stored final fitness.
32-case behavior-probe distinct counts27/205/76; these are not full-domain equivalence.
CUDA12.6 memcheck of32-generation P16 full-case owner test:0 errors.
Latest House nsys kernel means including warm/cold launches: eval29.827ms,
mutation2.015,crossover including selection1.643,lowering1.367,offset scan0.531,
final genotype compare0.323. Cold import annotation0.914 is initialization only.
These are diagnostics, not unprofiled headline arithmetic. Raw profile/ files.
Tracked evidence: results/native-counted-validation.json; full commands/raw logs
remain in archive. Final5-mode scaling is complete:60/60 cells,full1024 cases,all four populations.
P1024 CPU scaling reuses identical same-binary third-process CPU-view controls,
explicitly tagged reused_from_tag; remaining controls freshly measured. No NCU.

### Historical exact-trajectory reference (R)

Best validated GPU source: `5508a60` (Q+R immutable crossover handoff), with
locally extracted jemalloc 5.2.1-4ubuntu1. Use `round2.py` FLAGS and process env:
```
LD_PRELOAD=$PWD/logs/optimization/native-allocator/extracted/usr/lib/x86_64-linux-gnu/libjemalloc.so.2
MALLOC_CONF=background_thread:false,narenas:1
```
No system installation; allocator confirms no background workers and one arena.
Previous N (`d72b5f4`) nine-repeat medians Sum279.706 / House295.679 / Median288.718ms;
same-round allocator controls329.388 /348.488 /339.451ms.
Frozen headline6.583x Sum /4.336x House, not30x. All3072 snapshots,3 seeds x4/32
generation results and final ASTs match. Four focused native/safety tests pass.
All120 tests pass with N and the O window-view candidate; scaling still pending. Use `round2.py` flags including `GAGP_OWNED_PREPARATION=1`.
Archive/repeats/commands `logs/optimization/native-owned-validation/`;
[validation](results/native-owned-validation.json).
Dependency hashes at `logs/optimization/native-allocator/dependency.json`.
Earlier allocator and selected-site results remain in their historical manifests.

K selects logical sites before materializing contracts and uses owned scalar
parent continuations. Full fallback for multiple root requests, projected
budgets, registry constants, staged payloads or explicit multiple workers.
GPU operators/RNG/cases/admission semantics unchanged. Two GPU copybacks/AST
rebuilds remain. J's execution-only admission is incorporated as K's internal
step; its standalone flag/entry is removed (small isolated benefit).

Rejected L: exact CPU list-slice cache.64K entries: Sum17.441→22.109s,
13.09M hits/23.47M misses,358 capacity clears, zero mutable-payload invalidations.
524K diagnostic:35.24M hits/1.32M misses (96.4%), but snapshot process still17.88s
versus ~18.5s reference with additional memory. High hit rate gives too little
benefit to retain this mutable-registry memo policy. All3 tasks' CPU generation
fields and3072 CPU/GPU snapshot records match; safety tests passed. Feature,
flag, exclusive API/stats/tests removed; raw binaries/patch/counters retained in
`logs/optimization/native-cpu-slice/`. Short registered-list Slice bounds fix and
its negative test remain. Next CPU direction needs cheaper view execution, not
another cache capacity sweep.
Retained M: allocator reduces all three GPU generation times15-18%; all nine
32-generation searches preserve non-time results/exports. Full120-test pass.
New Median NVTX diagnostic (373ms profiled vs334ms unprofiled):13,915 runtime
identities92.5ms; membership Matcher21.5ms self, witness22.0ms self;
2 decode passes128.4ms inclusive; selected analysis63.4ms inclusive.
Raw timeline/attribution: `logs/optimization/native-selected-profile/`.
Retained N (`d72b5f4`, `GAGP_OWNED_PREPARATION=1`): an owned immutable scalar population can share its admission
witness/VerifiedAst/identity through native preprocess/pack, avoiding repeated
serialization and proof copies while preserving public input checks. Keep GPU
operators, RNG, membership and both copybacks unchanged for this experiment.
Quick paired full generation: Sum325.464->284.576 /House350.142->297.930 /
Median330.887->289.507ms. Four focused tests pass (including owner/input mutation,
request/domain/payload/index/lifetime checks). Raw `logs/optimization/native-owned-preparation/`;
9-repeat, fixed snapshot and3-seed4/32-gen validation `native-owned-validation/`.
Formal9-repeat and3-seed4/32-gen comparisons pass; retained.
Candidate O (`GAGP_CPU_REGION_VIEWS=1`): invocation-local IntList views
for statically proved region phases, eliminating intermediate Slice registry
hash/copy work; original CPU VM/region control flow and numeric/fuel remain.
5 focused parity/region/session tests and full120 suite pass. Initial P1024 CPU
Sum16942.169->9241.978ms, House7806.760->7848.474, Median712.673->751.051.
All CPU generation non-time fields and3072 CPU/GPU snapshots match.
Coordinate views had no supported performance benefit; implementation is now
restricted by `SequenceWindows` structure (not task name). Formal9 CPU repeats,
full-case snapshots and5-mode quick controls running in
`logs/optimization/native-cpu-window-validation/`. Initial all-region prototype
binary/results retained in `logs/optimization/native-cpu-views/`.
No CPU workers, no view escape into root code; memo list states unsupported.
Temporary slice registry contents are intentionally not materialized in this
profile (same scalar-content aim as GPU views); no claimed speedup yet.

## Current fair baseline

Source parent `9ce96a9`; thread-policy checkpoint follows. `GAGP_HOST_THREADS=1`
is now the default; `2..20` is optional host parallelism, excluded from fair
headlines. One-thread overlap runs preparation on the calling thread after GPU
launch, before synchronization. Existing native fast-profile flags below remain.

Exploratory P1024/full1024 baseline (one process, 1 warmup + 3 repeats):
Sum GPU-overlap 724.686 ms, House 853.414 ms, Median 792.708 ms.
These supersede historical multiworker times **for the 1T contract**; they do not
claim a performance improvement. Raw five-mode measurements and binary:
`logs/optimization/native-1t-20261002/`; compact manifest:
[1T baseline](results/native-1t-baseline.json).

Validation: complete Release build; all 120 native tests passed across full run
and focused reruns after adapting two worker-count expectations. Explicit 4-worker
resource/cache/parity tests also passed; tool tests 5, repository tests 24 passed.
A clone trace plus pthread_create interposer/backtrace smoke records three CUDA
driver thread creations, no GAGP host workers. Logs are alongside the baseline.

Candidate A (`GAGP_REUSE_ADMISSION_COMPILE=1`): retain admission's existing
lowering in its opaque native certificate, scalar constants only. Exploratory
Sum640.959 / House736.418 / Median699.902 ms; all non-timing fields equal to 1T
baseline. Compile drops to 12.8–13.9 ms. Tests reject stale AST/constants/input
layout/fuel and decline registry-dependent constants. Needs formal and changing
search validation before promotion. Raw `logs/optimization/native-admission-compile`.

NVTX diagnosis of candidate A, Median: site analysis self178.6 ms, runtime
identity105.9 ms/11024 calls, bytecode+region verification96.7 ms; full profiled
722.7 ms. Nested ranges are not additive. Raw source-only instrumentation and
restored sources at `logs/optimization/native-1t-host-profile/`.

Retained candidate C: 1T decode uses ordered admission in the owning run context;
no discarded worker caches or speculative child warming. Formal 3 processes ×
(1 warmup + 3 repeats), interleaved with 1T controls: Sum552.771 / House637.124 /
Median590.220 ms, reductions22.2% /25.1% /21.1%. All 3072 snapshot program records
match (full cases, GPU counts and CPU fitness); all nine 3-seed×4-generation
search comparisons match non-timing fields and exported ASTs. Quality limits:
short runs only, no convergence/long-run assertion. Raw commands/binaries/repeats:
`logs/optimization/native-checkpoint-validation/`; summary
[validation](results/native-1t-reuse-validation.json).

Rejected B: grammar-wide local-resource eligibility cache. Sum657.988 /
House752.135 /Median697.685 ms versus candidate A640.959 /736.418 /699.902;
no reliable gain. The cache change is reverted, raw data in
`logs/optimization/native-static-charges/`.

Rejected D: compact-before-admission. No performance gain (571.8/656.3/604.5 ms)
and a new negative test caught unused duplicate-name validation being hidden
by compaction. The implementation, flag and proposed spec change are reverted;
the acceptance tests remain and pass on the retained implementation. Raw rejected
patch/timings in `logs/optimization/native-compact-admission/`; no maintained entry.

Candidate E: analysis-local site contract reuse (`GAGP_REUSE_SITE_CONTRACTS=1`).
Same verified scope/nonterminal/template/group/closure reuses the exact key;
repeated-hole intersections retain full construction. Quick Sum505.950 /
House595.256 /Median542.493 ms, unchanged non-timing results. Differential
contract fixtures plus reproduction/bounded variation pass. Formal repeats and
changing-search checks pending. Raw `logs/optimization/native-site-contracts/`.
Detailed Median NVTX: 141886 contract constructions (~94.7 ms self), runtime
identity90.2 ms, root/region bytecode verification91.8 ms. Instrumented timings
are diagnosis only. Next: shared immutable verifier locals between instructions,
copy on Store/merge; preserve traversal and admission/error semantics.

Candidate F: verifier local environments share ownership between unchanged
instruction states; Store and control-flow merge detach before mutation. CFG
order, validation rules and diagnostics unchanged. Quick Sum455.695 /
House498.343 /Median484.025 ms after E; all non-timing fields match. Four focused
tests pass; independent old-source differential checks match all 100000 ordinary
bytecode and 20000 bounded-region results including exact diagnostics and resource
summaries. Full Release build passes. Raw `logs/optimization/native-verifier-locals/`.
E+F formal validation passed; archived in
`logs/optimization/native-contract-verifier-validation/`. Superseded by I then K.

Rejected G: retaining full sites in native certificates. Fixed generation slowed
~30–47 ms, and 32-generation searches slowed3–4% with RSS+120–140 MB. All short
and32-generation fields/exports matched, but copying/retaining sites lost.
Implementation/flag/exclusive tests removed; raw rejected patch and evidence:
`logs/optimization/native-certified-sites/`. Best runtime unchanged.
Rejected H: retain the run analysis context (also tested a smaller2P capacity).
All short and32-generation results/ASTs matched. After correctly charging final
cache destruction, fixed-gen changes were only ~1–3%, and32-generation process
wall Sum18.78→19.90s, House17.69→17.32s, Median20.53→19.63s. Mixed/small benefit
and Sum regression do not justify this extra lifetime policy. Implementation,
flags, exclusive eligibility/ownership tests and guide removed. Raw corrected
patch/results `logs/optimization/native-run-analysis-accounted/`; earlier timing
notices remain in `native-run-analysis` and `native-run-analysis-2p`.

Timing audit adds `evolve_wall_ms` outside the actual evolve call. Historical
`evolve_call_ms` is an internal timer excluding function-local destructors.
Keep both and original generation boundaries. Fixed benchmark smoke covers it.

Latest best-version NVTX: Median profiled563.5 ms, identity89.4, site build92.7,
key construction9.3, root/region verifier39.8 ms. Separate scopes are nested.
Raw `logs/optimization/native-refreshed-profile/`. Process-local SIGPROF PC samples
(no workers/system settings) in `logs/optimization/native-cpu-pc-profile/` show
allocation/free pressure and repeated linear descriptor lookups; CPU Sum also
spends substantial samples hashing/registering list slices. Full-process samples
include setup, not a precise per-generation attribution. Descriptor indexing (I) was subsequently validated:415.657/430.558/437.677 ms,
all full snapshots and short searches match. Raw `native-node-index-validation`.

Current best runtime `ed19fde`, archived binary/flags/commands in
`logs/optimization/native-contract-verifier-validation/manifest.json`.
Enable retained flags plus `GAGP_HOST_THREADS=1 GAGP_REUSE_ADMISSION_COMPILE=1
GAGP_REUSE_SITE_CONTRACTS=1`; round2 runner supplies these.
Formal nine-repeat Sum465.603 /House505.974 /Median499.036 ms versus same-round
controls562.207 /650.451 /590.818. Every snapshot/non-timing search field and
exported AST matches across 3 seeds ×4 generations. All120 native tests pass
across full run and repository rerun (admission contract's freeze hash updated).
[Raw-repeat summary](results/native-contract-verifier-validation.json).
Next: reprofile, then retain certified site analysis across actual generations.
Do not merely persist the compatibility registry: its numeric IDs are local and
its 65536-key bound must not become a long-run cumulative admission restriction.
CPU perf sampling remains blocked by perf_event_paranoid=4; optional request
pending. No system settings changed.

---

Worktree `/home/hschi1106/.t3/worktrees/gagp/gagp-opt-20261001`, branch
`opt/gpu-generation-20261001`. The maintained mainline is GPU evaluation plus
GPU reproduction. CPU controls remain part of the five-mode fixed benchmark.

## Removed route

CPU fragment evolution/reproduction, its phase-bank predecessor, exclusive
owners/executable certificates, owned packing and capacity buckets, probe modes,
runner options and exclusive tests have been deleted. They are not isolated
alternatives, fallback reproduction backends, or future recommended work.

Historical measurements remain unchanged; their status is recorded in the
[archive notice](results/README.md). The old CPU-fragment timings are withdrawn
from the main target assessment. Original worktree modifications, frozen inputs,
archived binaries and historical logs remain protected. No branch reset or push.

## Retained native settings

Presence enables these flags (including a value of `0`):
```
GAGP_CUDA_DEVICE=0
GAGP_VIEW_PROFILE=1 GAGP_TYPED_VIEW_PHASE=1 GAGP_SORT_CASES=1
GAGP_COMPACT_FRAMES=1 GAGP_CONSTANT_PHASE=1 GAGP_POPULATION_HANDOFF=1
GAGP_DIRECT_PHASE=1 GAGP_DIRECT_ROOT=1 GAGP_WINDOW_EXECUTOR=1
GAGP_UNBOXED_WINDOW_FRAMES=1 GAGP_BOUND_ADD_PHASE=1
```
Direct phase/root, window execution, unboxed frames, bound Add, case sorting,
compact workspaces and native generation-owned analysis handoff remain. The
native bytecode verifier, grammar admission/certificates, donor preparation and
GPU reproduction/decode paths retain their safety contracts and tests.

Most recent nine-repeat native medians at P1024/full1024 cases, before cleanup:
Sum205.448, House207.503, Median178.636 ms. These are historical native values,
not new cleanup measurements; 40× is not achieved. See native rows in
[round2-final.json](results/round2-final.json). Historical control binary
`logs/optimization/final_bench` and frozen inputs must not be overwritten.

## Reproduce maintained paths

Build the existing Release CUDA configuration:
```
cmake --build cpp/build/release -j10
```
The runner supplies the native flags above. Use a new output directory each time:
```
python3 benchmarks/fixed_asgp/optimization/round2.py formal NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --baseline logs/optimization/final_bench
python3 benchmarks/fixed_asgp/optimization/round2.py quality NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench --generations 32
python3 benchmarks/fixed_asgp/optimization/round2.py scaling NEW_DIR --binary cpp/build/release/gagp_fixed_asgp_bench
```
Scaling retains `asgp_1t`, `gagp_cpu`, `gpu_eval`, `gpu_repro`, `gpu_overlap` at
P1024/2048/4096/8192, using the existing frozen P8192 prefixes and all 1024 cases.
`snapshot` and `search` are native diagnostic modes. Never rerun prepare against
original inputs. GPU runs are serial; profile only for diagnosis, not headlines.

## Cleanup validation

Logs and dependency/protection audit:
`logs/optimization/cpu-fragment-removal-20261002/`.
Full Release build and 119 native tests passed. Tools: 89 tests (4 optional skips);
repository: 24 tests passed. Three tasks × five modes × (one warmup + three measured runs)
at P32/full 1024 cases matched every non-timing field in the pre-cleanup archived
native binary. This is smoke/regression evidence, not a new performance baseline.
Removed modes reject with `unknown mode`; no deleted symbols remain in native
libraries and no removed entrypoints remain in maintained source/guides/runners.
All original-file hashes match. [Validation record](results/cpu-fragment-removal-20261002.json).
The cleaned binary is archived at
`logs/optimization/cpu-fragment-removal-20261002/bench`.
No new architecture or optimization experiment is part of this cleanup.

Next high-value GPU question: can native GPU offspring be lowered directly into
existing proven phase bytecode, without CPU AST reconstruction between passes?
Start with a bounded prefix-phase lowering prototype and compare it against the
existing compiler (code, constants, fuel, outputs). This is necessary groundwork,
not a speedup claim or another reproduction backend. Grammar membership must
remain a distinct proof; type-correct phase lowering alone does not establish it.

P prototype: device prefix-phase lowering is now runnable in
`cpp/build/release/gagp_test_gpu_phase_compile [PREPARED_JSON GRAMMAR_JSON]`.
512 generated expressions (including branch/fuel cases) +8 negative cases pass;
all14336 phases from3 frozen P1024 populations match CPU compiler instructions,
constants and fuel. CUDA12.6 memcheck:0 errors. System `/usr/bin/compute-sanitizer`
misses its injection library; use `/usr/local/cuda-12.6/bin/compute-sanitizer`.
Warm batch medians ~0.62/0.89/0.55ms (Sum/House/Median); cold launch~2.9-3.2ms,
H2D~0.36-0.40ms,D2H~2.0-2.5ms. Host diagnostic setup~212-230ms includes JSON,
independent admission, reference CPU compilation and normalization; not a fair
end-to-end replacement measurement.40 registers,2816-byte indexed stack,0 spills.
[Raw manifest](results/native-gpu-phase-compile.json). Isolated test-only path;
no grammar trust, operators, evaluator or generation timing changed. This is
useful groundwork, not an adopted generation speedup.

O CPU window candidate follow-up: O2 lazy allocation and O3 separate VM
instantiations did not remove generic regressions. O4 moved private view handling
to scoped builtins; O5 delayed conversion but slowed recursive execution. O6
restricted eager setup to proven constant-false window predicates; O7 outlined
ownership/cleanup from the ordinary VM. O7 nine repeats: Sum16877.975->9520.944ms,
House7789.829->7982.784ms (+2.5%), Median707.697->714.291ms (+0.9%). GPU controls
remain ~280/292/284ms. This CPU candidate is NOT promoted; artifacts, all nine
raw repeats and source experiments are under `native-cpu-window-final-check/`
and preceding `native-cpu-window-*` directories. Current source also packs shared
CPU case bindings once per evaluator call (focused parity passed; separate timing
pending). No application workers were added.

P2 GPU donor construction: current opt-in `GAGP_GPU_DONORS=1` extends owned
selected-site native GPU mutation. Production/scope/domain tables build lazily
inside timed preparation; GPU makes fresh donors, native GPU applies mutation,
final canonical child admission remains. Unsupported jobs use native donors;
new donor RNG/budget-allocation trajectory is explicit, not canonical replay.
All4096 phase donors from3 frozen grammars independently admit/lower; default
and scope-permutation fixtures plus native deferred-preparation regression pass.
Current prototype source is not promoted and still needs full quality/scaling.

INVALID experiment `logs/optimization/native-gpu-donors-integration/`: a misplaced
deferred-batch loop produced no donors, so296 mutations fell back. The203/214/206ms
numbers were retracted; INVALID.txt preserves the reason. Accounting invariant
and actual native preparation test now prevent this failure. Corrected quick run
`native-gpu-donors-corrected/`: Sum280.360->288.195, House293.288->292.745,
Median292.184->281.072ms;1092/1064/1112 GPU donors,92/120/72 native fallback donors,
zero offspring fallback, generation or admission rejection. Not a stable overall
improvement. P2b packs lexical scope maps into24 bits (8 slots), reducing compiler
local stack8624->3168 bytes, zero spills; active A/B and selective batch trace:
`logs/optimization/native-gpu-donors-compact/`.

Refreshed N NVTX diagnostic: `native-owned-profile/`,323.803ms profiled versus
~289ms unprofiled Median.7771 identities49.172ms;2 decode passes129.812ms inclusive;
1320 admissions116.728ms inclusive;Matcher21.642ms self,Witness22.223ms self;
compile39.889ms inclusive, selected analysis41.973ms inclusive. Do not subtract
these annotated/overlapped times from formal totals.

### P2c checkpoint — GPU grammar donors, compact output

- Native GPU selection/crossover/mutation remain; opt-in `GAGP_GPU_DONORS=1`
  constructs supported fresh donors on GPU, then uses existing native splicing
  and canonical final admission. No CPU workers. Unsupported requests explicitly
  use the native donor preparation. New production/budget RNG trajectory.
- Nine unprofiled repeats against N, interleaved 3 processes × warm + 3:
  Sum 278.274330 → 262.496018 ms; House 292.987050 → 270.175429 ms;
  Median 283.401071 → 263.846364 ms. Raw/manifest/summary:
  `logs/optimization/native-gpu-donors-validation/`.
- Full 123-test suite passed. Three seeds × four generations: complete 1,048,576
  pairs/generation, zero timeouts/unscored, top-16 CPU fitness exact. New trajectories
  retain diversity but Sum grows larger programs and total search is slower;
  **not promoted**. 32-generation comparison underway in `native-gpu-donors-long/`.
- Compact output removes unused-capacity D2H/host allocation, not evaluated work.
  Earlier `native-gpu-donors-integration/` timings are INVALID (deferred batch was
  misplaced; mutation fallback); accounting invariant and 128-donor regression
  now exercise actual construction. Never cite that directory as a speedup.
- Next hypothesis: compile_population serializes each admitted AST for the cache
  key and again to validate its executable; one private combined lookup should
  eliminate the duplicate while retaining mutation/input/fuel validation.

### Q — single evaluation identity

- `GAGP_SINGLE_EVAL_IDENTITY=1` computes the current runtime identity once and
  returns the matching sealed executable in the same private lookup. External
  AST mutation, input order and fuel still invalidate reuse. No RNG/operator change.
- Focused membership/reproduction/parity tests pass; all non-time search fields
  and exports match for three tasks × four generations. Raw quick and nine-repeat
  results: `logs/optimization/native-single-identity{,-formal}/`.
- Next: preserve ownership between the already-admitted GPU crossover output
  and mutation preparation; no admission deletion at external boundaries.

### R — immutable crossover-to-mutation handoff

- `GAGP_OWNED_CROSSOVER_HANDOFF=1`, with Q, removes repeated certification/copy
  of the private, just-admitted crossover population. Both GPU operator passes,
  AST decodes and final canonical admission remain. No RNG/language change.
- Nine repeats vs N: Sum281.756493→269.700719, House297.769050→278.160918,
  Median288.717111→268.244850 ms. All three seeds ×32 generations match every
  non-time search field and final exported AST, including top16 CPU reevaluation.
  Search wall improves about5–8%; raw `native-owned-handoff-validation/`.
- Four focused tests pass, including repeated-hole/capture, wrong source/context,
  invalid output and destroyed-parent lifetime. Full123 suite passes; all3072
  complete-case CPU/GPU snapshot rows match N.
- Updated host/CUDA diagnostic underway in `native-owned-handoff-profile/`.
- P2 GPU donor32-generation tests did not justify promotion: most whole searches
  slower with larger programs despite fixed-parent gain. Keep it off the best
  flags; it is a GPU construction experiment, not a headline performance claim.

Q+R activation: N flags plus `GAGP_SINGLE_EVAL_IDENTITY=1` and
`GAGP_OWNED_CROSSOVER_HANDOFF=1`; keep GPU_DONORS and CPU_REGION_VIEWS unset.
Profile302.222ms (instrumented Median, not formal268.245ms): runtime identities
5736calls/34.001ms; decode2calls/128.005ms inclusive; matcher21.762ms self;
witness22.347ms self; compile16.341ms self; constant streams5.209ms.
Median fitness kernel2.113ms on final profiled generation. Remaining host proof
work dominates. Full123 suite and3072 snapshot rows pass. Scaling pending.

### S — incremental canonical crossover matcher prototype

- `GAGP_TRANSPORT_CROSSOVER_PROOF=1` uses immutable parents plus actual copied
  node/value correspondence to seed unchanged/donor subtree decisions. Affected
  ancestors are removed and re-matched in declaration order. Native verification,
  witness/resource construction and lowering remain; no RNG/language change.
- Zero-node alias roots, noninjective scope maps, coupled occurrence sites and
  inner-binder donors decline to ordinary admission. First disjoint-root profile
  admitted0 benchmark children (`native-proof-transport/`), not an improvement.
- Actual incremental matcher handles982/984/998 crossover children. Initial
  table/AST copies cancelled savings; moving owned temporaries gives quick
  paired271.820→262.111 /275.211→267.552 /266.850→263.308ms. Search4-generation
  times mixed; **not promoted**, formal/long comparison pending. Raw
  `native-incremental-matcher{,-move}/`.
- Focused4 tests pass; added512-child canonical witness check includes ambiguous
  ancestor alternatives and explicit alias fallback. Final ASTs remain exact.
- Remaining eval candidate: bounded one-coordinate memo lookup currently scans
  all occupied cells. Test direct indexing with a register validity bitmap,
  keeping complete state, lazy execution, fuel/base/capacity order unchanged.

S decision: **rejected from active runtime**, reproducible source checkpoint
`ed0448a`; patch/binary/raw in `native-incremental-matcher-validation/`. Nine
paired medians R→S: Sum261.695204→261.260983, House274.157456→270.559995,
Median272.448157→268.385679ms. Larger quick gains did not repeat. Sum gain0.2%,
House/Median1–2% with overlapping ranges; insufficient for added proof transport
complexity. Dedicated512-child ambiguity/alias witness tests passed, and the
prototype remains useful evidence, not the recommended path. Removed its flag,
private admission path, exclusive test and active guide/navigation entries.
R remains best; T dense coordinate memo now awaiting serial build/evaluation.

### T — dense coordinate memo: rejected

- One-Int-state memo domain <=128 slots; direct indexing plus per-invocation
  validity bitmap, with original fuel/base/capacity order. Three focused tests
  pass, including raw values/errors/fuel and boundary checks; all1024 House
  full-case CPU/GPU snapshot rows exact.
- Two paired orders give House272.33→278.59 and275.00→284.09ms; flag-off
  candidate272.46/276.44ms. Sum/Median changes are order-dependent noise.
  No end-to-end benefit: removed active code/flag/exclusive tests and guide.
  Patch, binary, raw JSON retained in `logs/optimization/native-dense-memo/`;
  tracked summary `results/native-dense-memo.json`.
- R remains best. Next hypothesis: table compaction after successful admission
  need not repeat matching, witness building and lowering when only unused
  scalar constants are removed and names/scopes are unchanged. Validate first,
  then transform owned data and bind its proof before publication.

### U — post-admission constant compaction: rejected

- Minimal owned path retains proof after full original-input validation, only
  remapping scalar constant indices; changed names use full readmission.
  Focused membership/reproduction tests pass including malformed unused entries,
  canonical witness comparison and stale public certificate rejection.
- Quick medians R→U:262.10→264.00 /268.41→274.96 /263.29→267.04ms.
  Diagnostic5280/5280 admissions have already compact constant/name tables:
  native GPU copyback already eliminates this work. No gain; source/flag and
  exclusive tests removed. Existing shared compaction safety tests retained.
- Corrected attribution: R Median1320 matcher/lowering calls under actual
  offspring decode; other1184 calls belong to donor preparation. This is not
  evidence of duplicate full offspring admission.
- Raw `native-admitted-compaction/`. Diagnostic runner overwrote its own quick
  archived binary (quick original SHA remains in manifest); diagnostic source
  patch retained. Quick timing is rejected evidence, not a reproducible best.

### V1 — GPU expression subtree variation foundation

- Device-owned prefix trees carry distinct nonterminal sites (not type-only
  tags). GPU crossover copies site metadata; new GPU mutation donors annotate
  construction once. No reannotation of existing offspring in operators.
  Independent diagnostic reconstruction checks every transported site separately.
- Prototype profile: pure Int/Bool/IntList expressions, scalar Int/Bool constants,
  identity scope maps, default variation/resource policy, no aliases/inner
  binders/templates/repeated holes inside phases. Unsupported combinations
  explicitly decline. This is NOT CPU fragment reproduction, a finite phase bank,
  a native benchmark mode, or a complete-generation performance claim.
- 1024 phases ×32 operator rounds for all16 three-task expression nonterminals;
 5120 independently admitted exports per root, first/final1024 GPU bytecodes
  exactly match CPU instructions/constants/fuel. Fresh mutation uses destination
  remaining node/depth budget:32768/32768 admitted per root. Crossover capacity
  rejections explicit. Distinct audited/final genotypes recorded separately.
- Cross+fresh-mutation+lowering warm CUDA-event medians0.95–2.43ms per1024 phases
  on these profiles. No selection fitness/evaluation/setup/export included, so
  not a generation speedup. Raw `native-gpu-phase-variation/{sum,house,median}-v3.log`.
- Scalar recursive fixture: memory sanitizer0 errors, node/index/NT/scope/capacity
  guards, exact transported metadata checked. Finite-language scope fixture
  exposed an overly broad diversity assertion; assertion retained for recursive
  built-in fixture, finite languages report actual uniqueness and validate all
  members. Scope fixture supports BA/Root, declines permutation AB and Float.
- Next: whole-program static skeleton admission, frozen import/export and resource
  accounting, then native GPU evaluator integration and actual selection/search.
  R remains validated best; this foundation is not promoted.

V2 cold layout adapter: `gpu/phase_layout.hpp` independently admits inputs,
extracts phase-aligned independent holes, and exports ASTs without carried proof.
All3072 frozen parents and the full1024-member R seed2 Median32-generation final
population roundtrip with exactly equal compiled instructions/constants/fuel,
bindings and plans. A separate two-coordinate memo grammar fixture also passes;
coupled/inner-lexical fixture explicitly declines. Raw `layout-*.log` alongsideV1.
This proves representability (including native later Median), not search quality.
Next implementation: run-owned double-buffered GPU phase population, completed
fitness-driven tournament/crossover/mutation, GPU lowering, timed code copyback
and native evaluator integration. No CPU reproduction. Best still R.


### V3 — native resident GPU generation, opt-in integration

- `GAGP_GPU_PHASE_POPULATION=1` integrates V1/V2 into existing GPU eval/repro
  modes. Selection/cross/fresh mutation/lowering all GPU, one host thread.
  Between operators and generations no AST rebuild/reverse membership or CPU
  compile; live bytecode copyback/integration remains. Existing native evaluator
  still independently verifies/repacks bytecode each evaluation.
- Explicit unsupported-profile fallback recorded; one admitted history-best
  export/gen and requested final outputs timed. Bounded double buffers, no
  ancestor storage. Default backend unchanged. Operator/RNG differences in
  `docs/design/gpu-reproduction.md`; final changed-child counts not attempts.
- Independent two-coordinate memo fixture:32 generations,P16,full1024cases,
  every exported genome independently admitted/CPU reevaluated exactly;26 unique,
  device storage5,249,984→5,251,568 bytes. Oversized declared capacity explicitly
  rejected, separate authored128-cell variant succeeds. Input/export isolation,
  input-name/index/incomplete-fitness guards, native evolution adapter and explicit
  unsupported-policy fallback pass (`test-integration.log`).
- One-process quick (warmup+3) full P1024 generation medians Sum64.918 /House61.889
  /Median30.656ms; cold import/admission343.993/310.496/352.181ms. Whole-call walls
  434.745/396.574/410.360ms. NOT promoted: formal repeats/longer quality pending.
- Three seeds ×4 generations: full1,048,576 cases/gen,0 unscored, top16 full-case
  CPU reevaluation exact. Final unique counts Sum964–982,House792–845,Median993–994.
  Initial error distribution unchanged; subsequent errors fall. Raw/archive
  `logs/optimization/native-gpu-phase-integration/` (manifest,quick,quality,ASTs).
- Next: evaluator packing remains16–20ms. Hypothesis: a private owner-bound
  certified bytecode view can reuse cold root/region verification plus checked GPU
  phase compiler bounds, without a public verified flag or mutable external
  metadata. Then measure and test31+ generations, wall-clock quality and scaling.
  R remains historical validated best; V3 is a substantial unpromoted candidate.
- V3 focused CTest3/3 pass; integrated32-generation fixture compute-sanitizer
  memcheck0 errors. Raw `native-gpu-phase-integration/{ctest,memcheck}.log`.

### V4 — private executable proof reuse, candidate

- `GAGP_OWNED_PHASE_PACK=1` in the GPU genotype owner uses a private synchronous
  borrowed view. Cold native verification covers immutable root/plan/bindings;
  checked GPU lowering supplies new phase type/instruction/stack proofs. No
  public verified flag; external `eval_programs` still verifies fully. Packing
  retains fuel and transport checks, and view capability analysis still runs.
- 32-generation independent fixture: owned GPU fitness and case counts exactly
  equal ordinary verified packing and independent exported CPU programs.
- Quick warmup+3 medians Sum51.961 /House47.949 /Median17.354ms;
  packing4.004/4.296/4.020ms (V3~16–20ms). Init359/325/364ms; whole-call436/398/409ms.
  Sum~35.4x,House~26.7x frozen denominator; not a formal target claim yet.
- House nsys kernel diagnostic averages (includes cold/warm launches): eval32.234,
  mutation3.297,crossover2.266,lowering1.329,serial offsets0.521,comparison0.355ms.
  Raw `native-gpu-owned-pack/profile/`; these are not headline times.
- `native-gpu-owned-pack/quality/run.py` running3 seeds×32gens, full1024-case
  diagnostics and all1024 final CPU re-evals for seed0,top16 for others. Audit
  fields explicitly record count/mismatches; output remains the original top16.
- Next hypothesis: a one-coordinate structural executor can avoid the generic
  multidimensional state/transition/frame loop while keeping original lazy
  recursion, linear memo lookup, phase expressions and fuel/error order. It is
  structurally selected, not task-selected. Do not retry rejected dense memo.

V4 quality completed:3 seeds×32 generations. Seed0 all1024 final genomes per
workload independently admitted and CPU reevaluated on full cases with0 fitness
mismatches; other seeds top16 exact. Full1,048,576 pairs/gen,0 unscored.
Search walls Sum5.419–6.175s,House2.624–2.818s,Median1.408–1.791s. Last-gen best
Sum0 all seeds,House -7043/-8431/-7460,Median -28478/-19364/-26258. Not convergence
or equal-wall equivalence. Sum mean final nodes217 versus R137 (seed0), and its
late generation166.6ms: initial-snapshot speedup must not represent whole search.
RSS1.29–1.62GB includes final population exports/audits; bounded-memory long run
still pending. Raw `native-gpu-owned-pack/quality/` includes all exports/resources.

### W — structural single-coordinate executor

- `GAGP_COORDINATE_EXECUTOR=1`: one Int coordinate/result,literal bounds,1–2
  offset requests,no preparations/bound operands. Unboxed frames; same lazy
  phases,linear memo,fuel/boundary/capacity behavior. Generic fallback otherwise.
- Initial full House test caught wrong frame byte stride in kernel slicing;
  fixed separately from allocation and covered by37 distinct programs ×1024
  cases. Initial failed binary/log retained under `native-coordinate-executor/`.
  New focused matrix covers inclusive/exclusive bounds,duplicate requests,memo
  on/off,zero/tight capacity and fuel,plus existing lazy-capture/2D fixtures.
  Nonmemoized positive cell-limit test explicitly rejects malformed metadata.
- Corrected v2 focused `gagp_test_bounded_region_gpu` passes. Same-binary flag-off/on
  three processes each, warmup+3, interleaved House medians48.302→44.118ms.
  Off range47.330–59.015,on43.190–44.915ms. ~29.1x frozen ASGP,not30x yet.
  Sum/Median quick51.859/17.401ms. Raw/archive/9repeats
  `native-coordinate-executor/v2/validation.json`; memcheck running.
- Next: reuse typed-view capability proof from immutable root and checked GPU
  phase compiler; retain actual case-type and payload checks. No public proof flag.

W full `gagp_test_bounded_region_gpu` compute-sanitizer memcheck:0 errors.

### X — private typed-view capability reuse

- `GAGP_OWNED_PHASE_CAPS=1`: cold immutable root/phase capability proof is tied
  to ordered grammar input types. Checked GPU phase compiler proves its closed
  typed operation/forward-branch subset; runtime still checks actual case-type
  signature,payload contents,stack/local thresholds. Different case types fall
  back to ordinary analysis.32-gen owned/ordinary/CPU fixture passes including
  a mismatched case-type session, which must use mixed fallback.
- Three independent processes,warmup+3 each, P1024/full1024 cases: Sum50.126518ms
  (49.546908–52.783495),House41.976280 (41.468189–43.851836),Median15.701990
  (15.545705–16.123876). Frozen headline medians~36.73x/~30.54x/~3.93x.
  House tail still crosses30x threshold. Raw9 repeats and immutable binary in
  `native-owned-capabilities/{formal.json,manifest.json}`. Cold/whole-call costs
  separately present in every row; not an equivalent full-search speedup.
- Next: GPU mutation currently generates construction origins then traverses the
  new tree again to annotate grammar sites. Record those sites at construction
  and independently audit them; preserve RNG/operator outputs, remove this work.
  Final whole-suite/scaling/equal-wall quality still required before promotion.

### Y — constructor-emitted site metadata: rejected

- GPU constructor recorded sites while generating donors, eliminating later
  annotation. Independent site reconstruction and32-gen CPU reevaluation pass.
  CUDA resources decreased from4784B/80 registers to3248B/76 registers,no spills.
- Same-binary flag A/B,3 processes each,warmup+3: House42.026227→43.539508ms;
  ranges41.422101–42.780943 vs43.086235–44.247228. Both evaluator and reproduction
  GPU times shifted, so no defensible instruction-level causal speedup claim.
- Rejected: restored X implementation and tests, removed active experimental
  switch. Patch,immutable binary,all raw repeats retained under
  `native-construction-sites/`. X remains candidate. Do not retry merely because
  the compiler resource report looks smaller.
- Freeze candidate for final validation. Add actual generation-completion times
  including cold initialization, and optional already-retained history exports,
  to compare completed prefixes at an identical5-second search budget. Longer
  fixed-generation runs preserve operators and all cases; no early-stop shortcut.
  Predeclared screening thresholds for this run:Sum fitness0,House>=-8000,
  Median>=-25000. Three seeds only; not convergence equivalence.

Final native rerun entry (same tested serial runner, parameterized output path):
`python3 benchmarks/fixed_asgp/optimization/native_candidate.py daily --out
logs/optimization/native-daily-check`. `paired` repeats three independent processes,
`scaling` covers all populations. Existing archived experiment still resumes with
its original `logs/optimization/native-counted-validation/run.py`; runtime binary
unchanged across final scaling and the later test-only profile-name correction. No prepare/input overwrite.

Final acceptance:
- Full suite127/127 with adopted evaluator/CPU-view settings; the explicit GPU
  genotype tests select their distinct operator profile.5 fixed-tool,24 repository
  checks pass. Numeric boundary profile-name assertion was corrected to include
  the eligible coordinate executor; exact values/fuel/errors were not relaxed.
- Full60-cell matrix and240 warm/measured rows retained in
  results/native-counted-scaling.csv and native-counted-scaling-raw.csv.
  Same-round fastest GPU ratios for P1024/2048/4096/8192:
  Sum37.77/39.80/44.73/48.81;House32.94/36.87/40.35/39.91;
  Median4.99/4.40/5.73/5.46. These use matrix controls, not headline denominators.
- P1024 headline remains9-repeat48.884881/40.384029/12.213825ms and frozen
  ASGP ratios37.66/31.75/5.06. Cold import333–370ms is not included in those
  generation rows; it is charged once per search, separately reported and
  included in all equal-wall-clock comparisons. Real64/96/512-generation
  searches took11.158–14.761 /7.609–9.318 /29.926–31.689s; initial admission
  occupied2.49–3.28% /3.55–4.38% /1.17–1.26%. Do not infer30x whole searches.
- Runtime/archived benchmark SHA unchanged after the full matrix. Parameterized
  checked-in runner resume verified without changing manifest or outputs.
- Objective reached for Sum/House; Median substantially improved. No further
  runtime experiment pending. Remaining work, if a new round is requested:
  reduce eval interpreter cost, CPU reproduction in GPU-e, padded GPU genotype
  allocation/cold import, and retained-history proof memory. These are not
  completed optimizations. Failed dense memo/construction-site/hash-only paths
  remain rejected as recorded above. CPU fragment route remains removed.

## CPU perf access and focused diagnosis — 2026-10-02

Host owner enabled perf_event_paranoid=1; own-process cycles:u sampling works.
No installation, system change, worker thread or runtime change by this experiment.
Best checkpoint remains beecdf5 / runtime d1712a5 with the recorded best flags.
Archive: logs/optimization/native-perf-20261002 (commands, flags, binary SHA,
raw perf.data, script, reports and summarizer). Tracked compact evidence:
results/native-perf-20261002.json. All non-time benchmark fields match controls.

Sum CPU whole-process self cycles: interpreter43.59%, region driver14.26%,
phase binding11.18%, builtins8.94%, ValueStack push7.43%. This motivates
precompiled phase bindings / less interpreter frame and Value movement.
Median GPU-e deeper999Hz/32768-byte callchain capture:1296 reproduction samples
out of2345; within recognized reproduction stacks inclusive variation analysis
86.08%, certification72.40%, witness18.19%, runtime identity15.67%, serialization
13.05%, compile11.45%. Inclusive percentages overlap; do not add or translate
them directly into generation milliseconds. Unknown leaf14.76% remains within
the reproduction subset; unwind coverage is incomplete. Initial shallow capture
is retained as such, not used to claim precise per-generation attribution.
Next host experiment should remove repeated analysis/certification inside existing
native operators, preserving CPU1T, grammar admission and GPU-er/o reproduction.
No speedup or new implementation is claimed by this diagnostic checkpoint.

## Parallel architecture-preserving optimization — 2026-10-13

Checkpoint: runtime/experiment tree `9ddab1d`; chat worktree snapshot `75d7f83`.
No push. Original frozen inputs and archives remain unchanged.
Target: CPU1T approaches ASGP1T; native GPU-er-o search-wall House >30x,
Median >5x. Historical frozen-generation ratios are not this target.
Short comparison: House32/seed0, Median64/seed2, P1024/C1024; all five modes.
Primary metric retains `evolve_wall_ms` (cold evolution init, every generation,
final eval). Input import and post-search audit/export are outside this metric;
process wall includes them and is reported separately. Flags and penalties match
long-search manifest; House1000, Median1. No algorithm or evaluation changes.
Runner: `python3 benchmarks/fixed_asgp/optimization/short_search.py --binary
logs/optimization/long-search-20261013/bench --out logs/optimization/parallel-20261013/baseline`.
Use fresh output directories; `--binary`, `--reference-root`, `--modes`,
`--generations`, `--seeds`, `--flag` support focused comparisons. Every benchmark
process takes `/tmp/gagp-parallel-20261013-benchmark.lock`; agents use the same
lock for GPU tests/profiling. Initial baseline may overlap compilation; important
candidates require quiet-machine interleaved confirmation.
Worktrees/branches: `parallel-cpu-eval-20261013`, `parallel-cpu-repro-20261013`,
`parallel-gpu-20261013`, under the existing worktrees directory and `opt/` branches.
Hypotheses: direct CPU phase binding; deferred native CPU site analysis; GPU
per-program compact execution / changed-phase lowering after cost attribution.
No CPU fragment backend, CPU workers, reduced cases, or benchmark solver.
Status: baseline in progress; no new runtime improvement claimed.

Transport candidate: `GAGP_BATCH_PHASE_IMPORT=1 GAGP_BATCH_PHASE_EXPORT=1`.
Cold/final compact transport preserves exact 4-generation traces/final ASTs;
32-generation full-case owner test also compares bulk/per-member export.
House4 median search752→662ms; Median4 797→~644ms (early exploratory).
Main House32 2245→2144ms; Median64 2058→1986ms, single runs; modest gain,
not target achievement. Raw: `logs/optimization/parallel-20261013/{import-on,
import-off,io-on,io-main}`; compact evidence `results/parallel-20261013/transport-experiment.json`.
Median CPU128 seed2 full population, champion-368, imports to GPU native profile
without fallback and preserves initial full-case fitness; representation supports
this known better solution. Probe is support evidence, not speed comparison.

Static grammar candidate: perf cold trace identified repeated `compile_phase_grammar`
inside each import (8.7% inclusive whole-process cycles in one diagnostic, not
8.7% of search-wall). `GAGP_REUSE_PHASE_GRAMMAR=1` now reuses pure grammar
compilation in an owner-bound cold cache. No admission/witness bypass. With
batched I/O, House32 search repeats2049/2045/2074ms; Median64 1793/1855/1793ms.
All traces and final ASTs exactly match original baseline. Focused owner/layout
tests pass. Evidence `results/parallel-20261013/static-grammar-experiment.json`.
### Parallel GPU exploration — 2026-10-13

Worktree `parallel-gpu-20261013`, checkpoint base `9ddab1d`.
Retained candidates: parallel lowering-offset scan; owner capability buckets
sharing packed tables with index-mapped output; opt-in smaller reproduction
blocks. No RNG/operator/numeric/fuel/language change. Copying subset bytecode
prototype was removed: Median host regrouping outweighed kernel savings.
Focused main House32/s0 and Median64/s2, P1024/full1024 cases: indexed buckets
and 1-thread reproduction blocks give1870.241/1773.683ms search wall versus
same-build prefix-only2189.835/1995.761ms (single process; formal confirmation
pending). All10 candidate/control pairs retain identical per-generation quality,
operator/error counters and final fitness/node rows. Full32-generation owner
fixture and257-member mixed-stack fixture pass independent CPU reevaluation.
NSYS Median16 diagnostic: offset scan0.821→0.0115ms; crossover1.758→0.636ms;
mutation4.852→4.056ms; generic eval4.024ms becomes compact2.353 plus
fallback0.248ms. Profiled components are not subtracted from unprofiled totals.
Raw paths/hashes/timings: `results/parallel-gpu-20261013/experiments.json`;
NSYS archives: worktree `logs/parallel-gpu/diagnostic{,-index}/`.
Next: integrate with root cold-import/export changes, repeat combined candidate,
then address remaining mutation and House evaluator cost. No benchmark controls
or original inputs were modified.

Rejected semantic experiment: fitness ties prefer smaller GPU genotype. Full
cases kept, but Median3seeds all end-642/mean~20nodes and427–448unique, whereas
control seed0 reaches-497 and~1000unique. House2/3seeds worse andseed1slower.
Median~1.03s is not accepted as5x goal evidence. Code/flag/docs removed; archived
binaries/raw outputs retained only as historical evidence. Record:
`results/parallel-20261013/rejected-size-ties.json`.
Combined exact-behavior GPU main3repeats: House median1662ms, Median1484ms;
raw `logs/optimization/parallel-20261013/combined-gpu-main`. Goals still unmet.

GPU static donor weights: `GAGP_GPU_STATIC_WEIGHTS=1` proves depth/capacity-
independent normalizing maxima at grammar import and pre-divides weights.
Compared with the combined capability-bucket/transport/grammar-cache candidate,
three independent complete-search repeats retain exact quality/operator/error
traces and final fitness/node rows. Raw repeats and medians are in
`results/parallel-gpu-20261013/static-weights.json`. The device construction
matrix across depths/capacities passes exact node/origin/constant comparisons;
32-generation independent CPU reevaluation also passes. No language, operator,
RNG or resource-limit change. Remaining House kernel cost is dominant; focused
NCU reports low eligible warps and substantial dynamically indexed local memory,
so a separate shared operand-stack prototype is being evaluated, not yet adopted.

Bucket workspace candidate: compact/generic grids planned independently under
the same512MiB sharedscratchceiling. Samebinary3process medians House1648→1583ms,
Median1396→1361ms; unchanged traces/finalpopulation. Mixed257program/32generation
CPUaudit test passes. Setting`GAGP_BUCKET_WORKSPACE=1`; evidence
`results/parallel-20261013/bucket-workspace.json`. Improvement modest, supports
capability-dependent concurrency without reducing execution limits.
House scalar structural comparison: `GAGP_BOUND_COMPARE_PHASE=1` recognizes
LOAD/Int CONST/comparison phases under typed proof and one non-parameter binding.
Three-process full-search results and exact trace checks are recorded in
`results/parallel-gpu-20261013/bound-comparison.json`; focused tests cover
LT/LE/GT/GE, both operand orders, optional Return, >2^53 conversion and fuel.
Shared-stack8 and cached-top interpreter prototypes were tested and rejected:
House same-binary1637-scale controls became1767→2006ms and1961→2144ms,
respectively (their disabled binaries also regressed). All prototype code and
switches were removed; patches/binaries/results remain under `logs/parallel-gpu/`.
They are not recommended paths. Next bounded experiment: phase-local fuel
ownership, then a forward-only phase fuel upper bound if profiling supports it.

Coordinate ring memo: `GAGP_COORDINATE_RING_MEMO=1` requires the typed coordinate
executor, memoization, first offset -1, other offsets -1..-4, and a statically
proved lower-interval base predicate. Four scalar values replace the linear
lookup; original entry/base/fuel/frame/insertion-capacity behavior remains.
Unsupported shapes keep generic lookup; workspace allocation is unchanged.
Six same-binary search repeats per setting show about2% House32 search-wall
and7% diagnostic kernel improvement. Exact final AST/trace comparison,
independent proof review and exhaustive focused fuel/capacity tests pass.
Evidence: `benchmarks/fixed_asgp/optimization/results/parallel-20261013/ring-memo.json`.
Bucket workspace candidate: compact/generic grids planned independently under
the same512MiB sharedscratchceiling. Samebinary3process medians House1648→1583ms,
Median1396→1361ms; unchanged traces/finalpopulation. Mixed257program/32generation
CPUaudit test passes. Setting`GAGP_BUCKET_WORKSPACE=1`; evidence
`results/parallel-20261013/bucket-workspace.json`. Improvement modest, supports
capability-dependent concurrency without reducing execution limits.

GPU follow-ups rejected (2026-10-13): phase-local fuel ownership plus a
forward-only phase fuel upper bound passed existing fuel/CPU-audit tests but
three-process House medians1471.377→1461.782ms overlapped, Median1398.421→1411.805ms
regressed. Per-warp diagnostic-count aggregation preserved all trace counters
and passed bounded-region tests, but House1471.947→1450.040ms and
Median1366.080→1377.148ms did not establish a useful total-time gain; kernel
differences were below1%. Both prototypes and switches were removed, patches
and raw outputs archived in `logs/parallel-gpu/`. Recorded results:
`results/parallel-gpu-20261013/rejected-followups.json`. Next: inspect remaining
cold native import/final export cost; root separately tests structurally proved
coordinate memo storage. No phase semantics, counters, or operator changes adopted.

Cold import reuse: `GAGP_REUSE_IMPORTED_EXECUTABLE=1` eliminates the second
AST verification/lowering after fresh grammar admission, using the evaluator's
actual input order. Secondary imports no longer copy already cached immutable
profile tables only to discard them. All input admission, GPU bytecode checks
and output membership checks remain. Three-process House1478.364→1408.281ms,
Median1394.884→1340.333ms; raw repeats and cold-prefix times:
`results/parallel-gpu-20261013/cold-reuse.json`. All quality/operator/error traces
and final fitness/node rows are exact. Existing32-generation CPU/grammar audit,
mixed257-member test, and new reversed input-order test pass. No changed
execution, grammar, RNG or operators. Next: eliminate repeated DInstr→Instr→DInstr
conversion and static repacking through private owner-bound packed artifacts.

Rejected full register-bank8 interpreter: compiler generated heavy spilling;
House samebinaryOFF1832→ON1949ms and disabledbinary already regressed from
~1404ms. Median1391→1373ms does not offset House loss. Focused parity/owner
tests passed, but no code/flag is retained. Rawpatch/binary/compilerreports:
`results/parallel-20261013/rejected-register-stack.json`. This differs from the
previous cached-top/shared-stack tests and also fails to remove interpreter cost.
Private packed carrier: `GAGP_OWNED_PHASE_PACKED=1` eliminates the per-generation
DInstr→Instr/fuel→DInstr round trip and static structure repacking. Checked GPU
compiler outputs update private phase ranges and refreshed optimization hints;
reference bytecode is materialized on demand, including different-input-type
fallback before capability analysis. Three-process House1460.209→1397.288ms,
Median1410.296→1212.252ms; initialization adds about21ms and remains counted.
Raw repeats, per-stage medians, RSS/device bytes and identical final AST hashes:
`results/parallel-gpu-20261013/owned-packed.json`. No RNG, operator, grammar,
numeric, fuel or capacity changes. Transfers and a per-evaluation carrier copy
remain; this is host-work elimination, not a resident executable claim. Focused
32-generation CPU/admission, mixed257-member, wrong-type fallback and pattern
invalidation tests cover the changed path. Next integrated bottleneck is House
evaluation; root independently explores a scalar operand-bank prototype.

Exact compact GPU generator tasks retained: pending entries16→8bytes using
existing expression/depth/scope bounds, with explicit pre-assignment ID and
reservation checks. No new runtime switch or grammar/RNG/operator change.
Three-process House1361.463→1367.503ms is neutral; Median1177.709→1160.406ms
improves1.47%, non-overlapping ranges. Reproduction-kernel medians improve about3%;
mutation registers86→84, peaklocalframe remains4784B and spills remainzero.
`results/parallel-gpu-20261013/compact-generator-task.json` records all repeats
and identical finalAST hashes. Reference-task differential tests cover scalar
and scoped grammar roots, maxdepth64, expressionID65536, eight-slot permutations,
invalid IDs/counts and complete generated-node/fuel/constant/status equality.
32-generation independent CPU/grammar checks pass. Packed-carrier quickquality
3seeds×4generations also completed (`logs/parallel-gpu/packed-quality`). All
subagent builds/performance runs are now stopped for root's quiet integrated
validation; no additional direction is queued on this branch.

2026-10-13 follow-up: same-slot GPU executable reuse rejected. Based on integrated
runtime `b60a336`, the minimal prototype retained only previous compact code and
constants, double-buffered certificates, and selected-parent slot lineage. Edits
invalidated that slot; first generation compiled fully. There was already no
crossover-intermediate bytecode compile to remove. The 40-generation, two-slot
exact differential passed with both mutation-buffer modes, repeated selected
parents, rejected mutation input, independent CPU export checks and existing
regressions. Three-process full-search medians OFF→ON: House1361.168→1352.552ms
(0.63%), Median1168.505→1160.476ms (0.69%); ranges overlap. Same-window archived
`b60a336` measured1362.134/1164.541ms, also comparable. Four-generation NSYS compile
totals fell4.064→3.482ms /3.614→3.304ms, but this diagnostic reduction did not
establish useful full-search improvement. Do not subtract it from unprofiled
search times. Both compiler variants used40registers,2816Bstack, zero spills.

The prototype, flag and exclusive tests are removed; no runtime/settings changes
remain. Evidence: `results/parallel-gpu-20261013/rejected-executable-reuse.json`.
Archived binaries, raw repeats, profiler CSVs, test log and rejected patch remain
under the GPU agent worktree's `logs/parallel-gpu/reuse-executable-*`. Its local
build contains the archived rejected experiment until rebuilt; the root's
validated runtime binary is unaffected. No dirty-work-queue extension is planned:
its additional complexity is unsupported by this full-search result.

2026-10-14 fused native GPU donor sites retained opt-in:
`GAGP_GPU_FUSED_DONOR_SITES=1` writes exact construction-site metadata while
emitting each mutation donor. Imported trees still use independent annotation;
splice/resource validation and the single post-variation lowering remain.
A dedicated mutation specialization removes the annotation traversal and reduces
in-place local frame4784→1712B, registers86→76, no spills (sm86).
Same-binary3-process House32 median1377.781→1358.585ms (1.39%); Median64
1188.736→1152.144ms (3.08%). Same-window archived-best1379.784/1184.140ms
is comparable to disabled. All final ASTs and quality/operator/error traces are
exact across archive/OFF/ON. Five scoped/unscoped generation, variation and
32-generation population tests pass. Evidence:
`results/parallel-astra-20261014/fused-donor-sites.json`. This changes construction
work only; size-near crossover is a separate search-rule experiment.

2026-10-14 size-near crossover rejected. The separate native GPU search profile
kept parent/destination/source-A streams and, on an independent 50% gate, drew
source B from the same compatible donor population, choosing nearer subtree size
(ties A). No added rejection, cap, fitness tie-break, task dispatch or mutation
change. Analytic sampler, scoped variation and32-generation CPU/grammar tests
passed. Three fixed seeds at House32/Median64, then both tasks128 generations,
retained fullP1024/C1024 coverage. House128 final fitness OFF→ON:
-4427/-865/-4535→-8280/-8303/-5480; House32 timing did not materially improve.
Median128 -478/-642/-642→-529/-642/-642. Actual untimed seed0/16-generation
novelty remained high (House final unique1009→992, novel versus all parents936→888;
Median1022→1017,943→929), so changed-child counts did not conceal the old small-tree
collapse. This still failed search-quality requirements; no parameter sweep follows.

All size-near runtime flags, exclusive probe/tests and documentation are removed.
Patch/binary/full raw traces and novelty exports remain in the donor worktree's
`logs/bloat/`; evidence is `results/parallel-astra-20261014/rejected-size-near.json`.
Root supplies supplemental preregistered equal-wallclock/heldout analysis. Fused
construction-site optimization is independent and remains retained.
2026-10-14 native CPU admission follow-up: two independent opt-in candidates.
`GAGP_CPU_FUSED_ADMISSION=1` fuses unique exact-head membership into witness
construction; ambiguous/alias heads retain recursive matching and repeated-hole
checks remain. Three-process full GPU-e House32/Median64 medians 9613.752 → 9348.258 /
26767.027 → 25443.351 ms. The speculative matching-work guard accounting changes
explicitly, including public unframed single-root reconstruction; strict
`require_membership` is unchanged. Evidence: `results/fused-admission-20261014.json`.

`GAGP_CPU_COMPACT_ADMISSION=1` instead validates/lowers fresh scalar single-root
inputs before stable compaction, then seals the same canonical witness to the
compacted AST when names are unchanged. Name-table changes rerun admission;
registry-backed constants, attached provenance and multiple roots keep the
ordinary path. No guard/RNG/grammar/resource/execution changes for this flag.
The old rejected GPU compaction experiment had zero hits; CPU splice output has
3045/13244 House and 2605/13122 Median constant compactions in 8 generations,
with zero name changes. This removes actual second reconstruction/verification.

Same-binary three-process GPU-e medians OFF/compact/both:
House 9746.720/8594.850/8287.609 ms; Median 26804.613/25124.282/23868.246 ms.
Every final/history AST, fitness/operator/error row is exact. Frozen CPU1T
three-process warmup+3 total medians OFF→both House 3370.624 → 3379.262 ms (neutral),
Median 549.086 → 531.677 ms; reproduction 235.714 → 211.529 / 217.208 → 193.454 ms.
One combined full CPU validation takes 82.929/41.725 s versus archived medians
84.247/44.762 s, not a paired full-CPU speedup claim. Native GPU-overlap three-process
medians 1379.315 → 1356.892 / 1188.305 → 1167.503 ms; cold admission saves only 7.64/6.50 ms.
Targets remain unmet: House CPU is evaluator-dominated and the GPU whole-search
improvement is small. No default or best-settings adoption is made here.

Eight focused native tests pass. Extra compaction coverage compares retained
versus independent lowering for every fuel 0..1000, checks invalid unused entries,
stale proofs and name fallback. Archived-binary strict admission and independent
full 1024-case CPU/GPU audit pass all 1056 House and 1088 Median final/history
programs; combined exports are structurally identical to those audited artifacts.
Raw preserved binaries/manifests/profiles/resources live in the admission worktree's
`logs/local-admission/`; tracked evidence is `results/compact-admission-20261014.json`.

2026-10-14 private constructive export retained opt-in:
`GAGP_GPU_CONSTRUCTIVE_EXPORT=1` returns the owner-constructed AST without an
optional reverse-parsed canonical derivation/compiled cache. Cold admission,
private grammar/domain generation, exact nonterminal/scope splices and physical
resource checks establish membership; existing export origin/opcode/fuel/index
validation and metadata compaction remain. No external trusted bit/certificate
or generic import shortcut is introduced. `ProgramGenome::derivation` is optional.

Same-binary3-process House32 search1377.520→1228.279ms (10.83%), Median64
1186.501→1057.472ms (10.87%); process2.636→2.480s /2.393→2.245s.
Final-evaluation/materialization medians190.052→39.112ms /138.506→14.714ms.
All6 OFF/ON finalAST exports and semantic/operator/error traces are exact.
The32-generation all-member CPU/grammar test runs with certificates disabled;
new cache/no-cache and modified external replay tests pass. Both fullP1024 final
populations additionally pass archived-runtime strict grammar import without
certificates and fullC1024 CPU/GPU evaluation, matching every search fitness.
Evidence: `results/parallel-astra-20261014/constructive-export.json`. Root's
separate import-certificate reuse and integrated timing are not conflated here.

2026-10-14 prepared immutable GPU IntList parameter views retained opt-in:
`GAGP_PREPARED_CASE_VIEWS=1` prepares a separate case table after the existing
complete-payload/type-flow proof, and only if a packed region declares an IntList
parameter. Offsets are rebuilt for each evaluation's payload table; original
lengths/presence masks and lazy fallback remain. No device VM, fuel, search rule,
reproduction backend or task dispatch changes. Build/pack/upload costs remain timed.

Alternating OFF/ON, ON/OFF, OFF/ON three-pair House32 medians1192.607→1172.735ms
(1.67% improvement), Median64 1008.864→1020.615ms (1.16% regression). House summed
eval kernel617.187→576.899ms; generation804.596→780.143ms, with extra host packing
and upload. All6 paired final AST and semantic/operator/error traces are exact.
The larger grouped-run apparent win is not attributed to this optimization.
Pre-experiment archived control in the same broader window was1211.302/1026.301ms.

Earlier unconditional and shared-list-presence-only stages are rejected: Median
DOES have a shared list input, but carries it as already-converted state and has
no parameter captures. Its extra table preparation therefore had no kernel benefit.
The final gate inspects packed region parameter types, never benchmark names.
Focused fitness/bounded-region parity passes; final captured-list tests pass
CUDA12.6 compute-sanitizer with0errors. Coverage includes private marker sanitization,
empty/list lengths, shifted constant-payload tables, fuel, missing/wrong captures,
and existing state-only sequence paths. Evidence:
`results/parallel-astra-20261014/prepared-case-views.json`. Root's combined nine-cell
and scaling runs determine final adoption; no further candidate directions planned.

2026-10-14 ring workspace liveness experiments rejected:
The proven four-register coordinate memo ring never reads global memo history,
while generic capability buckets still require that history. Removing dead
backing was correct, but exact-sized workspace allocation changed whenever the
live generic bucket count changed. Fixed-grid three-pair House32 median
1063.828 -> 1146.781 ms; summed upload/allocation roughly 29 -> 105 ms with neutral
kernel time. The original expanded-grid variant's regression was also dominated
by this allocation churn, not evidence of a large concurrency-only penalty.

A bounded geometric-capacity reuse variant preserved the original grid and
512 MiB total allocation ceiling, freeing the old layout before replacement.
Its three-pair House32 median 1074.895 -> 1092.077 ms; kernel 573.176 -> 572.886 ms,
upload 29.457 -> 48.462 ms. Median64 uses the window executor, so its apparent
922.079 ->911.414 ms change is control drift, not a live-ring benefit.
No runtime flag/code or test-only fixture is retained. All three rejected
patches and binaries remain archived under the liveness worktree's logs.

All paired full population/history ASTs and semantic/operator/error rows match.
Focused fitness/bounded-region/native population parity passes; a temporary
257-member mixed compact/generic unary-ring fixture checks full 1024-case parity
and reproduced offspring against independent CPU execution. CUDA 12.6
compute-sanitizer reports 0 errors. Structured results, raw paths, binary hashes,
and original archived controls: `results/bloat-liveness-20261014.json`.


2026-10-14 Sum scheduling/layout candidate (opt-in, pending integration):
`GAGP_DYNAMIC_PROGRAM_QUEUE=1` lets each physical region block claim its next
logical program row after completing all cases. It retains its own workspace,
original row scatter and each program's reduction order. Compact/generic queues
are separate. `GAGP_WINDOW_SOA=1` transposes unboxed window frame fields across
case lanes without changing frame sizes, declared limits or the 512 MiB budget.
Unsupported/generic frames retain their existing layout. Both flags are off by default.

Three alternating full Sum32/seed0 P1024/C1024 pairs: search median
4345.244 -> 4180.848 ms (3.78%); summed evaluation kernels
3599.167 -> 3455.570 ms (3.99%). Archived control4329.454 ms; initial same-binary
OFF/queue/SoA/both screens4395.224/4268.687/4296.499/4172.046 ms.
Every final/history AST and semantic/operator/error row matches across all11runs.
This is a small candidate improvement, not attainment of25x or cross-task evidence.

Validation correction: the reported new17-program/65-case forced-queue fixture
coverage was invalid. Its source was edited during compilation; the old-source
object completed after the edit, preventing subsequent timestamp-based rebuilds.
The resulting test binary lacked the new assertion strings. Existing tests in
that binary passed and memcheck reported0errors, but neither establishes coverage
of the new queue/SoA fixture. Its4096-frame limit also violates the existing GPU
capacity128. Root is replacing it with97programs×65cases and128legalframes,
then rebuilding and running fresh memcheck. Production capacity must not change.
Full-search benchmark binaries and all11exact trace/AST comparisons are unaffected.
The final host guard reserves16bytes for queue shared state; archived timing
binaries precede that guard-only change. Raw patches/binaries/manifests remain in
the scheduling worktree's logs; tracked evidence:
`results/sum25-scheduling-20261014.json`.


2026-10-14 per-phase compiler-bound metadata candidate:
`GAGP_PER_PHASE_STACK_BOUND=1` retains each dynamic phase's actual checked GPU
compiler stack certificate, using the existing packed-phase row mapping. Fixed
phases retain their original conservative cold verified bounds; whole-program
bounds and capability bucket admission stay unchanged. This lets small phases
within a generic bucket use the existing typed phase VM. No executable, fuel,
operator, RNG or architecture changes. `GAGP_GENERIC_PHASE_VM` remains dominant.

One isolated same-binary full Sum32/seed0 pair: OFF4345.325 -> ON3755.088ms
(13.58%); summed kernels3594.498 ->3042.660ms. Final/history AST and all semantic,
operator and error rows are exact. Root's measure cache/hybrid combinations and
repeated three-task timing are separate pending work, not part of this claim.

The257-row mixed-stack regression compares fresh compiler metadata against a
conservative owner, independentCPU fitness and a second forced-generic generation.
It exposed an existing debug-override bug: capability rebucketing ignored the
forced generic phase bound and could launch the compact interpreter. The bucket
gate now honors `GAGP_GENERIC_PHASE_VM`; the focused native population test passes.
Evidence: `results/sum25-phase-bounds-20261014.json`.
