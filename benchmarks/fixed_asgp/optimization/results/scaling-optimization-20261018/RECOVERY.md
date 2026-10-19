# Scaling optimization recovery — COMPLETE (2026-10-19 02:40 UTC)

This NEW scaling/performance sprint is complete. **Do not launch/resume any search, pilot, controller or champion judge for it.** No active benchmark children remain. Final delivery is [final-v3/REPORT.zh-TW.md](final-v3/REPORT.zh-TW.md); use [CURRENT.zh-TW.md](CURRENT.zh-TW.md) for the short explanation.

## Fixed scope and actual completion

- Every shell command used `/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015`, branch `opt/list-output-5x-20261015`. Older UI checkout untouched; no subagents or pushes.
- Original window **2026-10-18 11:18:51 UTC** (`1792322331`) to HARD **2026-10-19 11:18:51 UTC** (`1792408731`, Taiwan19:18:51). Never reset. Search, CPU judges, reports, real visuals/tests and local delivery completed inside it; final timestamps/commit recorded in raw delivery receipt.
- CPU1T; GPU evaluation AND reproduction;16GiB workspace; physicalAST2048/depth24/phase1024;C1024. All heavy work shared `/tmp/gagp-support-20261015-gpu.lock`; nsys only. No smaller-workspace/CPU-reproduction/replacement-population workaround, no hidden reruns or failed-row deletion.
- Formal `logs/scaling-optimization-20261018/formal-attempt1/`: **360/360ok,0failed**, all30 × P1024/16384 × ASGP1T/originalGPU/tupleGPU × G8 ×2 cold processes, seed0. Last child ended01:39:03.686 UTC; controller3212829/all children exited.
- Judge `logs/scaling-optimization-20261018/champion-judges-attempt0/`: **360/360ok,0failed**, final plus every generation champion on frozen training; ended02:10:25 UTC. Controller3235905/all children exited. No candidate reranking/new searches/test use.
- Both GPUs block512, tuple flag0/1; no compact-root VM or CPU training guard. Rep0 auditsALLP CPUfitness+wrong/full finalAST;rep1 audits first16/best export. All120 execution comparisons true,60 full finalAST pairs;1046400 fitness and1046400 wrong program observations,0mismatches. Not all-generation population/universal parity evidence.
- Independent report audit2352 hashes and720 nonoverlapping process intervals passed; all60 speed cells qualified; no analysis issues. Same binary/source/settings/inputs, fresh ASGP denominators. One evolutionseed0/frozen training set; timing repeats are not independent quality seeds, no new heldout/generalization/paper-replication/time-to-solution claim.

## Results and immutable provenance

ASGP/originalGPU geomean P1024/16384:7.8643/11.1035×; ASGP/tupleGPU9.2976/12.4306×; original/tuple1.1823/1.1195×. Most other tasks near1×; gains concentrated IsSorted/AllEqual. Negative original/tuple cells7/30 and11/30 retained. Training final success P1024 all4/30(gen0=2,new2);P16384 ASGP9/30 versusGPU8/30(gen0=5,new4/3). No independent test measured.

- BinarySHA256 `731697e685a3ca854027a83f18b87dd0e96e41532363cd3186d01c6f5a527ade`.
- Freeze `logs/scaling-optimization-20261018/formal-freeze-attempt1.json`,SHA256 `4a3cb7b9fd8ac9838ab2c791ad235fe5e3b89437cb7ba4116ed4d54459456276`.
- CPP unchanged from tested final/sourceHEADa67d6e8. Frozen pre-formal formal.py/serial.py/test_serial.py changes match the matrix freeze; never restore or modify its dependencies to reinterpret old data.
- Reports/CSV/PNG/PDF/summary/audit/validation/actual browser screenshots and console results in **final-v3/**. Original finalHTML SHA256 `f8ff60a11e82358f0493f3bd80252c6664f459c740a44796532e4d5341fe2da5`; html_render received this unchanged original, without preview checks.
- First two presentation versions archived byte-for-byte in raw `report-presentation-attempt0/` and `report-presentation-attempt1/`; path/hash map in final-v3/presentation-history.json. First tool input exceeded512000 chars; compact display projection fixed it; actual preview then exposed low contrast, fixed using paired theme colors/16px panel padding. All three scientific summaries(except generation time),CSV andPNG identical; no search/judge rerun. Initial tool rejection and correction records preserved.

## Actual checks (distinct executions)

- After prototype removal, final Release rebuildPASS and **native131/131 PASS255.09s** on exact731697 binary, raw final-build-attempt0/{manifest.json,check1.log}.
- Earlier harness26/27/28 passes preserved; post-search study56 PASS0.689s; display-projection regression added, **study57 PASS0.790s** in report-harness-check-attempt1. No claim of fresh full tools131.
- Post-timing repository24 PASS0.538s. Default staged whitespace check reports7249 standardCSV CRLF lines and4 extra blankEOF lines in frozen harnesses; preserve those bytes. Explicit local cr-at-eol/-blank-at-eof check passes, with default/first-policy failures retained in raw final-format-check-attempt0 and corrected policy in attempt1; final-v3/format-check.json records it. No scientific file changed. Final doc/staged checks and local commit recorded at delivery.
- Actual speedup.png/cost-quality.png inspected, hashes in validation.json. T3html_preview728/360 each56/56 interactions passed, no console errors/page overflow, original controls restored. Additional mobile filtered/test screenshot verified warning and footer. Default mobile full-page image caps at4000px; filtered screenshot covers the footer. Original final page html_render attachment is recorded in visuals/render.json.

## Preserved development/negative attempts

Pilot9/9 complete:3tasks P16384 G8, sixGPU full CPU audits98304 observations with0fitness/wrong mismatch; three fullAST pairs same. Sampled whole-card peak21.116GiB includes desktop/may miss spikes. Pilot ratios never used as formal denominators.

Geometry40 smaller-block runs:256/128/64 geomeans0.8647/0.6948/0.5280;256 descriptor mismatch. Extension1024 twenty new controls:geomean1.0642 but IsSortedP16384 fullAST/descriptor mismatch, rejected. Compact-root prototype implemented/tested,40 development rows/20fullpairs same;geomean1.008875 below1.03 gate, rejected/removed; exact sources/patch/tests/docs and07975 binary archived in raw compact-root-rejection. No new VM gain claimed. Nsys2probes show VM97.9/97.5% kernel time; static168registers/11760stackbytes; no64physical-warp prototype.

Explicit fixed-parent/operator diagnosis30/30 processes:336 root replacement requests,296 matching child AST/scope/fuel/constants,40mapping_unavailable(fibonacci/tribonacci/subregion_sum/fibonacci_array/stock_with_cooldown8each). First8 parents/fourpairs/eachsourcephase only; no internal-site/mutation-generator/RNG equivalence. Do not infer these5DP failures share the cause of known4tasks HeadIndex counts.

Failures preserved: toy nested-lock test deadlock before pilot(nochild); original compact-root owner profile-string assertion failure; pure waiting pipeline/diagnosis admission conflict; missingidentity.hpp diagnosis compilefailure(0samples); externalASGP plain-directory Git assumption failure; original formal.py controller/Stage same-lock deadlock afteroriginalfreeze but **before any formal sample**. Only corrected formal_attempt1 ran the scientific matrix with unchanged settings/binary. Never rerun the original pipeline/formal.py/freezer.

Full earlier development chronology preserved in raw recovery-before-monitor-20261018-1801.md, recovery-before-formal-attempt1.md, before-final-docs-RECOVERY.md and before-final-PROGRESS.md. Raw source/input/binary/attempt ledgers remain under logs/scaling-optimization-20261018/.

## Monitor and future work

This temporary monitor is **disabled**, nextRunAtnull, confirmed by update_scheduled_task(enabled=false); receipt `logs/scaling-optimization-20261018-monitor-disabled.json`. ID `scheduled-task:command:mcp:9b6db63f-47ac-4ebb-8095-b509f2b9f80a:schedule-task:gagp-scaling-optimization-20261018-monitor-v1`. Old day24/QC monitors remain off. Any stale scheduled message should inspect completed receipts and return without launching work.

Only report rebuilding into a NEW output directory is appropriate for reproducing this completed analysis; see README. A new experiment requires new user authorization/window/inputs, not resume of this matrix. Results and supporting harness/tests are committed locally; no push.
