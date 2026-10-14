# Astra experiments, 2026-10-14

Starting checkpoint: ce0d811. Starting authoritative binary/settings/input hashes:
`logs/optimization/parallel-20261013/followup-main/manifest.json`.
Archived binary is immutable. New results use fresh output directories beneath
`logs/optimization/astra-20261014/` (agent worktrees may hold separate raw roots).

Primary comparison is native changing-population House Robber 32 generations,
seed 0, and Median 64 generations, seed 2, P=1024, all 1024 cases, CPU1T+GPU.
Search wall includes cold admission/import, complete generations and final
full evaluation/export/admission. Process wall additionally includes input
parsing, post-search independent audit and JSON serialization. No fixed-parent
speedup is substituted for this contract. Original five modes remain available.

Exact execution candidates must retain per-program results, errors, timeouts,
fuel/resource boundaries and seeded trajectories. External input validation is
not removed. Internal grammar construction metadata is not an external proof.
Generic fallbacks remain explicit. No CPU fragment reproduction or CPU worker
threads are introduced.

Search-rule experiments are opt-in, separately labeled, and compared for
quality at equal wall time as well as generations. Bloat must not be hidden by
invalid offspring, clone collapse, timeout or omitted evaluations. Fitness
ties are unchanged. A small screen can reject obvious regressions; it cannot
establish convergence equivalence.

Performance execution and builds are serialized with
`/tmp/gagp-parallel-20261013-benchmark.lock`. Existing frozen inputs are never
prepared again. Initial baseline is only an environment check; important
candidates require repeated same-round controls and final five-mode validation.
