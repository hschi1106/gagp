# Version

Current release: 3.0.0

## 3.0.0

Release 3.0 makes GAGP one program with one search configuration. The
language, bytecode and value semantics are unchanged from 2.0.0: programs,
bytecode and generated populations of 2.0.0 load and run identically, and the
runtime semantic identity stays `gagp-native-2.0.0`.

**New**

- `gagp`, a single executable: `gagp run` (search a task), `gagp eval` (score
  a program) and `gagp grammar` (validate, inspect, resolve, generate).
- Task files (`gagp-task-v1`) and run results (`gagp-run-v1`) with a readable
  program text (`best.text`), saved populations (`gagp-population-v1`) and
  `--init` ([spec/task.md](spec/task.md)).
- One search configuration format (`gagp-search-config-v1`) whose built-in
  default is `configs/search/default.json`: synchronous steady state, tournament
  2, crossover 0.7, mutation 0.3, elitism 1.
- `benchmarks/asgp30`: the 30 tasks of the ASGP paper as ready-to-run tasks,
  with `benchmarks/run.py`.
- Automatic fallback: a grammar the resident GPU engine cannot hold runs with
  host reproduction instead of failing, and the result records why.

**Removed**

- The earlier executables (`gagp_evolve_cli`, `gagp_generate_cli`,
  `gagp_grammar_cli`, migration tools) and the Python operational tools.
- All engine environment switches; every exact optimization is now always on
  when it applies. Only `GAGP_CUDA_DEVICE`, `GAGP_GPU_DIAGNOSTICS` and
  `GAGP_SEARCH_TRACE` remain.
- Native AST reproduction on the GPU (the resident engine replaces it), the
  fixed-ASGP benchmark harness, host worker threads, CPU fitness verification
  and evaluation checkpoints.
- Release-1 migration (`grammar-config`, `ast-prefix` v1, `grammar-materialized-v2`).

Search results of 3.0 equal those of the last 2.x build for the same inputs
and search settings (checked on 65 golden runs). On ASGP-shaped searches
with population 16384, 3.0 is about 1.15x faster than 2.x.

## 2.0.0

Release 2.0 made compiled grammar definitions (`grammar-definition-v2`) the only
search-space format and introduced the v2 program formats.

| Contract | Release 1 | Release 2 and 3 |
| --- | --- | --- |
| Program AST | `ast-prefix` | `ast-prefix-v2` |
| Bytecode | `bytecode-json`, `bytecode-fixture` | `bytecode-json-v2`, `bytecode-fixture-v2` |
| Grammar | `grammar-config` | `grammar-definition-v2` |
| Generated program and population | `grammar-generated-v1`, `grammar-population-v1` | `grammar-generated-v2`, `grammar-population-v2` |
| Fitness cases | `fitness-cases` | `fitness-cases` (unchanged) |

Release-1 artifacts are rejected; programs must be regenerated or recompiled
from source. The v2 opcode layout assigns 0-24 and 28; 25-27 are permanent
holes. AST node kinds 53-70 are permanent holes for removed specialized nodes.
Decoders never reinterpret either range.
