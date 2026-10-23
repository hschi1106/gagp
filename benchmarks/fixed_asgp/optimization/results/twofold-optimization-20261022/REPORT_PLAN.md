# Report contract

The report is still pending. Development means do not meet the2x target and are
not formal or fresh-seed quality results.

- Lead with an explicit target decision and a two-row table: P1024/P16384,
  common G/R/C, fresh baseline/candidate search seconds and geometric mean.
  Show the pooled60-cell mean, qualified/expected cells and failures. Search
  time is unprofiled `evolve_wall_ms`; process/CPU auditing/report costs remain
  separate. No historical ASGP denominator or time-to-solution claim.
- Use `report_qualification.speed_with_native_audits` for the target decision.
  Timing completeness alone is insufficient: both variants need every rep0
  final member independently judged on all1024 cases, including exact-AST
  dedup mapping, actual executions and logical observations.
- One per-task speed row for each population, geometric averaging over cold
  repetitions; paired ranges/uncertainty remain descriptive with small R.
  Never discard slower or failed cells. Do not infer aggregate speed from a
  favorable development subset.
- Quality: expected grid is all30 tasks × frozen evolution seeds. Successful
  search plus both train/test common and native generation/final champion
  audits establishes an observed pair. Unknown/missing/failed/unadmitted pairs
  stay unknown and count as potential losses in the primary seed-cluster bound.
  Report actual coverage and per-task paired success differences/intervals.
- Distinguish gen0 champion success, final success, newly solved final pairs,
  and loss of initial success. Primary success requires the SAME final program
  to solve both training and test. Train/test toggle is descriptive, never a
  candidate selection mechanism. Also show exact trajectory fidelity.
- Use the preregistered5pp seed-cluster bound, including its limited fixed-task/
  fixed-data scope; low power or no significant difference is not equivalence.
  Disclose finite domains, disjoint splits identical across seeds, and overlap
  with historical inputs. No universal parity/paper replication claim.
- Include source/binary/external/input/harness/output hashes, GPU reproduction
  settings, workspace/AST/depth/phase/logical lanes, whole-card sampled VRAM
  (including desktop and missed-peak limitation), all rejected attempts and
  current native regression results without a fabricated full131 pass.
- Artifacts: Chinese findings/report, speed runs/cells CSV, quality runs/pairs/
  task summaries CSV, provenance/coverage JSON, standard PNG+PDF plots, one
  self-contained HTML dashboard with white background and black/gray text.
  Inspect actual plots; T3 HTML preview728/360, task filter/selection/train-test
  interactions and no overall overflow; render ORIGINAL final HTML.
- Finalization: after timed work ends, inspect changed/untracked paths, update
  affected docs only, run appropriate small repository checks, commit locally,
  no push, preserve unrelated presentations. Original deadline includes
  reporting; missing work remains an explicit limitation.

Pre-formal audit implementation update (no fresh heldout opened): speed freezev3
may share one FRESH paired CPU reference only when complete serialized AST/order,
ordered cases/labels, grammar, effective CPU settings and full source/literal
parent decoding prefix match. Candidate GPU snapshots remain fresh. Preserve
original GPU raw output separately from derived merged/expanded documents; the
reader recomputes identity against the actual baseline origin. Actual candidate
CPU executions are0; reference uses and logical observations are separate.
Historical CPU results are never substituted for formal references. Quality
champion native audits still execute their own CPU calls; do not discount their
forecast. Timing-only cells and native-reference-complete cells are distinct.
