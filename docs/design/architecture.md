# Architecture

GAGP is a C++/CUDA grammar-guided genetic programming system. This document
describes its components, how one `gagp run` flows through them, and the
invariants that hold across them. The behavior itself is defined by the
[specifications](../../spec/README.md); the resident GPU engine, the core of
the research system, has its own document, [gpu-reproduction.md](gpu-reproduction.md).

## Components

```text
                gagp (cpp/src/cli)
       task, search config, results, grammar tools
                        |
                        v
     gagp_evolution (cpp/src/evolution)
   population init, evolution loop, reproduction,
   AST verification and compilation to bytecode
          |                              |
          v                              v
 gagp_grammar                     resident engine
 (src/evolution/grammar)          (src/evolution/repro/gpu)
 definitions, compiled tables,    GPU: NativePhasePopulation
 generation, membership,          CPU: HostPhasePopulation
 variation contracts
          |                              |
          v                              v
 gagp_runtime_cpu (src/runtime/cpu)   gagp_gpu (src/runtime/gpu)
 bytecode VM, CPU fitness             CUDA VM, GPU fitness, payload arena
          \                              /
           +---------- gagp_core --------+
          values, opcodes, bytecode verifier, region plans
```

| Library | Owns |
| --- | --- |
| `gagp_core` | `Value`, opcodes, the bytecode verifier, bounded-region plans and progress proofs, fitness policy |
| `gagp_runtime_cpu` | The CPU bytecode interpreter, builtins and CPU fitness |
| `gagp_gpu` (CUDA builds) | The GPU interpreter over (program, case) lanes, GPU fitness, the device payload arena and overflow replay |
| `gagp_grammar` | Loading, import resolution and SHA-256 identity of `grammar-definition-v2`; compiled tables; generation, membership and variation contracts |
| `gagp_evolution` | ASTs, verification, compilation, population initialization, the evolution loop, native AST reproduction and the resident engine |
| `gagp_cli_support` | JSON codecs, task and search-configuration files, result output, the `gagp` commands |

Dependencies point downward only. The runtimes never see grammars or authoring
files: they execute verified bytecode. There is no grammar-specific dispatch
anywhere below the grammar layer, and the compiled grammar is the only source
of initial programs, crossover sites and mutation domains.

## One run

`gagp run TASK.json` (`cpp/src/cli/run.cpp`):

1. Reads the task (`task.cpp`), its grammar, and its training cases; builds the
   search configuration from the built-in default, `--search` and the options
   (`search_config.cpp`); turns it into an `EvolutionConfig`.
2. Calls `evolve_population` (`cpp/src/evolution/evolve.cpp`), which creates
   the initial population from the grammar and the seed, or imports `--init`.
3. Runs the generations. With the default `resident` representation, one of
   two engines owns the population:

   ```mermaid
   flowchart LR
       A[Population on the device<br/>phase genes] --> B[Lower changed<br/>programs to bytecode]
       B --> C[Evaluate all programs<br/>on all cases]
       C --> D[Host: rank the<br/>fitness vector]
       D --> E[Selection, crossover,<br/>mutation kernels]
       E --> A
   ```

   `NativePhasePopulation` keeps the genotype on the GPU and runs selection,
   variation, lowering and evaluation there; only the fitness vector and the
   generation's champion cross to the host. `HostPhasePopulation` runs the
   same operator code on the CPU with the CPU evaluator (`--engine cpu`).
   Both are driven by the same generation loop, so ranking, history and export
   are shared code.
4. When the resident engine declines the grammar, the loop reruns as the
   `native_ast` representation: a typed AST population, verified and compiled
   to bytecode every generation, evaluated on the selected engine and
   reproduced on the host (`cpp/src/evolution/repro/backend.cpp`).
5. Judges the best program on the test cases with the CPU interpreter and
   writes `result.json` ([format](../../spec/task.md#run-result)).

`gagp eval` loads a task and a program, checks the program's grammar
membership, and judges it the same way. `gagp grammar` exposes the grammar
layer: validation, inspection, import resolution and population generation.

## Representations and formats

| Data | Format | Defined in |
| --- | --- | --- |
| Task | `gagp-task-v1` | [task.md](../../spec/task.md) |
| Search space | `grammar-definition-v2` | [grammar_definition.md](../../spec/grammar_definition.md) |
| Search configuration | `gagp-search-config-v1` | [search_config.md](../../spec/search_config.md) |
| Cases | `fitness-cases` | [fitness_cases.md](../../spec/fitness_cases.md) |
| Program | `ast-prefix-v2` | [grammar.md](../../spec/grammar.md) |
| Bytecode | `bytecode-json-v2`, `bytecode-fixture-v2` | [bytecode_format.md](../../spec/bytecode_format.md) |
| Generated population | `grammar-population-v2` | [grammar_definition.md](../../spec/grammar_definition.md) |
| Run result | `gagp-run-v1` | [task.md](../../spec/task.md#run-result) |

The value domain is `Int`, `Float`, `Bool`, `Char`, `String`, `IntList`,
`FloatList` and `StringList`. Strings and lists are payload values; their
host and device representations are described in [payload.md](payload.md).

## Programs, compilation and execution

- The node descriptor table (`node_descriptor.cpp`) owns each AST node's
  serialized name, numeric kind, arity, index fields and typing rule. AST kinds
  53-70 and opcodes 25-27 are permanently unassigned holes.
- External ASTs and bytecode are verified at every trust boundary: AST
  structure, types and lexical scope (`ast_verify.cpp`, `ast_type_verify.cpp`)
  and bytecode (`bytecode_verify.cpp`).
- The compiler (`compiler.cpp`) lowers verified ASTs to bytecode. Lexical
  bodies use declaration IDs; traversals lower to ordinary control flow;
  recursive and memoized templates lower to one `BOUNDED_REGION` instruction
  (opcode 28) over a general `RegionPlan`. Hidden locals are reused once their
  expression ends, without changing instructions or fuel.
- The CPU and GPU interpreters execute the same verified bytecode and region
  plans with the same event order, fuel charges and errors. CUDA uses
  fixed-capacity descriptors and rejects a plan beyond its capacity before
  upload; GPU fitness sessions keep their region frame and memo workspace
  (512 MiB) between evaluations but never carry memoized answers across
  programs.

## Invariants

- **One semantics.** CPU and GPU produce the same fitness values for the same
  bytecode and cases; parity tests (`cpp/tests/parity/`) check every opcode,
  builtin and region form.
- **Engines never change results.** For the same task, search configuration
  and seed, `--engine gpu` and `--engine cpu` publish identical populations,
  champions and histories (`gagp_test_phase_population_cpu_reference`).
- **The search configuration is the only search knob.** Anything that changes
  which programs are explored is a member of the search configuration;
  environment variables only select devices and diagnostics.
- **Grammar membership is checked.** Generated programs, imported populations
  and `gagp eval` programs must belong to the task's grammar.
- **Determinism.** A run depends only on its inputs and seed; history and
  results are reproducible bit for bit.
