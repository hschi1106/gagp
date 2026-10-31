# Search Configuration

This document defines the `gagp-search-config-v1` document: the user-tunable
search semantics of an evolution run. It does not select an execution engine.
Engine choices (CPU or GPU evaluation and reproduction, warp kernels, buffer
strategies, overlap) must not change any published result of a configured
search.

See also:

- [grammar_definition.md](./grammar_definition.md) for the search space,
  execution fuel and search limits, which a search configuration never overrides
- [fitness.md](./fitness.md) for fitness and ranking values

## Document

```json
{
  "format": "gagp-search-config-v1",
  "representation": "phase_population",
  "population_size": 1024,
  "generations": 64,
  "seed": "35",
  "selection": {"method": "tournament", "size": 2},
  "elitism": 1,
  "crossover": {"probability": 1.0, "attempts": 8},
  "mutation": {"probability": 0.3, "max_subtree_nodes": 0},
  "sites": {"scope": "genome", "draw": "counted"},
  "limits": {"site_depth": 0},
  "ranking": {"tie_break": "ast_nodes"},
  "fitness": {"penalty": 1000},
  "execution": {"fuel": 2000000}
}
```

`format` is required. Every other member is optional; an absent member keeps
the value supplied by the entry point. Unknown members, wrong JSON types, and
out-of-range values are rejected; nothing is clamped or ignored.

| Member | Class | Values | Meaning |
| --- | --- | --- | --- |
| `representation` | gagp | `phase_population`, `native_ast` | `phase_population` selects the native phase-population genotype and operators below; `native_ast` selects the generic AST reproduction path |
| `population_size` | common | integer 1..2^20 | Individuals per generation |
| `generations` | common | integer 1..2^20 | Reproduction rounds; a final evaluation follows unless the entry point skips it |
| `seed` | common | integer 0..2^53, or a decimal string 0..2^64-1 | Run seed; larger seeds must use the string form |
| `selection.method` | common | `tournament` | Only tournament selection is defined |
| `selection.size` | common | integer >= 1 | Tournament size. The phase population draws contestants independently with replacement; the native AST path keeps its round-based tournaments |
| `elitism` | common | integer >= 0, < population size | Best members copied unchanged into the first offspring slots (highest fitness, ties to the lowest source index) |
| `crossover.probability` | common | number in [0,1] | Per-child crossover gate (phase population) |
| `crossover.attempts` | gagp | integer 1..64 | Destination/donor site retries per crossover (phase population) |
| `mutation.probability` | common | number in [0,1] | Per-child post-crossover mutation probability |
| `mutation.max_subtree_nodes` | common | integer 0..1024 | Node cap of a generated mutation subtree; 0 disables it (phase population) |
| `sites.scope` | common | `genome`, `uniform_phase` | Site population for crossover and mutation (phase population) |
| `sites.draw` | gagp | `reservoir`, `counted` | Uniform site draw method (phase population) |
| `limits.site_depth` | common | integer 0..15 | Grammar-invocation depth cap of phase-population variation; 0 disables it |
| `ranking.tie_break` | common | `ast_nodes`, `grammar_sites` | Tie break after fitness and wrong-case count: fewer AST nodes, or fewer mutable grammar invocations |
| `fitness.penalty` | common | number >= 0 | Fitness penalty; zero is permitted only where the task's fitness policy allows it |
| `execution.fuel` | common | integer >= 1 | Evaluation fuel per program and case. It may lower, never raise, the grammar's execution fuel, and only for an explicit `representation: phase_population` |

`Class` separates two kinds of members:

- `common`: a general GP search parameter. When GAGP is compared with another
  GP system, set these to the compared system's values (or the closest
  defined equivalent) so that speed and quality differences are not caused by
  a different search budget, tree-size limit or operator rate. A run must
  record which comparison profile it used.
- `gagp`: a GAGP mechanism without a direct counterpart in other systems
  (genotype representation, site-draw implementation, bounded crossover
  retries). These are GAGP design choices; they are evaluated by ablation
  against GAGP's own configurations, not aligned per comparison.

## Phase population variation

These rules define `crossover`, `sites`, `limits` and `ranking` for
`representation: phase_population`.

- If any member of `crossover` or `sites`, or `mutation.max_subtree_nodes`,
  is present, the whole variation policy is explicit. Absent members then take
  these defaults, never a legacy process setting: `scope: genome`,
  `draw: reservoir`, `probability: 1`, `attempts: 8`, `max_subtree_nodes: 0`.
- `crossover.probability` equal to 1 consumes no random draw for the gate.
  A smaller value draws one 53-bit uniform value per child after both
  tournaments; the child keeps its first tournament parent when that value is
  at least the probability. Mutation then applies as for any other child.
- `sites.draw: reservoir` draws one bounded value per eligible site;
  `counted` counts eligible sites and draws one rank. Both are uniform over
  sites but consume different random draws, so they are different searches.
- `sites.scope: uniform_phase` draws one variable phase uniformly before the
  site draw, once per operator, and retains it across crossover attempts.
- Crossover retries a rejected destination/donor pair up to `attempts` times;
  a child with no accepted pair keeps its parent's phases.
- `mutation.max_subtree_nodes` lowers the node capacity offered to the
  generator of a mutation subtree (it only ever reduces the phase-capacity
  bound). Generation samples only productions that fit the bound. If no
  derivation of the selected site fits, the child is not mutated; random draws
  already consumed for that child are not replayed.
- `limits.site_depth` counts nested grammar invocations, excluding lowering
  scaffolds. It cannot combine with projected resource budgets.
- `ranking.tie_break: grammar_sites` requires a task metric with wrong counts.

Legacy process settings remain accepted when no explicit policy is given:
`GAGP_COUNTED_SITE_DRAW` (present) selects `counted`,
`GAGP_GPU_UNIFORM_PHASE_SITES=1` selects `uniform_phase`, and
`GAGP_GPU_SOURCE_CROSSOVER_GATE=1` selects probability 0.7. When an explicit
policy is given, such a setting that disagrees with it is an error.

### Engine equivalence

For one search configuration, grammar, case set, initial population and seed,
the resident GPU phase owner (GPU evaluation and GPU reproduction) and the CPU
reference owner (CPU evaluation and CPU reproduction) execute the same
per-child operators, consume the same random draws and publish the same
genotypes, fitness values, wrong counts, champions and final population.
Host worker count and GPU engine variants do not change these results.

## Not configurable here

- Fitness metric and expected-output encoding: owned by the task and
  [fitness.md](./fitness.md).
- Search depth/node limits, memo/frame capacities and the maximum execution
  fuel: owned by the grammar definition. Only `execution.fuel` may lower the
  fuel, as described above; otherwise entry-point fuel equals the grammar's.
- Physical genotype capacity of the phase population (1024 nodes per phase
  row, at most 8 variable phases): fixed implementation limits. Inputs or
  offspring that exceed them are rejected, never truncated.
- Phase-population mutation always replaces a whole subtree
  (`mutation_subtree_prob` must be 1).

## Entry points and records

- `gagp_evolve_cli --search-config PATH`: a member may not set the same field
  as an explicitly given flag (`--population-size`, `--generations`, `--seed`,
  `--selection-pressure`, `--mutation-rate`, `--penalty`, `--fuel`).
- Fixed ASGP benchmark: `GAGP_SEARCH_CONFIG=PATH`. `population_size` must equal
  the input population, and a member may not contradict a benchmark setting
  that is present (`GAGP_SEARCH_GENERATIONS`, `GAGP_BM_SEED`,
  `GAGP_SEARCH_PENALTY`, `GAGP_GPU_SITE_DEPTH`, `GAGP_GPU_SITE_PARSIMONY`).
- Both entry points record the complete effective configuration as a
  `gagp-search-config-v1` object named `search_config` in their result JSON,
  with any legacy-setting policy resolved. Feeding that object back as a file
  reproduces the same search semantics.
