# Search Configuration

This document defines the `gagp-search-config-v1` document: the user-tunable
search semantics of an evolution run. The search always runs on the resident
phase population (the phase-gene genotype below). The document does not
select an execution engine. Engine choices (the GPU owner or the CPU reference
owner, warp kernels, buffer strategies) must not change any published result
of a configured search.

See also:

- [grammar_definition.md](./grammar_definition.md) for the search space,
  execution fuel and search limits, which a search configuration never overrides
- [fitness.md](./fitness.md) for fitness and ranking values

## Document

```json
{
  "format": "gagp-search-config-v1",
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
| `population_size` | common | integer 1..2^20 | Individuals per generation |
| `generations` | common | integer 1..2^20 | Reproduction rounds; a final evaluation follows unless the entry point skips it |
| `seed` | common | integer 0..2^53, or a decimal string 0..2^64-1 | Run seed; larger seeds must use the string form |
| `selection.method` | common | `tournament` | Only tournament selection is defined |
| `selection.size` | common | integer >= 1 | Tournament size. Contestants are drawn independently with replacement |
| `elitism` | common | integer >= 0, < population size | Best members copied unchanged into the first offspring slots (highest fitness, ties to the lowest source index) |
| `crossover.probability` | common | number in [0,1] | Per-child crossover gate |
| `crossover.attempts` | gagp | integer 1..64 | Destination/donor site retries per crossover |
| `mutation.probability` | common | number in [0,1] | Per-child post-crossover mutation probability |
| `mutation.max_subtree_nodes` | common | integer 0..1024 | Node cap of a generated mutation subtree; 0 disables it |
| `sites.scope` | common | `genome`, `uniform_phase` | Site population for crossover and mutation |
| `sites.draw` | gagp | `reservoir`, `counted` | Uniform site draw method |
| `limits.site_depth` | common | integer 0..15 | Grammar-invocation depth cap of phase-population variation; 0 disables it |
| `ranking.tie_break` | common | `ast_nodes`, `grammar_sites` | Tie break after fitness and wrong-case count: fewer AST nodes, or fewer mutable grammar invocations |
| `fitness.penalty` | common | number >= 0 | Fitness penalty; zero is permitted only where the task's fitness policy allows it |
| `execution.fuel` | common | integer >= 1 | Evaluation fuel per program and case. It may lower, never raise, the grammar's execution fuel |
| `schedule.mode` | gagp | `generational`, `steady_state` | Population schedule (see below) |
| `schedule.batch` | gagp | integer 1..population, dividing it (default: population) | `steady_state` only: children created and scored per round |
| `schedule.replacement_size` | gagp | integer 1..64 (default 2) | `steady_state` only: inverse-tournament size of the replaced member |

`Class` separates two kinds of members:

- `common`: a general GP search parameter. When GAGP is compared with another
  GP system, set these to the compared system's values (or the closest
  defined equivalent) so that speed and quality differences are not caused by
  a different search budget, tree-size limit or operator rate. A run must
  record which comparison profile it used.
- `gagp`: a GAGP mechanism without a direct counterpart in other systems
  (site-draw implementation, bounded crossover retries, population
  schedule). These are GAGP design choices; they are evaluated by ablation
  against GAGP's own configurations, not aligned per comparison.

## Variation

These rules define `crossover`, `sites`, `limits` and `ranking`.

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
  scaffolds.
- A run with `limits.site_depth > 0` and no initial
  population generates its own: one program generated from `seed` fixes the
  skeleton outside the phases, and every phase of member `i` is grown by the
  phase generator under the site-depth profile with invocation depth
  `2 + i mod (site_depth - 1)` (or `site_depth` when it is 1). A depth that
  cannot derive the phase is retried with fresh draws and then raised by one,
  up to `site_depth`. Phases are filled in order, each leaving room for the
  minimum size of the later phases within the grammar's `max_nodes`.
- `ranking.tie_break: grammar_sites` requires a task metric with wrong counts.
- A unit production (an alternative that is only a nonterminal reference,
  possibly inside template or hole wrappers) is inlined: it is replaced by the
  referenced nonterminal's alternatives, recursively, each weighted
  `w_unit * w / W`, where `W` sums the referenced alternatives that remain. An
  alternative that would revisit a nonterminal of the chain without deriving a
  node is dropped, and an expression reached through two chains is one
  alternative carrying both weights. One draw then equals choosing through the
  chain one reference at a time whenever every alternative fits the budget. An
  inlined chain is one grammar invocation: its root node carries one site, of
  the outermost nonterminal, which is the only variation site of the chain and
  counts one level of `limits.site_depth`.
- A template hole that occurs more than once in its body is one shared
  derivation. Generation, crossover and mutation work on the first occurrence
  in prefix order (later occurrences are derived or spliced like any subtree),
  and then every later occurrence is rebuilt as a copy of the first. A rebuilt
  program that exceeds `max_nodes`, `max_depth` or `limits.site_depth` is
  rejected: an initial phase is regenerated, a crossover attempt counts as
  rejected, and a mutation is skipped (the crossed child stands). Sites inside
  a copy are never selected, but count toward `limits.site_depth` and
  `grammar_sites`.
- A grammar whose only member is one fixed program has no variable site:
  every child equals its parent.
- Generation draws a constant from a domain's `sample_from` sampler when it
  has one, and samples a Float range with the construction law and
  quantization ([constant domains](grammar_definition.md#constant-domains)).

### Resident admission

A run first admits the grammar and every initial genome into the phase-gene
genotype. Unit productions, alias cycles, repeated template holes and
single-program grammars are admitted (see [Resident variation](#resident-variation)).

The genome layout is a function of the grammar and the initial population:

1. A grammar whose entry has exactly one production, a template, and which
   uses no `let` or traversal primitive is a region-template candidate. Its
   population takes the region-template layout (the template fixed, the holes
   of its one bounded region varied as separate phases) when every member
   takes it with one fixed skeleton. A member does not take it when its
   template holds anything other than one bounded region whose variable holes
   are exactly its phases, more than 8 variable phases, a phase grammar with a
   `let` or with constant domains other than Int, Bool and the empty IntList,
   or a skeleton constant outside those types.
2. Every other population, including a candidate's whose members do not all
   take the region-template layout, takes the whole-program layout: one phase
   holding the whole program.

A `resource_charge` has no effect on admission or on search.

A grammar or initial population is rejected when it exceeds a
[resident capacity](#resident-capacities), when a construct cannot be held by
the genotype (for example lowering capacity within the depth budget), or when
it uses one of three constructs of the language that the resident engine does
not yet hold: `sequence` constant domains, a nonterminal reached under two
different lexical binder environments, and a traversal inside a bounded region
phase. A genome is rejected when it does not share the population's fixed
skeleton. Rejection is an error raised before the first generation: the run
stops with a diagnostic that begins with
`resident engine cannot admit this grammar or initial population:` and names
the limit or construct and the offending value. `gagp run` then exits with
status 2 and writes no result. No other engine runs instead.

### Resident capacities

These are the limits of the language on the resident engine: a grammar, an
initial population or a run that exceeds one is rejected at admission, and the
message names the limit and the offending value. Program-shape limits are
proven over every program the grammar can derive within its `search_limits`,
so variation never builds a program beyond them. The one exception is the
number of bounded regions in one program, which is checked per program like
`max_nodes`: admission refuses an initial member with more, and generation
and variation reject a candidate with more and draw or try again.

| Resource | Limit |
| --- | --- |
| Population | 16384 members |
| `search_limits.max_depth` | 64 |
| `search_limits.max_nodes` | 2048 |
| Nodes of one phase (in the whole-program layout, of the whole program) | 1024 |
| Effective node limit of the whole-program layout | min(`search_limits.max_nodes`, 1024), because the program is one phase in a row of 1024 nodes. A larger `max_nodes` is accepted, but generation, crossover and mutation stay within the row and an initial member over it is rejected, identically on both engines |
| `search_limits.max_nodes` of a Program-entry grammar | 512 |
| Program inputs | 64 |
| Grammar locals referenced by the grammar | 16 |
| Locals of a lowered program (inputs, grammar locals and temporaries) on one path | 64 |
| Open traversals / `let`s / loops on one path | 8 / 16 / 16 |
| Analysed value `if`s on one path under an open traversal | 16 |
| Visible lexical bindings | 15 |
| Static binder numbers (`let` and traversal bindings) | 16 |
| Bindings in the scope of a nonterminal, reference, template or hole | 8 |
| Operands plus phases of one bounded region | 8 |
| Captures of one bounded region | 8 |
| Bindings of one bounded region phase | 7 |
| Bounded regions in one program | 4 |
| Frames / memo cells declared by one bounded region plan | 1024 / 4096, checked by `validate_region_plan` on both engines (see the [region workspace](../docs/design/payload.md#region-workspace)) |
| Reachable traversal and bounded region expressions | 1024 |
| Values of one finite constant domain / of all reachable domains | 65536 / 1048576 |
| Grammar table: (expressions + 1) x (`max_depth` + 1), and with `limits.site_depth` expressions x (`max_depth` + 1) x (`site_depth` + 1) | 4194304 entries |
| Alternatives produced by inlining unit productions | 65536 |
| GPU genotype workspace (population x phases x 1024 nodes) | 12 GiB |

### Engine equivalence

For one search configuration, grammar, case set, initial population and seed,
the resident GPU phase owner (GPU evaluation and GPU reproduction) and the CPU
reference owner (CPU evaluation and CPU reproduction) execute the same
per-child operators, consume the same random draws and publish the same
genotypes, fitness values, wrong counts, champions and final population.
GPU engine variants do not change these results.

## Synchronous batch steady state

`schedule.mode: steady_state` is supported by both phase owners (GPU and CPU
reference) and they publish identical searches. It takes only `batch` and
`replacement_size`. An omitted `batch` is the
population size (one round per generation-equivalent); the effective record
holds the resolved value. `elitism` is not used: the
population's best member is never replaced instead.

1. The initial population is evaluated and ranked with the generational
   ranking (fitness, wrong count, tie break, member index); selection values
   are the generational ones (rank values for metrics with wrong counts).
2. Each round draws one 64-bit seed from the run generator and creates
   `batch` children with the ordinary per-child crossover and mutation bodies
   (child index `0..batch-1`, that seed), choosing parents by tournament from
   all current members.
3. A child whose genotype equals a current member reuses that member's
   completed result; every other child is evaluated. All results are fixed
   before any placement.
4. Children are placed in child order. Each draws `replacement_size` members
   uniformly with replacement from the run generator and replaces the worst of
   them under the ranking, redrawing all of them when that member is the
   current best. A later child of the same round may replace an earlier one.
5. Rounds repeat until `population * generations` children were placed. A
   history entry (generation-equivalent) is recorded before every
   `population / batch` rounds; the final population is the ranked population
   after the last round, without a separate final evaluation.

Cumulative checkpoints and CPU fitness verification are not supported.

## Not configurable here

- Fitness metric and expected-output encoding: owned by the task and
  [fitness.md](./fitness.md).
- Search depth/node limits, memo/frame capacities and the maximum execution
  fuel: owned by the grammar definition. Only `execution.fuel` may lower the
  fuel, as described above; otherwise entry-point fuel equals the grammar's.
- Physical genotype capacity of the phase population (1024 nodes per phase
  row, at most 8 variable phases): fixed implementation limits. Inputs or
  offspring that exceed them are rejected, never truncated.
- Mutation always replaces a whole subtree.

## Entry points and records

- `gagp run TASK.json --search PATH`: an absent member keeps the built-in
  default (`configs/search/default.json`); the `--population`,
  `--generations` and `--seed` options override the file. Penalty defaults to
  the task's `fitness.case_cap` and fuel to the grammar's execution fuel.
- `result.json` records the complete effective configuration as a
  `gagp-search-config-v1` object named `search`. Feeding that object back as a
  file reproduces the same search semantics.
