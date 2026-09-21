# Native CLI Reference

This table is mechanically checked against `CliOptions` and the parser in
`cpp/include/gagp/cli/options.hpp` and `cpp/src/cli/options.cpp`. Update all
three together when a flag or default changes.

`gagp_evolve_cli` requires `--cases` and, for evolution, `--grammar-definition`.
It supports evolution by default and
switches to one-AST evaluation when `--eval-ast-json` is supplied. `--help` is
not supported; it follows the unknown-argument error contract.

| Flag | Field | Default | Meaning |
| --- | --- | --- | --- |
| `--cases` | `cases_path` | `required` | Fitness-case input path |
| `--population-json` | `population_json` | `unset` | Fixed `grammar-population-v2` replay path |
| `--grammar-definition` | `grammar_definition_path` | `unset` | Required compiled grammar definition path for evolution |
| `--eval-ast-json` | `eval_ast_json` | `unset` | Evaluate one native AST or generated grammar artifact on CPU |
| `--engine` | `engine` | `cpu` | Fitness backend: `cpu` or `gpu` |
| `--repro-backend` | `repro_backend` | `cpu` | Reproduction backend: `cpu` or `gpu` |
| `--cpu-repro-ablation` | `cpu_repro_ablation` | `none` | CPU experiment mode: `none`, `gpu_selection`, `gpu_candidates`, or `gpu_coupled_donor` |
| `--repro-overlap` | `repro_overlap` | `off` | Overlap GPU reproduction preparation with GPU evaluation |
| `--skip-final-eval` | `skip_final_eval` | `off` | Skip the post-generation final scoring pass |
| `--retain-final-population` | `retain_final_population` | `off` | Materialize the full final scored population |
| `--blocksize` | `blocksize` | `1024` | CUDA evaluation block size |
| `--population-size` | `population_size` | `64` | Generated individuals per generation |
| `--generations` | `generations` | `40` | Evolution generations |
| `--mutation-rate` | `mutation_rate` | `0.5` | Per-child post-crossover mutation probability |
| `--mutation-subtree-prob` | `mutation_subtree_prob` | `0.8` | Subtree versus constant mutation probability |
| `--penalty` | `penalty` | `1.0` | Non-negative fitness penalty |
| `--selection-pressure` | `selection_pressure` | `2` | Round-based tournament size |
| `--seed` | `seed` | `0` | Deterministic RNG seed |
| `--fuel` | `fuel` | `20000` | Explicit values must equal the grammar definition's execution fuel |
| `--max-expr-depth` | `max_expr_depth` | `7` | Explicit values must equal the grammar definition's maximum depth |
| `--max-stmts-per-block` | `max_stmts_per_block` | `6` | Legacy override rejected for compiled evolution |
| `--max-total-nodes` | `max_total_nodes` | `80` | Explicit values must equal the grammar definition's maximum nodes |
| `--max-for-k` | `max_for_k` | `16` | Legacy override rejected for compiled evolution |
| `--max-call-args` | `max_call_args` | `3` | Legacy override rejected for compiled evolution |
| `--show-program` | `show_program` | `none` | Program output: `none`, `ast`, `bytecode`, or `both` |
| `--timing` | `timing` | `summary` | Timing output: `none`, `summary`, `per_gen`, or `all` |
| `--out-json` | `out_json` | `unset` | Optional result JSON path |

Fixed-population timing should use `--population-json`, `--generations 1`,
`--skip-final-eval on`, and `--timing all`. See
[`../guides/benchmarking.md`](../guides/benchmarking.md) for the complete fair
comparison procedure.

Evolution uses the same canonical definition loader and compiler as
`gagp_generate_cli`, and result metadata records the resolved content hash. Passing a
legacy `format_version=grammar-config` file to `--grammar-definition` reports that it
must be migrated offline to `grammar-definition-v2`. `--population-json` accepts materialized
`grammar-population-v2` artifacts; seed-only `population-seeds` files are rejected
with instructions to materialize them in the frozen release-1 build and migrate the ASTs.

`--eval-ast-json` also accepts `grammar-generated-v2` and
`grammar-materialized-v2` artifacts. This evaluates
the stored materialized AST without requiring its generator version or resolved grammar.
The case file must match the recorded exact input schema and return type. Execution uses
the artifact's fuel; an explicit `--fuel` must equal that value. The result JSON reports
the fuel actually used. Native `ast-prefix-v2` inputs retain the normal CLI fuel behavior.
This evaluation establishes native validity; it does not replay or certify grammar
provenance. The C++ artifact replay APIs provide that separate check.


`gagp_generate_cli` creates `grammar-population-v2` initial-population
artifacts. Generation requires `--grammar-definition`, `--cases` and `--out-json`;
`--population-size` defaults to 1 (maximum 65536), and `--seed` defaults to 0 and accepts
a canonical unsigned 64-bit decimal. Replay uses `--replay-json` with `--cases` and
`--out-json`; an optional `--grammar-definition` imposes a required grammar identity.
Replay rejects explicit size/seed overrides. This CLI supports `--help`, rejects
unknown/duplicate flags, validates inputs before output replacement, and refuses to
overwrite a direct input file. It generates and replays populations without invoking
legacy reproduction.

`gagp_migrate_artifact` is the offline release-1 conversion command. It requires
`--input PATH` and `--out PATH`; input and output must identify different files.
Its remaining options are route-specific:

| Flag | Applies to | Meaning |
| --- | --- | --- |
| `--cases PATH` | `grammar-config`, `ast-prefix` | Exact fitness-case input and return schema |
| `--conversion-profile constrained-intent-v1` | `grammar-config` only | Required, explicitly lossy search-space conversion profile |
| `--fuel N` | `ast-prefix` only | Positive execution limit for a plain materialized AST |
| `--max-nodes N` | `ast-prefix` only | Positive recorded node limit |
| `--max-depth N` | `ast-prefix` only | Positive recorded prefix-depth limit |

The three numeric options must be supplied together. `grammar-config` uses its
embedded search limits and rejects the numeric triplet. A complete
`grammar-generated-v1` member uses its embedded schema, limits, fuel, semantic
identity, AST shape, and detached constants, so it needs neither cases nor
overrides. `grammar-population-v1` is rejected as a container; extract and migrate
each complete member independently. Bytecode and seed-only populations have no
exact direct route.

The command parses and validates the complete input before touching the output.
It writes a temporary file in the output directory, flushes that file, and renames
it over the destination; a conversion or write failure removes the temporary file
and leaves an existing destination unchanged. Unknown or duplicate options, missing
option values, incomplete numeric triplets, and unsupported route/option combinations
exit with status 2 and a diagnostic on standard error. The command has no `--help`
mode.


Compiled-grammar variation counters are exposed alongside reproduction timing. The nine
suffixes are `crossover_attempts`, `mutation_attempts`, `contract_rejections`,
`budget_rejections`, `generation_rejections`, `acceptance_rejections`, `fallback_children`,
`unchanged_children`, and `changed_children`. Aggregate JSON keys and summary stdout phases
use `generations_repro_<suffix>_total`; per-generation fields/JSON series use
`generation_repro_<suffix>`. Summary stdout uses `count=` for these integer values.
Legacy operators currently report zero. Counters classify operator outputs, including
both crossover children and subsequent mutation outputs, rather than only retained
population members; see [GPU reproduction](../design/gpu-reproduction.md).
