# Pre-run contract — 2026-10-13

Primary: same frozen P1024/C1024 initial populations and training cases on Sum,
House and Median; 3 reproduction seeds (0,1,2); all five native architectures;
16 generations plus complete final evaluation. No early success stop.
These are ASGP-generated tasks/data, not official PSB dataset files.
GPU best profile enabled; all application host workers limited to one.
ASGP retains existing fixed benchmark elite1, phase crossover0.7, mutation0.3,
max phase depth7. GAGP retains crossover then mutation0.3, subtree probability1,
grammar budgets, fuel2e6. Different search languages/operator policies remain;
results compare configured systems, not isolated hardware causality.

Compare current best, mean and cumulative best by generation and completed
search wall time; predeclared thresholds Sum0/House>=-8000/Median>=-25000.
Log generation/eval/repro/compile/kernel/packing time where supported, best nodes,
final population fitness/nodes/uniqueness, errors/timeouts/fallback/unscored where
available, operator counters, RSS, process wall, initialization/final evaluation.
Missing CPU/ASGP runtime counters are unavailable, not zero. Structural node
counts differ between ASGP phases and GAGP ASTs. Trace cost is in search wall;
post-search audit/export/report cost is in input-through-report/process wall.
Early/late timing = medians of first4 and last4 generations for each seed.
Same wall-time quality uses only completed generation records, with cold costs.
Three seeds characterize variability, not confidence in convergence equivalence.

If primary matrix finishes within budget, independently rerun GPU-er-o for128
changing generations with same3seeds/tasks, and assess bloat/late time. Never
call different populations identical-workload speedup. No profiler or concurrent
GPU benchmark. Budget 45min primary plus <=5min extension; failures/time limits
are retained explicitly. Frozen inputs/archived binaries are never overwritten.

Metric clarification found before primary matrix completion: ASGP Median uses
native exact-error count, GAGP uses capped L1. Raw cross-system fitness is not
commensurable. Audit each native-selected generation champion on the same1024
training cases with common GAGP CPU capped L1, after ASGP AST translation and
independent admission. Show raw native objectives separately. Held-out champions
are selected by each system's original training objective, never by held-out.
No claim of objective-equivalent operators or arbitrary-domain generalization.

Timing-only extension amendment (before launching any extensions): fixed16-gen
fast runs may finish before slow CPU completes its first generation. To make
wall-time quality comparisons useful, use10s as common search budget. Extend
primary runs ending<12s in fresh processes with the same seed; choose power-of-2
G from14s / median last4 primary generation time, min32 (GPU-er/o min128), max512.
No fitness-based length choice. Primary runs already>=12s are reused explicitly.
All modes are eligible, not only GPU-o. Check deterministic primary prefixes.
If any run still does not cover10s, mark uncovered rather than extrapolate.
Extension budget at most15min, subject to overall one-hour execution budget.
Compare fixed16-gen e2e separately from variable-length extension totals.

Objective-alignment amendment requested during execution: retain original Median
L1 raw runs, but rerun all five Median modes/seeds under ASGP exact-error objective.
GAGP admitted Int output/Int targets permit cap/error penalty1 as an equivalent
implementation. Sum/House unchanged cap1000. New Median results use a separate
archive/binary; no original results replaced. Threshold for aligned Median is
at most512 wrong cases (at least50% correct), declared before aligned runs;
original L1 threshold does not apply. All changes are fitness configuration only.
No general float exact-match claim. Fitness extensibility proposal is documented
separately; no new execution/reproduction architecture is introduced here.

Before any extension: raise timing-only generation cap from512 to2048, so fast Median paths can cover10s. No change in cases, objective, or timing boundary.

Long-run timing amendment: House extensions grew much slower than projected from generation16. After the first extension budget, remaining cells may use completed same-task/mode extensions to select the power-of-two count that covers14s, with existing minima/cap. All completed long runs retained. Selection uses timing only, never fitness. Stage budgets remain bounded to aim for total execution under one hour.
