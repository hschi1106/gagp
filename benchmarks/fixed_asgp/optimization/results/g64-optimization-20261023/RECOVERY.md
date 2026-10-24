# G64 optimization recovery — final verification

Actual formal searches, offline audits, full regressions and report are finished. **Numeric target not achieved:** all30/P1024/G64/R3 old/new GM1.0792574094191294, target1.3. Do not resume a controller, rerun samples, reset the clock, or relabel prototype checks as speed proof.

## Workspace / fixed window

- Every shell cwd `/home/hschi1106/.t3/worktrees/gagp/gagp-support-20261015`, branch `opt/list-output-5x-20261015`. Never switch/reset older UI worktree. No subagents or pushes; unrelated presentations untouched.
- START1792763488 =2026-10-23 13:51:28UTC; HARD DEADLINE1792849888 =2026-10-24 13:51:28UTC (Taiwan21:51:28). Reporting included; never reset. Frozen development/source-build cutoffs passed.
- CPU1T, GPU evaluation AND reproduction,16GiB workspace, native AST2048/depth24/backend phase1024, CUDA0. All builds/tests/timing/heavy analysis use `/tmp/gagp-support-20261015-gpu.lock`; nsys only. Headline unprofiled evolve_wall_ms, offline/cold/post-search cost separate.

## Actual terminal state

All current controller PIDs were inspected after closeout; no formal/build/test/report/prototype controller remains live.

| Controller | PID | Terminal result |
|---|---:|---|
| formal0 | 3587036 | validation_stages_finished / exit0 at05:13:51UTC |
| formal0-secondary | 3611715 | requested_phase_complete / exit0 |
| closeout0 | 3587029 | artifacts_ready_manual_review_pending / exit0 at06:11:02UTC |
| report | 3636945 | outputs sealed / exit0 at06:11:02UTC |
| packed-oracle0 | 3590044 | CPU optimized+sanitizers PASS, storage only |
| packed-cuda-oracle1 | 3591456 | CUDA storage oracle PASS |
| packed-access-oracle0 | 3602361 | CPU access optimized+sanitizers PASS |
| packed-executor-compile0 | 3605107 | 12 real entrypoints device-compiled, NO kernel launches |

`final/completion.json` is the immutable report-generation receipt; its remaining/manual wording is historical at generation time. Later manual proof is outside final: FINAL_HASH_CHECK.json, UI_VALIDATION.json, PRESET_CHECK.json, PLOT_REVIEW.json and DELIVERY_REVIEW.json. Do not overwrite sealed final outputs. Actual delivery/commit/render/scheduler cleanup receipt belongs in ignored RAW/DELIVERY_RECEIPT.json; inspect it first on resume to avoid duplicate delivery.

## Qualified results

- Primary270/270 searches,420/420 audits,240 native calls passed; all30 tasks qualified. Old/new1.0792574094191294×, paired-round95%CI1.0773224641185721–1.0811828497273197; ASGP1T/new4.673147009468375×. Interim timing reconciled exactly with final independent raw qualification.
- Quality600/600 searches,2400/2400 judges,1200 native calls passed;300/300 pairs agree on all65 selected champion AST/fitness observations, final descriptors and specified exports. Each method finaljoint90/300, gen0joint21/300, newlysolved69/300, no observed lost-success pairs. N10 seed-cluster one-sided97.5% lower bound−30.8497pp; **cannot establish5pp noninferiority or universal equivalence**.
- SecondaryP16384/G64/R1:90/90 searches,420/420 audits,240 native calls passed;30/30 old/new and ASGP cells qualified. Old/new1.080211849133956×; ASGP1T/new8.07584926757467×. Every formal GPU search has sampled VRAM,60/60; maxwholecard21.47GiB, samplingerrors0. Approx200ms sampling includes desktop and may miss short peaks; not an exact/universal bound.
- Overall960/960 searches and3240/3240 offline jobs OK.1680 native calls,1,145,880 logical program observations,964,682 within-call unique executions (not globalunique). Failed/missing development attempts remain preserved. Full successful execution is distinct from solving tasks.
- In-search CPU verification840 known calls:programs0,fitness/wrong/best corrections0,recordedwall0. Post-search CPU section60.6726805s across840 calls; offlinejudge process5882.7918625s. Both latter sections include preparation/common/GPU/native work and are not pureCPUevaluation time; excluded from headline.
- ASGP source qualityseed35N1 only:10/30 finaljointsuccess, separate fromGAGPN10. Sourcephase7, representation/operators/sites differ fromGAGP;4tasks HeadIndex sitecounts differ. No ASGP paper replication, equal evolution capability or every-population-every-generation parity claim.

## Frozen provenance

First freeze19:15:16UTC before fresh data dispatch. Baselinea8460b618e8cca5341cee1a1714241577472ae74 /binarybeb5585d6cd478be304ff37795dd6bf303cdf5460f2221a95f1aaf07708d2c57. Selected build7/private7 /binary81b26bdeed8492b686574a7aa43a00df78a62b17578209e527e9906b5db24384.

Global extras: VIEW_PHYSICAL_BLOCK0,RELAXED_VIEW_REGISTERS1,VIEW_BOUND1280,DENSE_CASE_LANES0,DENSE_CASE_TILEStuple,CASE_VIEW_BLOCKhybrid,SPECIALIZE_PRIVATE_LAYOUT0,PRIVATE_LAUNCH_BOUND32. Full env/frozen source/input hashes retained. Data20261024,qualityseeds35..44N10,train19–200/test<=1000 finiteexceptions,primaryseed35R3,secondaryR1. Disjoint train/test identical acrossseeds; crossPprefix equality only recorded. N/G/R chosen from oldTRAINING cost before fresh inputs; N10 forecast expected10.591h/guard14.298h, N20guard16.606h did not fit. No new heldout-guided tuning or historical denominator reuse.

Actual final report independently reread source/binary/input, full-final CPU mapping and generation champions. Finalmanualhash check reread all7 analysis files,18 sealed outputs and451 CPP identities, unchanged. GAGP main timer same old/new; ASGP includes parentscopy/grammar/history work whereas GAGP uses prepared compiledgrammar and post-search exports/CPU16 scope. Full CLI time separate.

## Actual regressions / manual checks

- Fresh fullRelease buildPASS,1555.115s. Actual fullnative136/136PASS,441.02s. Study129PASS; tools122OK/skipped4; repository24PASS. Read actual terminal logs; not an old131 or fragmented total.
- PNG plots and rendered singlepagePDFs actually inspected, labels/whiskers/footnotes legible. Speed heterogeneity and initial-versus-new success retained. PDF render receipt RAW/visual-checks0/pdf-render.json.
- T3 native browser exact728 and360 widths: focused HOUSE filter/task selection/Training click, then full both30-row tables/no-match/reset/train-test65point comparisons/nohorizontaloverflow passed. Headless html_preview at bothwidths returned screenshots and passing console probe. Original dashboard bytes retained for final html_render; probe injected only into preview copy. No incomplete formal rows exist, so UI incomplete-row branch not exercised. UI_VALIDATION.json retains full results.
- Exactcandidate preset exported; actual CLI dryrunsP1024/P16384 pass/no new search. First wrapper omittedPYTHONPATH and failedbeforeimport; preserved preset-dryrun-attempt0.json, explicit environment-only correctedattempt1 in PRESET_CHECK.json. No formal changes/reruns.
- Analysis-only revisions0..4 retained with prior source/output hashes. Secondary ASGP display, coverage guard, plot labels and VRAM unknowns corrected transparently. All21new guards are included in actual129pass, not pending. Seven current sealed analysis sources unchanged after report.
- Packed prototypes pass storage/access/compile checks only, remain outside officialCPP/unintegrated/unmeasured. No extra candidate or target proof.

## Final delivery

Review affected docs and git diff/untracked; run small repo checks after doc changes; local commit only, exclude presentations/oldstudies/ignoredRAW. Then html_render **original final/dashboard.html**, disable temporary monitor scheduled-task:7b1b6117-45ae-4bed-b82d-909c72ecd582 and verify old monitors stay off. Current owned previewtabtab_2_47e2e164-9f41-4a55-93d4-ff333dc9b821; read-only localHTTPserverPID3638546/port35817 must be stopped after UI checks. Record actual cleanup/deliveryreceipt in RAW, never edit successful/failed samples.

Do not mark native numeric goal complete:1.3 is unmet. No additional independent candidate/full freshASGP+quality+native validation fits the frozen remaining window/cutoffs. Report actual1.079 and material limits. Goal status must follow applicable actual blocked-audit rules; never invent success or pause without user.

## Preserved history

Development/rejected arms and failures remain in RECOVERY_HISTORY-20261023-213809.md, development_attempts.md/.csv and RAW. Exact previous operative recovery sheets retained RAW/recovery-before-closeout0.md and RAW/recovery-before-final-review0.md. Oldstudy states in PROGRESS are historical; do not resume them.
